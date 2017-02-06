/*
 * playground.c
 *
 *  Created on: Mar 15, 2016
 *      Author: administrator
 */

#include <stdio.h>
#include <stdlib.h>
#include "globals.h"


extern const int LZ4_COMPRESSOR_ID;

int main() {
  printf("%d\n", LZ4_COMPRESSOR_ID);
  printf("Not used.  It's a toy script.\n");
  return 0;
}

