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
#include <stdlib.h>
#include <time.h>     /* for gettimeofday() */
#include <string.h>   /* for memcpy() */
#include "lock.h"
#include "error.h"
#include "buffer.h"
#include "lz4.h"


/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L

/* Give extern access to locker_pool[] to us, even though I'm sure this is a no no and someone will yell at me. */
extern Lock locker_pool[];


/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_POOFED;
extern const int E_BUFFER_IS_VICTIMIZED;
extern const int E_BUFFER_MISSING_DATA;

/* Store the overhead of a Buffer for others to use for calculations */
const int BUFFER_OVERHEAD = sizeof(Buffer);



/* Functions */
/* buffer__initialize
 * Creates a new buffer, simply put.  We have to create a new buffer (with a new pointer) and return its address because we NULL
 * out existing buffers' pointers when we remove them from their pools.  The *page_filespec is the path to the file (page) we're
 * going to read into the buffer's ()->data member.
 */
Buffer* buffer__initialize(bufferid_t id, char *page_filespec) {
  Buffer *new_buffer = (Buffer *)malloc(sizeof(Buffer));
  if (new_buffer == NULL)
    show_error(E_GENERIC, "Error malloc-ing a new buffer in buffer__initialize.");
  /* Attributes for typical buffer organization and management. */
  new_buffer->id = id;
  new_buffer->ref_count = 0;
  new_buffer->popularity = 0;
  new_buffer->victimized = 0;
  lock__assign_next_id(&new_buffer->lock_id);

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  new_buffer->comp_cost = 0;
  new_buffer->io_cost = 0;
  new_buffer->comp_hits = 0;

  /* The actual payload we want to cache (i.e.: the page). */
  new_buffer->data_length = 0;
  new_buffer->comp_length = 0;
  new_buffer->data = NULL;

  if (page_filespec == NULL)
    return new_buffer;

  /* Use *page to try to read the page from the disk.  Time the operation.*/
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  FILE *fh = fopen(page_filespec, "rb");
  if (fh == NULL)
    show_error(E_GENERIC, "Unable to open file: %s.\n", page_filespec);
  fseek(fh, 0, SEEK_END);
  new_buffer->data_length = ftell(fh);
  rewind(fh);
  new_buffer->data = malloc(new_buffer->data_length);
  if (new_buffer->data == NULL)
    show_error(E_GENERIC, "Unable to allocate memory when loading a buffer's data member.");
  fread(new_buffer->data, new_buffer->data_length, 1, fh);
  fclose(fh);
  clock_gettime(CLOCK_MONOTONIC, &end);
  new_buffer->io_cost = BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;

  return new_buffer;
}


/* buffer__lock
 * Setting a lock just locks the mutex from the locker_pool[].  Since we support concurrency, it's possible to have a thread
 * waiting for a lock on a buffer while another thread is removing that buffer entirely.  So we add a little more logic for that.
 */
int buffer__lock(Buffer *buf) {
  /* Check to make sure the buffer exists. */
  if (buf == NULL)
    return E_BUFFER_POOFED;
  pthread_mutex_lock(&locker_pool[buf->lock_id].mutex);
  /* If a buffer is victimized we can still lock it, but the caller needs to know. This is safe because buffer__victimize locks. */
  if (buf->victimized != 0)
    return E_BUFFER_IS_VICTIMIZED;
  return E_OK;
}


/* buffer__unlock
 * This will unlock the element in the locker_pool[] with the element matching lock_id.  Since this can only be reached at the end
 * of a block who already owns the lock, we don't need any special checking.
 */
void buffer__unlock(Buffer *buf) {
  pthread_mutex_unlock(&locker_pool[buf->lock_id].mutex);
}


/* buffer__update_ref
 * Updates a buffer's ref_count.  This will only ever be +1 or -1.  Caller MUST lock the buffer to handle victimization properly.
 *   +1) When adding, only list__search can be the caller which maintains proper list-locking to prevent buffer *poofing*.
 *   -1) Anyone is safe to remove their own ref because victimization blocks, preventing *poofing*.
 */
int buffer__update_ref(Buffer *buf, int delta) {
  if (delta > 0 && buf->victimized > 0)
    return E_BUFFER_IS_VICTIMIZED;

  // At this point we're safe to modify the ref_count.  When decrementing, check to see if we need to broadcast.
  buf->ref_count += delta;
  if (buf->victimized != 0 && buf->ref_count == 0)
    pthread_cond_broadcast(&locker_pool[buf->lock_id].condition);

  // If we're incrementing we need to update popularity too.
  if (delta > 0)
    buf->popularity++;

  return E_OK;
}


/* buffer__victimize
 * Marks the victimized attribute of the buffer and sets up a condition to wait for the ref_count to reach 0.  This allows this
 * function to fully block the caller until the buffer is ready to be removed.  The list__remove() function is how you get rid of
 * a buffer from the list since we need to manage the pointers.  In fact, only list__remove() should ever call this.
 * The buffer MUST remain locked upon exit otherwise another thread could try reading the buffer while we go back up the stack.
 */
int buffer__victimize(Buffer *buf) {
  /* Try to lock the buffer.  If it returns already victimized then we don't need to do anything.  Any other non-zero, error. */
  int rv = buffer__lock(buf);
  if (rv > 0 && rv != E_BUFFER_IS_VICTIMIZED)
    return rv;
  buf->victimized = 1;
  while(buf->ref_count != 0)
    pthread_cond_wait(&locker_pool[buf->lock_id].condition, &locker_pool[buf->lock_id].mutex);
  return E_OK;
}


/* buffer__compress
 * Compresses the buffer's ->data element.  This is done with lz4 from https://github.com/Cyan4973/lz4
 * Whatever is in ->data will be obliterated without any checking (free()'d).
 * Compression only happens when a buffer is found to be useless and is prime for victimization and moved to the compressed list.
 * The list__migrate function will handle list locking; it will also invoke the buffer__victimize function to drain the buffer of
 * references before attempting to compress its ->data member and move it to the compressed list.  Since the buffer is victimized
 * and list__search is blocked we can be assured no one gets a bad read of ->data.
 */
int buffer__compress(Buffer *buf) {
  /* Make sure we have a valid buffer with valid data element. */
  if (buf == NULL)
    return E_BUFFER_NOT_FOUND;
  int rv = buffer__lock(buf);
  if (rv != E_OK)
    return rv;
  if (buf->data == NULL || buf->data_length == 0 || buf->comp_length != 0) {
    buffer__unlock(buf);
    return E_BUFFER_MISSING_DATA;
  }

  /* Data looks good, time to compress. */
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  int max_compressed_size = LZ4_compressBound(buf->data_length);
  void *compressed_data = (void *)malloc(max_compressed_size);
  if (compressed_data == NULL)
    show_error(E_GENERIC, "Failed to allocate memory during buffer__compress() operation for compressed_data pointer.");
  int compressed_bytes = LZ4_compress_default(buf->data, compressed_data, buf->data_length, max_compressed_size);
  if (compressed_bytes < 1) {
    printf("buffer__compress returned a negative result from LZ4_compress_default: %d\n.", rv);
    buffer__unlock(buf);
    return E_GENERIC;
  }

  /* Now free buf->data and modify the pointer to look at *compressed_data.  We cannot simply assign the pointer address of
   * compressed_data to buf->data because it'll waste space.  We need to malloc on the heap, memcpy data, and then free. */
  free(buf->data);
  buf->comp_length = compressed_bytes;
  buf->data = (void *)malloc(buf->comp_length);
  memcpy(buf->data, compressed_data, buf->comp_length);
  free(compressed_data);

  /* At this point we've compressed the raw data and saved it in a tightly allocated section of heap.  Save time and unlock. */
  clock_gettime(CLOCK_MONOTONIC, &end);
  buf->comp_cost += BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  buffer__unlock(buf);

  return E_OK;
}


/* buffer__decompress
 * Decompresses the buffer's ->data element.  This is done with lz4 from https://github.com/Cyan4973/lz4
 * The only way a buffer can be decompressed is if a list__search on an uncompressed list fails and the buffer is found in the
 * compressed list.  When this happens, list__migrate is invoked to handle list locking which prevents 2 threads from reaching this
 * this function at the same time.  The resident buffer__lock(buf) should protect the buffer itself from anyone who might be trying
 * to lock it for whatever reason.  Since no one is reading ->data (it's compressed) and we don't care about dirty reads from race
 * conditions with this buffer's other members, we don't need to victimize or drain it of refs.
 */
int buffer__decompress(Buffer *buf) {
  /* Make sure we have a valid buffer with valid data element. */
  if (buf == NULL)
    return E_BUFFER_NOT_FOUND;
  int rv = buffer__lock(buf);
  if (rv != E_OK)
    return rv;
  if (buf->data == NULL) {
    buffer__unlock(buf);
    return E_BUFFER_MISSING_DATA;
  }
  if (buf->data_length == 0) {
    buffer__unlock(buf);
    return E_BUFFER_MISSING_DATA;
  }
  if (buf->comp_length == 0) {
    // Someone beat us to it...
    buffer__unlock(buf);
    return E_OK;
  }

  /* Data looks good, time to decompress.  Lock the buffer for safety. */
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  void *decompressed_data = (void *)malloc(buf->data_length);
  if (decompressed_data == NULL)
    show_error(E_GENERIC, "Failed to allocate memory for buffer__decompress() for the decompressed_data pointer.");
  rv = LZ4_decompress_safe(buf->data, decompressed_data, buf->comp_length, buf->data_length);
  if (rv < 0) {
    printf("Failed to decompress the data in buffer %d, rv was %d.\n", buf->id, rv);
    buffer__unlock(buf);
    return E_GENERIC;
  }

  /* Now free buf->data of it's compressed information and modify the pointer to look at *decompressed_data now. We can avoid using
   * memcpy because we kept a record of how long the original data_length was, so no guess work. */
  free(buf->data);
  buf->data = decompressed_data;

  /* At this point we've decompressed the data and replaced buf->data with it.  Update tracking counters and move on. */
  clock_gettime(CLOCK_MONOTONIC, &end);
  buf->comp_hits++;
  buf->comp_length = 0;
  buf->comp_cost += BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  buffer__unlock(buf);

  return E_OK;
}


/* buffer__copy
 * Simple function to copy the contents of one buffer and all its elements to another.
 */
int buffer__copy(Buffer *src, Buffer *dst) {
  /* Make sure the buffer is real.  Caller must initialize. */
  if (dst == NULL)
    show_error(E_GENERIC, "The buffer__copy function was given a dst buffer pointer that was NULL.  This shouldn't happen.  Ever.");

  /* Attributes for typical buffer organization and management. */
  dst->id         = src->id;
  dst->ref_count  = src->ref_count;
  dst->popularity = src->popularity;
  dst->victimized = src->victimized;
  dst->lock_id    = src->lock_id;

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  dst->comp_cost = src->comp_cost;
  dst->io_cost   = src->io_cost;
  dst->comp_hits = src->comp_hits;

  /* The actual payload we want to cache (i.e.: the page). */
  dst->data_length = src->data_length;
  dst->comp_length = src->comp_length;
  memcpy(dst->data, src->data, (src->comp_length > 0 ? src->comp_length : src->data_length));

  return E_OK;
}
