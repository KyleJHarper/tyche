/*
 * tests.c
 *
 *  Created on: Jul 14, 2015
 *      Author: Kyle Harper
 */

#include "list.h"
#include "buffer.h"


void tests__elem() {
  List *raw_list = list__initialize();
  List *comp_list = list__initialize();
  Buffer *elem1 = list__add(raw_list);
  Buffer *elem2 = list__add(raw_list);
  Buffer *elem3 = list__add(raw_list);
  Buffer *rawr = list__add(comp_list);
  Buffer *rawr2 = list__add(comp_list);

  printf("Number of elements: %d\n", list__count(raw_list));
  printf("Number of elements: %d\n", list__count(comp_list));
  printf("Self: %p   Prev: %p   Next: %p\n", comp_list->head, comp_list->head->previous, comp_list->head->next);

  buffer__victimize(elem1);
  buffer__victimize(elem3);
  buffer__victimize(rawr);

  printf("Number of elements: %d\n", list__count(raw_list));
  printf("Number of elements: %d\n", list__count(comp_list));
  printf("Self: %p   Prev: %p   Next: %p\n", comp_list->head, comp_list->head->previous, comp_list->head->next);
}

