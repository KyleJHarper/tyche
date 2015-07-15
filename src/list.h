/*
 * list.h
 *
 *  Created on: Jun 21, 2015
 *      Author: administrator
 */

#ifndef SRC_LIST_H_
#define SRC_LIST_H_

/* Includes */
#include <stdint.h>
#include <pthread.h>
#include "buffer.h"

/* Build the typedef and structure for a List */
typedef struct list List;
struct List {
  Buffer *head;          /* Top of the ring buffer.  Mostly for a fixed point. */
  uint32_t count;        /* Number of buffers in the list. */
  pthread_mutex_t lock;  /* For operations requiring exclusive locking of the list. */
};

/* Function prototypes.  Not required, but whatever. */
void list__initialize();
Buffer* list__add(Buffer *list);
void list__remove(Buffer **buf, char *list_name);
uint32_t list__count(Buffer *list);


#endif /* SRC_LIST_H_ */
