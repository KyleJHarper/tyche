/*
 * manager.c
 *
 *  Created on: Dec 8, 2015
 *      Author: Kyle Harper
 * Description: See manager.h
 */

#include <pthread.h>
#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <locale.h>
#include <string.h>
#include "options.h"
#include "list.h"
#include "error.h"
#include "manager.h"
#include "tests.h"


/* We need to know what one billion is for clock timing and others are for pretty output. */
#define TRILLION 1000000000000L
#define BILLION     1000000000L
#define MILLION        1000000L
#define THOUSAND          1000L

/* Specify the default raw ratio to start with. */
#define INITIAL_RAW_RATIO   80    // 80%

/* Extern the global options. */
extern Options opts;

/* Extern the error codes we need. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_ALREADY_EXISTS;

extern const int DESTROY_DATA;

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
  List *list = NULL;
  int list_rv = E_OK;
  list_rv = list__initialize(&list, opts.cpu_count, opts.compressor_id, opts.compressor_level, opts.max_memory);
  if (list_rv != E_OK)
    show_error(E_GENERIC, "Couldn't create the list for manager "PRIu8".  This is fatal.", id);
  mgr->list = list;

  /* Set the memory sizes for both lists. */
  list__balance(list, opts.fixed_ratio > 0 ? opts.fixed_ratio : INITIAL_RAW_RATIO, opts.max_memory);

  /* Return our manager. */
  return mgr;
}


/* manager__start
 * Takes a previous created manager object and attempts to start the main tyche logic with it.
 */
int manager__start(Manager *mgr) {
  /* If a test was specified, run it instead of the manager(s) and then leave. */
  if (opts.test != NULL) {
    tests__run_test(mgr->list, mgr->pages);
    fprintf(stderr, "A test (-t %s) was specified so we ran it.  All done.  Quitting non-zero for safety.\n", opts.test);
    /* Stop the sweeper.  It requires being woken up. */
    mgr->runnable = 0;
    list__show_structure(mgr->list);
    list__destroy(mgr->list);
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
  for(int i=0; i<opts.workers; i++) {
    pthread_join(workers[i], NULL);
  }

  /* Show results and leave. */
  uint64_t total_acquisitions = mgr->hits + mgr->misses;
  clock_gettime(CLOCK_MONOTONIC, &end);
  mgr->run_duration = (BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec) / MILLION;
  printf("Tyche Results\n");
  printf("=============\n");
  printf("Buffer Acquisitions : %'"PRIu64" (%'.f per sec).  %'"PRIu64" hits.  %'"PRIu64" misses.\n", total_acquisitions, total_acquisitions / (1.0 * mgr->run_duration / 1000), mgr->hits, mgr->misses);
  printf("Pages in Data Set   : %'"PRIu32" (%'"PRIu64" bytes)\n",opts.page_count, opts.dataset_size);
  printf("Compressions        : %'"PRIu64" compressions (%'.f per sec)\n", mgr->list->compressions, mgr->list->compressions / (1.0 * mgr->run_duration / 1000));
  printf("Restorations        : %'"PRIu64" restorations (%'.f per sec)\n", mgr->list->restorations, mgr->list->restorations / (1.0 * mgr->run_duration / 1000));
  printf("Hit Ratio           : %5.2f%%\n", 100.0 * mgr->hits / total_acquisitions);
  printf("Fixed Memory Ratio  : %"PRIi8"%% (%'"PRIu64" bytes raw, %'"PRIu64" bytes compressed)\n", opts.fixed_ratio, mgr->list->max_raw_size, mgr->list->max_comp_size);
  printf("Manager run time    : %.1f sec\n", 1.0 * mgr->run_duration / 1000);
  printf("Time sweeping       : %'"PRIu64" sweeps (%'"PRIu64" ns)\n", mgr->list->sweeps, mgr->list->sweep_cost);
  printf("Threads & Workers   : %"PRIu16" CPUs.  %"PRIu16" Workers.\n", opts.cpu_count, opts.workers);
  if(opts.verbosity > 0)
    list__show_structure(mgr->list);
  if(opts.verbosity > 1)
    list__dump_structure(mgr->list);
  return E_OK;
}


/* manager__timer
 * A function to start counting down a timer to help signal when workers should end.
 */
void manager__timer(Manager *mgr) {
  /* Create time structures so we can break out after the right duration. */
  uint RECHECK_RESOLUTION = 250000;
  switch(opts.verbosity) {
    case 0:
      RECHECK_RESOLUTION = 250000;
      break;
    case 1:
      RECHECK_RESOLUTION = 100000;
      break;
    case 2:
      RECHECK_RESOLUTION =  10000;
      break;
  }
  uint16_t elapsed = 0;
  uint64_t total_hits = 0, total_misses = 0;
  double hits = 0.0, misses = 0.0, compressions = 0.0, restorations = 0.0;
  char hits_unit = '\0', misses_unit = '\0', compressions_unit = '\0', restorations_unit = '\0';
  struct timespec start, current;
  clock_gettime(CLOCK_MONOTONIC, &start);
  clock_gettime(CLOCK_MONOTONIC, &current);
  setlocale(LC_NUMERIC, "");
  while(opts.duration > elapsed) {
    usleep(RECHECK_RESOLUTION);
    elapsed = (uint16_t)(current.tv_sec - start.tv_sec);
    total_hits = 0;
    total_misses = 0;
    for(workerid_t i = 0; i<opts.workers; i++) {
      total_hits += mgr->workers[i].hits;
      total_misses += mgr->workers[i].misses;
    }
    clock_gettime(CLOCK_MONOTONIC, &current);
    if(opts.quiet == 1)
      continue;
    // Derive several values to make pretty output.
    manager__abbreviate_number(total_hits, &hits, &hits_unit);
    manager__abbreviate_number(total_misses, &misses, &misses_unit);
    manager__abbreviate_number(mgr->list->compressions, &compressions, &compressions_unit);
    manager__abbreviate_number(mgr->list->restorations, &restorations, &restorations_unit);
    fprintf(stderr, "\r%-120s", "                                                                                                                        ");
    fprintf(stderr, "\r%5"PRIu16" ETA.  Raw %'"PRIu32" (%"PRIu64" MB).  Comp %'"PRIu32" (%"PRIu64" MB).  %'.2f%c Comps (%'.2f%c Res).  %'.2f%c Hits (%'.2f%c Miss).", opts.duration - elapsed, mgr->list->raw_count, mgr->list->current_raw_size / MILLION, mgr->list->comp_count, mgr->list->current_comp_size / MILLION, compressions, compressions_unit, restorations, restorations_unit, hits, hits_unit, misses, misses_unit);
    fflush(stderr);
  }
  if(opts.quiet == 0)
    fprintf(stderr, "\n");

  /* Flag the manager is no longer runnable, which will stop all workers. */
  mgr->runnable = 0;
  pthread_exit(0);
}


/* manager__abbreviate_number
 * Takes a given number and shortens it to an abbreviated form and sets the unit.
 */
void manager__abbreviate_number(uint64_t source_number, double *short_number, char *unit) {
  char *no_unit = "";
  char *k_unit = "K";
  char *m_unit = "M";
  char *b_unit = "B";
  char *t_unit = "T";
  if(source_number <= THOUSAND) {
    *short_number = 1.0 * source_number;
    strncpy(unit, no_unit, 1);
  }
  if(source_number > THOUSAND) {
    *short_number = 1.0 * source_number / THOUSAND;
    strncpy(unit, k_unit, 1);
  }
  if(source_number > MILLION) {
    *short_number = 1.0 * source_number / MILLION;
    strncpy(unit, m_unit, 1);
  }
  if(source_number > BILLION) {
    *short_number = 1.0 * source_number / BILLION;
    strncpy(unit, b_unit, 1);
  }
  if(source_number > TRILLION) {
    *short_number = 1.0 * source_number / TRILLION;
    strncpy(unit, t_unit, 1);
  }
  return;
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
  unsigned int seed = time(NULL) + id;
  uint8_t has_list_pin = 0;

  /* Begin the main loop for grabbing buffers. */
  Buffer *buf = NULL;
  bufferid_t id_to_get = 0;
  int rv = 0;
  int buf_rv = 0;
  while(mgr->runnable != 0) {
    /* Callers can provide their own list pins before calling read operations.  Do so here to reduce lock contention. */
    if(has_list_pin == 0) {
      list__update_ref(mgr->list, 1);
      has_list_pin = 1;
    }

    /* Go find buffers to play with!  If the one we need doesn't exist, get it and add it. */
    id_to_get = rand_r(&seed) % opts.page_count;
    rv = list__search(mgr->list, &buf, id_to_get, has_list_pin);

    if(rv == E_OK)
      mgr->workers[id].hits++;
    if(rv == E_BUFFER_NOT_FOUND) {
      mgr->workers[id].misses++;
      buf_rv = buffer__initialize(&buf, id_to_get, 0, NULL, mgr->pages[id_to_get]);
      if (buf_rv != E_OK)
        show_error(buf_rv, "Unable to get a buffer.  RV is %d.", buf_rv);
      buf->ref_count++;
      rv = list__add(mgr->list, buf, has_list_pin);
      if (rv == E_BUFFER_ALREADY_EXISTS) {
        // Someone beat us to it.  Just free it and loop around for something else.
        buffer__destroy(buf, DESTROY_DATA);
        buf = NULL;
        continue;
      }
    }
    /* Now we should have a valid buffer.  Hooray.  Mission accomplished. */
    __sync_fetch_and_add(&buf->ref_count, -1);
    buf = NULL;

    /* Release the list pin if there are pending writers.  This is a dirty read/race but that's ok for an extra loop */
    if(mgr->list->pending_writers != 0) {
      list__update_ref(mgr->list, -1);
      has_list_pin = 0;
    }
  }

  // We ran out of time.  Let's update the manager with our statistics before we quit.  Then release our pin.
  pthread_mutex_lock(&mgr->lock);
  mgr->hits += mgr->workers[id].hits;
  mgr->misses += mgr->workers[id].misses;
  pthread_mutex_unlock(&mgr->lock);
  if(has_list_pin != 0) {
    list__update_ref(mgr->list, -1);
    has_list_pin = 0;
  }

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
  free(mgr);
  return E_OK;
}
