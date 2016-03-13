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
#include <assert.h>
#include <inttypes.h>
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

/* Set a threshold for when a compressed buffer is a reasonable candidate for restoration. */
const int RESTORATION_THRESHOLD = 32;


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
  list->raw_count = 0;
  list->comp_count = 0;
  list->current_raw_size = 0;
  list->max_raw_size = 0;
  list->current_comp_size = 0;
  list->max_comp_size = 0;

  /* Locking, Reference Counters, and Similar Members */
  if (pthread_mutex_init(&list->lock, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize mutex for a list.  This is fatal.");
  list->lock_owner = 0;
  list->lock_depth = 0;
  if (pthread_cond_init(&list->writer_condition, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize writer condition for a list.  This is fatal.");
  if (pthread_cond_init(&list->reader_condition, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize reader condition for a list.  This is fatal.");
  if (pthread_cond_init(&list->sweeper_condition, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialized sweeper condition for a list.  This is fatal.");
  list->ref_count = 0;
  list->pending_writers = 0;

  /* Management and Administration Members */
  list->sweep_goal = 5;
  list->sweeps = 0;
  list->sweep_cost = 0;
  list->restorations = 0;

  /* Head Nodes of the List and Skiplist (Index). Make the Buffer list head a dummy buffer. */
  list->head = buffer__initialize(BUFFER_ID_MAX, NULL);
  list->head->next = list->head;
  list->clock_hand = list->head;
  list->levels = 1;
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
    list->comp_victims[i] = NULL;
  list->comp_victims_index = 0;
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
  /* Initialize a few basic values. */
  int rv = E_OK;
  int i_had_to_wait = 0;

  /* Grab the list lock so we can handle sweeping processes and signaling correctly. */
  pthread_mutex_lock(&list->lock);
  while(list->current_raw_size > list->max_raw_size) {
printf("Need to wait for the sweeper, waking it up.\n");
    i_had_to_wait = 1;
    pthread_cond_signal(&list->sweeper_condition);
    pthread_cond_wait(&list->reader_condition, &list->lock);
printf("Done waiting for sweeper.\n");
  }
  if(i_had_to_wait == 1)
    pthread_cond_broadcast(&list->reader_condition);
  pthread_mutex_unlock(&list->lock);

  /* Add a read pin to prevent a writer from removing anything from underneath us. */
  list__update_ref(list, 1);

  // Decide how many levels we're willing to set the node upon.
  int levels = 0;
  while((levels < SKIPLIST_MAX) && (levels < list->levels) && (rand() % 2 == 0))
    levels++;

  // Build a local stack based on the main list->indexes[] to build breadcrumbs.  Lock each buffer as we descend the skiplist tree.
  // until we have the whole chain.  Since scanning always down-and-forward we're safe.
  SkiplistNode *slstack[SKIPLIST_MAX];
  Buffer *locked_buffers[SKIPLIST_MAX];
  bufferid_t last_lock_id = BUFFER_ID_MAX;
  int locked_ids_index = -1;
  for(int i = 0; i < SKIPLIST_MAX; i++)
    slstack[i] = list->indexes[i];

  // Traverse the list to find the ideal location at each level.  Since we're searching, use list->levels as the start height.
  for(int i = list->levels; i >= 0; i--) {
    for(;;) {
      // Scan forward until we are as close as we can get.
      while(slstack[i]->right != NULL && slstack[i]->right->target->id <= buf->id)
        slstack[i] = slstack[i]->right;
      // Lock the buffer pointed to (if we haven't already) to effectively lock this SkiplistNode so we can test it.
      if(slstack[i]->target->id != last_lock_id)
        buffer__lock(slstack[i]->target);
      // If right is NULL or the ->right member is still bigger, we're as far over as we can go and should have a lock.
      if(slstack[i]->right == NULL || slstack[i]->right->target->id > buf->id) {
        if(slstack[i]->target->id != last_lock_id) {
          last_lock_id = slstack[i]->target->id;
          locked_ids_index++;
          locked_buffers[locked_ids_index] = slstack[i]->target;
        }
        break;
      }
      // Otherwise, someone inserted while we acquired this lock.  Release and try moving forward again.
      buffer__unlock(slstack[i]->target);
    }
    // If the buffer already exists, flag it with rv.  We'll release any locks we acquired before we leave.
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

  // Unlock any buffers we locked along the way.  Then remove our reference pin from the list.
  for(int i = locked_ids_index; i >= 0; i--)
    buffer__unlock(locked_buffers[i]);
  list__update_ref(list, -1);

  // If everything worked, grab the list lock whenever it's available and increment counters.
  if (rv == E_OK) {
    pthread_mutex_lock(&list->lock);
    if(levels == list->levels)
      list->levels++;
    list->raw_count++;
    list->current_raw_size += BUFFER_OVERHEAD + buf->data_length;
    pthread_mutex_unlock(&list->lock);
  }

  return rv;
}


/* list__remove
 * Removes the buffer from the list's pool while respecting the list lock and readers.  Caller must grab and pass buffer_id before
 * unlocking its reference to the buffer; this way we can rely on finding the ID even if the buffer is removed by another thread.
 * Note: this function is not responsible for managing list sizes or HCRS logic.  It just removes buffers.
 */
int list__remove(List *list, bufferid_t id) {
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
    if(buf->comp_length == 0) {
      list->current_raw_size -= BUFFER_SIZE;
      list->raw_count--;
    }
    else {
      list->current_comp_size -= BUFFER_SIZE;
      list->comp_count--;
    }
    // Now change our ->next pointer for nearest_neighbor to drop this from the list, then destroy buf.
    nearest_neighbor->next = buf->next;
    buffer__destroy(buf);
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
        // Assign it.
        *buf = nearest_neighbor;
        buffer__update_ref(*buf, 1);
      }
      // If E_OK or victimized we still need to unlock it.
      if(rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED)
        buffer__unlock(nearest_neighbor);
    }
  }

  /* If the buffer was found and is compressed, we need to send a decompressed copy or perform a restoration on it. */
  if(rv == E_OK && (*buf)->comp_length != 0) {
    if((*buf)->popularity < RESTORATION_THRESHOLD) {
      // Simply send back a copy.
      Buffer *copy = buffer__initialize(0, NULL);
      buffer__copy((*buf), copy);
      // The original buffer needs to be released since we're sending back a copy.
      buffer__lock(*buf);
      buffer__update_ref(*buf, -1);
      copy->is_ephemeral = 1;
      if(buffer__decompress(copy) != E_OK)
        show_error(E_GENERIC, "A buffer received a non-OK return code when being decompressed.");
      *buf = copy;
    } else {
      // This buffer warrants full restoration to the raw list.  Hooray for it!  Block for protection.
      buffer__block(*buf);
      if (buffer__decompress(*buf) != E_OK)
        show_error(E_GENERIC, "A buffer that deserved restoration failed to decompress properly.");
      buffer__unblock(*buf);
      pthread_mutex_lock(&list->lock);
      list->restorations++;
      pthread_mutex_unlock(&list->lock);
    }
  }

  /* Regardless of whether we found it we need to remove our pin on the list's ref count because we're done searching it. */
  list__update_ref(list, -1);
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
  if (delta > 0 && opts.workers > list->pending_writers) {
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
  // Variables and tracking data.
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  Buffer *victim = NULL;
  uint32_t bytes_freed = 0;
  uint32_t comp_bytes_added = 0;
  uint32_t total_victims = 0;
  const uint32_t BYTES_NEEDED = list->current_raw_size * sweep_goal / 100;
printf("sweep1\n");
  // Loop forever to free up memory.  Memory checks happen near the end of the loop.  Surround with a zero-check for initial balancing.
  if(BYTES_NEEDED != 0) {
    while(1) {
printf("sweep2: needed/freed... %d / %d\n", BYTES_NEEDED, bytes_freed);
      // Scan until we find a buffer to remove.  Popularity is halved until a victim is found.  Skip head matches.
      while(1) {
        list->clock_hand = list->clock_hand->next;
        if (list->clock_hand->popularity == 0 && list->clock_hand != list->head) {
          // If it's compressed, just update the comp_victims array (if possible) and continue.
          if (list->clock_hand->comp_length > 0) {
            if(list->comp_victims_index < VICTIM_BATCH_SIZE) {
              list->comp_victims[list->comp_victims_index] = list->clock_hand;
              list->comp_victims_index++;
            }
            continue;
          }
          // We found a raw buffer victim, yay.
          victim = list->clock_hand;
          break;
        }
        list->clock_hand->popularity >>= 1;
      }
      // We only reach this when an unpopular raw victim id is found.  Update space we'll free and start tracking victim.
      bytes_freed += victim->data_length;
      list->victims[list->victims_index] = victim;
      list->victims_index++;
      total_victims++;

      // If the victim pool is full or we've found enough memory to free, flush everything and reset counters.
      if(list->victims_index == VICTIM_BATCH_SIZE || BYTES_NEEDED <= bytes_freed) {
        // Grab the jobs lock and rely on our condition to tell us when compressor_jobs is empty.
printf("sweep3\n");
        pthread_mutex_lock(&list->jobs_lock);
printf("sweep4\n");
        while(list->active_compressors > 0 || list->victims_index > list->victims_compressor_index) {
printf("sweep4: waking up jobs_cond\n");
          pthread_cond_broadcast(&list->jobs_cond);
          pthread_cond_wait(&list->jobs_parent_cond, &list->jobs_lock);
printf("sweep4: parent was woken up\n");
        }
        pthread_mutex_unlock(&list->jobs_lock);
printf("sweep5\n");
        for(int i=0; i<list->victims_index; i++) {
          comp_bytes_added += list->victims[i]->comp_length;
          list->victims[i] = NULL;
        }
        for(int i=0; i<list->victims_index; i++)
        list->victims_index = 0;
        list->victims_compressor_index = 0;
        // Check to see if we're done scanning.  This prevents double checking via while() with every loop iteration.
        if(BYTES_NEEDED <= bytes_freed)
          break;
      }
    }
  }

  // We freed up enough raw space.  If comp space is too large start freeing up space.  Update some counters under write protection.
printf("I need the write lock... %u\n", list->ref_count);
  list__acquire_write_lock(list);
printf("I got the write lock.\n");
  list->compressions += total_victims;
  list->raw_count -= total_victims;
  list->comp_count += total_victims;
  list->current_raw_size -= bytes_freed;
  list->current_comp_size += comp_bytes_added;
  if(list->current_comp_size > list->max_comp_size) {
    for(int i=0; i<list->comp_victims_index && list->current_comp_size > list->max_comp_size; i++) {
      list__remove(list, list->comp_victims[i]->id);
      list->comp_victims[i] = NULL;
    }
    list->comp_victims_index = 0;
    // If we still haven't freed enough comp space, start clockhand motion again and take more comp victims.
    while(list->current_comp_size > list->max_comp_size) {
      while(1) {
        list->clock_hand = list->clock_hand->next;
        if (list->clock_hand->popularity == 0 && list->clock_hand != list->head && list->clock_hand->comp_length != 0)
            list__remove(list, list->clock_hand->id);
        list->clock_hand->popularity >>= 1;
      }
    }
  }
  // Wrap up and leave.
  clock_gettime(CLOCK_MONOTONIC, &end);
  list->sweeps++;
  list->sweep_cost += BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  list__release_write_lock(list);

  return bytes_freed;
}


/* list__balance
 * Redistributes memory between a list and it's offload target.  The list__sweep() and list__pop() functions will handle the
 * buffer migration while respecting the new boundaries.
 */
int list__balance(List *list, uint32_t ratio) {
  // As always, be safe.
  if (list == NULL)
    show_error(E_GENERIC, "The list__balance function was given a NULL list.  This should never happen.");
  list__acquire_write_lock(list);

  // Set the memory values according to the ratio.
  list->max_raw_size = opts.max_memory * ratio / 100;
  list->max_comp_size = opts.max_memory - list->max_raw_size;

  // Call list__sweep to clean up any needed raw space and remove any compressed buffers, if necessary.
  /* Determine the minimum sweep goal we need, then use the larger of the two. */
  const uint8_t MINIMUM_SWEEP_GOAL = list->current_raw_size > list->max_raw_size ? 101 - (100 * list->max_raw_size / list->current_raw_size) : 1;
  if (MINIMUM_SWEEP_GOAL > 99)
    show_error(E_GENERIC, "When trying to balance the lists, sweep goal was incremented to 100+ which would eliminate the entire list.  This is, I believe, a condition that should never happen.");
  list__sweep(list, MINIMUM_SWEEP_GOAL > list->sweep_goal ? MINIMUM_SWEEP_GOAL : list->sweep_goal);

  // All done.  Release our lock and go home.
  list__release_write_lock(list);
  return E_OK;
}


/* list__destroy
 * Frees the data held by a list.
 */
int list__destroy(List *list) {
  while(list->head->next != list->head)
    list__remove(list, list->head->next->id);
  list__remove(list, list->head->id);
  for(int i=0; i<SKIPLIST_MAX; i++)
    free(list->indexes[i]);
  for(int i=0; i<opts.cpu_count; i++)
    list->compressor_pool[i].runnable = 1;
  for(int i=0; i<opts.cpu_count; i++) {
    pthread_mutex_lock(&list->jobs_lock);
    pthread_cond_broadcast(&list->jobs_cond);
    pthread_mutex_unlock(&list->jobs_lock);
  }
  for(int i=0; i<opts.cpu_count; i++)
    pthread_join(list->compressor_threads[i], NULL);
  free(list->compressor_pool);
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

  while(1) {
    // Secure the lock to test the predicate.  If there's no work to do, do some signaling and wait.
printf("c-1\n");
    pthread_mutex_lock(comp->jobs_lock);
    if((*comp->victims_index) == (*comp->victims_compressor_index)) {
      // There's no work to do.  Remove our active pin, notify the parent if pin count is 0, and then wait to be woken up again.
      (*comp->active_compressors)--;
      if(*comp->active_compressors == 0)
        pthread_cond_broadcast(comp->jobs_parent_cond);
      while((*comp->victims_index) == (*comp->victims_compressor_index) && comp->runnable == 0)
        pthread_cond_wait(comp->jobs_cond, comp->jobs_lock);
      // Someone woke us up and we have work to do.  Increment active counter.
printf("Someone woke me up\n");
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
    pthread_cond_broadcast(comp->jobs_cond);
    pthread_mutex_unlock(comp->jobs_lock);
    for(int i = 0; i < work_me_count; i++) {
      rv = E_OK;
      buffer__block(work_me[i]);
printf("Iteration %d: Going to compress id %u\n", i, work_me[i]->id);
      rv = buffer__compress(work_me[i]);
      buffer__unblock(work_me[i]);
      if(rv != E_OK)
        show_error(rv, "Compressor job failed to compress a buffer.  RV was %d.  Buffer id was %u.  Data and comp lengths are %u %u", rv, work_me[i]->id, work_me[i]->data_length, work_me[i]->comp_length);
      work_me[i] = NULL;
    }
  }
  return;
}
