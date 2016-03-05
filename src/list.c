/*
 * list.c
 *
 *  Created on: Jun 19, 2015
 *      Author: Kyle Harper
 * Description: Builds the buffer lists and functions for them.  We use circular lists (doubly-linked) mostly because:
 *                1.  They make implementing clock sweep easy (no checking head/tail).
 *                2.  We can traverse forward and backward, making random node removal possibly without scanning the whole list.
 *                3.  We can lock the buffers in-front of and behind a given buffer to allow more efficient concurrency with list
 *                    manipulation.
 *                    (Note: we don't actually do this to keep concurrency/locking simpler for this test program.)
 *
 *              Clock sweep is the method this implementation uses for victim selection in the raw buffer list.  Combined with the
 *              'popularity' attribute we get approximate LRU with better performance.  This is similar to how Postgresql handles
 *              it.  In our implementation we decay faster by bit-shifting rather than decrementing a counter.  I'm not sure this
 *              improves anything but meh.
 *
 *              FIFO is the method this implementation uses for eviction from the compressed buffer list, because frankly nothing
 *              else makes sense; the buffer was already found to be dis-used and victimized.  If a compressed cache-hit doesn't
 *              save the buffer in time, it's dead.
 */

/* Include necessary headers here. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <time.h>      /* for clock_gettime() */
#include "buffer.h"
#include "error.h"
#include "options.h"
#include "list.h"
#include "options.h"

#include <locale.h> /* Remove me after debugging... probably */
#include <unistd.h> //Also remove after debug.


/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L
#define MILLION    1000000L

/* Limit generation size to help partition it and speed up victim selection. */
const int MAX_BUFFERS_PER_GENERATION = 32;


/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_IS_VICTIMIZED;
extern const int E_BUFFER_ALREADY_EXISTS;

/* We need some information from the buffer */
extern const int BUFFER_OVERHEAD;

/* Extern the global options. */
extern Options opts;



/* list__initialize
 * Creates the actual list that we're being given a pointer to.  We will also create the head of it as a reference point.
 */
List* list__initialize() {
  /* Quick error checking, then initialize the list.  We don't need to lock it because it's synchronous. */
  List *list = (List *)malloc(sizeof(List));
  if (list == NULL)
    show_error(E_GENERIC, "Failed to malloc a new list.");

  /* Size and Counter Members */
  list->count = 0;
  list->current_size = 0;
  list->max_size = 0;

  /* Locking, Reference Counters, and Similar Members */
  if (pthread_mutex_init(&list->lock, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize mutex for a list.  This is fatal.");
  list->lock_owner = 0;
  list->lock_depth = 0;
  if (pthread_cond_init(&list->writer_condition, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize writer condition for a list.  This is fatal.");
  if (pthread_cond_init(&list->reader_condition, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize reader condition for a list.  This is fatal.");
  list->ref_count = 0;
  list->pending_writers = 0;

  /* Management and Administration Members */
  list->offload_to = NULL;
  list->restore_to = NULL;
  list->sweep_goal = 5;
  list->sweeps = 0;
  list->sweep_cost = 0;
  list->offloads = 0;
  list->restorations = 0;

  /* Head Nodes of the List and Skiplist (Index). Make the Buffer list head a dummy buffer. */
  list->head = buffer__initialize(BUFFER_ID_MAX, NULL);
  list->head->next = list->head;
  list->clock_hand = list->head;
  list->levels = 1;
  list->youngest_generation = 0;
  list->oldest_generation = 0;
  list->generations_index_ceiling = 10000;
  list->generations = malloc((MAX_POPULARITY+1) * sizeof(Buffer *));
  if(list->generations == NULL)
    show_error(E_GENERIC, "Failed to allocate memory for the generations double pointer.");
  for(int i=0; i<=MAX_GENERATION; i++) {
    list->generations_index[i] = 0;
    list->generations[i] = calloc(list->generations_index_ceiling, sizeof(Buffer *));
  }
  SkiplistNode *slnode = NULL;
  for(int i=0; i<SKIPLIST_MAX; i++) {
    slnode = (SkiplistNode *)malloc(sizeof(SkiplistNode));
    if(slnode == NULL)
      show_error(E_GENERIC, "Failed to allocate memory for an slnode for list initialization.  This is fatal.");
    // The skiplist index heads should always target the list head buffer.
    slnode->target = list->head;
    // The right pointer (next) is always NULL starting off.
    slnode->right = NULL;
    // Set the down pointer to NULL to start.  If we're above level 0, point to the next lower level.
    slnode->down = NULL;
    if(i != 0)
      slnode->down = list->indexes[i-1];
    // Assign it to the correct index.
    list->indexes[i] = slnode;
  }

  /* Compressor Pool Management */
  pthread_mutex_init(&list->jobs_lock, NULL);
  pthread_cond_init(&list->jobs_cond, NULL);
  pthread_cond_init(&list->jobs_parent_cond, NULL);
  list->compressor_threads = calloc(opts.cpu_count, sizeof(pthread_t));
  if(list->compressor_threads == NULL)
    show_error(E_GENERIC, "Failed to allocate memory for the compressor_threads");
  for(int i=0; i<VICTIM_BATCH_SIZE; i++)
    list->victims[i] = NULL;
  list->victims_index = 0;
  list->victims_compressor_index = 0;
  list->active_compressors = 0;
  list->compressor_pool = calloc(opts.cpu_count, sizeof(Compressor));
  if(list->compressor_pool == NULL)
    show_error(E_GENERIC, "Failed to allocate memory for the compressor_pool");
  for(int i=0; i<opts.cpu_count; i++) {
    list->compressor_pool[i].jobs_cond = &list->jobs_cond;
    list->compressor_pool[i].jobs_lock = &list->jobs_lock;
    list->compressor_pool[i].jobs_parent_cond = &list->jobs_parent_cond;
    list->compressor_pool[i].active_compressors = &list->active_compressors;
    list->compressor_pool[i].runnable = 0;
    list->compressor_pool[i].victims = list->victims;
    list->compressor_pool[i].victims_index = &list->victims_index;
    list->compressor_pool[i].victims_compressor_index = &list->victims_compressor_index;
    pthread_create(&list->compressor_threads[i], NULL, (void*) &list__compressor_start, &list->compressor_pool[i]);
  }

  /* Debug */
  list->popping = 0;
  list->pop_remove = 0;
  list->find_victim = 0;
  list->add = 0;
  list->remove = 0;
  list->compression = 0;

  return list;
}


/* list__initialize_skiplistnode
 * Simply builds an empty skiplist node.
 */
SkiplistNode* list__initialize_skiplistnode(Buffer *buf) {
  SkiplistNode *slnode = (SkiplistNode *)malloc(sizeof(SkiplistNode));
  if(slnode == NULL)
    show_error(E_GENERIC, "Failed to allocate memory for a Skiplist Node.  This is fatal.");
  slnode->down = NULL;
  slnode->right = NULL;
  slnode->target = buf;
  return slnode;
}


/* list__acquire_write_lock
 * Drains the list of references so we don't modify the list while another thread is scanning it.  The combination of conditions
 * and mutexes should provide perfect consistency/synchronization.  Readers all use list__update_ref which respects these same
 * precepts.
 */
int list__acquire_write_lock(List *list) {
  /* Check to see if the current lock owner is us; this is free of race conditions because it's only a race when we own it... */
  if (pthread_equal(list->lock_owner, pthread_self())) {
    list->lock_depth++;
    return E_OK;
  }

  /* Block until we get the list lock, then we can safely update pending writers. */
  pthread_mutex_lock(&list->lock);
  if (list->lock_depth != 0)
    show_error(E_GENERIC, "A new lock chain is being established but someone else left the list with a non-zero depth.  This is fatal.");
  list->pending_writers++;
  /* Begin a predicate check while under the protection of the mutex.  Block if the predicate remains true. */
  while(list->ref_count != 0)
    pthread_cond_wait(&list->writer_condition, &list->lock);
  /* We now have the lock again and our predicate is guaranteed protected (it respects the list lock). */
  list->pending_writers--;
  /* Set ourself to the lock owner in case future paths function calls try to ensure this thread has the list locked. */
  list->lock_owner = pthread_self();
  list->lock_depth++;
  return E_OK;
}


/* list__release_write_lock
 * Unlocks the mutex for a list.  Begin the broadcast chain for either writesr or readers, depending on the predicate others are
 * blocking on.  Readers all use list__update_ref which respects these same precepts.
 */
int list__release_write_lock(List *list) {
  /* Decrement the lock depth.  If the lock depth is more than 0 we aren't done with the lock yet. */
  list->lock_depth--;
  if (list->lock_depth != 0)
    return E_OK;

  /* Determine if we need to notify other writers, or just the readers. */
  if(list->pending_writers > 0) {
    pthread_cond_broadcast(&list->writer_condition);
  } else {
    pthread_cond_broadcast(&list->reader_condition);
  }
  /* Remove ourself as the lock owner so another can take our place.  Don't have to... just NULL-ing for safety. */
  list->lock_owner = 0;
  /* Release the lock now that the proper broadcast is out there.  Releasing AFTER a broadcast is supported and safe. */
  pthread_mutex_unlock(&list->lock);
  return E_OK;
}


/* list__add
 * Adds a node to the list specified.  Buffer must be created by caller.  We will lock the list here so insertion is guaranteed
 * to be sort-ordered.  We will also fail safely if the buffer already exists because we don't want duplicates.
 */
int list__add(List *list, Buffer *buf) {
  /* Get a write lock. */
  list__acquire_write_lock(list);
  int rv = E_OK;

  /* Make sure we have room to add a new buffer before we proceed. */
  const uint32_t BUFFER_SIZE = BUFFER_OVERHEAD + (buf->comp_length == 0 ? buf->data_length : buf->comp_length);
  if (list->max_size < list->current_size + BUFFER_SIZE) {
    /* Determine the minimum sweep goal we need, then use the larger of the two. */
    const uint8_t MINIMUM_SWEEP_GOAL = (100 * (list->current_size + BUFFER_SIZE) / list->current_size) - 99;
    if (MINIMUM_SWEEP_GOAL > 99)
      show_error(E_GENERIC, "When trying to add a buffer, sweep goal was incremented to 100+ which would eliminate the entire list.  This is, I believe, a condition that should never happen.");
    /* Sweeping uses the write lock and checks the predicate again.  So we're safe to call it unlocked. */
    list__sweep(list, MINIMUM_SWEEP_GOAL > list->sweep_goal ? MINIMUM_SWEEP_GOAL : list->sweep_goal);
  }

  // Decide how many levels we're willing to set the node upon.
  int levels = 0;
  while((levels < SKIPLIST_MAX) && (levels < list->levels) && (rand() % 2 == 0))
    levels++;
  if(levels == list->levels)
    list->levels++;

  // Build a local stack based on the main list->indexes[] to build breadcrumbs.
  SkiplistNode *slstack[SKIPLIST_MAX];
  for(int i = 0; i < SKIPLIST_MAX; i++)
    slstack[i] = list->indexes[i];
  // Traverse the list to find the ideal location at each level.  Since we're searching, use list->levels as the start height.
  for(int i = list->levels-1; i >= 0; i--) {
    // Try shifting right until the ->right member is NULL or its value is too high.
    while(slstack[i]->right != NULL && slstack[i]->right->target->id <= buf->id)
      slstack[i] = slstack[i]->right;
    // If this node's ->target is the lists's head, we never left the index's head.  Just continue to the next level.
    if(slstack[i]->target == list->head)
      continue;
    // If the buffer already exists, flag it with rv and leave.
    if(slstack[i]->target->id == buf->id) {
      rv = E_BUFFER_ALREADY_EXISTS;
      break;
    }
    // Modify the next slstack node to look at the more-forward position we just jumped to.  If we're at index 0, skip it, we're done.
    if(i != 0)
      slstack[i-1] = slstack[i]->down;
  }

  // Continue searching the list from slstack[0] to ensure it doesn't already exist.  Then add to the list.
  if (rv == E_OK) {
    Buffer *nearest_neighbor = slstack[0]->target;
    // Move right in the buffer list.  ->head is always max, so no need to check anything but ->id.
    while(nearest_neighbor->next->id <= buf->id)
      nearest_neighbor = nearest_neighbor->next;
    if(nearest_neighbor->id == buf->id) {
      rv = E_BUFFER_ALREADY_EXISTS;
    } else {
      buf->next = nearest_neighbor->next;
      nearest_neighbor->next = buf;
      list->current_size += BUFFER_SIZE;
      list->count++;
    }
  }

  // Loop through the slstack and begin linking Skiplist Nodes together everything is still E_OK.
  if (rv == E_OK) {
    SkiplistNode *slnode = NULL;
    for(int i = 0; i < levels; i++) {
      // Create a new Skiplist Node for each level we'll be inserting at and insert it into that index.
      slnode = list__initialize_skiplistnode(buf);
      slnode->right = slstack[i]->right;
      slstack[i]->right = slnode;
    }
    // Now that the Nodes all exist (if any) and our slstack's ->right members point to them, we can set their ->down members.
    for(int i = levels - 1; i > 0; i--)
      slstack[i]->right->down = slstack[i-1]->right;
  }

  /* Let go of the write lock we acquired. */
  list__release_write_lock(list);
  return rv;
}


/* list__remove
 * Removes the buffer from the list's pool while respecting the list lock and readers.  Caller must grab and pass buffer_id before
 * unlocking its reference to the buffer; this way we can rely on finding the ID even if the buffer is removed by another thread.
 * Note: this function is not responsible for managing list sizes or HCRS logic.  It just removes buffers.
 */
int list__remove(List *list, bufferid_t id, bool destroy) {
  /* Get a write lock, which guarantees flushing all readers first. */
  list__acquire_write_lock(list);
  int rv = E_BUFFER_NOT_FOUND;

  /* Build a local stack of SkipistNode pointers to serve as a reference later. */
  SkiplistNode *slstack[SKIPLIST_MAX];
  for(int i = 0; i < SKIPLIST_MAX; i++)
    slstack[i] = list->indexes[i];

  /* Start forward scanning until we find it at each level. */
  int levels = 0;
  for(int i = list->levels - 1; i >= 0; i--) {
    // Shift right until the ->right target ID is too high.  Do NOT land on matches!  We don't have ->left pointers.
    while(slstack[i]->right != NULL && slstack[i]->right->target->id < id)
      slstack[i] = slstack[i]->right;
    // Set the next slstack index to the ->down member of the most-forward position thus far.  Skip if we're at 0.
    if(i != 0)
      slstack[i-1] = slstack[i]->down;
    // If ->right exists and its id matches increment levels so we can modify the skiplist levels later.
    if(slstack[i]->right != NULL && slstack[i]->right->target->id == id)
      levels++;
  }

  /* Set up the nearest neighbor from slstack[0] and start removing nodes and the buffer. */
  Buffer *nearest_neighbor = slstack[0]->target;
  while(nearest_neighbor->next != NULL && nearest_neighbor->next->id < id)
    nearest_neighbor = nearest_neighbor->next;
  // We should be close-as-can-be.  If ->next matches, victimize and remove it.  Otherwise we're still E_BUFFER_NOT_FOUND.
  if(nearest_neighbor->next->id == id) {
    rv = E_OK;
    Buffer *buf = nearest_neighbor->next;
    const uint32_t BUFFER_SIZE = BUFFER_OVERHEAD + (buf->comp_length == 0 ? buf->data_length : buf->comp_length);
    int victimize_status = buffer__victimize(buf);
    if (victimize_status != 0)
      show_error(victimize_status, "The list__remove function received an error when trying to victimize the buffer (%d).", victimize_status);
    // Update the list metrics and move the clock hand if it's pointing at the same address as buf.
    if(list->clock_hand == buf)
      list->clock_hand = list->clock_hand->next;
    list->count--;
    list->current_size -= BUFFER_SIZE;
    // Now change our ->next pointer for nearest_neighbor to drop this from the list, then destroy buf.
    nearest_neighbor->next = buf->next;
    if(destroy)
      buffer__destroy(buf);
    else {
      buf->victimized = 0;
      buffer__unlock(buf);
    }
  }

  /* Remove the Skiplist Nodes that were found at various levels. */
  SkiplistNode *slnode = NULL;
  for(int i = levels - 1; i >= 0; i--) {
    // Each of these levels was already found to have the node, so ->right->right has to exist or at least be NULL.
    slnode = slstack[i]->right;
    slstack[i]->right = slstack[i]->right->right;
    free(slnode);
    // If the list's skip-index at this level is empty, drop the list levels height.
    if(list->indexes[i]->right == NULL)
      list->levels--;
  }

  /* Let go of the write lock we acquired. */
  list__release_write_lock(list);
  return rv;
}


/* list__search
 * Searches for a buffer in the list specified so it can be sent back as a double pointer.  We need to pin the list so we can
 * search it.  When successfully found, we increment ref_count.
 */
int list__search(List *list, Buffer **buf, bufferid_t id) {
  /* Update the ref count so we can begin searching. */
  list__update_ref(list, 1);

  /* Begin searching the list at the highest level's index head. */
  int rv = E_BUFFER_NOT_FOUND;
  SkiplistNode *slnode = list->indexes[list->levels];
  while(rv == E_BUFFER_NOT_FOUND) {
    // Move right until we can't go farther.
    while(slnode->right != NULL && slnode->right->target->id <= id)
      slnode = slnode->right;
    // If the node matches, we're done!  Try to update the ref and assign everything appropriately.
    if(slnode->target->id == id) {
      rv = buffer__lock(slnode->target);
      if(rv == E_OK) {
        *buf = slnode->target;
        buffer__update_ref(*buf, 1);
      }
      // If E_OK or victimized we still need to unlock it.
      if(rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED)
        buffer__unlock(slnode->target);
    }
    // If we can't move down, leave.
    if(slnode->down == NULL)
      break;
    slnode = slnode->down;
  }

  /* If we're still E_BUFFER_NOT_FOUND, scan the nearest_neighbor until we find it. */
  if(rv == E_BUFFER_NOT_FOUND) {
    Buffer *nearest_neighbor = slnode->target;
    while(nearest_neighbor->next->id <= id)
      nearest_neighbor = nearest_neighbor->next;
    // If we got a match, score.  Our nearest_neighbor is now the match.  Update ref and assign things.
    if(nearest_neighbor->id == id) {
      rv = buffer__lock(nearest_neighbor);
      if(rv == E_OK) {
        *buf = nearest_neighbor;
        buffer__update_ref(*buf, 1);
      }
      // If E_OK or victimized we still need to unlock it.
      if(rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED)
        buffer__unlock(nearest_neighbor);
    }
  }

  /* Regardless of whether we found it we need to remove our pin on the list's ref count because we're done searching it. */
  list__update_ref(list, -1);

  /* If we were unable to find the buffer, see if an offload_to list exists for us to search. */
  if (rv == E_BUFFER_NOT_FOUND && list->offload_to != NULL) {
    rv = list__search(list->offload_to, buf, id);
    if (rv == E_OK) {
      // Found it!  To avoid a race, release our pin, acquire the write lock, and then search again under its protection.
      buffer__lock(*buf);
      buffer__update_ref(*buf, -1);
      buffer__unlock(*buf);
      list__acquire_write_lock(list);
      rv = list__search(list->offload_to, buf, id);
      if (rv == E_OK) {
        // We won any race to restore the given buffer.  Release our pin (again), restore it, and release the write lock protecting us.
        buffer__lock(*buf);
        buffer__update_ref(*buf, -1);
        buffer__unlock(*buf);
        if (list__restore(list, *buf) != E_OK)
          show_error(E_GENERIC, "Failed to list__restore a buffer.  This should never happen.");
        list__release_write_lock(list);
      } else {
        // We lost a race to restore the buffer.  It should exist in the raw list now.  Release the write lock and start all over.
        list__release_write_lock(list);
        // Currently list should equate to ->offload_to->restore_to; but this behavior may change later, so be pedantic, even if circular.
        rv = list__search(list->offload_to->restore_to, buf, id);
      }
    }
  }

  return rv;
}


/* list__update_ref
 * Edits the reference count of threads currently pinning the list.  Pinning happens for searching the list.  Delta should only be
 * 1 or -1, ever.  Also note: writer operations (list__add, list__remove) do *not* call this.  Unlike buffer__update_ref we will
 * lock the list for the caller since list-poofing isn't a reality like buffer poofing.
 */
int list__update_ref(List *list, int delta) {
  /* Do we own the lock as a writer?  If so, we don't need to mess with reference counting.  Avoids deadlocking. */
  if (pthread_equal(list->lock_owner, pthread_self()))
    return E_OK;

  /* Lock the list and check pending writers.  If non-zero and we're incrementing, wait on the reader condition. */
  int i_had_to_wait = 0;
  pthread_mutex_lock(&list->lock);
  if (delta > 0) {
    while(list->pending_writers > 0) {
      i_had_to_wait = 1;
      pthread_cond_wait(&list->reader_condition, &list->lock);
    }
  }
  list->ref_count += delta;

  /* When writers are waiting and we are decrementing, we need to broadcast to the writer condition that it's safe to proceed. */
  if (delta < 0 && list->pending_writers != 0 && list->ref_count == 0)
    pthread_cond_broadcast(&list->writer_condition);

  /* If we were forced to wait, others may have been too.  Call the broadcast again (once per waiter) so others will wake up. */
  if (i_had_to_wait > 0)
    pthread_cond_broadcast(&list->reader_condition);

  /* Release the lock, which will also finally unblock any threads we woke up with broadcast above.  Then leave happy. */
  pthread_mutex_unlock(&list->lock);
  return E_OK;
}


/* list__sweep
 * Attempts to run the sweep algorithm on the list to find space to free up.  Only a raw list should ever call this.  Removal of
 * compressed buffers is handled by list__pop().
 * Note:  We attempt to free a percentage of ->current_size, NOT ->max_size!  There are pros/cons to both; in normal usage the
 * current size should always be high enough to avoid errors because sweeping shouldn't be called until we're low on memory.
 */
uint32_t list__sweep(List *list, uint8_t sweep_goal) {
  // Acquire a list lock just to ensure we're operating safely.
  list__acquire_write_lock(list);
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__sweep() function was given a list that doesn't have an offload_to target.  This is definitely a problem.");

  // Variables and tracking data.
  struct timespec start, end;
  struct timespec s_pop, e_pop, s_find_victim, e_find_victim, s_add, e_add, s_remove, e_remove, s_compression, e_compression;
  clock_gettime(CLOCK_MONOTONIC, &start);
  int rv = E_OK;
  Buffer *victim = NULL;
  generation_t current_generation = list->offload_to->youngest_generation;
  uint32_t bytes_freed = 0;
  uint32_t bytes_to_add = 0;
  const uint32_t BYTES_NEEDED = list->current_size * sweep_goal / 100;

  // Check for a race condition.
  if ((list->current_size < list->max_size) && BYTES_NEEDED < (list->max_size - list->current_size)) {
    // Someone already did a sweep and freed up plenty of room.  Don't re-sweep in a race.
    list__release_write_lock(list);
    return rv;
  }
  list->sweeps++;

  // Loop forever to free up memory.  Memory checks happen near the end of the loop.
  while(true) {
    // Scan until we find a buffer to remove.  Popularity is halved until a victim is found.  Skip head matches.
clock_gettime(CLOCK_MONOTONIC, &s_find_victim);
    while(true) {
      list->clock_hand = list->clock_hand->next;
      if (list->clock_hand->popularity == 0 && list->clock_hand != list->head) {
        victim = list->clock_hand;
        break;
      }
      list->clock_hand->popularity >>= 1;
    }
clock_gettime(CLOCK_MONOTONIC, &e_find_victim);
list->find_victim += BILLION * (e_find_victim.tv_sec - s_find_victim.tv_sec) + e_find_victim.tv_nsec - s_find_victim.tv_nsec;
    // We only reach this when an unpopular victim id is found.  Update space we'll free and offload count.
    bytes_freed += victim->data_length + BUFFER_OVERHEAD;
    list->offloads++;
    // Assign the current generation and push on the generations_index stack.
    victim->generation = current_generation;
    if(list->offload_to->generations_index[current_generation] == list->offload_to->generations_index_ceiling) {
      for(int i=0; i<=MAX_GENERATION; i++) {
        *(list->offload_to->generations + i) = realloc(*(list->offload_to->generations + i), list->offload_to->generations_index_ceiling * 2 * sizeof(Buffer *));
        show_error(E_GENERIC, "Failed to recalloc memory to expand the generation index %"PRIu32" from %"PRIu32" to %"PRIu32".", i, list->offload_to->generations_index[i], list->offload_to->generations_index[i] * 2);
      }
      list->offload_to->generations_index_ceiling *= 2;
    }
    list->offload_to->generations[current_generation][list->offload_to->generations_index[current_generation]] = victim;
    list->offload_to->generations_index[current_generation]++;
    // Track the pointer so we can batch compress later, then remove it from the raw list.
    list->victims[list->victims_index] = victim;
    list->victims_index++;
clock_gettime(CLOCK_MONOTONIC, &s_remove);
    rv = list__remove(list, victim->id, false);
clock_gettime(CLOCK_MONOTONIC, &e_remove);
list->remove += BILLION * (e_remove.tv_sec - s_remove.tv_sec) + e_remove.tv_nsec - s_remove.tv_nsec;
    if (rv != E_OK)
      show_error(rv, "Failed to remove the selected victim while sweeping.  Not sure how.  Return code is %d", rv);

    // If the victim pool is full or we've found enough memory to free, flush everything and reset counters.
    if(list->victims_index == VICTIM_BATCH_SIZE || BYTES_NEEDED <= bytes_freed) {
      // Grab the jobs lock and rely on our condition to tell us when compressor_jobs is empty.
clock_gettime(CLOCK_MONOTONIC, &s_compression);
      pthread_mutex_lock(&list->jobs_lock);
      while(list->active_compressors > 0 || list->victims_index > list->victims_compressor_index) {
        pthread_cond_broadcast(&list->jobs_cond);
        pthread_cond_wait(&list->jobs_parent_cond, &list->jobs_lock);
      }
      pthread_mutex_unlock(&list->jobs_lock);
clock_gettime(CLOCK_MONOTONIC, &e_compression);
list->compression += BILLION * (e_compression.tv_sec - s_compression.tv_sec) + e_compression.tv_nsec - s_compression.tv_nsec;
      for(int i=0; i<list->victims_index; i++)
        bytes_to_add += list->victims[i]->comp_length + BUFFER_OVERHEAD;
clock_gettime(CLOCK_MONOTONIC, &s_pop);
      while((list->offload_to->current_size + bytes_to_add) > list->offload_to->max_size)
        list__pop(list->offload_to, bytes_to_add);
clock_gettime(CLOCK_MONOTONIC, &e_pop);
list->popping += BILLION * (e_pop.tv_sec - s_pop.tv_sec) + e_pop.tv_nsec - s_pop.tv_nsec;
clock_gettime(CLOCK_MONOTONIC, &s_add);
      for(int i=0; i<list->victims_index; i++) {
        rv = list__add(list->offload_to, list->victims[i]);
        if (rv != E_OK)
          show_error(rv, "Failed to send buf to the offload_list while sweeping.  Not sure how.  Return code is %d.", rv);
        list->victims[i] = NULL;
      }
clock_gettime(CLOCK_MONOTONIC, &e_add);
list->add += BILLION * (e_add.tv_sec - s_add.tv_sec) + e_add.tv_nsec - s_add.tv_nsec;
      list->victims_index = 0;
      list->victims_compressor_index = 0;
      bytes_to_add = 0;
      // Check once again to see if we're done scanning.  This prevents double checking via while() with every loop iteration.
      if(BYTES_NEEDED <= bytes_freed)
        break;
    }
  }

  // Wrap up and leave.
  clock_gettime(CLOCK_MONOTONIC, &end);
  list->sweep_cost += BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  list->offload_to->youngest_generation = list->offload_to->youngest_generation == MAX_GENERATION ? 0 : list->offload_to->youngest_generation + 1;
  list__release_write_lock(list);
  return bytes_freed;
}


/* list__pop
 * A wrapper to scan for the least popular buffer(s) in a compressed list and have it/them removed until bytes_needed is satisfied.
 */
int list__pop(List *list, uint64_t bytes_needed) {
  // Quick error checking.  Then grab a lock.
  if (list == NULL)
    show_error(E_GENERIC, "The list__pop function was given a null list.  This is fatal.");
  list__acquire_write_lock(list);

  // Loop until we've freed up enough memory.
  while(true) {
    // Loop through and remove buffers until we've freed up enough space or this generation is all gone.
    while(list->generations_index[list->oldest_generation] > 0 && bytes_needed > (list->max_size - list->current_size)) {
      list->generations_index[list->oldest_generation]--;
      list__remove(list, list->generations[list->oldest_generation][list->generations_index[list->oldest_generation]]->id, true);
      list->offloads++;
    }
    // If this generation is depleted, increment oldest_generation and continue scanning from the top.
    if(list->generations_index[list->oldest_generation] == 0) {
      list->oldest_generation = list->oldest_generation == MAX_GENERATION ? 0 : list->oldest_generation + 1;
      continue;
    }
    // If we reached this point we freed enough memory; otherwise we'd have jumped back to the top by now.
    break;
  }

  // Release lock and leave.
  list__release_write_lock(list);
  return E_OK;
}


/* list__restore
 * Takes the buffer specified and adds it back to the list specified after decompressing the data member and updating all of the
 * tracking data.
 */
int list__restore(List *list, Buffer *buf) {
  // Make sure we have a list, an offload_to, and a valid buffer.
  if (list == NULL)
    show_error(E_GENERIC, "The list__restore function was given a NULL list.  This should never happen.");
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__restore function was given a list without an offload_to target list.  This should never happen.");
  if (buf == NULL)
    show_error(E_GENERIC, "The list__restore function was given an invalid buffer.  This should never happen.");

  // Lock the raw list.  Offload list doesn't need it.
  list__acquire_write_lock(list);

  // Remove the buffer from the offload list without actually destroying it.
  list__remove(list->offload_to, buf->id, false);

  // Pop it from the generations array.
  generation_t my_generation = buf->generation;
  for(int i=0; i<list->offload_to->generations_index[my_generation]; i++) {
    if(list->offload_to->generations[my_generation][i] == buf) {
      // Found it.  Swap the last position with it, decrement index, and leave.
      list->offload_to->generations[my_generation][i] = list->offload_to->generations[my_generation][list->offload_to->generations_index[my_generation]-1];
      list->offload_to->generations_index[my_generation]--;
      break;
    }
    // If we hit the end and didn't break out, we screwed up.
    if(i == list->offload_to->generations_index[my_generation] - 1)
      show_error(E_GENERIC, "Trying to restore buffer %u with generation %u but couldn't find it in the generations list.  This should never happen.", buf->id, buf->generation);
  }

  // Now decompress the orphaned buffer, update its metrics, and add it to the list.
  if (buffer__decompress(buf) != E_OK)
    show_error(E_GENERIC, "The list__restore function was unable to decompress the new buffer.");
  buf->comp_hits++;
  buf->victimized = 0;
  buf->popularity = 1;
  buf->ref_count = 1;
  list__add(list, buf);

  // Update restorations and release the write lock.
  list->restorations++;
  list__release_write_lock(list);
  return E_OK;
}


/* list__balance
 * Redistributes memory between a list and it's offload target.  The list__sweep() and list__pop() functions will handle the
 * buffer migration while respecting the new boundaries.
 */
int list__balance(List *list, uint32_t ratio) {
  // As always, be safe.
  if (list == NULL)
    show_error(E_GENERIC, "The list__balance function was given a NULL list.  This should never happen.");
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__balance function was given a list with a NULL offload_to target.");
  list__acquire_write_lock(list);

  // Set the memory values according to the ratio.
  list->max_size = opts.max_memory * ratio / 100;
  list->offload_to->max_size = opts.max_memory - list->max_size;

  // Pop() the offload list if it shrunk.
  if (list->offload_to->current_size > list->offload_to->max_size)
    list__pop(list->offload_to, (list->offload_to->current_size - list->offload_to->max_size));

  // Try to sweep() the raw list if it shrunk.  Attempt to do this with a single call, if even necessary.
  if(list->current_size > list->max_size) {
    /* Determine the minimum sweep goal we need, then use the larger of the two. */
    const uint8_t MINIMUM_SWEEP_GOAL = (100 * list->current_size / list->current_size) - 99;
    if (MINIMUM_SWEEP_GOAL > 99)
      show_error(E_GENERIC, "When trying to balance the lists, sweep goal was incremented to 100+ which would eliminate the entire list.  This is, I believe, a condition that should never happen.");
    /* Sweeping uses the write lock and checks the predicate again.  So we're safe to call it unlocked. */
    list__sweep(list, MINIMUM_SWEEP_GOAL > list->sweep_goal ? MINIMUM_SWEEP_GOAL : list->sweep_goal);
  }

  // All done.  Release our lock and go home.
  list__release_write_lock(list);
  return E_OK;
}


/* list__destroy
 * Frees the data held by a list.
 */
int list__destroy(List *list) {
  while(list->head->next != list->head)
    list__remove(list, list->head->next->id, true);
  list__remove(list, list->head->id, true);
  free(list);
  return E_OK;
}


/* list__compressor_start
 * The is the initialization point for a compressor via pthread_create().
 */
void list__compressor_start(Compressor *comp) {
  // Try to do work forever.  We need to start off assuming we're an active worker.  It self-regulates within the loop.
  Buffer *work_me[COMPRESSOR_BATCH_SIZE];
  int work_me_count = 0;
  int rv = E_OK;
  (*comp->active_compressors)++;

  while(true) {
    // Secure the lock to test the predicate.  If there's no work to do, do some signaling and wait.
    pthread_mutex_lock(comp->jobs_lock);
    if((*comp->victims_index) == (*comp->victims_compressor_index)) {
      // There's no work to do.  Remove our active pin, notify the parent if pin count is 0, and then wait to be woken up again.
      (*comp->active_compressors)--;
      if(*comp->active_compressors == 0)
        pthread_cond_broadcast(comp->jobs_parent_cond);
      while((*comp->victims_index) == (*comp->victims_compressor_index) && comp->runnable == 0)
        pthread_cond_wait(comp->jobs_cond, comp->jobs_lock);
      // Someone woke us up and we have work to do.  Increment active counter.
      (*comp->active_compressors)++;
    }

    // We have a lock and our predicate indicates there's a job to do.  Try to grab it and start working, if we're allowed.
    if(comp->runnable != 0) {
      (*comp->active_compressors)--;
      pthread_cond_broadcast(comp->jobs_cond);
      pthread_mutex_unlock(comp->jobs_lock);
      break;
    }

    // Yep, we have work to do!  Grab some items and release the lock so others can have it.
    work_me_count = 0;
    while(work_me_count < COMPRESSOR_BATCH_SIZE && (*comp->victims_index) > (*comp->victims_compressor_index)) {
      work_me[work_me_count] = comp->victims[(*comp->victims_compressor_index)];
      work_me_count++;
      (*comp->victims_compressor_index)++;
    }
    pthread_mutex_unlock(comp->jobs_lock);
    pthread_cond_broadcast(comp->jobs_cond);
    for(int i = 0; i < work_me_count; i++) {
      rv = E_OK;
      rv = buffer__compress(work_me[i]);
      if(rv != E_OK)
        show_error(rv, "Compressor job failed to compress a buffer.  RV was %d.  Buffer id was %u", rv, work_me[i]->id);
      work_me[i] = NULL;
    }
  }
  return;
}
