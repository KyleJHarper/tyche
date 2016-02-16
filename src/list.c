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
#include <time.h>      /* for clock_gettime() */
#include "buffer.h"
#include "error.h"
#include "options.h"
#include "list.h"
#include "options.h"

#include <locale.h> /* Remove me */


/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L
#define MILLION    1000000L

/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_IS_VICTIMIZED;
extern const int E_BUFFER_ALREADY_EXISTS;

/* We need some information from the buffer header */
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
    __sync_fetch_and_add(&list->levels, 1);

  // Get a reference pin so we can be sure to finish our operation before any removal (write) operations are released.
  list__update_ref(list, 1);

  // Build a local stack based on the main list->indexes[] to build breadcrumbs.  Lock each buffer as we descend the skiplist tree.
  // until we have the whole chain.  Since scanning always down-and-forward we're safe.
  SkiplistNode *slstack[SKIPLIST_MAX];
  Buffer *locked_buffers[SKIPLIST_MAX];
  bufferid_t last_lock_id = BUFFER_ID_MAX;
  int locked_ids_index = -1;
  for(int i = 0; i < SKIPLIST_MAX; i++)
    slstack[i] = list->indexes[i];

  // Traverse the list to find the ideal location at each level.  Since we're searching, use list->levels as the start height.
  for(int i = list->levels-1; i >= 0; i--) {
    for(;;) {
      // Scan forward until we are as close as we can get.
      while(slstack[i]->right != NULL && slstack[i]->right->target->id <= buf->id)
        slstack[i] = slstack[i]->right;
      // Lock the buffer pointed to (if we haven't already) to effectively lock this SkiplistNode so we can test it.
      if(slstack[i]->target->id != last_lock_id)
        buffer__lock(slstack[i]->target);
      // If right is NULL or the ->right member is still bigger, we're as far over as we can go and should have a lock.
      if(slstack[i]->right == NULL || slstack[i]->right->target->id > buf->id) {
        last_lock_id = slstack[i]->target->id;
        locked_ids_index++;
        locked_buffers[locked_ids_index] = slstack[i]->target;
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
      __sync_fetch_and_add(&list->current_size, BUFFER_SIZE);
      __sync_fetch_and_add(&list->count, 1);
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
    list->count--;
    list->current_size -= BUFFER_SIZE;
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
        if (list__restore(list, buf) != E_OK)
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

  // Set up some temporary stuff to calculate our needs and store victims.
  int rv = E_OK;
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  uint32_t bytes_freed = 0;
  const uint32_t BYTES_NEEDED = list->current_size * sweep_goal / 100;
  if ((list->current_size < list->max_size) && BYTES_NEEDED < (list->max_size - list->current_size)) {
    // Someone already did a sweep and freed up plenty of room.  Don't re-sweep in a race.
    list__release_write_lock(list);
    return rv;
  }
  list->sweeps++;
  Buffer *buf = NULL;
  List *temp_list = list__initialize();
  temp_list->max_size = BYTES_NEEDED * 2;
  temp_list->offload_to = temp_list;

  // Sanity Check.  Then start looping to free up memory.
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__sweep() function was given a list that doesn't have an offload_to target.  This is definitely a problem.");
  while (BYTES_NEEDED > bytes_freed) {
    // Scan until we find a buffer to remove.  Popularity is halved until a victim is found.  Skip head matches.
    for(;;) {
      if (list->clock_hand->popularity == 0 && list->clock_hand != list->head)
        break;
      list->clock_hand->popularity >>= 1;
      list->clock_hand = list->clock_hand->next;
    }

    // We only reach this when an unpopular victim id is found.  Let's get to work copying the buffer and compressing it.
    buf = buffer__initialize(0, NULL);
    buffer__copy(list->clock_hand, buf);
    rv = buffer__compress(buf);
    if (rv != E_OK)
      show_error(rv, "Failed to compress a buffer when performing list__sweep().  This shouldn't happen.  Return code was %d.", rv);
    // Reset reference and victimization elements on the copy since it's now a separate entity from it's doppelganger.
    buf->ref_count = 0;
    buf->victimized = 0;
    buf->popularity = 0;

    // Assign the new buffer to the temp_list, track the size difference, and remove the original buffer.
    rv = list__add(temp_list, buf);
    if (rv != E_OK)
      show_error(rv, "Failed to send buf to the temp_list while sweeping.  Not sure how.  Return code is %d.", rv);
    rv = list__remove(list, list->clock_hand->id);
    if (rv != E_OK)
      show_error(rv, "Failed to remove the selected victim while sweeping.  Not sure how.  Return code is %d", rv);
    bytes_freed += buf->data_length - buf->comp_length;
  }

  // Now that we've freed up memory, move stuff from the temp_list to the offload (compressed) list.
  if (temp_list->current_size + list->offload_to->current_size > list->offload_to->max_size)
    list__pop(list->offload_to, temp_list->current_size);
  // Decrement popularity, creating "generations" for list__pop()-ing later.
  Buffer *current = list->offload_to->head;
  while(current->next != list->offload_to->head) {
    current = current->next;
    current->popularity--;
  }
  current = temp_list->head;
  Buffer *next = current->next;
  while(next != temp_list->head) {
    current = next;
    next = next->next;
    list__push(list->offload_to, current);
    list->offloads++;
  }

  // Wrap up and leave.
  clock_gettime(CLOCK_MONOTONIC, &end);
  list->sweep_cost += BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  free(temp_list);
  list__release_write_lock(list);
  return bytes_freed;
}


/* list__push
 * A wrapper that will prepare a compressed buffer for "pushing" to the specified list.  For searching purposes we actually
 * maintain sorted lists; so we need to ensure that certain things are done to the buffer's members before list__add()-ing it.
 * See: list__pop() for this function's counterpart.
 */
int list__push(List *list, Buffer *buf) {
  // Quick error checking.
  if (list == NULL || buf == NULL)
    show_error(E_GENERIC, "The list__push function was given a null List or Buffer.  This is fatal.");

  // The caller will usually lock the list, but we'll do it just to be safe.
  list__acquire_write_lock(list);

  // Buffers being recently pushed are popular with regard to other items in a compressed list.  This helps emulate FIFO.
  buf->popularity = MAX_POPULARITY;

  // Reset the buf's victimized so that list__search will work as intended on a compressed list.
  buf->victimized = 0;

  // That's really all for now.  The list__add() function will handle list size tracking and so forth.
  list__add(list, buf);
  list__release_write_lock(list);

  return E_OK;
}


/* list__pop
 * A wrapper to scan for the least popular buffer(s) in a compressed list and have it/them removed until bytes_needed is satisfied.
 * See:  list__push() for this function's counterpart.
 */
int list__pop(List *list, uint64_t bytes_needed) {
  // Quick error checking.
  if (list == NULL)
    show_error(E_GENERIC, "The list__pop function was given a null list.  This is fatal.");

  // The caller will usually lock the list, but we'll do it just to be safe.
  list__acquire_write_lock(list);

  // Set up some tracking items.
  uint8_t lowest_popularity = MAX_POPULARITY;
  Buffer *current = NULL;
  int to_be_freed[MAX_POPULARITY+1];
  SkiplistNode *popularity_slstack[MAX_POPULARITY+1];
  SkiplistNode *slstack_forward_nodes[MAX_POPULARITY+1];
  SkiplistNode *slnode = NULL;
  for(int i = 0; i <= MAX_POPULARITY; i++) {
    popularity_slstack[i] = list__initialize_skiplistnode(NULL);
    slstack_forward_nodes[i] = popularity_slstack[i];
    to_be_freed[i] = 0;
  }

  // Loop until we find enough memory to free up.
  current = list->head;
  while (bytes_needed > (list->max_size - list->current_size)) {
    while(current->next != list->head) {
      current = current->next;
      // Rapidly decline to the lowest popularity item and track all occurrences of the generation.
      if(current->popularity > lowest_popularity)
        continue;
      to_be_freed[current->popularity] += BUFFER_OVERHEAD + (current->comp_length == 0 ? current->data_length : current->comp_length);
      // Set lowest and add a new node to this stack's list.  The ->right is always NULL, so just change the forward pointer.
      lowest_popularity = current->popularity;
      slnode = list__initialize_skiplistnode(current);
      slstack_forward_nodes[lowest_popularity]->right = slnode;
      slstack_forward_nodes[lowest_popularity] = slnode;
      // If a long enough list exists to free all required memory, leave.
      if(to_be_freed[lowest_popularity] >= bytes_needed)
        break;
    }
    // Free up the memory at the lowest priority.  We'll kill slnodes later below.
    slnode = popularity_slstack[lowest_popularity];
    while(slnode->right != NULL) {
      slnode = slnode->right;
      list__remove(list, slnode->target->id);
      list->offloads++;
      slnode->target = NULL;
    }
    // Set current to the most-forward position of the next lowest popularity found in case we need to scan for more memory.
    for(int new_low = lowest_popularity+1; new_low <= MAX_POPULARITY; new_low++) {
      if(popularity_slstack[new_low]->right != NULL) {
        current = slstack_forward_nodes[new_low]->target;
        lowest_popularity = new_low;
        break;
      }
    }
  }

  // Clean up the memory we used to build the slstacks.
  for(int i = 0; i <= MAX_POPULARITY; i++) {
    while(popularity_slstack[i]->right != NULL) {
      slnode = popularity_slstack[i]->right;
      popularity_slstack[i]->right = slnode->right;
      free(slnode);
    }
    free(popularity_slstack[i]);
  }

  list__release_write_lock(list);
  return E_OK;
}


/* list__restore
 * Takes the buffer specified and adds it back to the list specified after decompressing the data member and updating all of the
 * tracking data.
 * Caller MUST acquire the list's write lock; this prevents another reader from pinning the buffer we're about to remove.
 */
int list__restore(List *list, Buffer **buf) {
  // Make sure we have a list, an offload_to, and a valid buffer.
  if (list == NULL)
    show_error(E_GENERIC, "The list__restore function was given a NULL list.  This should never happen.");
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__restore function was given a list without an offload_to target list.  This should never happen.");
  if (buf == NULL)
    show_error(E_GENERIC, "The list__restore function was given an invalid buffer.  This should never happen.");

  // Lock both lists, for safety.
  list__acquire_write_lock(list);
  list__acquire_write_lock(list->offload_to);

  // Copy the buffer so we can begin restoring it.  Bump comp_hits since it's most logical here.
  Buffer *new_buf = buffer__initialize(0, NULL);
  buffer__copy(*buf, new_buf);
  new_buf->comp_hits++;

  // Decompress the new_buf Buffer
  if (buffer__decompress(new_buf) != E_OK)
    show_error(E_GENERIC, "The list__restore function was unable to decompress the new buffer.");
  // Reset the victimization now that it's a valid buffer.  Set ref_count to 1 manually to avoid a little overhead.
  new_buf->victimized = 0;
  new_buf->ref_count = 1;

  // Add the decompressed copy to the list source list and remove the compressed buffer from the offload list.
  list__add(list, new_buf);
  list__remove(list->offload_to, (*buf)->id);

  // Assign the source pointer to our local copy's address, release write locks, and leave.
  *buf = new_buf;
  list->restorations++;
  list__release_write_lock(list);
  list__release_write_lock(list->offload_to);
  return E_OK;
}


/* list__balance
 * Redistributes memory between a list and it's offload target.  The list__sweep() and list__push/pop() functions will handle the
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
    list__remove(list, list->head->next->id);
  list__remove(list, list->head->id);
  free(list);
  return E_OK;
}
