/*
 * playground.c
 *
 *  Created on: Mar 15, 2016
 *      Author: administrator
 */

#include <stdio.h>
#include <stdlib.h>
#include "globals.h"



int main() {
  float value = 0.0;
  for(int i = 1; i<=1048576; i *= 2) {
    value += (1.0f / i);
  }
  printf("Value is %9.6f\n", value);
  return 0;
}

