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
#include <time.h>     /* for clock_gettime() */
#include <string.h>   /* for memcpy() */
#include "error.h"
#include "buffer.h"
#include "lz4.h"


/* Create a buffer initializer to help save time. */
const Buffer BUFFER_INITIALIZER = {
  /* Attributes for typical buffer organization and management. */
  .id = 0,
  .ref_count = 0,
  .popularity = 0,
  .generation = 0,
  .victimized = 0,
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .condition = PTHREAD_COND_INITIALIZER,
  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  .comp_cost = 0,
  .io_cost = 0,
  .comp_hits = 0,
  /* The actual payload we want to cache (i.e.: the page). */
  .data_length = 0,
  .comp_length = 0,
  .data = NULL,
  /* Tracking for the list we're part of. */
  .next = NULL
};

/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L

/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
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
  /* Load default values via memcpy from a template defined above. */
  memcpy(new_buffer, &BUFFER_INITIALIZER, sizeof(Buffer));
  new_buffer->id = id;

  /* If we weren't given a filespec, then we're done.  Peace out. */
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
  if (fread(new_buffer->data, new_buffer->data_length, 1, fh) == 0)
    show_error(E_GENERIC, "Failed to read the data for a new buffer: %s", page_filespec);
  fclose(fh);
  clock_gettime(CLOCK_MONOTONIC, &end);
  new_buffer->io_cost = BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;

  return new_buffer;
}


/* buffer__destroy
 * A concise function to completely free up the memory used by a buffer.
 * Caller MUST have victimized the buffer before this is allowed!  This means the buffer's lock will be LOCKED!
 */
int buffer__destroy(Buffer *buf) {
  if (buf == NULL)
    return E_OK;
  if (buf->victimized == 0)
    show_error(E_GENERIC, "The buffer__destroy() function was called with a non-victimized buffer.  This isn't allowed.");

  /* Free the members which are pointers to other data locations. */
  free(buf->data);
  buf->data = NULL;
  /* All remaining members will die when free is invoked against the buffer itself. */
  free(buf);
  // Setting buf to NULL here is useless because it's not a double pointer, on purpose.  Caller needs to NULL their own pointers.

  /* Then unlock the element with lock_id now that the buffer is gone, in case others use this ID. */
  return E_OK;
}

/* buffer__lock
 * Simply locks the ->lock (mutex) of the buffer for times when atomicity matter.
 */
int buffer__lock(Buffer *buf) {
  pthread_mutex_lock(&buf->lock);
  /* If a buffer is victimized we can still lock it, but the caller needs to know. This is safe because buffer__victimize locks. */
  if (buf->victimized != 0)
    return E_BUFFER_IS_VICTIMIZED;
  return E_OK;
}


/* buffer__unlock
 * Unlocks the ->lock element (mutex) for the given buffer.
 */
void buffer__unlock(Buffer *buf) {
  pthread_mutex_unlock(&buf->lock);
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
    pthread_cond_broadcast(&buf->condition);

  // If we're incrementing we need to update popularity too.
  if (delta > 0 && buf->popularity < MAX_POPULARITY)
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
    pthread_cond_wait(&buf->condition, &buf->lock);
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
buf->comp_length = buf->data_length;
return E_OK;
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
buf->comp_length = 0;
return E_OK;
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
  // The lock and condition do not need to be linked.

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  dst->comp_cost = src->comp_cost;
  dst->io_cost   = src->io_cost;
  dst->comp_hits = src->comp_hits;

  /* The actual payload we want to cache (i.e.: the page). */
  dst->data_length = src->data_length;
  dst->comp_length = src->comp_length;
  free(dst->data);
  dst->data = malloc(src->comp_length > 0 ? src->comp_length : src->data_length);
  memcpy(dst->data, src->data, (src->comp_length > 0 ? src->comp_length : src->data_length));

  /* Tracking for the list we're part of. */
  // We do NOT copy ->next data because that's handled by list__* functions.
  dst->next = NULL;

  return E_OK;
}
