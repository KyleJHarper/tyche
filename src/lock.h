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

/* Prototypes */
void lock__initialize();
void lock__acquire(uint16_t lock_id);
void lock__release(uint16_t lock_id);


typedef struct lock Lock;
struct lock {
  pthread_mutex_t mutex;  /* The actual mutex used for locking. */
};

#endif /* SRC_LOCK_H_ */
