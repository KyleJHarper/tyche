/*
 * playground.c
 *
 *  Created on: Mar 15, 2016
 *      Author: administrator
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "globals.h"




int main() {
  uint32_t bob = 0;
  for(int i=0; i<32; i++) {
    bob = 1 << i;
    printf("bob is %u\n", bob);
  }
  return 0;
}

