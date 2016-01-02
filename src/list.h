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


/* A list is simply the collection of buffers, metadata to describe the list for management, and control attributes to protect it.
 * List functions come in reader and writer flavors.
 *   Reader functions can safely rely on the list__update_ref_count() function to 'pin' the list so writers can't change it.
 *   Writer functions can safely rely on the list__acquire_write_lock() to block new readers and wait on existing readers.  Writers
 *   can also safely rely on the list__release_write_lock() to unblock other writers and readers (in that order!).
 * In short: writers have priority but are patient for existing readers.
 *
 * -- A Note About The Pool (Specifically: No Linked List)
 * Short Version:
 * People jump to "OMG linked list!"... and if someone wants to fork this and prove it's faster, go ahead.
 *
 * Longer Version:
 * The ephemeral nature of buffers and the ever-changing status of the list often leads developers to leap toward a linked list.
 * There is NOTHING WRONG with that approach.  You will need skip-lists or similar techniques to ensure rapid searching, but again
 * a linked list is fine.
 * Tyche opts to use an array because:
 *   1.  No need to manage a skip list, period.
 *   2.  No need to have a Buffer->next* or Buffer->prev*, saving 16 bytes per buffer (64-bit platform, gcc).
 *   3.  Overhead is genuinely minimal for the higher numbered elements that will never be used.  A linked list avoids this, but
 *       again a linked list also requires 16 bytes per buffer more which rapidly diminishes this savings.
 * We are not arguing that an array is better; we're simply explaining our decision.  Remember, this is just one implementation.
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

  /* Management of Nodes for Skiplist and Buffers */
  Buffer *head;                         /* The head of the list of buffers. */
  Buffer *clock_hand;                   /* The current Buffer to be checked when sweeping is invoked. */
  SkiplistNode *indexes[SKIPLIST_MAX];  /* List of the heads of the bottom-most (least-granular) Skiplists. */
  uint8_t levels;                       /* The current height of the skip list thus far. */
};


/* Function prototypes.  Not required, but whatever. */
List* list__initialize();
SkiplistNode* list__initialize_skiplistnode(Buffer *buf);
int list__add(List *list, Buffer *buf);
int list__remove(List *list, bufferid_t id);
int list__update_ref(List *list, int delta);
int list__search(List *list, Buffer **buf, bufferid_t id);
int list__acquire_write_lock(List *list);
int list__release_write_lock(List *list);
uint list__sweep(List *list);
int list__push(List *list, Buffer *buf);
int list__pop(List *list, uint64_t bytes_needed);
int list__restore(List *list, Buffer **buf);
int list__balance(List *list, uint ratio);


#endif /* SRC_LIST_H_ */
