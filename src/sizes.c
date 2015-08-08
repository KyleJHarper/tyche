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

  printf("Size of Buffer->id         : %zu Bytes\n", sizeof((Buffer *)0)->id);
  printf("Size of Buffer->ref_count  : %zu Bytes\n", sizeof((Buffer *)0)->ref_count);
  printf("Size of Buffer->popularity : %zu Bytes\n", sizeof((Buffer *)0)->popularity);
  printf("Size of Buffer->victimized : %zu Bytes\n", sizeof((Buffer *)0)->victimized);
  printf("Size of Buffer->comp_cost  : %zu Bytes\n", sizeof((Buffer *)0)->comp_cost);
  printf("Size of Buffer->io_cost    : %zu Bytes\n", sizeof((Buffer *)0)->io_cost);
  printf("Size of Buffer->comp_hits  : %zu Bytes\n", sizeof((Buffer *)0)->comp_hits);
  printf("Size of Buffer->previous   : %zu Bytes\n", sizeof((Buffer *)0)->previous);
  printf("Size of Buffer->next       : %zu Bytes\n", sizeof((Buffer *)0)->next);
  printf("Size of Buffer->lock_id    : %zu Bytes\n", sizeof((Buffer *)0)->lock_id);
  printf("Size of Buffer->data_length: %zu Bytes\n", sizeof((Buffer *)0)->data_length);
  //printf("Size of Buffer->lock       : %zu Bytes\n", sizeof((Buffer *)0)->lock);
  //printf("Size of Buffer->cond       : %zu Bytes\n", sizeof((Buffer *)0)->cond);
  printf("Size of Buffer->data       : %zu Bytes\n", sizeof((Buffer *)0)->data);
  printf("Size of Buffer             : %zu Bytes\n", sizeof(Buffer));

  printf("\n\n%10s%10s\n", "PAGE_SIZE", "Overhead");
  float size = 0.0;
  for (int i=64; i<=65536; i*=2) {
    size = (100.0f * sizeof(Buffer)) / (sizeof(Buffer) + i);
    printf("%10i%9.3f%%\n", i, size);
  }
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
