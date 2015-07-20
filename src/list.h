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
#include "list.h"

/* Build the typedef and structure for a List */
typedef struct list List;
struct list {
  Buffer *head;          /* Top of the ring buffer.  Mostly for a fixed point. */
  uint32_t count;        /* Number of buffers in the list. */
  pthread_mutex_t lock;  /* For operations requiring exclusive locking of the list. */
};

/* Function prototypes.  Not required, but whatever. */
List* list__initialize();
void list__add(List *list, Buffer *buf);
void list__remove(List *list, Buffer **buf);
int list__search(List *list, Buffer **buf, bufferid_t id);


#endif /* SRC_LIST_H_ */
