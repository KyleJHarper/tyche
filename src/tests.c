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
const int LIST_COUNT       =   800;
const int WORKER_COUNT     =   0;
const int CHAOS_MONKIES    =     2;
const int READS_PER_WORKER =  10000;
const int LIST_FLOOR       =   700;
const int SLEEP_DELAY      =    123;


void tests__synchronized_read() {
  List *raw_list = list__initialize();
  Buffer *temp;
  // Create LIST_COUNT buffers with some data in them.
  for (bufferid_t i=1; i<=LIST_COUNT; i++) {
    temp = buffer__initialize(i);
    temp->data = "some text, hooray for me";
    list__add(raw_list, temp);
  }

  // Start worker threads which will try to read data at the same time.
  printf("Starting readers\n");
  pthread_t workers[WORKER_COUNT];
  for (int i=0; i<WORKER_COUNT; i++)
    pthread_create(&workers[i], NULL, (void *) &tests__read, raw_list);

  // Start up a chaos monkies for insanity.
  pthread_t chaos_monkies[CHAOS_MONKIES];
  for (int i=0; i<CHAOS_MONKIES; i++)
    pthread_create(&chaos_monkies[i], NULL, (void *) &tests__chaos, raw_list);

  // Wait for them to finish.
  for (int i=0; i<WORKER_COUNT; i++)
    pthread_join(workers[i], NULL);
  for (int i=0; i<CHAOS_MONKIES; i++)
    pthread_join(chaos_monkies[i], NULL);
printf("All done.\n");
  for (int i=0; i<raw_list->count; i++) {
    if (temp->ref_count != 0)
      printf("Buffer number %d has non-zero ref_count: %d\n", temp->id, temp->ref_count);
  }
}
void tests__read(List *raw_list) {
  // Pick random buffers in the list and pin them for reading.  The only way to pick randomly from a list that shrinks is to search
  // over the list itself, which is kinda magoo but it suits the purpose.
  srand(time(0));
  int rv = 0;
  bufferid_t id_to_get = 0;
  Buffer *selected;
  for (int i=0; i<READS_PER_WORKER; i++) {
    for(;;) {
      id_to_get = rand() % raw_list->count;
      rv = list__search(raw_list, &selected, id_to_get);
      if (rv == 0)
        break;
      if (rv == E_BUFFER_NOT_FOUND || rv == E_BUFFER_POOFED || E_BUFFER_IS_VICTIMIZED)
        continue;
      printf("We should never hit this (rv is %d).\n", rv);
    }
    usleep(rand() % SLEEP_DELAY);  // This just emulates some random time the reader will use this buffer.
    rv = buffer__lock(selected);
    if (rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED) {
      buffer__update_ref(selected, -1);
      buffer__unlock(selected);
    }
  }
  printf("I'm a reader and I'm O-K!\n");
  pthread_exit(0);
}
void tests__chaos(List *raw_list) {
  // Remove a buffer from the list every so often until we're down to LIST_FLOOR, just because.
  Buffer *temp;
  int rv = 0;
  bufferid_t id_to_remove = 0;
  while(raw_list->count > LIST_FLOOR) {
    for(;;) {
      //id_to_remove = rand() % LIST_COUNT;
      //if (rand() % 2 == 1)
        id_to_remove = 777;
      rv = list__search(raw_list, &temp, id_to_remove);
      if (rv == 0)
        break;
      if (rv == E_BUFFER_NOT_FOUND || rv == E_BUFFER_POOFED)
        continue;
      printf("We should never hit this either.\n");
    }
    // List search gave us a ref_count, need to decrement ourself.
    buffer__lock(temp);
    buffer__update_ref(temp, -1);
    buffer__unlock(temp);
    printf("Going to remove buffer id: %d (count is: %d, list size is: %d)\n", temp->id, temp->ref_count, raw_list->count);
    list__remove(raw_list, &temp);
    printf("Did it.\n");
    usleep(1000);
  }
  printf("Removed all buffers.  Count is now %d\n", raw_list->count);
  pthread_exit(0);
}


void tests__elem() {
  List *raw_list = list__initialize();
  List *comp_list = list__initialize();
  Buffer *elem1 = buffer__initialize(1);  list__add(raw_list, elem1);
  Buffer *elem2 = buffer__initialize(2);  list__add(raw_list, elem2);
  Buffer *elem3 = buffer__initialize(3);  list__add(raw_list, elem3);
  Buffer *rawr1 = buffer__initialize(10);  list__add(comp_list, rawr1);
  Buffer *rawr2 = buffer__initialize(11);  list__add(comp_list, rawr2);

  printf("Number of raw  elements: %d\n", raw_list->count);
  printf("Number of comp elements: %d\n", comp_list->count);

  list__remove(raw_list, &elem1);
  list__remove(raw_list, &elem3);
  list__remove(comp_list, &rawr1);

  printf("Number of raw  elements: %d\n", raw_list->count);
  printf("Number of comp elements: %d\n", comp_list->count);
}

