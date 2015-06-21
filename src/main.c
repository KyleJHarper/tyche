/*
 * main.c
 *
 *  Created on: Jun 18, 2015
 *      Author: Kyle Harper
 */

/* Headers */
#include <stdio.h>
#include "buffer_lists.h"

int main(int argc, char *argv[]) {
  printf("%s", "This is the main.  Here are the args.\n");
  for (int i; i<argc; i++)
    printf("%s%d%s%s%s", "Arg ", i, " is: ", argv[i], "\n");
  return 0;

  /* Initialize lists here. */
  if (! list__initialize(raw_head))
    return 1;
}
