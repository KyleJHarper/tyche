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

/* Set up the next_removal_index to aid with keeping lists sorted but keeping freshest data. */
#define MAX_REMOVAL_INDEX_VALUE UINT16_MAX
removal_index_t next_removal_index = 0;
pthread_mutex_t next_removal_index_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_POOFED;
extern const int E_BUFFER_IS_VICTIMIZED;
extern const int E_BUFFER_MISSING_DATA;


/* Functions */
/* buffer__initialize
 * Creates a new buffer, simply put.  We have to create a new buffer (with a new pointer) and return its address because we NULL
 * out existing buffers' pointers when we remove them from their pools.  The *page_filespec is the path to the file (page) we're
 * going to read into the buffer's ()->data member.
 */
Buffer* buffer__initialize(bufferid_t id, char *page_filespec) {
  Buffer *new_buffer = (Buffer *)malloc(sizeof(Buffer));
  if (new_buffer == NULL)
    show_error("Error malloc-ing a new buffer in buffer__initialize.", E_GENERIC);
  lock__assign_next_id(&new_buffer->lock_id);
  new_buffer->ref_count = 0;
  new_buffer->removal_index = 0;
  new_buffer->id = id;
  new_buffer->comp_cost = 0;
  new_buffer->comp_hits = 0;
  new_buffer->victimized = 0;
  new_buffer->popularity = 0;
  new_buffer->data_length = 0;
  new_buffer->io_cost = 0;
  new_buffer->removal_index = 0;
  new_buffer->data = NULL;
  if (page_filespec == NULL)
    return new_buffer;

  /* Use *page to try to read the page from the disk.  Time the operation.*/
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  FILE *fh = fopen(page_filespec, "rb");
  if (fh == NULL)
    show_error("Unable to open file.\n", E_GENERIC);
  fseek(fh, 0, SEEK_END);
  new_buffer->data_length = ftell(fh);
  rewind(fh);
  new_buffer->data = malloc(new_buffer->data_length);
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


/* buffer__assign_next_removal_index
 * This will assign the next available removal_index to the caller (via the pointer sent).  Checks for max value and circles back to
 * zero.  Only functions moving items from the raw list to the compressed list should use this.
 */
void buffer__assign_next_removal_index(removal_index_t *referring_id_ptr) {
  pthread_mutex_lock(&next_removal_index_mutex);
  if (next_removal_index == MAX_REMOVAL_INDEX_VALUE)
    next_removal_index = 0;
  next_removal_index++;
  *referring_id_ptr = next_removal_index;
  pthread_mutex_unlock(&next_removal_index_mutex);
}


/* buffer__compress
 * Compresses the buffer's ->data element.  This is done with lz4 from https://github.com/Cyan4973/lz4
 * Whatever is in ->data will be obliterated without any checking (free()'d).
 */
int buffer__compress(Buffer *buf) {
  /* Make sure we have a valid buffer with valid data element. */
  if (buf == NULL)
    return E_BUFFER_NOT_FOUND;
  if (buf->data == NULL)
    return E_BUFFER_MISSING_DATA;
  if (buf->data_length == 0)
    return E_BUFFER_MISSING_DATA;

  /* Data looks good, time to compress.  Lock the buffer, start the timer, and start working. */
  int rv = buffer__lock(buf);
  if (rv != E_OK)
    return rv;
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  int max_compressed_size = LZ4_compressBound(buf->data_length);
  unsigned char *compressed_data = (unsigned char *)malloc(max_compressed_size);
  int compressed_bytes = LZ4_compress_default(buf->data, compressed_data, buf->data_length, max_compressed_size);
  if (compressed_bytes < 1) {
    printf("buffer__compress returned a negative result from LZ4_compress_default: %d\n.", rv);
    return E_GENERIC;
  }

  /* Now free buf->data and modify the pointer to look at *compressed_data.  We cannot simply assign the pointer address of
   * compressed_data to buf->data because it'll waste space.  We need to malloc on the heap, memcpy data, and then free. */
  free(buf->data);
  buf->data_length = compressed_bytes;
  buf->data = (unsigned char *)malloc(buf->data_length);
  buf->data = compressed_data;
  memcpy(buf->data, compressed_data, buf->data_length);
  free(compressed_data);

  /* At this point we've compressed the raw data and saved it in a tightly allocated section of heap.  Save time and unlock. */
  clock_gettime(CLOCK_MONOTONIC, &end);
  buf->comp_cost += BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  buffer__unlock(buf);

  return E_OK;
}


/* buffer__decompress
 * Decompresses the buffer's ->data element.  This is done with lz4 from https://github.com/Cyan4973/lz4
 */
int buffer__decompress(Buffer *buf) {
  /* Make sure we have a valid buffer with valid data element. */
  if (buf == NULL)
    return E_BUFFER_NOT_FOUND;
  if (buf->data == NULL)
    return E_BUFFER_MISSING_DATA;
  if (buf->data_length == 0)
    return E_BUFFER_MISSING_DATA;

  /* Data looks good, time to decompress. */
  char *decompressed_data = (char *)malloc(buf->data_length);
  int rv = LZ4_decompress_fast(buf->data, decompressed_data, buf->data_length);
  printf("decomp rv is %d\n", rv);

  /* Now free buf->data and modify the pointer to look at *decompressed_data now. */
  free(buf->data);
  buf->data = decompressed_data;

  return E_OK;
}
