/*
 * buffer.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

/* Include Headers */
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "lock.h"
#include "error.h"
#include "buffer.h"

/* Give extern access to locker_pool[] to us, even though I'm sure this is a no no and someone will yell at me. */
extern Lock locker_pool[];

/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_BUFFER_POOFED;



/* Functions */
/* buffer__lock
 * Setting a lock just locks the mutex from the locker_pool[].  Since we support concurrency, it's possible to have a thread
 * waiting for a lock on a buffer while another thread is removing that buffer entirely.  So we add a little more logic for that.
 */
int buffer__lock(Buffer *buf) {
  lockid_t lock_id;
  if (buf)
    lock_id = buf->lock_id;
  /* When lock_id is 0, buf is NULL or unusable.  When buf goes NULL or is victimized, we're still unusable. */
  if (lock_id == 0)
    return E_BUFFER_POOFED;
  lock__acquire(lock_id);
  if (!buf || buf->victimized != 0)
    return E_BUFFER_POOFED;
  return E_OK;
}

/* buffer__unlock
 * This will unlock the element in the locker_pool[] with the element matching lock_id.  Since this can only be reached at the end
 * of a block who already owns the lock, we don't need any special checking.
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
  int rv = buffer__lock(buf);
  if (rv > 0)
    return rv;
  buf->ref_count += delta;

  /* When decrementing we need to broadcast to our cond that we're ready. */
  if (buf->victimized != 0 && buf->ref_count == 0)
    pthread_cond_broadcast(&locker_pool[buf->lock_id].cond);
  buffer__unlock(buf);
  return E_OK;
}

/*buffer__victimize
 * Marks the victimized attribute of the buffer and sets up a condition to wait for the ref_count to reach 0.  This allows this
 * function to fully block the caller until the buffer is ready to be removed.  The list__remove() function is how you get rid of
 * a buffer from the list since we need to manage the pointers.
 */
int buffer__victimize(Buffer *buf) {
  int rv = buffer__lock(buf);
  if (rv > 0)
    return rv;
  buf->victimized = 1;  /* This can only ever be set, so there's no check; or race condition since we own the lock. */
  while(buf->ref_count > 0)
    pthread_cond_wait(&locker_pool[buf->lock_id].cond, &locker_pool[buf->lock_id].mutex);
  buffer__unlock(buf);
  return E_OK;
}
