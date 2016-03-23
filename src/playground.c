/*
 * playground.c
 *
 *  Created on: Mar 15, 2016
 *      Author: administrator
 */

#include <stdio.h>
#include <pthread.h>

int main() {
  pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&lock);
  printf("Lock 1\n");
  pthread_mutex_lock(&lock);
  printf("Lock 2\n");

  printf("All done.\n");
  return 0;
}
