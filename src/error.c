/*
 * error.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

#include <stdio.h>

void show_err(char *message, int exit_code) {
  printf("%s\n", message);
  if (exit_code != 0) exit(exit_code);
}
