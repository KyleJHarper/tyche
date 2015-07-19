/*
 * tests.c
 *
 *  Created on: Jul 14, 2015
 *      Author: Kyle Harper
 */

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include "list.h"
#include "buffer.h"
#include "tests.h"

extern const int E_OK;
extern const int E_BUFFER_POOFED;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_IS_VICTIMIZED;

// A global for testing cuz I'm bad
const int LIST_COUNT = 500;

void tests__synchronized_read() {
  List *raw_list = list__initialize();
  Buffer *temp, *head;
  head = raw_list->head;
  // Create LIST_COUNT buffers with random data in them.
  for (int i=0; i<LIST_COUNT; i++)
    temp = list__add(raw_list);
  temp = head;
  for (int i=1; i<raw_list->count; i++) {
    temp = temp->next;
    temp->data = "some text, hooray for me";
    temp->id = i;
  }

  // Start worker threads which will try to read data at the same time.
  int worker_count = 200;
  pthread_t workers[worker_count];
  for (int i=0; i<worker_count; i++)
    pthread_create(&workers[i], NULL, (void *) &tests__read, raw_list);

  // Start up a chaos monkey to remove some of the buffers.
  pthread_t chaos_worker;
  pthread_create(&chaos_worker, NULL, (void *) &tests__chaos, raw_list);

  // Wait for them to finish.
  for (int i=0; i<worker_count; i++)
    pthread_join(workers[i], NULL);
  pthread_join(chaos_worker, NULL);

  temp = head;
  for (int i=1; i<raw_list->count; i++) {
    temp = temp->next;
    if (temp->ref_count != 0)
      printf("Buffer number %02d has non-zero ref_count: %02d\n	", temp->id, temp->ref_count);
    printf("Buffer id %03d has ref_count of %d\n", temp->id, temp->ref_count);
  }
}
void tests__read(List *raw_list) {
  // Pick random buffers in the list and pin them for reading.  The only way to pick randomly from a list that shrinks is to search
  // over the list itself, which is kinda magoo but it suits the purpose.
  srand(time(0));
  int rv = 0;
  int id_to_get = 0;
  Buffer *selected;
  for (int i=0; i<1000; i++) {
    for(;;) {
      id_to_get = rand() % LIST_COUNT + 1;
      //printf("Read buf id %02d\n", id_to_get);
      rv = list__acquire(raw_list, &selected, id_to_get);
      if (rv == 0)
        break;
      if (rv == E_BUFFER_NOT_FOUND || rv == E_BUFFER_POOFED || rv == E_BUFFER_IS_VICTIMIZED)
        continue;
      printf("We should never hit this.\n");
    }
    usleep(rand() % 1234);  // This helps skew interlocking (letting ref_count go above 1)
    rv = buffer__lock(selected);
    if (rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED) {
      buffer__update_ref(selected, -1);
      buffer__unlock(selected);
    }
    //printf("Released buffer id %02d.  Ref count is now %02d.\n", selected->id, selected->ref_count);
  }
  pthread_exit(0);
}
void tests__chaos(List *raw_list) {
  // Remove a buffer from the list every so often until we're down to 5.  There are 45 to remove and we want it to take about half
  // a second so we'll micro-sleep for about 11,000 seconds each time.
  Buffer *temp;
  int rv, id_to_remove = 0;
  while(raw_list->count > 5) {
    printf("Want to remove: ");
    for(;;) {
      id_to_remove = rand() % LIST_COUNT + 1;
      printf(" %2d", id_to_remove);
      rv = list__acquire(raw_list, &temp, id_to_remove);
      if (rv == 0)
        break;
      if (rv == E_BUFFER_NOT_FOUND || rv == E_BUFFER_POOFED)
        continue;
      printf("We should never hit this either.\n");
    }
    printf("\n");
    rv = buffer__lock(temp);
    if (rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED){
      buffer__update_ref(temp, -1);
      buffer__unlock(temp);
    } else {
      printf("Grr %d\n", rv);
    }
    printf("Going to remove buffer id: %2d (count is: %d, list size is: %d)\n", temp->id, temp->ref_count, raw_list->count);
    list__remove(raw_list, &temp);  // There's only one thread removing buffers so no checking required.
    usleep(11000);
  }
  printf("Removed all buffers.  Count is now %d\n", raw_list->count);
  pthread_exit(0);
}


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

