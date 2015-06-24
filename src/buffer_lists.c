/*
 * buffer_lists.c
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
#include "buffer.h"
#include "buffer_lists.h"
#include "error.h"


/* Globals for ease. */
Buffer *raw_list;                                            /* Circular list of raw buffers. */
Buffer *comp_list;                                           /* Circular list of compressed buffers. */
pthread_mutex_t raw_list_lock = PTHREAD_MUTEX_INITIALIZER;   /* Lock for the raw list. */
pthread_mutex_t comp_list_lock = PTHREAD_MUTEX_INITIALIZER;  /* Lock for the compressed list. */


/* Functions */
/* list__initialize
 * Creates the two lists we'll need need to maintain.  The raw list and the compressed list. Sets up the first node which is
 * special because it references only itself.  Allows us to avoid checking *list == NULL.
 */
void list__initialize() {
  /* Raw List */
  if (raw_list != NULL)
    show_err("Raw list must be NULL, otherwise this is going to fail.", E_GENERIC);
  raw_list = (Buffer *)malloc(sizeof(Buffer));
  if (raw_list == NULL)
    show_err("Failed to malloc the head of the raw_list.", E_GENERIC);
  raw_list->next = raw_list;
  lock__assign_next_id(&raw_list->lock_id);

  /* Compressed List */
  if (comp_list != NULL)
    show_err("Compressed list must be NULL, otherwise this is going to fail.", E_GENERIC);
  comp_list = (Buffer *)malloc(sizeof(Buffer));
  if (comp_list == NULL)
    show_err("Failed to malloc the head of the comp_list.", E_GENERIC);
  comp_list->next = comp_list;
  lock__assign_next_id(&comp_list->lock_id);
}

/* list__add
 * Adds a node to the list specified.  By 'list' we really just mean the node buffer in the list, always 'head'.
 */
void list__add(Buffer *list) {
  pthread_mutex_t *lock = raw_list_lock;
  if (list == comp_list)
    lock = comp_list_lock;

  /* Lock the list and make edits.  We could use a freelist if fragmentation becomes an issue. */
  pthread_mutex_lock(&lock);
  Buffer *new_node = (Buffer *)malloc(sizeof(Buffer));
  if (new_node == NULL)
    show_err("Error malloc-ing new buffer from list__add.", E_GENERIC);
  new_node->next = list->next;
  new_node->previous = list;
  list->next->previous = new_node;
  list->next = new_node;
  pthread_mutex_unlock(&lock);
}

/* list__remove
 * Removes the node from the list it is associated with.  We need the caller to tell us which list it is associated with so we
 * don't have to scan the whole list to find a matching raw_list or comp_list pointer.
 */
void list__remove(Buffer *buf, char *list_name) {
  pthread_mutex_t *lock = raw_list_lock;
  if (*list_name == "comp")
    lock = comp_list_lock;

  /* Caller must mark the buffer as victimized.  Caller should also make ref_count zero but we'll double check here. */
  lock__acquire(&buf->lock_id);
  while (buf->ref_count > 0)

TODO:  Need to finish this cond but it's opening an edge case.  I either need to re-check and re-loop or use a unique mutex and cond per buffer.'
    pthread_cond_wait()
  lock__release(&buf->lock_id);

  /* Lock the list so we can move pointers. */
  pthread_mutex_lock(&lock);


  //Buffer needs to be unpinned before we can continue.
  //Remove node and change links.
}
