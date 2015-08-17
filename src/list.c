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
#include "buffer.h"
#include "lock.h"
#include "error.h"
#include "list.h"


/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_POOFED;
extern const int E_BUFFER_IS_VICTIMIZED;
extern const int E_BUFFER_ALREADY_EXISTS;


/* Functions */
/* list__initialize
 * Creates the actual list that we're being given a pointer to.  We will also create the head of it as a reference point.
 */
List* list__initialize() {
  /* Quick error checking, then initialize the list.  We don't need to lock it because it's synchronous. */
  List *list = (List *)malloc(sizeof(List));
  if (list == NULL)
    show_err("Failed to malloc a new list.", E_GENERIC);
  list->count = 0;
  list->ref_count = 0;
  list->pending_writers = 0;
  if (pthread_mutex_init(&list->lock, NULL) != 0)
    show_err("Failed to initialize mutex for a list.  This is fatal.", E_GENERIC);
  if (pthread_cond_init(&list->reader_condition, NULL) != 0)
      show_err("Failed to initialize reader condition for a list.  This is fatal.", E_GENERIC);
  if (pthread_cond_init(&list->writer_condition, NULL) != 0)
      show_err("Failed to initialize writer condition for a list.  This is fatal.", E_GENERIC);

  return list;
}


/* list__add
 * Adds a node to the list specified.  Buffer must be created by caller.  We will lock the list here so insertion is guaranteed
 * to be sort-ordered.  We will also fail safely if the buffer already exists because we don't want duplicates.
 * Note: this function is not responsible for managing list sizes or HCRS logic.  It just adds buffers.
 */
int list__add(List *list, Buffer *buf) {
  /* Drain the list of references so we don't modify the list while another thread is scanning it. */
  pthread_mutex_lock(&list->lock);
  list->pending_writers++;
  while(list->ref_count != 0)
    pthread_cond_wait(&list->writer_condition, &list->lock);
  list->pending_writers--;

  /* We now own the lock and no one should be scanning the list.  We can safely scan it ourselves and then edit it. */
  int rv = E_OK, low = 0, high = list->count, mid = 0;
  for(;;) {
    // Reset mid and begin testing.
    mid = (low + high)/2;

    // If low == high then mid also matches and we didn't find it (which is good!).  We're safe to shift (if needed) and insert.
    if (low == high) {
      for(int i=list->count; i<mid; i--)
        list->pool[i] = list->pool[i-1];
      list->pool[mid] = buf;
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

  /* Start the notification chain for reader_conditions, release the lock and leave with whatever rv is. */
  if (rv == 0)
    list->count++;
  pthread_cond_broadcast(&list->reader_condition);
  pthread_mutex_unlock(&list->lock);
  return rv;
}


/* list__remove
 * Removes the buffer from the list's pool while respecting the list lock and readers.
 * Note: this function is not responsible for managing list sizes or HCRS logic.  It just removes buffers.
 */
int list__remove(List *list, Buffer **buf) {
  /* Update the pending writers in case others are waiting. */
  pthread_mutex_lock(&list->lock);
  list->pending_writers++;
  while(list->ref_count != 0)
    pthread_cond_wait(&list->writer_condition, &list->lock);
  list->pending_writers--;

  /* At this point we own the list lock.  Try to victimize the buffer so we can remove it. */
  int rv = buffer__victimize(*buf);
  /* If the buffer poofed we can't work with it let alone remove it (someone else already did).  It's unlocked at this point too. */
  if (rv == E_BUFFER_POOFED)
    return E_OK;
  if (rv != 0)
    show_err("The list__remove function received an error when trying to victimize the buffer.\n", rv);

  /* We now have the list locked and the buffer fully victimized and locked.  Store the lock and begin searching for index. */
  int low = 0, high = list->count, mid = 0;
  rv = E_OK;
  lockid_t lock_id = (*buf)->lock_id;
  for(;;) {
    // Reset mid and begin testing.
    mid = (low + high)/2;

    // If the pool[mid] ID matches, we found the right index.  Collapse downward and break out.
    if (list->pool[mid]->id == (*buf)->id) {
      for (int i=mid; i<list->count; i++)
        list->pool[i] = list->pool[i+1];
      break;
    }

    // If our current pool[mid] ID is too high, update low.
    if (list->pool[mid]->id < (*buf)->id) {
      low = mid + 1;
      continue;
    }

    // If the pool[mid] ID is too low, we need to update high.
    if (list->pool[mid]->id > (*buf)->id) {
      high = mid - 1;
      continue;
    }

    // If low == high then mid also matches and we didn't find it.  This is a problem.
    if (low == high) {
      rv = E_BUFFER_NOT_FOUND;
      break;
    }
  }

  /* Update the count if everything is all good.  Then free the buf, null it out, and leave. */
  if (rv == 0)
    list->count--;
  free(*buf);
  *buf = NULL;
  lock__release(lock_id);
  pthread_cond_broadcast(&list->reader_condition);
  pthread_mutex_unlock(&list->lock);
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
  int rv = E_OK, low = 0, high = list->count, mid = 0;
  for(;;) {
    // Reset mid and begin testing.
    mid = (low + high)/2;
//printf("Looping through search.  Low, mid, and high are %d, %d, %d\n", low, mid, high);

    // If the pool[mid] ID matches, we found the right index.  Hooray!
    if (list->pool[mid]->id == id) {
//printf("  Found it.  Breaking.\n");
      *buf = list->pool[mid];
      break;
    }

    // If our current pool[mid] ID is too high, update low.
    if (list->pool[mid]->id < id) {
//printf("  The mid ID (%d) is lower than the id_to_get (%d).  Setting low to mid+1\n", mid, id);
      low = mid + 1;
      continue;
    }

    // If the pool[mid] ID is too low, we need to update high.
    if (list->pool[mid]->id > id) {
//printf("  The mid ID (%d) is higher then the id_to_get (%d).  Setting high to mid-1\n", mid, id);
      high = mid - 1;
      continue;
    }

    // If low == high then mid also matches and we didn't find it.  Break out and let the caller know.
    if (low == high) {
//printf("  Buffer wasn't found.  Sad face.\n");
      rv = E_BUFFER_NOT_FOUND;
      break;
    }
  }

  /* If we found it we need to update the buffer's ref count while we still own the list lock. */
  if (rv == E_OK) {
    buffer__lock(*buf);
    buffer__update_ref(*buf, 1);
    buffer__unlock(*buf);
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
