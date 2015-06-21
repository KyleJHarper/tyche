/*
 * buffer_lists.h
 *
 *  Created on: Jun 19, 2015
 *      Author: Kyle Harper
 * Description: Builds the buffer lists and functions for them.
 */

#ifndef SRC_BUFFER_LISTS_H_
#define SRC_BUFFER_LISTS_H_

/* Include necessary headers here. */
#include <stdio.h>
#include "buffer.h"

/* Function prototypes.  Not required, but helpful. */
int list__initialize(Buffer *head);
int list__add();
int list__remove();

/* Globals for ease. */
Buffer *raw_head;
Buffer *comp_head;


/* Functions */
/*
 * list__initialize
 * Creates the two lists we'll need need to maintain.  The raw list and the compressed list.
 */
int list__initialize() {
  /* Raw List */
  if (raw_head == NULL) return 1;
  raw_head = (Buffer *)malloc(sizeof(Buffer));
  raw_head->next = *raw_head;
  raw_head->previous = *raw_head;

  /* Compressed List */
  return 0;
}

int list__add() {
  return 0;
}

int list__remove() {
  return 0;
}

#endif /* SRC_BUFFER_LISTS_H_ */
