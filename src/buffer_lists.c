/*
 * buffer_lists.c
 *
 *  Created on: Jun 19, 2015
 *      Author: Kyle Harper
 * Description: Builds the buffer lists and functions for them.  We use circular lists (doubly-linked) for a few reasons:
 *                1.  We can traverse both forward and backward.
 *                2.  We can lock the buffers in-front and behind a given buffer to allow concurrency with list manipulation.
 *                3.  They make implementing clock sweep easy (no checking head/tail).
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
#include "buffer.h"
#include "buffer_lists.h"
#include "error.h"


/* Globals for ease. */
Buffer *raw_list;   /* Circular list of raw buffers. */
Buffer *free_list;  /* Circular list of free nodes in raw buffers list. */
Buffer *comp_list;  /* Circular list of compressed buffers. */


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
  raw_list->previous = raw_list;
  lock__assign_next_id(&raw_list->lock_id);

  /* Compressed List */
  if (comp_list != NULL)
    show_err("Compressed list must be NULL, otherwise this is going to fail.", E_GENERIC);
  comp_list = (Buffer *)malloc(sizeof(Buffer));
  if (comp_list == NULL)
    show_err("Failed to malloc the head of the comp_list.", E_GENERIC);
  comp_list->next = comp_list;
  comp_list->previous = comp_list;
  lock__assign_next_id(&comp_list->lock_id);
}

/* list__add
 * Adds a node to the list specified.  By 'list' we really just mean the node buffer in the list, typically 'head'.
 * Adding a node is simple because we only lock the insertion_node and the ->next node.  There are no situations where a deadlock
 * can occur with a race condition between 2+ adjacent nodes who only lock themself and ->next.
 * Also: Given the number of consecutive changes to our doubly-linked lists I'm not sure CAS and/or DCAS could even be used, let
 * alone used in a way that performs better than a simple mutex.
 */
int list__add(Buffer *insertion_node) {
  uint16_t insertion_lock_id;
  uint16_t acquired_lock_id;
   /* Lock the insertion node and the ->next node because we're going to edit them.
    * Use to loop to ensure we get the real ->next->lock_id in case it was changed in front of us.
   */
  for (;;) {
    if (insertion_node == NULL) {
      /* Our insertion node was removed (list__remove) before we could lock. */
      return E_TRY_AGAIN;
    }
    lock__acquire(insertion_node->lock_id);

   if (insertion_node->lock_id != insertion_node->next->lock_id) {
      acquired_lock_id = insertion_node->next->lock_id;
      lock__acquire(acquired_lock_id);
      /* Assure that the lock we acquired matches the ->next in case another thread changed ->next. */
      if (acquired_lock_id == insertion_node->next->lock_id)
        break;
      lock__release(acquired_lock_id);
    }
  }

  /* Now that they're locked, malloc a new Buffer node and reassign pointers. */
  Buffer *new_node = (Buffer *)malloc(sizeof(Buffer));
  if (new_node == NULL)
    show_err("Error malloc-ing new buffer from list__add.", 1);
  new_node->previous = insertion_node;
  new_node->next = insertion_node->next;
  insertion_node->next->previous = new_node;
  insertion_node->next = new_node;

  /* Release the locks and get out of here. Note: we never locked new_node because it wasn't in the chain until here. */
  lock__release(insertion_node->lock_id);
  return 0;
}

int list__remove(Buffer *buf) {
  if (buf == NULL)
    show_err("A null buffer node was passed to list__remove.");
  //Buffer needs to be unpinned before we can continue.
  //Remove node and change links.
  return 0;
}
