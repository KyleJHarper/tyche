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
#include <pthread.h>
#include "buffer.h"

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


/* Build the Compressor Structures */
typedef struct compressor Compressor;
struct compressor {
  pthread_t worker;                    /* The thread to actually do work. */
  pthread_mutex_t *jobs_lock;          /* Pointer to the shared jobs lock. */
  pthread_cond_t *jobs_cond;           /* Pointer to the shared condition variable to wake up when there's work to do. */
  pthread_cond_t *jobs_parent_cond;    /* Pointer to the parent condition to trigger when job queue is empty and active is 0. */
  uint16_t *active_compressors;        /* Pointer to shared counter of active compressors. */
  uint8_t runnable;                    /* Flag determining if we are still allowed to be running.  If not, pthread_exit(). */
  Buffer **victims;                    /* Link to the array of victims. */
  uint16_t *victims_index;             /* Link to the victims index to track the next-available position. */
  uint16_t *victims_compressor_index;  /* Link to the next-available Buffer to check for compressing in *victims[]. */
};


/* Build the typedef and structure for a List */
#define SKIPLIST_MAX 32
#define VICTIM_BATCH_SIZE 1000
#define COMPRESSOR_BATCH_SIZE 25
typedef struct list List;
struct list {
  /* Size and Counter Members */
  uint32_t raw_count;                            /* Number of raw buffers in the list. */
  uint32_t comp_count;                           /* Number of compressed buffers in the list. */
  uint64_t current_raw_size;                     /* Number of bytes currently allocated to the raw buffers in this list. */
  uint64_t max_raw_size;                         /* Maximum number of bytes the raw list is allowed to hold, ever. */
  uint64_t current_comp_size;                    /* Number of bytes currently allocated to the comp buffers in this list. */
  uint64_t max_comp_size;                        /* Maximum number of bytes the comp list is allowed to hold, ever. */

  /* Locking, Reference Counters, and Similar Members */
  pthread_mutex_t lock;                          /* For operations requiring exclusive locking of the list (writing to it). */
  pthread_t lock_owner;                          /* Stores the pthread_self() value to avoid double/dead locking. */
  uint8_t lock_depth;                            /* The depth of functions which have locked us, to ensure deeper calls don't release locks. */
  pthread_cond_t writer_condition;               /* The condition variable for writers to wait for when attempting to drain a list of refs. */
  pthread_cond_t reader_condition;               /* The condition variable for readers to wait for when attempting to increment ref count. */
  pthread_cond_t sweeper_condition;              /* The condition variable for sweeping signals. */
  uint32_t ref_count;                            /* Number of threads pinning this list (searching it) */
  uint8_t pending_writers;                       /* Value to indicate how many writers are waiting to edit the list. */

  /* Management and Administration Members */
  uint8_t sweep_goal;                            /* Minimum percentage of memory we want to free up whenever we sweep, relative to current_size. */
  uint64_t sweeps;                               /* Number of times the list has been swept. */
  uint64_t sweep_cost;                           /* Time in ns spent sweeping lists. */
  uint64_t restorations;                         /* Number of buffers restored to the raw list (compressed list doesn't use this). */
  uint64_t compressions;                         /* Buffers compressed during the life of the list. */

  /* Management of Nodes for Skiplist and Buffers */
  Buffer *head;                                  /* The head of the list of buffers. */
  Buffer *clock_hand;                            /* The current Buffer to be checked when sweeping is invoked. */
  SkiplistNode *indexes[SKIPLIST_MAX];           /* List of the heads of the bottom-most (least-granular) Skiplists. */
  uint8_t levels;                                /* The current height of the skip list thus far. */

  /* Compressor Pool Management */
  pthread_mutex_t jobs_lock;                     /* The mutex that all jobs need to respect. */
  pthread_cond_t jobs_cond;                      /* The shared condition variable for compressors to respect. */
  pthread_cond_t jobs_parent_cond;               /* The parent condition to signal when the job queue is empty and active compressors is 0. */
  Buffer *comp_victims[VICTIM_BATCH_SIZE];       /* An array of available compressed buffers to remove if comp_size is too high after a sweep. */
  uint16_t comp_victims_index;                   /* The index for the next-available comp buffer to be stored in comp_victims[]. */
  Buffer *victims[VICTIM_BATCH_SIZE];            /* Items which are victimized and ready for compression. */
  uint16_t victims_index;                        /* The tracking index for the next-available victims[] insertion point. */
  uint16_t victims_compressor_index;             /* The index for the next-available buffer to be compressed by a compressor. */
  uint16_t active_compressors;                   /* The number of compressors currently doing work. */
  pthread_t *compressor_threads;                 /* A pool of threads for each compressor to run within. */
  Compressor *compressor_pool;                   /* A pool of workers for buffer compression when sweeping. */
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
uint32_t list__sweep(List *list, uint8_t sweep_goal);
int list__balance(List *list, uint32_t ratio);
int list__destroy(List *list);
void list__compressor_start(Compressor *comp);
void list__show_structure(List *list);

#endif /* SRC_LIST_H_ */
