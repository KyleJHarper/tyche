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
#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>
#include "buffer.h"
#include "thpool.h"

/* A list is simply the collection of buffers, metadata to describe the list for management, and control attributes to protect it.
 * List functions come in reader and writer flavors.
 *   Reader functions can safely rely on the list__update_ref_count() function to 'pin' the list so writers can't change it.
 *   Writer functions can safely rely on the list__acquire_write_lock() to block new readers and wait on existing readers.  Writers
 *   can also safely rely on the list__release_write_lock() to unblock other writers and readers (in that order!).
 * In short: writers have priority but are patient for existing readers.
 */


/* Build the Skiplist & Node Structures. */
typedef struct skiplistnode SkiplistNode;
struct skiplistnode {
  /* Directions for Traversal.  Left-To-Right Mentality. */
  SkiplistNode *right;  /* The next highest node in the skiplist graph. */
  SkiplistNode *down;   /* The next more-granular node in the skiplist graph. */

  /* Buffer Reference to the List Item Itself */
  Buffer *target;       /* The buffer this node points to.  Always NULL when *up exists. */
};


/* Build the typedef and structure for a List */
#define SKIPLIST_MAX 32
typedef struct list List;
struct list {
  /* Size and Counter Members */
  uint32_t count;                       /* Number of buffers in the list. */
  uint64_t current_size;                /* Number of bytes currently allocated to the buffers in this list. */
  uint64_t max_size;                    /* Maximum number of bytes this buffer is allowed to hold, ever. */

  /* Locking, Reference Counters, and Similar Members */
  pthread_mutex_t lock;                 /* For operations requiring exclusive locking of the list (writing to it). */
  pthread_t lock_owner;                 /* Stores the pthread_self() value to avoid double/dead locking. */
  uint8_t lock_depth;                   /* The depth of functions which have locked us, to ensure deeper calls don't release locks. */
  pthread_cond_t writer_condition;      /* The condition variable for writers to wait for when attempting to drain a list of refs. */
  pthread_cond_t reader_condition;      /* The condition variable for readers to wait for when attempting to increment ref count. */
  uint32_t ref_count;                   /* Number of threads pinning this list (searching it) */
  uint8_t pending_writers;              /* Value to indicate how many writers are waiting to edit the list. */

  /* Management and Administration Members */
  List *offload_to;                     /* The target list to offload buffers to.  Currently raw -> comp and comp -> free() */
  List *restore_to;                     /* The target list to restore buffers to.  Currently, comp -> raw. */
  uint8_t sweep_goal;                   /* Minimum percentage of memory we want to free up whenever we sweep, relative to current_size. */
  uint32_t sweeps;                      /* Number of times the list has been swept. */
  uint64_t sweep_cost;                  /* Time in ns spent sweeping lists. */
  uint32_t offloads;                    /* Number of buffers removed from the list (offloaded from raw, popped from compressed). */
  uint32_t restorations;                /* Number of buffers restored to the raw list (compressed list doesn't use this). */

  /* Management of Nodes for Skiplist and Buffers */
  Buffer *head;                         /* The head of the list of buffers. */
  Buffer *clock_hand;                   /* The current Buffer to be checked when sweeping is invoked. */
  SkiplistNode *indexes[SKIPLIST_MAX];  /* List of the heads of the bottom-most (least-granular) Skiplists. */
  uint8_t levels;                       /* The current height of the skip list thus far. */
  uint8_t youngest_generation;          /* The tag to assign to the youngest generation for compressed buffer generation management. */
  uint8_t oldest_generation;            /* The oldest-known generation for list__pop()-ing.  Always "chasing" youngest_generation. */
  threadpool compressor_pool;           /* A pool of workers for buffer compression when sweeping. */
};


/* Function prototypes.  Not required, but whatever. */
List* list__initialize();
SkiplistNode* list__initialize_skiplistnode(Buffer *buf);
int list__add(List *list, Buffer *buf);
int list__remove(List *list, bufferid_t id, bool destroy);
int list__update_ref(List *list, int delta);
int list__search(List *list, Buffer **buf, bufferid_t id);
int list__acquire_write_lock(List *list);
int list__release_write_lock(List *list);
uint32_t list__sweep(List *list, uint8_t sweep_goal);
int list__pop(List *list, uint64_t bytes_needed);
int list__restore(List *list, Buffer **buf);
int list__balance(List *list, uint32_t ratio);
int list__destroy(List *list);


#endif /* SRC_LIST_H_ */
