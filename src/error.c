/*
 * error.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

#include <stdio.h>
#include <stdlib.h>

void show_err(char *message, int exit_code) {
  printf("%s\nABORTING\n", message);
  if (exit_code != 0) exit(exit_code);
}
