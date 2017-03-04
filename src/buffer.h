/*
 * buffer.h
 *
 *  Created on: Jun 18, 2015
 *      Author: Kyle Harper
 * Description: Exposes the structures and whatnot for a buffer.  Each buffer holds some basic tracking information as well as the
 *              data field itself that we will call a Page.
 */

#ifndef SRC_BUFFER_H_
#define SRC_BUFFER_H_

/* Include necessary headers here. */
#include <stdint.h>  /* Used for the uint_ types */
#include <stdbool.h> /* For bool types. */


/* Globals to help track limits. */
#define MAX_POPULARITY UINT8_MAX
#define BUFFER_ID_MAX  UINT32_MAX

/* Enumerator for bit-flags in the buffer. */
typedef enum buffer_flags {
  // These control CoW (copy-on-write) synchronization.
  dirty         = 1 <<  0,   //  1
  pending_sweep = 1 <<  1,   //  2
  updating      = 1 <<  2,   //  4
  removing      = 1 <<  3,   //  8
  removed       = 1 <<  4,   // 16
  // When sweeping we need to mark the buffer as 'compressing' so list__update() doesn't flake out (deadlock).
  compressing   = 1 <<  5,   // 32
  compressed    = 1 <<  6,   // 64
} buffer_flags;

/* Build the typedef and structure for a Buffer */
typedef uint32_t bufferid_t;
typedef uint8_t popularity_t;
typedef struct buffer Buffer;
struct buffer {
  /* Tracking for the list we're part of. */
  Buffer *next;                /* Pointer to the next neighbor since lists are singularly linked. */

  /* Attributes for typical buffer organization and management. */
  bufferid_t id;               /* Identifier of the page. Should come from the system providing the data itself (e.g.: inode). */
  uint16_t ref_count;          /* Number of references currently holding this buffer. */
  buffer_flags flags;          /* Holds 32 bit flags.  See enum above for details. */
  popularity_t popularity;     /* Rapidly decaying counter used for victim selection with clock sweep.  Ceiling of MAX_POPULARITY. */
  pthread_mutex_t lock;        /* The primary locking element for individual buffer protection. */

  /* Cost values for each buffer. */
  uint32_t comp_cost;          /* Time spent, in ns, to compress and decompress a page.  Using clock_gettime(3) */
  uint16_t comp_hits;          /* Number of times reclaimed from the compressed table during a polling period. */

  /* The actual payload we want to cache (i.e.: the page). */
  uint32_t data_length;        /* Number of bytes originally in *data. */
  uint32_t comp_length;        /* Number of bytes in *data if it was compressed.  Set to 0 when not used. */
  void *data;                  /* Pointer to the memory holding the page data, whether raw or compressed. */
};


/* Prototypes */
int buffer__initialize(Buffer **buf, bufferid_t id, uint32_t size, void *data, char *page_filespec);
void buffer__destroy(Buffer *buf, const bool destroy_data);
void buffer__lock(Buffer *buf);
void buffer__unlock(Buffer *buf);
void buffer__release_pin(Buffer *buf);
int buffer__compress(Buffer *buf, void **compressed_data, int compressor_id, int compressor_level);
int buffer__decompress(Buffer *buf, int compressor_id);
void buffer__copy(Buffer *src, Buffer *dst, bool copy_data);


#endif /* SRC_BUFFER_H_ */
