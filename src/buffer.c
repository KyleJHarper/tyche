/*
 * buffer.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

/* Include Headers */
#include <pthread.h>
#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>     /* for clock_gettime() */
#include <string.h>   /* for memcpy() */
#include "buffer.h"
#include "lz4/lz4.h"
#include "zlib/zlib.h"
#include "zstd/zstd.h"


/* We use the compressor IDs in this file. */
extern const int NO_COMPRESSOR_ID;
extern const int LZ4_COMPRESSOR_ID;
extern const int ZLIB_COMPRESSOR_ID;
extern const int ZSTD_COMPRESSOR_ID;


/* Create a buffer initializer to help save time. */
const Buffer BUFFER_INITIALIZER = {
  /* Attributes for typical buffer organization and management. */
  .id = 0,
  .ref_count = 0,
  .pending_sweep = 0,
  .popularity = 0,
  .victimized = 0,
  .is_ephemeral = 0,
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .reader_cond = PTHREAD_COND_INITIALIZER,
  .writer_cond = PTHREAD_COND_INITIALIZER,
  .pending_writers = 0,
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
extern const int E_BUFFER_ALREADY_COMPRESSED;
extern const int E_BUFFER_ALREADY_DECOMPRESSED;
// Also get the NO_MEMORY one for allocations.
extern const int E_NO_MEMORY;
extern const int E_BAD_ARGS;


/* Store the overhead of a Buffer for others to use for calculations.  Note this doesn't count the SkipListNode (24 bytes), probabilistically. */
const int BUFFER_OVERHEAD = sizeof(Buffer);




/* buffer__initialize
 * TODO Fix description
 * Creates a new buffer, simply put.  We have to create a new buffer (with a new pointer) and return its address because we NULL
 * out existing buffers' pointers when we remove them from their pools.  The *page_filespec is the path to the file (page) we're
 * going to read into the buffer's ()->data member.
 */
int buffer__initialize(Buffer **buf, bufferid_t id, char *page_filespec) {
  *buf = (Buffer *)malloc(sizeof(Buffer));
  if (*buf == NULL)
    return E_NO_MEMORY;
  /* Load default values via memcpy from a template defined above. */
  memcpy(*buf, &BUFFER_INITIALIZER, sizeof(Buffer));
  (*buf)->id = id;

  /* If we weren't given a filespec, then we're done.  Peace out. */
  if (page_filespec == NULL)
    return E_OK;

  /* Use *page to try to read the page from the disk.  Time the operation.*/
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  FILE *fh = fopen(page_filespec, "rb");
  if (fh == NULL)
    return E_GENERIC;
  fseek(fh, 0, SEEK_END);
  (*buf)->data_length = ftell(fh);
  rewind(fh);
  (*buf)->data = malloc((*buf)->data_length);
  if ((*buf)->data == NULL)
    return E_NO_MEMORY;
  if (fread((*buf)->data, (*buf)->data_length, 1, fh) == 0)
    return E_GENERIC;
  fclose(fh);
  clock_gettime(CLOCK_MONOTONIC, &end);
  (*buf)->io_cost = BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;

  return E_OK;
}


/* buffer__destroy
 * A concise function to completely free up the memory used by a buffer.
 * Caller MUST have victimized the buffer before this is allowed!  This means the buffer's lock will be LOCKED!
 */
int buffer__destroy(Buffer *buf) {
  if (buf == NULL)
    return E_OK;
  if (buf->victimized == 0 && buf->is_ephemeral == 0)
    return E_BAD_ARGS;

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
 * Simply locks the ->lock (mutex) of the buffer for times when atomicity matter.  Always checks for victimization, that's it.
 */
int buffer__lock(Buffer *buf) {
  pthread_mutex_lock(&buf->lock);
  /* If a buffer is victimized we can still lock it, but the caller needs to know. This is safe because buffer__victimize locks. */
  if (buf->victimized != 0)
    return E_BUFFER_IS_VICTIMIZED;
  return E_OK;
}


/* buffer__unlock
 * Unlocks the ->lock element (mutex) for the given buffer.  Compiler will likely inline, but we might add complexity later.
 */
void buffer__unlock(Buffer *buf) {
  pthread_mutex_unlock(&buf->lock);
}


/* buffer__update_ref
 * Updates a buffer's ref_count.  This will only ever be +1 or -1.  Caller MUST lock the buffer to handle victimization properly.
 *   +1) Adds a pin so the buffer can't be removed or modified.  Blocks if a modification operation is underway.
 *   -1) Anyone can remove their pin.  Upon 0 we notify pending operators (writers).
 */
int buffer__update_ref(Buffer *buf, int delta) {
  // Check to see if the buffer is victimized, if so we can't add new pins.
  if (delta > 0 && buf->victimized > 0)
    return E_BUFFER_IS_VICTIMIZED;

  // Check to see if new refs are supposed to be blocked.  If so, wait.
  while (delta > 0 && buf->pending_writers != 0)
    pthread_cond_wait(&buf->reader_cond, &buf->lock);

  // At this point we're safe to modify the ref_count.  When decrementing, check to see if we need to broadcast to anyone.
  buf->ref_count += delta;
  if ((buf->pending_writers != 0 || buf->victimized != 0) && buf->ref_count == 0)
    pthread_cond_broadcast(&buf->writer_cond);

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
    pthread_cond_wait(&buf->writer_cond, &buf->lock);
  return E_OK;
}


/* buffer__block
 * Similar to buffer__victimize().  The main difference is this will simply block the caller until ref_count hits zero, upon which
 * it will continue on while holding the lock to prevent others from doing anything else.  See buffer__update_ref() for how it works
 * together.
 * The buffer will REMAIN LOCKED since only a buffer__unblock() from this same thread should ever resume normal flow.
 */
int buffer__block(Buffer *buf) {
  // Lock the buffer.  It might be victimized, so check for that and let caller know.
  int rv = buffer__lock(buf);
  if (rv != E_OK)
    return rv;
  buf->pending_writers++;
  while(buf->ref_count != 0)
    pthread_cond_wait(&buf->writer_cond, &buf->lock);
  return E_OK;
}


/* buffer__unblock
 * Removes the blocking status from the buffer and starts a cascade of signals to others if pending_writers is 0.
 * Caller MUST have this buffer locked from buffer__block() already!
 */
int buffer__unblock(Buffer *buf) {
  // We don't need to do any checking because a block call previously has already protected us.  We just need to signal later.
  buf->pending_writers--;
  if(buf->pending_writers == 0)
    pthread_cond_broadcast(&buf->reader_cond);
  buffer__unlock(buf);
  return E_OK;
}


/* buffer__compress
 * Compresses the buffer's ->data element.
 * Whatever is in ->data will be obliterated without any checking (free()'d).
 * The data_length will remain intact because the compressor needs it for safety (and a future malloc), and we set comp_length to
 * allow us to modify the size(s) in the list accurately.  See buffer__decompress() for the counterpart to this.
 * Caller MUST block (buffer__block/unblock) to drain readers!
 */
int buffer__compress(Buffer *buf, int compressor_id, int compressor_level) {
  // Make sure we're supposed to be here.
  if(compressor_id == NO_COMPRESSOR_ID) {
    buf->comp_length = buf->data_length;
    return E_OK;
  }

  /* Make sure we have a valid buffer with valid data element. */
  if (buf == NULL)
    return E_BUFFER_NOT_FOUND;
  int rv = E_OK;
  if (buf->data == NULL || buf->data_length == 0)
    return E_BUFFER_MISSING_DATA;
  if (buf->comp_length != 0)
    return E_BUFFER_ALREADY_COMPRESSED;

  /* Data looks good, time to compress. */
  struct timespec start, end;
  void *compressed_data = NULL;
  clock_gettime(CLOCK_MONOTONIC, &start);
  // -- Using LZ4
  if(compressor_id == LZ4_COMPRESSOR_ID) {
    int max_compressed_size = LZ4_compressBound(buf->data_length);
    compressed_data = (void *)malloc(max_compressed_size);
    if (compressed_data == NULL)
      return E_NO_MEMORY;
    rv = LZ4_compress_default(buf->data, compressed_data, buf->data_length, max_compressed_size);
    if (rv < 1) {
      printf("buffer__compress returned a negative result from LZ4_compress_default: %d\n.", rv);
      return E_GENERIC;
    }
    // LZ4 returns the compressed size in the rv itself, assign it here.
    buf->comp_length = rv;
  }
  // -- Using Zlib
  if(compressor_id == ZLIB_COMPRESSOR_ID) {
    uLongf max_compressed_size = compressBound(buf->data_length);
    compressed_data = (void *)malloc(max_compressed_size);
    if (compressed_data == NULL)
      return E_NO_MEMORY;
    rv = compress2(compressed_data, &max_compressed_size, buf->data, buf->data_length, compressor_level);
    if (rv != Z_OK) {
      printf("buffer__compress returned an error from zlib's compress2: %d\n.", rv);
      return E_GENERIC;
    }
    // Zlib returns the compressed length in max_compressed_size; so assign it here.
    buf->comp_length = max_compressed_size;
  }
  // -- Using Zstd
  if(compressor_id == ZSTD_COMPRESSOR_ID) {
    int max_compressed_size = ZSTD_compressBound(buf->data_length);
    compressed_data = (void *)malloc(max_compressed_size);
    if (compressed_data == NULL)
      return E_NO_MEMORY;
    rv = ZSTD_compress(compressed_data, max_compressed_size, buf->data, buf->data_length, compressor_level);
    if (ZSTD_isError(rv)) {
      printf("buffer__compress returned a negative result from ZSTD_compress: %d\n.", rv);
      return E_GENERIC;
    }
    // ZSTD returns the compressed size in the rv itself, assign it here.
    buf->comp_length = rv;
  }

  /* Now free buf->data and modify the pointer to look at *compressed_data.  We cannot simply assign the pointer address of
   * compressed_data to buf->data because it'll waste space.  We need to malloc on the heap, memcpy data, and then free. */
  free(buf->data);
  buf->data = (void *)malloc(buf->comp_length);
  memcpy(buf->data, compressed_data, buf->comp_length);
  free(compressed_data);

  /* At this point we've compressed the raw data and saved it in a tightly allocated section of heap. */
  clock_gettime(CLOCK_MONOTONIC, &end);
  buf->comp_cost += BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;

  return E_OK;
}


/* buffer__decompress
 * Decompresses the buffer's ->data element.
 * This sets comp_length back to 0 which signals that the buffer is no longer in a compressed state.
 * Caller MUST block (buffer__block/unblock) to drain readers!
 */
int buffer__decompress(Buffer *buf, int compressor_id) {
  // Make sure we're supposed to actually be doing work.
  if(compressor_id == NO_COMPRESSOR_ID) {
    buf->comp_length = 0;
    return E_OK;
  }

  /* Make sure we have a valid buffer with valid data element. */
  int rv = E_OK;
  if (buf == NULL)
    return E_BUFFER_NOT_FOUND;
  if (buf->data == NULL || buf->data_length == 0)
    return E_BUFFER_MISSING_DATA;
  if (buf->comp_length == 0)
    return E_BUFFER_ALREADY_DECOMPRESSED;

  /* Data looks good, time to decompress.  Lock the buffer for safety. */
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  void *decompressed_data = (void *)malloc(buf->data_length);
  if (decompressed_data == NULL)
    return E_NO_MEMORY;
  // -- Use LZ4
  if(compressor_id == LZ4_COMPRESSOR_ID) {
    rv = LZ4_decompress_safe(buf->data, decompressed_data, buf->comp_length, buf->data_length);
    if (rv < 0) {
      printf("Failed to decompress the data in buffer %d, rv was %d.\n", buf->id, rv);
      return E_GENERIC;
    }
  }
  // -- Use Zlib
  if(compressor_id == ZLIB_COMPRESSOR_ID) {
    uLongf data_length = buf->data_length;
    rv = uncompress(decompressed_data, &data_length, buf->data, buf->comp_length);
    if (rv < 0 || data_length != buf->data_length) {
      printf("Failed to decompress the data in buffer %d with zlib, rv was %d.\n", buf->id, rv);
      return E_GENERIC;
    }
  }
  // -- Use Zstd
  if(compressor_id == ZSTD_COMPRESSOR_ID) {
    rv = ZSTD_decompress(decompressed_data, buf->data_length, buf->data, buf->comp_length);
    if (ZSTD_isError(rv)) {
      printf("Failed to decompress the data in buffer %d, rv was %d.\n", buf->id, rv);
      return E_GENERIC;
    }
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

  return E_OK;
}


/* buffer__copy
 * Simple function to copy the contents of one buffer and all its elements to another.
 */
int buffer__copy(Buffer *src, Buffer *dst) {
  /* Make sure the buffer is real.  Caller must initialize. */
  if (dst == NULL)
    return E_BAD_ARGS;

  /* Attributes for typical buffer organization and management. */
  dst->id           = src->id;
  dst->ref_count    = src->ref_count;
  dst->popularity   = src->popularity;
  dst->is_ephemeral = src->is_ephemeral;
  dst->victimized   = src->victimized;
  // The lock and conditions do not need to be linked.  Nor do pending writers.

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
