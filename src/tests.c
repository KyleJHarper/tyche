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

  printf("Number of raw  elements: %d and %d\n", list__count(raw_list), raw_list->count);
  printf("Number of comp elements: %d and %d\n", list__count(comp_list), comp_list->count);
  printf("Raw  Self: %p   Prev: %p   Next: %p\n", raw_list->head, raw_list->head->previous, raw_list->head->next);
  printf("Comp Self: %p   Prev: %p   Next: %p\n", comp_list->head, comp_list->head->previous, comp_list->head->next);

  list__remove(raw_list, &elem1);
  list__remove(raw_list, &elem3);
  list__remove(comp_list, &rawr);

  printf("Number of raw  elements: %d and %d\n", list__count(raw_list), raw_list->count);
  printf("Number of comp elements: %d and %d\n", list__count(comp_list), comp_list->count);
  printf("Raw  Self: %p   Prev: %p   Next: %p\n", raw_list->head, raw_list->head->previous, raw_list->head->next);
  printf("Comp Self: %p   Prev: %p   Next: %p\n", comp_list->head, comp_list->head->previous, comp_list->head->next);
}

