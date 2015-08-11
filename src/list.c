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
    show_err("Failed to malloc the list.", E_GENERIC);
  list->count = 1;

  /* Create the head buffer and set it.  No locking required as this isn't a parallelized action. */
  Buffer *head = buffer__initialize(0);
  head->next = head;
  head->previous = head;
  list->head = head;

  /* Since we use list->count to handle the skip list upper bound, we don't need to do any initialization.  Just add head. */
  list->skiplist[0].id = head->id;
  list->skiplist[0].buf = head;

  return list;
}


/* list__add
 * Adds a node to the list specified.  Buffer must be created by caller.  We will lock the list here so insertion is guaranteed
 * to be sort-ordered.  We will also fail safely if the buffer already exists because we don't want duplicates.
 */
int list__add(List *list, Buffer *buf) {
  /* Drain the list of references so we don't modify the list while another thread is scanning it. */
  pthread_mutex_lock(&list->lock);
  list->pending_writers++;
  while(list->ref_count != 0)
    pthread_cond_wait(&list->writer_condition, &list->lock);
  list->pending_writers--;

  /* We now own the lock and no one should be scanning the list.  We can safely scan it ourselves and then edit it. */
  Buffer *previous, *next;
  int rv = E_OK, low = 0, high = list->count - 1, mid;
  previous = list->skiplist[low];
  next = list->skiplist[high];
  for(;;) {
    mid = (low + high)/2;

    // If our current skiplist[mid] ID is too high, update low and the prev pointer.
    if (list->skiplist[mid].id < buf->id) {
      previous = list->skiplist[mid].next;
      low = mid + 1;
      continue;
    }

    // If the skiplist[mid]->id matches, we have an error.  We can't add an existing buffer (duplicate).
    if (list->skiplist[mid].id == buf->id) {
      rv = E_BUFFER_ALREADY_EXISTS;
      break;
    }

    // If the skiplist[mid] ID is too low, we need to update high and next pointer.
    if(list->skiplist[mid].id > buf->id) {
      next = list->skiplist[mid].previous;
      high = mid - 1;
      continue;
    }

    /* If we're here we *should* have the correct prev and next buffers, and they should point to eachother.  Verify and set. */
    if (previous->next != next || next->previous != previous)
      show_err("The list__add function just produced an insertion point whose previous and next pointers don't point to eachother.", E_GENERIC);
    buf->next = next;
    buf->previous = previous;
    next->previous = buf;
    previous->next = buf;

    /* Update the skiplist by pushing.  Then bring the list count up to date and break out. */
    for(int i=list->count; i<mid; i--)
      list->skiplist[i] = list->skiplist[i-1];
    list->skiplist[mid] = buf;
    list->count++;
    break;
  }

  /* Start the notification chain for reader_conditions, release the lock and leave with whatever rv is. */
  pthread_cond_broadcast(&list->reader_condition);
  pthread_mutex_unlock(&list->lock);
  return rv;
}


/* list__remove
 * Removes the node from the list it is associated with.  We will ensure victimization happens.
 * This process merely removes it from a list specified; we don't handle the HCRS logic here.
 */
int list__remove(List *list, Buffer **buf) {
  /* Victimize the buffer so we can ensure it's flushed of references and we own it. */
  int rv = buffer__victimize(list, *buf);
  /* If the buffer poofed we can't work with it let alone remove it (someone else did).  It's unlocked at this point too. */
  if (rv == E_BUFFER_POOFED)
    return;
  if (rv != 0)
    show_err("The list__remove function received an error when trying to victimize the buffer.\n", rv);

  /* Store the lock id so we can unlock it below (because we're going NULL/free() the victimized buffer). */
  lockid_t lock_id = (*buf)->lock_id;
  Buffer *next = (*buf)->next;
  Buffer *previous = (*buf)->previous;

  /* NOW we can safely lock the list so we can move pointers and set the buffer to NULL and free it.  Since this is a manipulation
   * of just the ->next and ->previous pointers we only need the list lock, not the next/previous buffers' locks. */
  pthread_mutex_lock(&list->lock);
  previous->next = next;
  next->previous = previous;
  list->count--;
  pthread_mutex_unlock(&list->lock);
  lock__release(lock_id);

  /* Free()-ing is supposed to be threadsafe with -pthreads and gcc.  But this might fail with segfaults while readers try to read
   * the buffer we're about to free if I'm wrong... so we'll see.  We can wrap a shared lock around this to test if need be. */
  free(*buf);
  *buf = NULL;
}


/* list__search
 * Searches for a buffer in the list specified so it can be sent back as a double pointer.  We need to lock the list so we can
 * search for it.  When successfully found, we increment ref_count.
 */
int list__search(List *list, Buffer **buf, bufferid_t id) {
  int rv = 0;
  if (list == NULL)
    show_err("List specified is null.  This shouldn't ever happen.", E_GENERIC);
  /* Lock the list to ensure the buffers we're scanning don't poof while we perform checks. */
  pthread_mutex_lock(&list->lock);
  Buffer *temp = list->head->next;
  while (temp != list->head) {
    if (temp->id == id) {
      // Found it.  Grab the lock directly because we need to hold list lock to avoid a race condition.
      lock__acquire(temp->lock_id);
      pthread_mutex_unlock(&list->lock);
      if (rv == E_BUFFER_POOFED)
        return E_BUFFER_NOT_FOUND;
      // If we're here we got a buffer (possibly victimized but that's ok).  Assign pointer and update refs.  Then quit. */
      (*buf) = temp;
      buffer__update_ref(temp, 1);
      buffer__unlock(temp);  // We can release the lock because it's pinned now.
      return rv;
    }
    temp = temp->next;
  }
  // Didn't find it... boo...  If rv isn't 0 (i.e.: victimized), we need to let the caller know.
  pthread_mutex_unlock(&list->lock);
  if (rv > 0)
    return rv;
  return E_BUFFER_NOT_FOUND;
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
  if(delta > 0) {
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
