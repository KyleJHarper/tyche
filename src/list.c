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


/* Functions */
/* list__initialize
 * Creates the actual list that we're being given a pointer to.  We will also create the head of it as a reference point.
 */
List* list__initialize() {
  /* Quick error checking, then initialize the list. */
  List *list = (List *)malloc(sizeof(List));
  if (list == NULL)
    show_err("Failed to malloc the list.", E_GENERIC);
  list->count = 1;
  pthread_mutex_init(&list->lock, NULL);

  /* Create the head buffer and set it.  No locking required as this isn't a parallelized action. */
  Buffer *head = (Buffer *)malloc(sizeof(Buffer));
  if (head == NULL)
    show_err("Error malloc-ing head buffer in list_add.", E_GENERIC);
  head->next = head;
  head->previous = head;
  list->head = head;
  return list;
}

/* list__add
 * Adds a node to the list specified.
 */
Buffer* list__add(List *list) {
  /* Lock the list and make edits.  We could use a freelist if fragmentation becomes an issue. */
  pthread_mutex_lock(&list->lock);
  Buffer *new_node = (Buffer *)malloc(sizeof(Buffer));
  if (new_node == NULL)
    show_err("Error malloc-ing new buffer from list__add.", E_GENERIC);
  new_node->next = list->head->next;
  new_node->previous = list->head;
  list->head->next->previous = new_node;
  list->head->next = new_node;
  lock__assign_next_id(&new_node->lock_id);
  list->count++;
  pthread_mutex_unlock(&list->lock);
  return new_node;
}

/* list__remove
 * Removes the node from the list it is associated with.  We will ensure victimization happens.
 * This process merely removes it from a list specified; we don't handle the HCRS logic here.
 */
void list__remove(List *list, Buffer **buf) {
  /* Victimize the buffer so we can ensure it's flushed of references. */
  int rv = buffer__victimize(*buf);
  /* If the buffer poofed we can't work with it let alone remove it.  This should never happen but we'll protect against it. */
  if (rv == E_BUFFER_POOFED)
    return;
  if (rv != 0)
    show_err("The list__remove function received an error when trying to victimize the buffer.\n", rv);

  /* Lock the list so we can move pointers and set the buffer to NULL and free it. */
  pthread_mutex_lock(&list->lock);
  (*buf)->previous->next = (*buf)->next;
  (*buf)->next->previous = (*buf)->previous;

  /* Lock the buffer just to be safe.  This will return E_BUFFER_POOFED because victimized == 1 but that's ok. */
  buffer__lock(*buf);
  lockid_t lock_id = (*buf)->lock_id;
  free(*buf);
  *buf = NULL;
  lock__release(lock_id);
  list->count--;
  pthread_mutex_unlock(&list->lock);
}

/* list__count
 * Counts the number of elements in the list that the buffer is associated with.  Requires locking the list.
 */
uint32_t list__count(List *list) {
  uint32_t count = 0;
  Buffer *start = list->head;
  Buffer *current = list->head;
  pthread_mutex_lock(&list->lock);
  while(current->next != start) {
    count++;
    current = current->next;
  }
  pthread_mutex_unlock(&list->lock);
  /* Add one for head to give accurate count of the whole list. */
  return ++count;
}

/* list__acquire
 * Searches for a buffer in the list specified so it can be sent back as a double pointer.  We need to lock the list so we can
 * search for it.  The opposite of this function is to simply lower the ref_count; caller(s) need to do that themselves.
 */
int list__acquire(List *list, Buffer **buf, uint32_t id) {
  int rv = 0;
  if (list == NULL)
    show_err("List specified is null.  This shouldn't ever happen.", E_GENERIC);
  pthread_mutex_lock(&list->lock);
  Buffer *temp = list->head->next;
  while (temp != list->head) {
    if (temp->id == id) {
      // Found it.  Assign the double pointer and bail.
      rv = buffer__lock(temp);
      if (rv == E_BUFFER_POOFED || rv == E_BUFFER_NOT_FOUND) {
        // We need to leave because we didn't actually get the buffer, so don't adjust the count below.
        pthread_mutex_unlock(&list->lock);
        return rv;
      }
      (*buf) = temp;
      buffer__update_ref(temp, 1);
      buffer__unlock(temp);
      pthread_mutex_unlock(&list->lock);
      return E_OK;
    }
    temp = temp->next;
  }
  // Didn't find it... boo...
  pthread_mutex_unlock(&list->lock);
  return E_BUFFER_NOT_FOUND;
}
