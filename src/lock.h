/*
 * lock.h
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 * Description: Define the lock (mutex) so we can create a locker pool in the source file.  Building a struct for it in case I
 *              need to add additional attributes later.  If not, this could have simply been a pthread_mutex_t array in lock.c.
 *
 */

#ifndef SRC_LOCK_H_
#define SRC_LOCK_H_

/* Include all the necessaries. */
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>


typedef uint16_t lockid_t;
typedef struct lock Lock;
struct lock {
  pthread_mutex_t mutex;  /* The actual mutex used for locking. */
  pthread_cond_t cond;    /* The cond to use when we need signaling. */
};


/* Prototypes */
void lock__initialize();
void lock__acquire(lockid_t lock_id);
void lock__release(lockid_t lock_id);
void lock__assign_next_id(lockid_t *referring_id_ptr);


#endif /* SRC_LOCK_H_ */
