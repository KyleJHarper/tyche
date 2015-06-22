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
    show_err("Raw list must be NULL, otherwise this is going to fail.", 1);
  raw_list = (Buffer *)malloc(sizeof(Buffer));
  if (raw_list == NULL)
    show_err("Failed to malloc the head of the raw_list.", 1);
  raw_list->next = raw_list;
  raw_list->previous = raw_list;

  /* Compressed List */
  if (comp_list != NULL)
    show_err("Compressed list must be NULL, otherwise this is going to fail.", 1);
  comp_list = (Buffer *)malloc(sizeof(Buffer));
  if (comp_list == NULL)
    show_err("Failed to malloc the head of the comp_list.", 1);
  comp_list->next = comp_list;
  comp_list->previous = comp_list;
}

int list__add() {
  return 0;
}

int list__remove() {
  return 0;
}
