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
#include <time.h>     /* for clock_gettime() */
#include "buffer.h"
#include "lock.h"
#include "error.h"
#include "options.h"
#include "list.h"
#include "options.h"

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
  list->clock_hand_index = 0;
  list->sweep_goal = 10;

  /* Buffer Array Itself */
  for (int i = 0; i < BUFFER_POOL_SIZE; i++)
    list->pool[i] = NULL;

  return list;
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
 * Note: this function is not responsible for managing list sizes or HCRS logic.  It just adds buffers.
 */
int list__add(List *list, Buffer *buf) {
  /* Get a write lock, which guarantees flushing all readers first. */
  list__acquire_write_lock(list);

  /* Make sure we have room to add a new buffer before we proceed. */
  const uint BUFFER_SIZE = BUFFER_OVERHEAD + (buf->comp_length == 0 ? buf->data_length : buf->comp_length);
  if (list->max_size < list->current_size + BUFFER_SIZE) {
    /* If our current size and sweep goal won't yield enough results, increase the goal until it will. */
    const uint8_t ORIGINAL_SWEEP_GOAL = list->sweep_goal;
    while (BUFFER_SIZE > (list->current_size * list->sweep_goal / 100) && list->sweep_goal < 99)
      list->sweep_goal++;
    if (list->sweep_goal >= 99)
      show_error(E_GENERIC, "The list__add function needs %d bytes freed but the current list size (%d bytes) cannot achieve that even with our sweep goal maxed.", list->current_size);
    const uint BYTES_FREED = list__sweep(list);
    list->sweep_goal = ORIGINAL_SWEEP_GOAL;
    if (BYTES_FREED < BUFFER_SIZE)
      show_error(E_GENERIC, "The list__sweep() function was couldn't free enough bytes to store the buffer.  Bytes reclaimed: %d, needed, %d.", BYTES_FREED, BUFFER_SIZE);
  }

  /* We now own the lock and no one should be scanning the list.  We have bytes free to hold it.  We can safely scan and edit. */
  int rv = E_OK, low = 0, high = list->count, mid = 0;
  for(;;) {
    // Reset mid and begin testing.
    mid = (low + high)/2;
    // If low > high or mid is at the top of the list then we didn't find it.  Time to shift items and update.
    if (low > high || mid == list->count) {
      // Integer division can give us the wrong mid since we're post-checking.  Make sure by checking to see if low > mid.
      if (low > mid)
        mid = low;
      for(int i=list->count; i>mid; i--)
        list->pool[i] = list->pool[i-1];
      list->pool[mid] = buf;
      list->count++;
      list->current_size += BUFFER_SIZE;
      if (list->clock_hand_index >= mid)
        list->clock_hand_index++;
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
int list__remove(List *list, bufferid_t id) {
  /* Get a write lock, which guarantees flushing all readers first. */
  list__acquire_write_lock(list);

  /* We now have the list locked and can begin searching for the index of id. */
  int low = 0, high = list->count, mid = 0, rv = E_OK;
  for(;;) {
    // Reset mid and begin testing.  Start with boundary testing to break if we're done.
    mid = (low + high)/2;
    if (high < low || low > high || mid >= list->count) {
      rv = E_BUFFER_NOT_FOUND;
      break;
    }

    // If the pool[mid] ID matches, we found the right index.  Victimize the buffer, collapse array downward, & update the list.
    if (list->pool[mid]->id == id) {
      const uint BUFFER_SIZE = BUFFER_OVERHEAD + (list->pool[mid]->comp_length == 0 ? list->pool[mid]->data_length : list->pool[mid]->comp_length);
      if (buffer__victimize(list->pool[mid]) != 0)
        show_error(rv, "The list__remove function received an error when trying to victimize the buffer (%d).\n", rv);
      buffer__destroy(list->pool[mid]);
      for (int i=mid; i<list->count - 1; i++)
        list->pool[i] = list->pool[i+1];
      list->pool[list->count - 1] = NULL;
      list->count--;
      list->current_size -= BUFFER_SIZE;
      if (list->clock_hand_index >= mid)
        list->clock_hand_index--;
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
 * Searches for a buffer in the list specified so it can be sent back as a double pointer.  We need to pin the list so we can
 * search it.  When successfully found, we increment ref_count.
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
      rv = buffer__lock(list->pool[mid]);
      if (rv == E_OK) {
        // We got a good buffer, update the ref and assign it to our pointer argument.
        *buf = list->pool[mid];
        buffer__update_ref(*buf, 1);
      }
      // If E_OK or victimized we still need to unlock it.
      if (rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED)
        buffer__unlock(list->pool[mid]);
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

  /* Regardless of whether we found it we need to remove our pin on the list's ref count because we're done searching it. */
  list__update_ref(list, -1);

  /* If we were unable to find the buffer, see if an offload_to list exists for us to search. */
  if (rv == E_BUFFER_NOT_FOUND && list->offload_to != NULL) {
    rv = list__search(list->offload_to, buf, id);
    if (rv == E_OK) {
      // We found it!  Remove our reference pin from the compressed buffer we just found.
      buffer__lock(*buf);
      buffer__update_ref(*buf, -1);
      buffer__unlock(*buf);
      // Restore the buffer.  Restoration always sets ref_count to 1 for us.
      if (list__restore(list, buf) != E_OK)
        show_error(E_GENERIC, "Failed to list__restore a buffer.  This should never happen.");
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
  /* Do we own the lock?  If so, we don't need to mess with reference counting.  Avoids reader interference. */
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
uint list__sweep(List *list) {
  // Acquire a list lock just to ensure we're operating safely.
  list__acquire_write_lock(list);

  // Set up some temporary stuff to calculate our needs and store victims.
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  list->sweeps++;
  uint bytes_freed = 0;
  const uint BYTES_NEEDED = list->current_size * list->sweep_goal / 100;
  Buffer *buf = NULL;
  List *temp_list = list__initialize();
  temp_list->max_size = BYTES_NEEDED * 2;
  temp_list->offload_to = temp_list;
  int rv = 0;

  // Sanity Checks
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__sweep() function was given a list that doesn't have an offload_to target.  This is definitely a problem.");
  while (BYTES_NEEDED > bytes_freed) {
    // Sweeping until we find a buffer to remove.  Popularity is halved until a victim is found.  The race condition on this is ok.
    for(;;) {
      if (list->clock_hand_index >= list->count)
        list->clock_hand_index = 0;
      if (list->pool[list->clock_hand_index]->popularity == 0)
        break;
      list->pool[list->clock_hand_index]->popularity = list->pool[list->clock_hand_index]->popularity >> 1;
      list->clock_hand_index++;
    }
    // We only reach this when an unpopular victim id is found.  Let's get to work copying the buffer and compressing it.
    buf = buffer__initialize(0, NULL);
    buffer__copy(list->pool[list->clock_hand_index], buf);
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
    rv = list__remove(list, list->pool[list->clock_hand_index]->id);
    if (rv != E_OK)
      show_error(rv, "Failed to remove the selected victim while sweeping.  Not sure how.  Return code is %d", rv);
    bytes_freed += buf->data_length - buf->comp_length;
  }

  // Now that we've freed up memory, move stuff from the temp_list to the offload (compressed) list.
  if (temp_list->current_size + list->offload_to->current_size > list->offload_to->max_size)
    list__pop(list->offload_to, temp_list->current_size);
  // This function is the ONLY one who should ever decrement popularity, creating "generations" for list__pop()-ing later.
  for (int i=0; i<list->offload_to->count; i++)
    list->offload_to->pool[i]->popularity--;
  for (int i=0; i<temp_list->count; i++)
    list__push(list->offload_to, temp_list->pool[i]);

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
 * To prevent excessive looping and work, we scan for the lowest popularity once, then remove buffers with that popularity.  If
 * our goal still isn't met, the low-threshold is incremented and we do it again.  This doesn't give true FIFO, but it give FIFO in
 * regard to "generations" of buffers.  Each generation is added with list__push() calls (from list__sweep()) and their popularity
 * is set to MAX_POPULARITY; at the end of each list__sweep() we decrement all buffers' popularity members.
 * See:  list__push() for this function's counterpart.
 */
int list__pop(List *list, uint64_t bytes_needed) {
  // Quick error checking.
  if (list == NULL)
    show_error(E_GENERIC, "The list__pop function was given a null list.  This is fatal.");

  // The caller will usually lock the list, but we'll do it just to be safe.
  list__acquire_write_lock(list);
  // Loop through and eliminate buffers until we have enough space to add everything.  First, find the lowest value.
  uint8_t lowest_popularity = MAX_POPULARITY;
  for(uint32_t i = 0; i < list->count; i++)
    lowest_popularity = lowest_popularity < list->pool[i]->popularity ? lowest_popularity : list->pool[i]->popularity;
  while (bytes_needed > (list->max_size - list->current_size)) {
    // Just for safety...
    if (lowest_popularity > MAX_POPULARITY)
      show_error(E_GENERIC, "The list__pop() function exceeded MAX_POPULARITY (%d) with %d without freeing enough memory.  This is fatal.", MAX_POPULARITY, lowest_popularity);
    // Remove buffers from the generation with popularity of lowest_popularity until we have enough space.
    for (uint32_t i = 0; i < list->count; i++) {
      if (list->pool[i]->popularity == lowest_popularity) {
        list__remove(list, list->pool[i]->id);
        if (bytes_needed <= (list->max_size - list->current_size))
          break;
        i--;
      }
    }
    lowest_popularity++;
  }

  list__release_write_lock(list);
  return E_OK;
}


/* list__restore
 * Takes the buffer specified and adds it back to the list specified after decompressing the data member and updating all of the
 * tracking data.
 */
int list__restore(List *list, Buffer **buf) {
  // Make sure we have a list, an offload_to, and a valid buffer.
  if (list == NULL)
    show_error(E_GENERIC, "The list__restore function was given a NULL list.  This should never happen.");
  if (list->offload_to == NULL)
    show_error(E_GENERIC, "The list__restore function was given a list without an offload_to target list.  This should never happen.");
  if (buf == NULL)
    show_error(E_GENERIC, "The list__restore function was given an invalid buffer.  This should never happen.");

  // Copy the buffer so we can begin restoring it.
  Buffer *new_buf = buffer__initialize(0, NULL);
  buffer__copy(*buf, new_buf);

  // Decompress the new_buf Buffer
  if (buffer__decompress(new_buf) != E_OK)
    show_error(E_GENERIC, "The list__restore function was unable to decompress the new buffer.");
  // Reset the victimization now that it's a valid buffer.  Set ref_count to 1 manually to avoid a little overhead.
  new_buf->victimized = 0;
  new_buf->ref_count = 1;

  // Add the decompressed copy to the list source list and remove the compressed buffer from the offload list.
  list__add(list, new_buf);
  list__remove(list->offload_to, (*buf)->id);

  // Assign the source pointer to our local copy's address and leave.
  *buf = new_buf;
  return E_OK;
}


/* list__balance
 * Redistributes memory between a list and it's offload target.  The list__sweep() and list__push/pop() functions will handle the
 * buffer migration while respecting the new boundaries.
 */
int list__balance(List *list, uint ratio) {
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
    uint8_t original_sweep_goal = list->sweep_goal;
    list->sweep_goal = (100 * list->max_size / list->current_size) + 1;
    if (list->sweep_goal > 99)
      show_error(E_GENERIC, "When trying to balance the lists, sweep goal was incremented to 100+ which would eliminate the entire list.  This is, I believe, a condition that should never happen.");
    list__sweep(list);
    list->sweep_goal = original_sweep_goal;
  }

  // All done.  Release our lock and go home.
  list__release_write_lock(list);
  return E_OK;
}
