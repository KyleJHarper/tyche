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
#include "buffer.h"
#include "lock.h"
#include "error.h"
#include "list.h"


/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_ALREADY_EXISTS;

/* We need some information from the buffer header */
extern const int BUFFER_OVERHEAD;


/* Functions */
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
  list->lock_owner = NULL;
  if (pthread_cond_init(&list->writer_condition, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize writer condition for a list.  This is fatal.");
  if (pthread_cond_init(&list->reader_condition, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize reader condition for a list.  This is fatal.");
  list->ref_count = 0;
  list->pending_writers = 0;

  /* Management and Administration Members */
  list->offload_to = NULL;
  list->clock_hand_index = 0;
  list->sweep_goal = 1;

  /* Buffer Array Itself */
  for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    list->pool[i] = NULL;

  return list;
}


/* list__destroy
 * Destroys the elements of a list, specifically the Buffer pointers in the pool.
 */
int list__destroy(List *list) {
  for (int i = 0; i < list->count - 1; i++) {
    free(list->pool[i]);
    list->pool[i] = NULL;
  }
  free(list);
  return E_OK;
}


/* list__acquire_write_lock
 * Drains the list of references so we don't modify the list while another thread is scanning it.  The combination of conditions
 * and mutexes should provide perfect consistency/synchronization.  Readers all use list__update_ref which respects these same
 * precepts.
 */
int list__acquire_write_lock(List *list) {
  /* Check to see if the current lock owner is us; this is free of race conditions because it's only a race when we own it... */
  if (pthread_equal(list->lock_owner, pthread_self()))
    return E_OK;

  /* Block until we get the list lock, then we can safely update pending writers. */
  pthread_mutex_lock(&list->lock);
  list->pending_writers++;
  /* Begin a predicate check while under the protection of the mutex.  Block if the predicate remains true. */
  while(list->ref_count != 0)
    pthread_cond_wait(&list->writer_condition, &list->lock);
  /* Set ourself to the lock owner in case future paths function calls try to ensure this thread has the list locked. */
  list->lock_owner = pthread_self();
  /* We now have the lock again and our predicate is guaranteed protected (it respects the list lock); decrement and leave. */
  list->pending_writers--;
  return E_OK;
}


/* list__release_write_lock
 * Unlocks the mutex for a list.  Begin the broadcast chain for either writesr or readers, depending on the predicate others are
 * blocking on.  Readers all use list__update_ref which respects these same precepts.
 */
int list__release_write_lock(List *list) {
  /* Determine if we need to notify other writers, or just the readers. */
  if(list->pending_writers > 0) {
    pthread_cond_broadcast(&list->writer_condition);
  } else {
    pthread_cond_broadcast(&list->reader_condition);
  }
  /* Remove ourself as the lock owner so another can take our place.  Don't have to... just NULL-ing for safety. */
  list->lock_owner = NULL;
  /* Release the lock now that the proper broadcast is out there.  Releasing AFTER a broadcast is supported and safe. */
  pthread_mutex_unlock(&list->lock);
  return E_OK;
}


/* list__add
 * Adds a node to the list specified.  Buffer must be created by caller.  We will lock the list here so insertion is guaranteed
 * to be sort-ordered.  We will also fail safely if the buffer already exists because we don't want duplicates.
 * Note: this function is not responsible for managing list sizes or HCRS logic.  It just adds buffers.
 */
int list__add(List *list, Buffer *buf) {
  /* Get a write lock, which guarantees flushing all readers first. */
  list__acquire_write_lock(list);

  /* Make sure we have room to add a new buffer before we proceed. */
  const uint BUFFER_SIZE = BUFFER_OVERHEAD + (buf->comp_length == 0 ? buf->data_length : buf->comp_length);
  if (list->max_size < list->current_size + BUFFER_SIZE) {
    const uint BYTES_FREED = list__sweep(list);
    if (BYTES_FREED < BUFFER_SIZE)
      show_error(E_GENERIC, "The list__sweep() function was couldn't free enough bytes to store the buffer.  Bytes reclaimed: %d, needed, %d.", BYTES_FREED, BUFFER_SIZE);
  }

  /* We now own the lock and no one should be scanning the list.  We have bytes free to hold it.  We can safely scan and edit. */
  int rv = E_OK, low = 0, high = list->count, mid = 0;
  for(;;) {
    // Reset mid and begin testing.
    mid = (low + high)/2;

    // If low == high then mid also matches and we didn't find it (which is good!).  We're safe to shift (if needed) and insert.
    if (low == high || mid == list->count) {
      for(int i=list->count; i<mid; i--)
        list->pool[i] = list->pool[i-1];
      list->pool[mid] = buf;
      list->count++;
      list->current_size += BUFFER_SIZE;
      break;
    }

    // If the pool[mid] ID matches, we have an error.  We can't add an existing buffer (duplicate).
    if (list->pool[mid]->id == buf->id) {
      rv = E_BUFFER_ALREADY_EXISTS;
      break;
    }

    // If our current pool[mid] ID is too high, update low.
    if (list->pool[mid]->id < buf->id) {
      low = mid + 1;
      continue;
    }

    // If the pool[mid] ID is too low, we need to update high.
    if (list->pool[mid]->id > buf->id) {
      high = mid - 1;
      continue;
    }
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
int list__remove(List *list, Buffer *buf, bufferid_t id) {
  /* Get a write lock, which guarantees flushing all readers first. */
  list__acquire_write_lock(list);

  /* We now have the list locked and can begin searching for the index of id. */
  const uint BUFFER_SIZE = BUFFER_OVERHEAD + (buf->comp_length == 0 ? buf->data_length : buf->comp_length);
  int low = 0, high = list->count, mid = 0, rv = E_OK;
  for(;;) {
    // Reset mid and begin testing.  Start with boundary testing to break if we're done.
    mid = (low + high)/2;
    if (high < low || low > high || mid >= list->count) {
      rv = E_BUFFER_NOT_FOUND;
      break;
    }

    // If the pool[mid] ID matches, we found the right index which means buf is a valid pointer.  Victimize the buffer, collapse array downward, & update the list.
    if (list->pool[mid]->id == id) {
      if (buffer__victimize(buf) != 0)
        show_error(rv, "The list__remove function received an error when trying to victimize the buffer (%d).\n", rv);
      lockid_t lock_id = buf->lock_id;
      free(list->pool[mid]);
      lock__release(lock_id);
      for (int i=mid; i<list->count - 1; i++)
        list->pool[i] = list->pool[i+1];
      list->pool[list->count - 1] = NULL;
      list->count--;
      list->current_size -= BUFFER_SIZE;
      break;
    }

    // If our current pool[mid] ID is too high, update low.
    if (list->pool[mid]->id < id) {
      low = mid + 1;
      continue;
    }

    // If the pool[mid] ID is too low, we need to update high.
    if (list->pool[mid]->id > id) {
      high = mid - 1;
      continue;
    }
  }

  /* Let go of the write lock we acquired. */
  list__release_write_lock(list);
  return rv;
}


/* list__search
 * Searches for a buffer in the list specified so it can be sent back as a double pointer.  We need to lock the list so we can
 * search for it.  When successfully found, we increment ref_count.
 */
int list__search(List *list, Buffer **buf, bufferid_t id) {
  /* Update the ref count so we can begin searching. */
  list__update_ref(list, 1);

  /* Begin searching the list. */
  int rv = E_BUFFER_NOT_FOUND, low = 0, high = list->count, mid = 0;
  for(;;) {
    // Reset mid and begin testing.  Start with boundary testing to break if we're done.
    mid = (low + high)/2;
    if (high < low || low > high || mid >= list->count)
      break;

    // If the pool[mid] ID matches, we found the right index.  Hooray!  Update ref count if possible.  If not, let caller know.
    if (list->pool[mid]->id == id) {
      rv = E_OK;
      *buf = list->pool[mid];
      if (buffer__lock(*buf) == E_OK)
        buffer__update_ref(*buf, 1);
      buffer__unlock(*buf);
      break;
    }

    // If our current pool[mid] ID is too high, update low.
    if (list->pool[mid]->id < id) {
      low = mid + 1;
      continue;
    }

    // If the pool[mid] ID is too low, we need to update high.
    if (list->pool[mid]->id > id) {
      high = mid - 1;
      continue;
    }
  }

  /* Regardless of whether we found it we need to remove our pin on the list's ref count and let the caller know. */
  list__update_ref(list, -1);
  return rv;
}


/* list__update_ref
 * Edits the reference count of threads currently pinning the list.  Pinning happens for searching the list.  Delta should only be
 * 1 or -1, ever.  Also note: writer operations (list__add, list__remove) do *not* call this.  Unlike buffer__update_ref we will
 * lock the list for the caller since list-poofing isn't a reality like buffer poofing.
 */
int list__update_ref(List *list, int delta) {
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
 * The caller MUST acquire the list lock BEFORE calling this!!!
 */
uint list__sweep(List *list) {
  uint bytes_freed = 0;
  const uint BYTES_NEEDED = list->current_size * list->sweep_goal / 100;
  Buffer *buf = NULL;
  List temp_list = list__initialize();

  // Sanity Checks
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__sweep() function was given a list that doesn't have an offload_to target.  This is definitely a problem.");

  while (BYTES_NEEDED > bytes_freed) {
    // Start sweeping until we find a buffer to remove.  Popularity is halved until a victim is found.
    for(;;) {
      if (list->clock_hand_index >= list->count)
        list->clock_hand_index = 0;
      if (list->pool[list->clock_hand_index]->popularity == 0)
        break;
      list->pool[list->clock_hand_index]->popularity = list->pool[list->clock_hand_index]->popularity >> 1;
      list->clock_hand_index++;
    }
    // We only reach this when an unpopular victim id is found.  Let's get to work.
    buffer__copy(list->pool[list->clock_hand_index], buf);

    // make a copy of the buffer
    // compress the new buffer
    // assign the new buffer to the temp_list with list__add()
    // bytes_freed += sizeof(list->pool[bufferid])
    // call list__remove() to victimize and safely nuke the buffer
    // if compressed list doesn't have room, list__pop(list->offload_to) until there is enough room.
  }
  return bytes_freed;
}
