/*
 * buffer.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

/* Include Headers */
#include <stdio.h>
#include "lock.h"


/* Functions */
/* buffer__lock
 * Typically we shouldn't call a function just to set a lock, but there may be needs for this so I'm adding it now.
 */
void buffer__lock(Buffer *buf) {
  lock__acquire(buf->lock_id);
}

/* buffer__unlock
 * Same with buffer__lock, this might never be called directly but I'll add it for simplicity for now.
 */
void buffer_unlock(Buffer *buf) {
  lock__release(buf->lock_id);
}
