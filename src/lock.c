/*
 * lock.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 * Description: The lock functions protect buffers through mutexes and variable conditions.  These pthread objects are expensive in
 *              a few ways, one of which is the amount of memory required (in my tests, 40 + 44 bytes).  This more than triples the
 *              footprint of each Buffer and makes the BUFFER_OVERHEAD ratio significantly less favorable.
 *
 *              We will use a pool of locks that can be shared, based on the Options->max_locks value.  By default we use a ratio
 *              of 1:1, based on Options->page_count.  Using a ratio higher than 1 results in multiple buffers sharing the same
 *              mutex and condition which, in theory, will save on memory.  The cost to this is the risk of buffers waiting on
 *              each other (contention).  The trade-off becomes:  does the additional (theoretical) contention cost less than the
 *              performance gain by using the saved memory for more raw/compressed buffers.
 *
 *              Even if the lock contention mentioned above forced us to always use a ratio of 1:1, we would still use this locker
 *              pool.  Buffers will come and go at runtime, but the locker_pool will remain constant.  We get to avoid creation,
 *              destruction, and initialization of mutexes and conditions every time a Buffer is created/destroyed.
 */

#include <stdint.h>
#include <pthread.h>
#include <stdlib.h>
#include "lock.h"
#include "error.h"
#include "options.h"


/* Defines and global values */
lockid_t next_id = 0;
pthread_mutex_t next_id_mutex = PTHREAD_MUTEX_INITIALIZER;
Lock *locker_pool = NULL;

/* Extern the error codes we'll use. */
extern const int E_GENERIC;

/* We need access to the global options. */
extern Options opts;


/* Functions */

/* lock__initialize
 * Sets the default values for the locker_pool array elements.  GCC has support to do this in the declaration but meh.  Again,
 * this is just a pthread_mutex_t array but if we add attributes it'll be much easier to update this way.
 */
void lock__initialize() {
  // Free the locker_pool in case it's being re-set by a test or whatever.  This is safe on 1st run because we start as NULL.
  free(locker_pool);
  locker_pool = calloc(opts.max_locks, sizeof(Lock));
  if (locker_pool == NULL)
    show_error(E_GENERIC, "Failed to calloc memory for the locker pool.");
  for (lockid_t i=0; i<opts.max_locks; i++) {
    if (pthread_mutex_init(&(locker_pool + i)->mutex, NULL) != 0)
      show_error(E_GENERIC, "Failed to initialize mutex for locker pool.  This is fatal.");
    if (pthread_cond_init(&locker_pool[i].condition, NULL) != 0)
      show_error(E_GENERIC, "Failed to initialize condition for locker pool.  This is fatal.");
  }
}


/* lock__assign_next_id
 * This will assign the next available lock_id to the caller (via the pointer sent).  Checks for max value and circles back to zero
 * and always ensures that the lock_id 0 is not assigned.  Lock ID 0 isn't special yet but might be.
 */
void lock__assign_next_id(lockid_t *referring_id_ptr) {
  pthread_mutex_lock(&next_id_mutex);
  next_id++;
  if (next_id == opts.max_locks)
    next_id = 0;
  *referring_id_ptr = next_id;
  pthread_mutex_unlock(&next_id_mutex);
}


/* lock__acquire
 * Locks the lock_id specified.  This should probably never be needed, but we'll add it.
 */
void lock__acquire(lockid_t lock_id) {
  pthread_mutex_lock(&(locker_pool + lock_id)->mutex);
}


/* lock__release
 * Releases a lock from the locker_pool.  Used when a buffer is removed and its data (including lock_id) are wiped.
 */
void lock__release(lockid_t lock_id) {
  pthread_mutex_unlock(&(locker_pool + lock_id)->mutex);
}

