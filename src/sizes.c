/*
 * sizes.c
 *
 *  Created on: Jun 24, 2015
 *      Author: administrator
 */

#include <stdio.h>
#include <pthread.h>
#include "buffer.h"

int main() {
  int *iptr = NULL;
  float *fptr = NULL;
  char *cptr = NULL;
  //Buffer buf = (Buffer)malloc(sizeof(Buffer));

  printf("Size of Buffer             : %d Bytes\n", sizeof(Buffer));
  printf("Size of Buffer->id         : %d Bytes\n", sizeof((Buffer *)0)->id);
  printf("Size of Buffer->ref_count  : %d Bytes\n", sizeof((Buffer *)0)->ref_count);
  printf("Size of Buffer->popularity : %d Bytes\n", sizeof((Buffer *)0)->popularity);
  printf("Size of Buffer->victimized : %d Bytes\n", sizeof((Buffer *)0)->victimized);
  printf("Size of Buffer->comp_cost  : %d Bytes\n", sizeof((Buffer *)0)->comp_cost);
  printf("Size of Buffer->io_cost    : %d Bytes\n", sizeof((Buffer *)0)->io_cost);
  printf("Size of Buffer->comp_hits  : %d Bytes\n", sizeof((Buffer *)0)->comp_hits);
  printf("Size of Buffer->previous   : %d Bytes\n", sizeof((Buffer *)0)->previous);
  printf("Size of Buffer->next       : %d Bytes\n", sizeof((Buffer *)0)->next);
  printf("Size of Buffer->lock_id    : %d Bytes\n", sizeof((Buffer *)0)->lock_id);
  printf("Size of Buffer->data_length: %d Bytes\n", sizeof((Buffer *)0)->data_length);
  printf("Size of Buffer->data       : %d Bytes\n", sizeof((Buffer *)0)->data);

  uint8_t popularity;    /* Rapidly decaying counter used for victim selection with clock sweep.  Ceiling of MAX_POPULARITY. */
  uint8_t victimized;    /* If the buffer has been victimized this is set non-zero.  Prevents incrementing of ref_count. */

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  uint32_t comp_cost;    /* Time spent, in ns, to compress and decompress a page during a polling period.  Using clock_gettime(3) */
  uint32_t io_cost;      /* Time spent, in ns, to read this buffer from the disk.  Using clock_gettime(3) */
  uint16_t comp_hits;    /* Number of times reclaimed from the compressed table during a polling period. */

  /* We use a ring buffer so we track previous and next Buffers. */
  Buffer *previous;    /* Pointer to the previous buffer for use in a circular queue. */
  Buffer *next;          /* Pointer to the next buffer for use in a circular queue. */
  uint16_t lock_id;      /* Lock ID from the locker_pool[], rather than having a pthread mutex for each Buffer. */

  /* The actual payload we want to cache (i.e.: the page). */
  uint16_t data_length;  /* Number of bytes in data.  For raw tables, always PAGE_SIZE.  Compressed will vary. */
  char *data;            /* Pointer to the character array holding the page data. */

  return 0;
}
