/*
 * list.h
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_LIST_H_
#define SRC_LIST_H_

/* Includes */
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h> /* For bool types. */
#include <inttypes.h>
#include "buffer.h"

/* A list is simply the collection of buffers, metadata to describe the list for management, and control attributes to protect it.
 * Most people will call this a "pool"... shrugs.
 */


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
  int compressor_id;                   /* The ID of the compressor we're supposed to use. */
  int compressor_level;                /* The level to send the compressor, only supported by zlib and zstd right now. */
};


/* Build the typedef and structure for a List */
#define MAX_COMP_VICTIMS 10000
#define VICTIM_BATCH_SIZE 1000
#define COMPRESSOR_BATCH_SIZE 250
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
  pthread_mutex_t lock;                          /* For operations requiring exclusive locking of the list. */
  pthread_t lock_owner;                          /* Stores the pthread_self() value to avoid double/dead locking. */
  uint8_t lock_depth;                            /* The depth of functions which have locked us, to ensure deeper calls don't release locks. */
  pthread_cond_t writer_condition;               /* The condition variable for writers to wait for when attempting to drain a list of refs. */
  pthread_cond_t reader_condition;               /* The condition variable for readers to wait for when attempting to increment ref count. */
  pthread_cond_t sweeper_condition;              /* The condition variable for sweeping signals. */
  uint32_t ref_count;                            /* Number of threads pinning this list.  Useful for draining. */
  uint16_t pending_writers;                      /* Value to indicate how many writers are waiting to edit the list. */

  /* Management and Administration Members */
  pthread_t sweeper_thread;                      /* The threads that the sweeper runs in. */
  uint8_t active;                                /* Boolean for active/inactive status for async processes like sweeping. */
  uint8_t sweep_goal;                            /* Minimum percentage of memory we want to free up whenever we sweep, relative to current_size. */
  uint64_t sweeps;                               /* Number of times the list has been swept. */
  uint64_t sweep_cost;                           /* Time in ns spent sweeping lists. */
  uint64_t restorations;                         /* Number of buffers restored. */
  uint64_t compressions;                         /* Buffers compressed during the life of the list. */
  uint64_t evictions;                            /* Buffers that were evicted from the list entirely. */

  /* Management of Nodes for Skiplist and Buffers */
  Buffer *head;                                  /* The head of the list of buffers. */
  Buffer *clock_hand;                            /* The current Buffer to be checked when sweeping is invoked. */
  float WINDOW_WEIGHTS[MAX_WINDOWS];             /* The weights the user wants to assign to each window. */
  int window_index;                              /* The index for the current window. */

  /* Compressor Pool Management */
  pthread_mutex_t jobs_lock;                     /* The mutex that all jobs need to respect. */
  pthread_cond_t jobs_cond;                      /* The shared condition variable for compressors to respect. */
  pthread_cond_t jobs_parent_cond;               /* The parent condition to signal when the job queue is empty and active compressors is 0. */
  Buffer *comp_victims[MAX_COMP_VICTIMS];        /* An array of available compressed buffers to remove if comp_size is too high after a sweep. */
  uint16_t comp_victims_index;                   /* The index for the next-available comp buffer to be stored in comp_victims[]. */
  Buffer *victims[VICTIM_BATCH_SIZE];            /* Items which are ready for compression. */
  uint16_t victims_index;                        /* The tracking index for the next-available victims[] insertion point. */
  uint16_t victims_compressor_index;             /* The index for the next-available buffer to be compressed by a compressor. */
  uint16_t active_compressors;                   /* The number of compressors currently doing work. */
  pthread_t *compressor_threads;                 /* A pool of threads for each compressor to run within. */
  Compressor *compressor_pool;                   /* A pool of workers for buffer compression when sweeping. */
  int compressor_id;                             /* The ID of the compressor we're supposed to use. */
  int compressor_level;                          /* The level to send the compressor, only supported by zlib and zstd right now. */
  int compressor_count;                          /* The number of compressors to run from the list. */

  /* Copy-On-Write Space (of Buffers) */
  uint64_t cow_max_size;                         /* Size, in bytes, for cow space. */
  uint64_t cow_current_size;                     /* Current cow space size. */
  pthread_mutex_t cow_lock;                      /* The mutex for adding/removing items from the list. */
  pthread_cond_t cow_killer_cond;                /* The condition to signal the cow killer. */
  pthread_cond_t cow_waiter_cond;                /* The condition for callers to wait on until the cow processor is done. */
  Buffer *cow_head;                              /* The head for our circular list of copy-space. */
  pthread_t slaughter_house_thread;              /* The thread our cow killer will run in... lols. */
};


/* Function prototypes.  Not required, but whatever. */
int list__initialize(List **list, int compressor_count, int compressor_id, int compressor_level, uint64_t max_memory);
int list__add(List *list, Buffer **buf, uint8_t list_pin_status);
int list__remove(List *list, Buffer *buf);
int list__update(List *list, Buffer **callers_buf, void *data, uint32_t size, uint8_t list_pin_status);
int list__update_ref(List *list, int delta);
int list__search(List *list, Buffer **buf, bufferid_t id, uint8_t list_pin_status);
int list__acquire_write_lock(List *list);
int list__release_write_lock(List *list);
inline void list__check_out_of_memory(List *list, uint8_t list_pin_status);
uint64_t list__sweep(List *list, uint8_t sweep_goal);
void list__sweeper_start(List *list);
int list__balance(List *list, uint32_t ratio, uint64_t max_memory);
int list__destroy(List *list);
void list__compressor_start(List *list);
void list__show_structure(List *list);
void list__dump_structure(List *list);
void list__add_cow(List *list, Buffer *buf);
void list__slaughter_house(List *list);

#endif /* SRC_LIST_H_ */
