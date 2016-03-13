/*
 * manager.c
 *
 *  Created on: Dec 8, 2015
 *      Author: Kyle Harper
 * Description: Wheeeee
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <pthread.h>
#include <locale.h>
#include <string.h>
#include "options.h"
#include "list.h"
#include "error.h"
#include "manager.h"
#include "tests.h"


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
  mgr->workers = calloc(opts.workers, sizeof(Worker));
  if(mgr->workers == NULL)
    show_error(E_GENERIC, "Failed to calloc the memory for the workers pool for a manager.");
  mgr->hits = 0;
  mgr->misses = 0;

  /* Create the listset for this manager to use. */
  List *list = list__initialize();
  if (list == NULL)
    show_error(E_GENERIC, "Couldn't create the list for manager "PRIu8".  This is fatal.", id);
  mgr->list = list;

  /* Set the memory sizes for both lists. */
  list__balance(list, opts.fixed_ratio > 0 ? opts.fixed_ratio : INITIAL_RAW_RATIO);

  /* Return our manager. */
  return mgr;
}


/* manager__start
 * Takes a previous created manager object and attempts to start the main tyche logic with it.
 */
int manager__start(Manager *mgr) {
  /* Whether we're doing tests or not, we need the sweeper running for this manager. */
  pthread_t pt_sweeper;
  pthread_create(&pt_sweeper, NULL, (void *) &manager__sweeper, mgr);

  /* If a test was specified, run it instead of the manager(s) and then leave. */
  if (opts.test != NULL) {
    tests__run_test(mgr->list, mgr->pages);
    fprintf(stderr, "A test (-t %s) was specified so we ran it.  All done.  Quitting non-zero for safety.\n", opts.test);
    /* Stop the sweeper.  It requires being woken up. */
    mgr->runnable = 0;
    pthread_mutex_lock(&mgr->list->lock);
    pthread_cond_broadcast(&mgr->list->sweeper_condition);
    pthread_mutex_unlock(&mgr->list->lock);
    pthread_join(pt_sweeper, NULL);
    exit(E_GENERIC);
  }

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

  /* Stop the sweeper.  It requires being woken up. */
  pthread_mutex_lock(&mgr->list->lock);
  pthread_cond_broadcast(&mgr->list->sweeper_condition);
  pthread_mutex_unlock(&mgr->list->lock);
  pthread_join(pt_sweeper, NULL);

  /* Show results and leave. */
  uint64_t total_acquisitions = mgr->hits + mgr->misses;
  clock_gettime(CLOCK_MONOTONIC, &end);
  mgr->run_duration = (BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec) / MILLION;
  printf("Buffer Acquisitions : %"PRIu64" (%"PRIu64" hits, %"PRIu64" misses)\n", total_acquisitions, mgr->hits, mgr->misses);
  printf("Pages in Data Set   : %"PRIu32" (%"PRIu64" bytes)\n",opts.page_count, opts.dataset_size);
  printf("Compressions        : %"PRIu64" compressions\n", mgr->list->compressions);
  printf("Restorations        : %"PRIu64" restorations\n", mgr->list->restorations);
  printf("Hit Ratio           : %4.2f%%\n", 100.0 * mgr->hits / total_acquisitions);
  printf("Fixed Memory Ratio  : %"PRIi8"%% (%"PRIu64" bytes raw, %"PRIu64" bytes compressed)\n", opts.fixed_ratio, mgr->list->max_raw_size, mgr->list->max_comp_size);
  printf("Manager run time    : %.1f sec\n", 1.0 * mgr->run_duration / 1000);
  printf("Time sweeping       : %"PRIu64" sweeps, %'"PRIu64"\n", mgr->list->sweeps, mgr->list->sweep_cost);
  return E_OK;
}


/* manager__sweeper
 * A dedicated thread for sweeping the list when it's time.  If this fails, bye bye RAM.
 */
void manager__sweeper(Manager *mgr) {
  while(1) {
    pthread_mutex_lock(&mgr->list->lock);
    while(mgr->list->current_raw_size < mgr->list->max_raw_size) {
printf("Raw size is less than max.  Waking up reader and then waiting.\n");
      pthread_cond_broadcast(&mgr->list->reader_condition);
      pthread_cond_wait(&mgr->list->sweeper_condition, &mgr->list->lock);
printf("Sweeper was woken up.\n");
    }
    pthread_mutex_unlock(&mgr->list->lock);
    if(mgr->runnable == 0)
      break;
printf("Going to sweep.\n");
    list__sweep(mgr->list, mgr->list->sweep_goal);
printf("Done sweeping.  Looping around.\n");
  }

  return;
}


/* manager__timer
 * A function to start counting down a timer to help signal when workers should end.
 */
void manager__timer(Manager *mgr) {
  /* Create time structures so we can break out after the right duration. */
  const uint RECHECK_RESOLUTION = 250000;
  uint16_t elapsed = 0;
  uint64_t hits = 0, misses = 0;
  struct timespec start, current;
  clock_gettime(CLOCK_MONOTONIC, &start);
  clock_gettime(CLOCK_MONOTONIC, &current);
  setlocale(LC_NUMERIC, "");
  while(opts.duration > elapsed) {
    usleep(RECHECK_RESOLUTION);
    elapsed = (uint16_t)(current.tv_sec - start.tv_sec);
    hits = 0;
    misses = 0;
    for(workerid_t i = 0; i<opts.workers; i++) {
      misses += mgr->workers[i].misses;
      hits += mgr->workers[i].hits;
    }
    clock_gettime(CLOCK_MONOTONIC, &current);
    if(opts.quiet == 1)
      continue;
    fprintf(stderr, "\r%-90s", "");
    fprintf(stderr, "\r%"PRIu16" sec ETA.  %'"PRIu32" raw (%'"PRIu32" comp) buffers.  %'"PRIu64" restorations.  %'"PRIu64" compressions.  %'"PRIu64" hits.  %'"PRIu64" misses.", opts.duration - elapsed, mgr->list->raw_count, mgr->list->comp_count, mgr->list->restorations, mgr->list->compressions, hits, misses);
    fflush(stderr);
  }
  if(opts.quiet == 0)
    fprintf(stderr, "\n");

  /* Flag the manager is no longer runnable, which will stop all workers. */
  mgr->runnable = 0;
  pthread_exit(0);
}


/* manager__spawn_worker
 * The initialization point for a worker to start doing real work for a manager.
 */
void manager__spawn_worker(Manager *mgr) {
  /* Set up our worker. */
  workerid_t id = MAX_WORKER_ID;
  manager__assign_worker_id(&id);
  if(id == MAX_WORKER_ID)
    show_error(E_GENERIC, "Unable to get a worker ID.  This should never happen.");
  mgr->workers[id].id = id;
  mgr->workers[id].hits = 0;
  mgr->workers[id].misses = 0;

  /* While srand should affect all threads per POSIX, call it per-thread anyway since each thread uses list__* functions.  For skiplist. */
  srand((uint)(time(NULL)));

  /* Begin the main loop for grabbing buffers. */
  Buffer *buf = NULL;
  bufferid_t id_to_get = 0;
  int rv = 0;
  while(mgr->runnable != 0) {
    /* Go find buffers to play with!  If the one we need doesn't exist, get it and add it. */
    id_to_get = rand() % opts.page_count;
    rv = list__search(mgr->list, &buf, id_to_get);
    if(rv == E_OK)
      mgr->workers[id].hits++;
    if(rv == E_BUFFER_NOT_FOUND) {
      mgr->workers[id].misses++;
      buf = buffer__initialize(id_to_get, mgr->pages[id_to_get]);
      buffer__update_ref(buf, 1);
      rv = list__add(mgr->list, buf);
      if (rv == E_BUFFER_ALREADY_EXISTS) {
        // Someone beat us to it.  Just free it and loop around for something else.
        buffer__destroy(buf);
        buf = NULL;
        continue;
      }
    }
    /* Now we should have a valid buffer.  Hooray.  Mission accomplished. */
    buffer__lock(buf);
    buffer__update_ref(buf, -1);
    buffer__unlock(buf);
    if(buf->is_ephemeral)
      buffer__destroy(buf);
    buf = NULL;
  }

  // We ran out of time.  Let's update the manager with our statistics before we quit.
  pthread_mutex_lock(&mgr->lock);
  mgr->hits += mgr->workers[id].hits;
  mgr->misses += mgr->workers[id].misses;
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


/* manager__destroy
 * Deconstruct the members of a Manager object.
 */
int manager__destroy(Manager *mgr) {
  // We have to wake up the sweeper so it knows to shut down.
  list__destroy(mgr->list);
  free(mgr->workers);
  return E_OK;
}
