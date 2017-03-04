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

/* When workers do a delete, what ratio should they use?  Since wiping entire sets of pages is usually abnormal usage patterns. */
#define DELETE_RATIO  25   // 25%

/* Extern the global options. */
extern Options opts;

/* Extern the error codes we need. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_ALREADY_EXISTS;
extern const int E_BUFFER_IS_DIRTY;

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
  mgr->rounds = 0;
  mgr->updates = 0;
  mgr->deletions = 0;

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
  printf("CRUD Operations     : %'"PRIu64" rounds/transactions (%'.f per sec)\n", mgr->rounds, mgr->rounds / (1.0 * mgr->run_duration / 1000));
  printf("  Create/Read       : %'"PRIu64" pages read (%'.f per sec).\n", mgr->hits + mgr->misses, (mgr->hits + mgr->misses) / (1.0 * mgr->run_duration / 1000));
  printf("  Updates           : %'"PRIu64" pages updated (%'.f per sec).\n", mgr->updates, mgr->updates / (1.0 * mgr->run_duration / 1000));
  printf("  Deletions         : %'"PRIu64" pages deleted (%'.f per sec).\n", mgr->deletions, mgr->deletions / (1.0 * mgr->run_duration / 1000));
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
  mgr->workers[id].rounds = 0;
  mgr->workers[id].updates = 0;
  mgr->workers[id].deletions = 0;
  unsigned int seed = time(NULL) + id;
  uint8_t has_list_pin = 0;

  /* Begin the main loop for grabbing buffers.
   * Note that workers operate in "rounds".  This more closely mimics the behavior of a real application.  Each round the worker
   * will attempt acquire a pin on X number of buffers (controlled by opts values).  It will continue working on each round until
   * all buffers are held and pinned.  Once the round is finished, it will do 1 of 3 things:
   *   1) Simulate read-only: just release the pins.
   *   2) Simulate update   : modify the data in the pages (with random garbage) to update them and invoke the Copy-on-Write logic.
   *                          Obviously this isn't emulating write-through or MVCC, just the usage pattern for the in-memory
   *                          operations a buffer pool would have to do.
   *   3) Simulate delete   : deletes all the references buffers, it's quite rare for delete operations, even in real applications,
   *                          so this should be used sparingly.  Tyche will automatically limit the removed pages to DELETE_RATIO
   *                          or 1 page, whichever is greater.
   *
   * The workers will also follow whatever bias was established via opts.  Each workers will consider the first X% of the total
   * data set to be considered "hot" and will account for Y% of all pages used cumulatively through all the rounds.  This allows
   * the workers to emulate real-world usage patterns such as the Pareto Principle (80/20 Rule).  This defaults to 100/100 which
   * means "no bias" :(  I have yet to meet an application that has no bias in data...
   */
  // Initial a few values.
  const int BUF_MAX = 10000;  // At 8 bytes per pointer, this uses 80KB on the stack.
  const int modulo = (opts.max_pages_retrieved - opts.min_pages_retrieved) > 1 ? (opts.max_pages_retrieved - opts.min_pages_retrieved) : 2;
  Buffer *bufs[BUF_MAX];
  int fetch_this_round = 0;
  int rv = 0;
  int buf_rv = 0;
  bufferid_t id_to_get = 0;
  int delete_ceiling = 0;
  const int hot_floor = 0;
  const int hot_ceiling = opts.page_count * opts.bias_percent;
  const int cold_floor = hot_ceiling;
  const int cold_ceiling = opts.page_count + 1;
  int temp_ceiling = 0;
  int temp_floor = 0;
  uint64_t hot_selections = 0;
  uint64_t cold_selections = 0;
  float my_aggregate = 0.0;
  uint64_t updates = 0;
  float my_update_frequency = 0.0;
  uint64_t deletions = 0;
  float my_delete_frequency = 0.0;
  while(mgr->runnable != 0) {
    /* Callers can provide their own list pins before calling read operations.  Do so here to reduce lock contention. */
    if(has_list_pin == 0) {
      list__update_ref(mgr->list, 1);
      has_list_pin = 1;
    }

    /* Determine how many buffers we're supposed to get, whether they should come from "hot" or not, etc. */
    fetch_this_round = (rand_r(&seed) % modulo) + opts.min_pages_retrieved;
    if(fetch_this_round == 0)
      fetch_this_round++;
    for(int i = 0; i<fetch_this_round; i++) {
      my_aggregate = 1.0 * hot_selections / (hot_selections + cold_selections);
      // Find the ID to get.  Make it hot if necessary.
      temp_ceiling = opts.page_count;
      temp_floor = 0;
      // If bias percent is non-zero, gotta find hot/cold explicitly.
      if(opts.bias_percent != 0.0) {
        temp_ceiling = hot_ceiling;
        temp_floor = hot_floor;
        hot_selections++;
        if(my_aggregate > opts.bias_aggregate) {
          temp_ceiling = cold_ceiling;
          temp_floor = cold_floor;
          hot_selections--;
          cold_selections++;
        }
      }

      /* Go find our buffer!  If the one we need doesn't exist, get it and add it. */
      id_to_get = (rand_r(&seed) % temp_ceiling) - temp_floor;
      rv = list__search(mgr->list, &bufs[i], id_to_get, has_list_pin);

      if(rv == E_OK)
        mgr->workers[id].hits++;
      while(rv == E_BUFFER_NOT_FOUND) {
        mgr->workers[id].misses++;
        buf_rv = buffer__initialize(&bufs[i], id_to_get, 0, NULL, mgr->pages[id_to_get]);
        if (buf_rv != E_OK)
          show_error(buf_rv, "Unable to get a buffer.  RV is %d.", buf_rv);
        bufs[i]->ref_count++;
        rv = list__add(mgr->list, bufs[i], has_list_pin);
        if (rv == E_OK)
          break;
        // If it already exists, destroy our copy and search again.
        if (rv == E_BUFFER_ALREADY_EXISTS)
          buffer__destroy(bufs[i], DESTROY_DATA);
        rv = list__search(mgr->list, &bufs[i], id_to_get, has_list_pin);
      }
    }
    // Hooray, we finished a round!
    mgr->workers[id].rounds++;

    /* We should now have all the buffers we wanted for this round, pinned.  Decide if we should update or delete. */
    if(my_update_frequency < opts.update_frequency) {
      // Try to update the buffers.  The purpose of tyche is to stress test the API, not data randomizing speed.  So we'll cheat by
      // simply copying the same data.
      for(int i=0; i<fetch_this_round; i++) {
        void *new_data = malloc(bufs[i]->data_length);
        memcpy(new_data, bufs[i]->data, bufs[i]->data_length);
        rv = list__update(mgr->list, &bufs[i], new_data, bufs[i]->data_length, has_list_pin);
        while(rv == E_BUFFER_IS_DIRTY) {
          // Someone else updated this buffer before us and it's in the slaughter house now.  Find the updated one.
          id_to_get = bufs[i]->id;
          buffer__release_pin(bufs[i]);
          rv = list__search(mgr->list, &bufs[i], id_to_get, has_list_pin);
          while(rv == E_BUFFER_NOT_FOUND) {
            buf_rv = buffer__initialize(&bufs[i], id_to_get, 0, NULL, mgr->pages[id_to_get]);
            bufs[i]->ref_count++;
            rv = list__add(mgr->list, bufs[i], has_list_pin);
            if (rv == E_OK)
              break;
            if (rv == E_BUFFER_ALREADY_EXISTS)
              buffer__destroy(bufs[i], DESTROY_DATA);
            rv = list__search(mgr->list, &bufs[i], id_to_get, has_list_pin);
          }
          // Now try the update again.
          rv = list__update(mgr->list, &bufs[i], new_data, bufs[i]->data_length, has_list_pin);
        }
      }
      // We finished this round's update.  Increment it.
      updates++;
      mgr->workers[id].updates += fetch_this_round;
    }
    my_update_frequency = 1.0 * updates / mgr->workers[id].rounds;

    // Now decide if we should delete any buffers.
    delete_ceiling = 0;
    if(my_delete_frequency < opts.delete_frequency) {
      // Try to delete a portion of the buffers.  See DELETE_RATIO.
      delete_ceiling = (fetch_this_round * DELETE_RATIO / 100) + 1;
      for(int i=0; i<delete_ceiling; i++) {
        rv = list__remove(mgr->list, bufs[i]);
      }
      deletions++;
      mgr->workers[id].deletions += delete_ceiling;
    }
    my_delete_frequency = 1.0 * deletions / mgr->workers[id].rounds;

    // Now release any remaining pins we have.  Deleted ones already lost their pin, so we start from delete_ceiling, if applicable.
    for(int i = delete_ceiling; i<fetch_this_round; i++)
      buffer__release_pin(bufs[i]);

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
  mgr->rounds += mgr->workers[id].rounds;
  mgr->deletions += mgr->workers[id].deletions;
  mgr->updates += mgr->workers[id].updates;
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
