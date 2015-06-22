/*
 * lock.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 * Description: The lock functions control buffers through mutexes.  Mutexes are expensive in a few ways, one of which is the
 *              amount of memory required (in my tests, 40 bytes).  This more than doubles the footprint of each Buffer and
 *              makes the ratio significantly less favorable for PAGE_SIZE values lower than 512 bytes.
 *
 *              We will use a pool of locks that can be shared.  In this test program we are only ever expect to have 8192-byte
 *              pages, up to a maximum of 1,000,000 pages, for a sharing ratio of ~15:1.  In theory this should avoid the bulk of
 *              lock contention for simple buffer locking while giving us 15x memory savings on overhead.  These values may
 *              change as part of the testing and again: ARE JUST FOR THIS IMPLEMENTATIONS TESTING.  YMMV in your own
 *              implementation you can handle locking however you want to.
 */

#include <stdint.h>
#include <pthread.h>
#include "lock.h"
#include "error.h"

/* Defines and global values */
#define MAX_LOCK_VALUE UINT16_MAX
uint16_t next_value;
Lock locker_pool[MAX_LOCK_VALUE];


/* Functions */

/* lock__initialize
 * Sets the default values for the locker_pool array elements.  GCC has support to do this in the declaration but meh.  Again,
 * this is just a pthread_mutex_t array but if we add attributes it'll be much easier to update this way.
 */
void lock__initialize() {
  for (uint16_t i=0; i<MAX_LOCK_VALUE; i++) {
    if (pthread_mutex_init(&locker_pool[i].mutex, NULL) != 0)
      show_err("Failed to initialize mutex.  This is fatal.", 1);
  }
}

/* lock__acquire
 * Set the mutex indicated by the lock_id.
 */
void lock__acquire(uint16_t lock_id) {
  /* Really would like to be lock-less but I'm not verse enough to do that yet */
  pthread_mutex_lock(&locker_pool[lock_id].mutex);
}

/* lock__release
 * Release the mutex indicated by the lock_id
 */
void lock__release(uint16_t lock_id) {
  pthread_mutex_unlock(&locker_pool[lock_id].mutex);
}

