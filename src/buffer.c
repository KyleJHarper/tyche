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
  .data = NULL,
  .data_length = 0,
  .comp_length = 0,
  .bucket_right = NULL,
  .bucket_left = NULL,
  .windows = {0},
  .id = 0,
  .overhead = 0,
  .ref_count = 0,
  .flags = 0,
  .bucket_index = 0,
  .bucket_cas_lock = 0,
  .cas_lock = 0,
  .sl_levels = 1,
  .padding = 0
  // We can't set the ->nexts[] FAM here.
};

/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L

/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_MISSING_DATA;
extern const int E_BUFFER_ALREADY_COMPRESSED;
extern const int E_BUFFER_ALREADY_DECOMPRESSED;
extern const int E_BUFFER_COMPRESSION_PROBLEM;
// Also get the NO_MEMORY one for allocations.
extern const int E_NO_MEMORY;




/* buffer__initialize
 * Creates a new buffer which will link to the *data provided.
 */
int buffer__initialize(Buffer **buf, bufferid_t id, uint8_t sl_levels, uint32_t size, void *data) {
  *buf = (Buffer *)malloc(sizeof(Buffer) + (sizeof(Buffer*) * sl_levels));
  if (*buf == NULL)
    return E_NO_MEMORY;
  /* Load default values via memcpy from a template defined above. */
  memcpy(*buf, &BUFFER_INITIALIZER, sizeof(Buffer));
  (*buf)->id = id;
  for(int i=0; i<sl_levels; i++)
    (*buf)->nexts = NULL;
  (*buf)->sl_levels = sl_levels;
  /* Set the overhead. */
  (*buf)->overhead = sizeof(Buffer) + (sizeof(Buffer*) * sl_levels);
  /* If the *data and size are all null/0, the user just wants a blank buffer. */
  if (size == 0 && data == NULL)
    return E_OK;
  /* Set the data and size, then move on. */
  (*buf)->data = data;
  (*buf)->data_length = size;

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
 * Simply locks the ->cas_lock of the buffer to control synchronization at a localized level.
 */
void buffer__lock(Buffer *buf) {
  while(!(__sync_bool_compare_and_swap(&buf->cas_lock, UNLOCKED, LOCKED)))
    __sync_synchronize();
  return;
}


/* buffer__unlock
 * Unlocks the ->cas_lock of the buffer, allowing another thread to take over.
 */
void buffer__unlock(Buffer *buf) {
  // There's no race here because only the owner should ever call unlock.
  buf->cas_lock = UNLOCKED;
  return;
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
  buf->comp_length = 0;

  return E_OK;
}


/* buffer__copy
 * Simple function to copy the contents of one buffer and all its elements to another.
 * Caller MUST have initialized with the same levels.
 */
void buffer__copy(Buffer *src, Buffer *dst, bool copy_data) {
  if(copy_data) {
    free(dst->data);
    dst->data = malloc(src->comp_length > 0 ? src->comp_length : src->data_length);
    memcpy(dst->data, src->data, (src->comp_length > 0 ? src->comp_length : src->data_length));
  }
  dst->data_length = src->data_length;
  dst->comp_length = src->comp_length;
  // Don't copy bucket_right or left
  for(int i=0; i<MAX_WINDOWS; i++)
    dst->windows[i] = src->windows[i];
  dst->id = src->id;
  dst->overhead = src->overhead;
  dst->ref_count = src->ref_count;
  // Do NOT copy flags.  Only list functions manage these.
  // Do NOT copy the bucket index or bucket cas lock
  // Do NOT copy the cas lock.
  dst->sl_levels = src->sl_levels;
  // Do NOT track ->nexts.  Only a list function should do this.

  return;
}
