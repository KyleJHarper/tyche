/*
 * playground.c
 *
 *  Created on: Mar 15, 2016
 *      Author: administrator
 */

#include <stdio.h>
#include <stdlib.h>
#include "list.h"
#include "globals.h"

extern const int LZ4_COMPRESSOR_ID;

int main() {
  List *list = NULL;
  int rv = 0;
  rv = list__initialize(&list, 1, LZ4_COMPRESSOR_ID, 1, 10000000);
  printf("%p and %i\n", &list, rv);
  return 0;
}
