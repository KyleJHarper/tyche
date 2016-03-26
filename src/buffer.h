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


/* Globals to help track limits. */
#define MAX_POPULARITY UINT8_MAX
#define MAX_GENERATION UINT8_MAX
#define BUFFER_ID_MAX  UINT32_MAX

/* Build the typedef and structure for a Buffer */
typedef uint32_t bufferid_t;
typedef uint8_t popularity_t;
typedef struct buffer Buffer;
struct buffer {
  /* Attributes for typical buffer organization and management. */
  bufferid_t id;               /* Identifier of the page. Should come from the system providing the data itself (e.g.: inode). */
  uint16_t ref_count;          /* Number of references currently holding this buffer. */
  uint8_t pending_sweep;       /* Flag to indicate if this buffer is pending a sweep operation.  Prevents re-victimizing during a sweep. */
  popularity_t popularity;     /* Rapidly decaying counter used for victim selection with clock sweep.  Ceiling of MAX_POPULARITY. */
  uint8_t victimized;          /* If the buffer has been victimized this is set non-zero.  Prevents incrementing of ref_count. */
  uint8_t is_ephemeral;        /* Is this buffer part of a list, or an ephemeral copy the caller needs to remove? */
  pthread_mutex_t lock;        /* The primary locking element for individual buffer protection. */
  pthread_cond_t reader_cond;  /* The condition for readers to wait upon. */
  pthread_cond_t writer_cond;  /* The condition for writers to wait upon. */
  uint8_t pending_writers;     /* The number of writers (usually blockers) trying to work on the buffer. */

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  uint32_t comp_cost;          /* Time spent, in ns, to compress and decompress a page.  Using clock_gettime(3) */
  uint32_t io_cost;            /* Time spent, in ns, to read this buffer from the disk.  Using clock_gettime(3) */
  uint16_t comp_hits;          /* Number of times reclaimed from the compressed table during a polling period. */

  /* The actual payload we want to cache (i.e.: the page). */
  uint16_t data_length;        /* Number of bytes originally in *data. */
  uint16_t comp_length;        /* Number of bytes in *data if it was compressed.  Set to 0 when not used. */
  void *data;                  /* Pointer to the memory holding the page data, whether raw or compressed. */

  /* Tracking for the list we're part of. */
  Buffer *next;                /* Pointer to the next neighbor since lists are singularly linked. */
};


/* Prototypes */
Buffer* buffer__initialize(bufferid_t id, char *page_filespec);
int buffer__destroy(Buffer *buf);
int buffer__lock(Buffer *buf);
void buffer__unlock(Buffer *buf);
int buffer__update_ref(Buffer *buf, int delta);
int buffer__victimize(Buffer *buf);
int buffer__block(Buffer *buf);
int buffer__unblock(Buffer *buf);
int buffer__compress(Buffer *buf);
int buffer__decompress(Buffer *buf);
int buffer__copy(Buffer *src, Buffer *dst);


#endif /* SRC_BUFFER_H_ */
