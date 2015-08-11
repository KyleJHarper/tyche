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


/* Create a structure to allow for faster searching of the list.  Normally we'd use multiple levels of a Skip List, but we given
 * the fixed dataset of this program will work with, I'm just going to hard-code an array.  If others want to use the theory they
 * can implement whatever search methodology they'd like.
 * We use a fixed size of elements with push/pop since our minimum page size is 4096 bytes and maximum data set is < 20GB.
 */
#define SKIP_LIST_SIZE 5000000   /* 5 Million */
typedef struct skiplistentry SkipListEntry;
struct skiplistentry {
  bufferid_t id;   /* ID of the buffer we want to store. */
  Buffer *buf;     /* Pointer to the buffer we're managing. */
};

/* Build the typedef and structure for a List */
typedef struct list List;
struct list {
  Buffer *head;                     /* Top of the ring buffer.  Mostly for a fixed point. */
  uint32_t count;                   /* Number of buffers in the list. */
  pthread_mutex_t lock;             /* For operations requiring exclusive locking of the list (writing to it). */
  pthread_cond_t writer_condition;  /* The condition variable for writers to wait for when attempting to drain a list of refs. */
  pthread_cond_t reader_condition;  /* The condition variable for readers to wait for when attempting to increment ref count. */
  uint32_t ref_count;               /* Number of threads pinning this list (searching it) */
  uint8_t pending_writers;          /* Value to indicate how many writers are waiting to edit the list. */
  Buffer skiplist[SKIP_LIST_SIZE];  /* Array of entries to create a crude but effective skip list. */
};

/* Function prototypes.  Not required, but whatever. */
List* list__initialize();
int list__add(List *list, Buffer *buf);
int list__remove(List *list, Buffer **buf);
int list__update_ref(List *list, int delta);
int list__search(List *list, Buffer **buf, bufferid_t id);


#endif /* SRC_LIST_H_ */
