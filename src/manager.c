/*
 * manager.c
 *
 *  Created on: Dec 8, 2015
 *      Author: Kyle Harper
 * Description: Wheeeee
 */

#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <pthread.h>
#include "options.h"
#include "list.h"
#include "error.h"
#include "manager.h"


/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L
#define MILLION    1000000L

/* Specify the default raw ratio to start with. */
#define INITIAL_RAW_RATIO   80    // 80%

/* Extern the global options. */
extern Options opts;

/* Extern the error codes we need. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_ALREADY_EXISTS;

/* Globals to protect worker IDs. */
#define MAX_WORKER_ID UINT32_MAX
workerid_t next_worker_id = 0;
pthread_mutex_t next_worker_id_mutex = PTHREAD_MUTEX_INITIALIZER;



/* manager__initialize
 * Builds a manager, plain and simple.
 */
Manager* manager__initialize(managerid_t id, char **pages) {
  /* Malloc the new manager and check. */
  Manager *mgr = (Manager *)malloc(sizeof(Manager));
  if(mgr == NULL)
    show_error(E_GENERIC, "Unable to create the manager with ID "PRIu8".  Couldn't malloc.", id);

  /* Set the manager basics */
  mgr->id = id;
  mgr->runnable = 1;
  mgr->run_duration = 0;
  mgr->pages = pages;
  if (pthread_mutex_init(&mgr->lock, NULL) != 0)
    show_error(E_GENERIC, "Failed to initialize mutex for a manager.  This is fatal.");

  /* Allocate the array of pointers for our **workers element so it's the right size. */
  mgr->workers = calloc(opts.workers, sizeof(Worker *));
  if(mgr->workers == NULL)
    show_error(E_GENERIC, "Failed to calloc the memory for the workers pool for a manager.");

  /* Create the listset for this manager to use. */
  List *raw_list = list__initialize();
  if (raw_list == NULL)
    show_error(E_GENERIC, "Couldn't create the raw list for manager "PRIu8".  This is fatal.", id);
  List *comp_list = list__initialize();
  if (comp_list == NULL)
    show_error(E_GENERIC, "Couldn't create the compressed list for manager "PRIu8".  This is fatal.", id);

  /* Add the lists to the Manager and then set their offload/restore values. */
  mgr->raw_list = raw_list;
  mgr->comp_list = comp_list;
  raw_list->offload_to = comp_list;
  comp_list->restore_to = raw_list;

  /* Allocate the array of pointers for our **workers element so it's the right size. */
  mgr->workers = calloc(opts.workers, sizeof(Worker *));
  if(mgr->workers == NULL)
    show_error(E_GENERIC, "Failed to calloc the memory for the workers pool for a manager.");

  /* Synchronize mutexes and conditions to avoid t1 getting the 'raw' lock and t2 getting the 'compressed' lock and deadlocking. */
  comp_list->lock = raw_list->lock;
  comp_list->reader_condition = raw_list->reader_condition;
  comp_list->writer_condition = raw_list->writer_condition;

  /* Set the memory sizes for both lists. */
  list__balance(raw_list, opts.fixed_ratio > 0 ? opts.fixed_ratio : INITIAL_RAW_RATIO);

  /* Return our manager. */
  return mgr;
}


/* manager__start
 * Takes a previous created manager object and attempts to start the main tyche logic with it.
 */
int manager__start(Manager *mgr) {
  /* Start another thread to change our runnable flag when the timer is up. */
  pthread_t pt_timer;
  pthread_create(&pt_timer, NULL, (void *) &manager__timer, mgr);
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  /* Start all of the workers and then wait for them to finish. */
  pthread_t workers[opts.workers];
  for(int i=0; i<opts.workers; i++)
    pthread_create(&workers[i], NULL, (void *) &manager__spawn_worker, mgr);
  pthread_join(pt_timer, NULL);
  for(int i=0; i<opts.workers; i++)
    pthread_join(workers[i], NULL);

  /* Show results and leave.  We */
  uint64_t total_acquisitions = mgr->hits + mgr->misses;
  clock_gettime(CLOCK_MONOTONIC, &end);
  mgr->run_duration = (BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec) / MILLION;
  printf("Buffer Acquisitions: %"PRIu64" (%"PRIu64" hits, %"PRIu64" misses)\n", total_acquisitions, mgr->hits, mgr->misses);
  printf("Pages in Data Set  : %"PRIu32" (%"PRIu64" bytes)\n",opts.page_count, opts.dataset_size);
  printf("Hit Ratio          : %4.2f%%\n", 100.0 * mgr->hits / total_acquisitions);
  printf("Fixed Memory Ratio : %"PRIu8"%% (%"PRIu64" bytes raw, %"PRIu64" bytes compressed)\n", opts.fixed_ratio, mgr->raw_list->max_size, mgr->comp_list->max_size);
  printf("Manager run time   : %"PRIu32"\n", mgr->run_duration);
  return E_OK;
}


/* manager__timer
 * A function to start counting down a timer to help signal when workers should end.
 */
void manager__timer(Manager *mgr) {
  /* Create time structures so we can break out after the right duration. */
  const uint RECHECK_RESOLUTION = 100000;
  struct timespec start, current;
  clock_gettime(CLOCK_MONOTONIC, &start);
  clock_gettime(CLOCK_MONOTONIC, &current);
  while(opts.duration > (uint16_t)(current.tv_sec - start.tv_sec)) {
    usleep(RECHECK_RESOLUTION);
    clock_gettime(CLOCK_MONOTONIC, &current);
  }

  /* Flag the manager is no longer runnable, which will stop all workers. */
  mgr->runnable = 0;
  pthread_exit(0);
}


/* manager__spawn_worker
 * The initialization point for a worker to start doing real work for a manager.
 */
void manager__spawn_worker(Manager *mgr) {
  /* Set up our worker. */
  Worker peon;
  peon.id = MAX_WORKER_ID;
  peon.hits = 0;
  peon.misses = 0;
  manager__assign_worker_id(&peon.id);
  if(peon.id == MAX_WORKER_ID)
    show_error(E_GENERIC, "Unable to get a worker ID.  This should never happen.");
  mgr->workers[peon.id] = &peon;

  /* While srand should affect all threads per POSIX, call it per-thread anyway since each thread uses list__* functions.  For skiplist. */
  srand((uint)(time(NULL)));

  /* Begin the main loop for grabbing buffers. */
  Buffer *buf = NULL;
  bufferid_t id_to_get = 0;
  int rv = 0;
  while(mgr->runnable != 0) {
    /* Go find buffers to play with!  If the one we need doesn't exist, get it and add it. */
    id_to_get = rand() % opts.page_count;
    rv = list__search(mgr->raw_list, &buf, id_to_get);
    if(rv == E_OK)
      peon.hits++;
    if(rv == E_BUFFER_NOT_FOUND) {
      peon.misses++;
      buf = buffer__initialize(id_to_get, mgr->pages[id_to_get]);
      buffer__update_ref(buf, 1);
      rv = list__add(mgr->raw_list, buf);
      if (rv == E_BUFFER_ALREADY_EXISTS) {
        // Someone beat us to it.  Just free it and loop around for something else.
        free(buf);
        buf = NULL;
        continue;
      }
    }
    /* Now we should have a valid buffer.  Hooray.  Mission accomplished. */
    buffer__lock(buf);
    buffer__update_ref(buf, -1);
    buffer__unlock(buf);
    buf = NULL;
  }

  // We ran out of time.  Let's update the manager with our statistics.
  pthread_mutex_lock(&mgr->lock);
  mgr->hits += peon.hits;
  mgr->misses += peon.misses;
  pthread_mutex_unlock(&mgr->lock);

  // All done.
  pthread_exit(0);
}


/* manager__assign_worker_id
 * Simply increments the global worker ID variable under the protection of a mutex.
 * First id is 0-based to work cleanly with **workers array notation: &(workers + workerid)->bla
 */
void manager__assign_worker_id(workerid_t *referring_id_ptr) {
  pthread_mutex_lock(&next_worker_id_mutex);
  *referring_id_ptr = next_worker_id;
  next_worker_id++;
  if (next_worker_id == MAX_WORKER_ID)
    next_worker_id = 0;
  pthread_mutex_unlock(&next_worker_id_mutex);
  return;
}
