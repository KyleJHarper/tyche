/*
 * playground.c
 *
 *  Created on: Mar 15, 2016
 *      Author: administrator
 */

#include <pthread.h>
#include <stdio.h>
#include <time.h>     /* for clock_gettime() */
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include "globals.h"


pthread_mutex_t mutex;
uint32_t goal = UINT32_MAX - 100;  // Save padding for the race condition in the predicate checks (the while(...) loops)

void run_atomic(uint32_t *bob) {
  while(*bob < goal)
    __sync_fetch_and_add(bob, 1);
}

void run_mutex(uint32_t *bob) {
  while(*bob < goal) {
    pthread_mutex_lock(&mutex);
    (*bob)++;
    pthread_mutex_unlock(&mutex);
  }
}

int main() {
  uint32_t bob = 0;
  struct timespec start, end;
  float delta = 0.0;
  int thread_count = 4;
  pthread_t threads[thread_count];
  pthread_mutex_init(&mutex, NULL);

  pthread_mutex_lock(&mutex);
  pthread_mutex_lock(&mutex);
  printf("This shouldn't happen\n");
  // No protection (single thread)
  clock_gettime(CLOCK_MONOTONIC, &start);
  bob = 0;
  while(bob < goal)
    bob++;
  clock_gettime(CLOCK_MONOTONIC, &end);
  delta = 1.0 * (end.tv_sec - start.tv_sec) + (1.0 * (end.tv_nsec - start.tv_nsec) / 1000000000);
  printf("%-12s: %6.3f %u\n", "Unprotected", delta, bob);

  // Atomic built-in (single thread)
  clock_gettime(CLOCK_MONOTONIC, &start);
  bob = 0;
  run_atomic(&bob);
  clock_gettime(CLOCK_MONOTONIC, &end);
  delta = 1.0 * (end.tv_sec - start.tv_sec) + (1.0 * (end.tv_nsec - start.tv_nsec) / 1000000000);
  printf("%-12s: %6.3f %u\n", "Atomic", delta, bob);

  // Mutex (single thread)
  clock_gettime(CLOCK_MONOTONIC, &start);
  bob = 0;
  run_mutex(&bob);
  clock_gettime(CLOCK_MONOTONIC, &end);
  delta = 1.0 * (end.tv_sec - start.tv_sec) + (1.0 * (end.tv_nsec - start.tv_nsec) / 1000000000);
  printf("%-12s: %6.3f %u\n", "Mutex", delta, bob);

  /* Multithread Tests */
  // Atomic built-in (multi-thread)
  clock_gettime(CLOCK_MONOTONIC, &start);
  bob = 0;
  for(int i = 0; i < thread_count; i++)
    pthread_create(&threads[i], NULL, (void *) &run_atomic, &bob);
  for(int i = 0; i < thread_count; i++)
    pthread_join(threads[i], NULL);
  clock_gettime(CLOCK_MONOTONIC, &end);
  delta = 1.0 * (end.tv_sec - start.tv_sec) + (1.0 * (end.tv_nsec - start.tv_nsec) / 1000000000);
  printf("%-12s: %6.3f %u\n", "Atomic MT", delta, bob);

  // Mutex built-in (multi-thread)
  clock_gettime(CLOCK_MONOTONIC, &start);
  bob = 0;
  for(int i = 0; i < thread_count; i++)
    pthread_create(&threads[i], NULL, (void *) &run_mutex, &bob);
  for(int i = 0; i < thread_count; i++)
    pthread_join(threads[i], NULL);
  clock_gettime(CLOCK_MONOTONIC, &end);
  delta = 1.0 * (end.tv_sec - start.tv_sec) + (1.0 * (end.tv_nsec - start.tv_nsec) / 1000000000);
  printf("%-12s: %6.3f %u\n", "Mutex MT", delta, bob);

  return 0;
}

