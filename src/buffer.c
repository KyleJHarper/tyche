/*
 * buffer.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

/* Include Headers */
#include <stdio.h>
#include <pthread.h>
#include "lock.h"

/* Give extern access to us, even though I'm sure this is a no no and someone will yell at me. */
extern Lock locker_pool[];


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
void buffer__unlock(Buffer *buf) {
  lock__release(buf->lock_id);
}

/* buffer__update_ref
 * Makes atomic changes to the buffers ref_count.  This should only ever be 1 or -1.  If we're victimized we need to return a
 * warning-level error code (E_TRY_AGAIN) to indicate the caller should retry whatever it was intending knowing this buffer is
 * invalidated.
 */
int buffer__update_ref(Buffer *buf, int delta) {
  lock__acquire(buf->lock_id);
  if (buf->victimized != 0)
    return E_TRY_AGAIN;
  buf->ref_count += delta;

  /* When decrementing we need to broadcast to our cond that we're ready. */
  if (buf->victimized != 0 && buf->ref_count == 0)
    pthread_cond_broadcast(&buf->lock_id);
  lock__release(buf->lock_id);
}

/*buffer__victimize
 * Marks the victimized attribute of the buffer and sets up a condition to wait for the ref_count to reach 0.  This allows this
 * function to fully block the caller until the buffer is ready to be removed.  The list__remove() function is how you get rid of
 * a buffer from the list since we need to manage the pointers.
 */
void buffer__victimize(Buffer *buf) {
  lock__acquire(buf->lock_id);
  buf->victimized = 1;  /* This can only ever be set, so there's no check; or race condition since we own the lock. */
  while(buf->ref_count > 0)
    pthread_cond_wait(&locker_pool[buf->lock_id].cond, &locker_pool[buf->lock_id].mutex);
  lock__release(buf->lock_id);
}
