/*
 * main.c
 *
 *  Created on: Jun 18, 2015
 *      Author: Kyle Harper
 * Description: This is a simple program designed to test Harper's Cache Replacement Strategy (HCRS).  This is an implementation
 *              of HCRS, not a library or API to be copied or used wholesale.  The basics of this program include maintaining
 *              circular lists of raw and compressed buffers.  Read up on HCRS for more details on the theory.
 *
 *              Important Note!
 *              The design, organization, and peak optimization of every nuance of this program isn't the target.  Obviously I
 *              don't want massive performance problems (e.g.: lock contentions).  The point of tyche is to build a reasonably
 *              performant benchmarking tool to identify trends in an HCRS implementation.
 */

/* Headers */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "error_codes.h"
#include "error.h"
#include "list.h"
#include "lock.h"

/* Globals and externs because I'm bad */
extern Buffer *raw_list;
extern Buffer *comp_list;

int main(int argc, char *argv[]) {
  /* Initialize locker and lists here. */
  lock__initialize();
  list__initialize();
  Buffer *elem1 = list__add(raw_list);
  Buffer *elem2 = list__add(raw_list);
  Buffer *elem3 = list__add(raw_list);
  Buffer *rawr = list__add(comp_list);
  Buffer *rawr2 = list__add(comp_list);

  printf("Number of elements: %d\n", list__count(raw_list));
  printf("Number of elements: %d\n", list__count(comp_list));
  printf("Self: %p   Prev: %p   Next: %p\n", comp_list, comp_list->previous, comp_list->next);
  return 0;
}
