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
#include <stdbool.h> /* For bool types. */
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
  .flags = 0,
  .popularity = 0,
  .lock = PTHREAD_MUTEX_INITIALIZER,
  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  .comp_cost = 0,
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
extern const int E_BUFFER_MISSING_DATA;
extern const int E_BUFFER_ALREADY_COMPRESSED;
extern const int E_BUFFER_ALREADY_DECOMPRESSED;
extern const int E_BUFFER_COMPRESSION_PROBLEM;
// Also get the NO_MEMORY one for allocations.
extern const int E_NO_MEMORY;
extern const int E_BAD_ARGS;




/* buffer__initialize
 * Creates a new buffer which will link to the *data provided.
 * The *page_filespec is purely for tyche testing, and frankly should go away.
 */
int buffer__initialize(Buffer **buf, bufferid_t id, uint32_t size, void *data, char *page_filespec) {
  *buf = (Buffer *)malloc(sizeof(Buffer));
  if (*buf == NULL)
    return E_NO_MEMORY;
  /* Load default values via memcpy from a template defined above. */
  memcpy(*buf, &BUFFER_INITIALIZER, sizeof(Buffer));
  (*buf)->id = id;
  /* If the page_filespec, *data, and size are all null/0, the user just wants a blank buffer. */
  if (page_filespec == NULL && size == 0 && data == NULL)
    return E_OK;
  /* Error if page_filespec and *data or size exist.  Logical XOR via boolean equality. */
  if ((page_filespec != NULL) == (size > 0 || data != NULL))
    return E_BAD_ARGS;

  /* If we weren't given a filespec, we better have been given *data and size. */
  if (page_filespec == NULL) {
    (*buf)->data = data;
    (*buf)->data_length = size;
    //memcpy((*buf)->data, data, size);
    return E_OK;
  }

  /* Use *page to try to read the page from the disk. */
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

  return E_OK;
}


/* buffer__destroy
 * A concise function to completely free up the memory used by a buffer.
 * Caller MUST have ensured the buffer is not referenced!
 */
void buffer__destroy(Buffer *buf, const bool destroy_data) {
  if (destroy_data) {
    /* Free the members which are pointers to other data locations. */
    free(buf->data);
  }
  /* All remaining members will die when free is invoked against the buffer itself. */
  free(buf);

  return;
}


/* buffer__lock
 * Simply locks the ->lock (mutex) of the buffer for times when atomicity matter.
 */
void buffer__lock(Buffer *buf) {
  pthread_mutex_lock(&buf->lock);
  return;
}


/* buffer__unlock
 * Unlocks the ->lock element (mutex) for the given buffer.
 */
void buffer__unlock(Buffer *buf) {
  pthread_mutex_unlock(&buf->lock);
}


/* buffer__release_pin
 * Pulls out a pin, atomically, for a buffer.
 */
void buffer__release_pin(Buffer *buf) {
  __sync_fetch_and_add(&buf->ref_count, -1);
}


/* buffer__compress
 * Compresses the buffer's ->data element.
 * Whatever is in ->data will be obliterated without any checking (free()'d).
 * The data_length will remain intact because the compressor needs it for safety (and a future malloc), and we set comp_length to
 * allow us to modify the size(s) in the list accurately.  See buffer__decompress() for the counterpart to this.
 * Caller MUST drain readers.  (Only sweep should use this...)
 */
int buffer__compress(Buffer *buf, void **compressed_data, int compressor_id, int compressor_level) {
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
  clock_gettime(CLOCK_MONOTONIC, &start);
  // -- Using LZ4
  if(compressor_id == LZ4_COMPRESSOR_ID) {
    int max_compressed_size = LZ4_compressBound(buf->data_length);
    *compressed_data = (void *)malloc(max_compressed_size);
    if (*compressed_data == NULL)
      return E_NO_MEMORY;
    rv = LZ4_compress_default(buf->data, *compressed_data, buf->data_length, max_compressed_size);
    if (rv < 1)
      return E_BUFFER_COMPRESSION_PROBLEM;
    // LZ4 returns the compressed size in the rv itself, assign it here.
    buf->comp_length = rv;
  }
  // -- Using Zlib
  if(compressor_id == ZLIB_COMPRESSOR_ID) {
    uLongf max_compressed_size = compressBound(buf->data_length);
    *compressed_data = (void *)malloc(max_compressed_size);
    if (*compressed_data == NULL)
      return E_NO_MEMORY;
    rv = compress2(*compressed_data, &max_compressed_size, buf->data, buf->data_length, compressor_level);
    if (rv != Z_OK)
      return E_BUFFER_COMPRESSION_PROBLEM;
    // Zlib returns the compressed length in max_compressed_size; so assign it here.
    buf->comp_length = max_compressed_size;
  }
  // -- Using Zstd
  if(compressor_id == ZSTD_COMPRESSOR_ID) {
    int max_compressed_size = ZSTD_compressBound(buf->data_length);
    *compressed_data = (void *)malloc(max_compressed_size);
    if (*compressed_data == NULL)
      return E_NO_MEMORY;
    rv = ZSTD_compress(*compressed_data, max_compressed_size, buf->data, buf->data_length, compressor_level);
    if (ZSTD_isError(rv))
      return E_BUFFER_COMPRESSION_PROBLEM;
    // ZSTD returns the compressed size in the rv itself, assign it here.
    buf->comp_length = rv;
  }

  /* At this point we've compressed the raw data and saved it in a tightly allocated section of heap. */
  clock_gettime(CLOCK_MONOTONIC, &end);
  buf->comp_cost += BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  return E_OK;
}


/* buffer__decompress
 * Decompresses the buffer's ->data element.
 * This sets comp_length back to 0 which signals that the buffer is no longer in a compressed state.
 * Caller MUST ensure no pins are in this (only search restores, which is safe for this).
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
    if (rv < 0)
      return E_BUFFER_COMPRESSION_PROBLEM;
  }
  // -- Use Zlib
  if(compressor_id == ZLIB_COMPRESSOR_ID) {
    uLongf data_length = buf->data_length;
    rv = uncompress(decompressed_data, &data_length, buf->data, buf->comp_length);
    if (rv < 0 || data_length != buf->data_length)
      return E_BUFFER_COMPRESSION_PROBLEM;
  }
  // -- Use Zstd
  if(compressor_id == ZSTD_COMPRESSOR_ID) {
    rv = ZSTD_decompress(decompressed_data, buf->data_length, buf->data, buf->comp_length);
    if (ZSTD_isError(rv))
      return E_BUFFER_COMPRESSION_PROBLEM;
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
void buffer__copy(Buffer *src, Buffer *dst, bool copy_data) {
  /* Attributes for typical buffer organization and management. */
  dst->id           = src->id;
  dst->ref_count    = src->ref_count;
  dst->popularity   = src->popularity;
  // The lock and conditions do not need to be linked.  Nor do pending writers.
  // Do NOT copy flags!

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  dst->comp_cost = src->comp_cost;
  dst->comp_hits = src->comp_hits;

  /* The actual payload we want to cache (i.e.: the page). */
  dst->data_length = src->data_length;
  dst->comp_length = src->comp_length;
  if(copy_data) {
    free(dst->data);
    dst->data = malloc(src->comp_length > 0 ? src->comp_length : src->data_length);
    memcpy(dst->data, src->data, (src->comp_length > 0 ? src->comp_length : src->data_length));
  }

  /* Tracking for the list we're part of. */
  // We do NOT copy ->next data because that's handled by list__* functions.
  dst->next = NULL;

  return;
}
