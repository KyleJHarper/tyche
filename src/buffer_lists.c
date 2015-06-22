/*
 * buffer_lists.c
 *
 *  Created on: Jun 19, 2015
 *      Author: Kyle Harper
 * Description: Builds the buffer lists and functions for them.
 */

/* Include necessary headers here. */
#include <stdio.h>
#include "buffer.h"
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
int list__initialize() {
  /* Raw List */
  if (raw_list != NULL) show_err(strcat(LIST__BAD_INITIALIZE_MSG, "raw_list"));
  raw_list = (Buffer *)malloc(sizeof(Buffer));
  raw_list->next = *raw_list;
  raw_list->previous = *raw_list;

  /* Compressed List */
  return 0;
}

int list__add() {
  return 0;
}

int list__remove() {
  return 0;
}
