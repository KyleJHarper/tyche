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

  printf("Size of Buffer->id           : %zu Bytes\n", sizeof((Buffer *)0)->id);
  printf("Size of Buffer->ref_count    : %zu Bytes\n", sizeof((Buffer *)0)->ref_count);
  printf("Size of Buffer->popularity   : %zu Bytes\n", sizeof((Buffer *)0)->popularity);
  printf("Size of Buffer->victimized   : %zu Bytes\n", sizeof((Buffer *)0)->victimized);
  printf("Size of Buffer->lock_id      : %zu Bytes\n", sizeof((Buffer *)0)->lock_id);
  printf("Size of Buffer->removal_index: %zu Bytes\n", sizeof((Buffer *)0)->removal_index);
  printf("Size of Buffer->comp_cost    : %zu Bytes\n", sizeof((Buffer *)0)->comp_cost);
  printf("Size of Buffer->io_cost      : %zu Bytes\n", sizeof((Buffer *)0)->io_cost);
  printf("Size of Buffer->comp_hits    : %zu Bytes\n", sizeof((Buffer *)0)->comp_hits);
  printf("Size of Buffer->data_length  : %zu Bytes\n", sizeof((Buffer *)0)->data_length);
  printf("Size of Buffer->data         : %zu Bytes\n", sizeof((Buffer *)0)->data);
  printf("Size of Buffer               : %zu Bytes\n", sizeof(Buffer));

  printf("\n\n%10s%10s\n", "PAGE_SIZE", "Overhead");
  float size = 0.0;
  for (int i=64; i<=65536; i*=2) {
    size = (100.0f * sizeof(Buffer)) / (sizeof(Buffer) + i);
    printf("%10i%9.3f%%\n", i, size);
  }

  return 0;
}
