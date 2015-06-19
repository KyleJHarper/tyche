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

/* Defines used by this header. */
#define MAX_POPULARITY=UINT8_MAX

/* Build the typedef and structure for a Buffer */
typedef struct buffer Buffer;
struct buffer {
  /* Attributes for typical buffer organization and management. */
  uint32_t id;           /* Identifier of the page. Should come from the system providing the data itself (e.g.: inode). */
  uint16_t ref_count;    /* Number of references currently holding this buffer. */
  uint8_t popularity;    /* Rapidly decaying counter used for victim selection with clock sweep.  Ceiling of MAX_POPULARITY. */

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  uint32_t comp_cost;    /* Time spent, in ns, to compress and decompress a page during a polling period.  Using clock_gettime(3) */
  uint32_t io_cost;      /* Time spent, in ns, to read this buffer from the disk.  Using clock_gettime(3) */
  uint16_t comp_hits;    /* Number of times reclaimed from the compressed table during a polling period. */

  /* The actual payload we want to cache (i.e.: the page). */
  char *data[];          /* Pointer to the character array holding the page data. */
  uint16_t data_length;  /* Number of bytes in data.  For raw tables, always PAGE_SIZE.  Compressed will vary. */

  /* We use a ring buffer so we track previous and next Buffers. */
  Buffer *previous;      /* Pointer to the previous buffer for use in a circular queue. */
  Buffer *next;          /* Pointer to the next buffer for use in a circular queue. */
  uint16_t lock_id;      /* Lock ID from the lock_pool[], rather than having a pthread mutex for each Buffer. */
};

#endif /* SRC_BUFFER_H_ */