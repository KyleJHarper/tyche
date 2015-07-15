/*
 * list.h
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_LIST_H_
#define SRC_LIST_H_

/* Includes */
#include <stdint.h>
#include <pthread.h>
#include "buffer.h"

/* Build the typedef and structure for a List */
typedef struct list List;
struct list {
  Buffer *head;          /* Top of the ring buffer.  Mostly for a fixed point. */
  uint32_t count;        /* Number of buffers in the list. */
  pthread_mutex_t lock;  /* For operations requiring exclusive locking of the list. */
};

/* Function prototypes.  Not required, but whatever. */
List* list__initialize();
Buffer* list__add(List *list);
void list__remove(Buffer **buf, List *list);
uint32_t list__count(List *list);


#endif /* SRC_LIST_H_ */
