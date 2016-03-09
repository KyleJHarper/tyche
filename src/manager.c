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
  printf("Buffer Acquisitions : %"PRIu64" (%"PRIu64" hits, %"PRIu64" misses)\n", total_acquisitions, mgr->hits, mgr->misses);
  printf("Pages in Data Set   : %"PRIu32" (%"PRIu64" bytes)\n",opts.page_count, opts.dataset_size);
  printf("Raw List Migrations : %"PRIu32" offloads, %"PRIu32" restorations\n", mgr->raw_list->offloads, mgr->raw_list->restorations);
  printf("Comp List Migrations: %"PRIu32" offloads (popped)\n", mgr->comp_list->offloads);
  printf("Hit Ratio           : %4.2f%%\n", 100.0 * mgr->hits / total_acquisitions);
  printf("Fixed Memory Ratio  : %"PRIi8"%% (%"PRIu64" bytes raw, %"PRIu64" bytes compressed)\n", opts.fixed_ratio, mgr->raw_list->max_size, mgr->comp_list->max_size);
  printf("Manager run time    : %.1f sec\n", 1.0 * mgr->run_duration / 1000);
  printf("Time sweeping       : %u sweeps, %'"PRIu64"\n", mgr->raw_list->sweeps, mgr->raw_list->sweep_cost);
  return E_OK;
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
    fprintf(stderr, "\r%"PRIu16" sec ETA.  %'"PRIu32" raw (%'"PRIu32" comp) buffers.  %'"PRIu32" restorations.  %'"PRIu32" pops.  %'"PRIu64" hits.  %'"PRIu64" misses.", opts.duration - elapsed, mgr->raw_list->count, mgr->comp_list->count, mgr->raw_list->restorations, mgr->comp_list->offloads, hits, misses);
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
    rv = list__search(mgr->raw_list, &buf, id_to_get);
    if(rv == E_OK)
      mgr->workers[id].hits++;
    if(rv == E_BUFFER_NOT_FOUND) {
      mgr->workers[id].misses++;
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
  list__destroy(mgr->comp_list);
  list__destroy(mgr->raw_list);
  free(mgr->workers);
  return E_OK;
}
