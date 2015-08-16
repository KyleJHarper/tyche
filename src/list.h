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


/* Create a structure to allow for faster searching of the list.  Normally we'd use multiple levels of indexing to create a true
 * Skip List for a traditional linked-list, but given the fixed dataset of this program I'm just going to hard-code a thin array to
 * emulate the best of both worlds.  If others want to use the theory they can implement whatever linked-list, array, search, or
 * whatever methodology they'd like for list control.
 * We use a fixed size of elements with push/pop since our minimum page size is 4096 bytes and maximum data set is < 20GB.
 */
#define BUFFER_POOL_SIZE 5000000   /* 5 Million */

/* Build the typedef and structure for a List */
typedef struct list List;
struct list {
  uint8_t is_sorted;                /* Should this buffer be sorted? */
  uint32_t count;                   /* Number of buffers in the list. */
  pthread_mutex_t lock;             /* For operations requiring exclusive locking of the list (writing to it). */
  pthread_cond_t writer_condition;  /* The condition variable for writers to wait for when attempting to drain a list of refs. */
  pthread_cond_t reader_condition;  /* The condition variable for readers to wait for when attempting to increment ref count. */
  uint32_t ref_count;               /* Number of threads pinning this list (searching it) */
  uint8_t pending_writers;          /* Value to indicate how many writers are waiting to edit the list. */
  Buffer *pool[BUFFER_POOL_SIZE];   /* Array of pointers to Buffers since we're avoiding the linked list. */
};

/* Function prototypes.  Not required, but whatever. */
List* list__initialize(uint8_t is_sorted);
int list__add(List *list, Buffer *buf);
int list__remove(List *list, Buffer **buf);
int list__update_ref(List *list, int delta);
int list__search(List *list, Buffer **buf, bufferid_t id);


#endif /* SRC_LIST_H_ */
