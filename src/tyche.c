/*
 * tyche.c
 *
 *  Created on: Jun 18, 2015
 *      Author: Kyle Harper
 * Description: This is a simple program designed to test Adaptive Compressed-Cache Replacement Strategy (ACCRS).  This is an
 *              implementation of the ACCRS, not a library or API to be copied or used wholesale.  The basics of this program
 *              include tests that simulate a system reading buffers under constrained conditions (not enough RAM to store the
 *              entire data set).  Read up on the ACCRS for more details on the theory.
 *
 *              Important Note!
 *              The design, organization, and peak optimization of every nuance of this program isn't the target.  Obviously I
 *              don't want massive performance problems (e.g.: lock contentions).  The point of tyche is to build a reasonably
 *              performant benchmarking tool to identify trends in an ACCRS implementation to prove various hypotheses.
 */

/* Headers */
#include <stdlib.h>       /* for exit() */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "error_codes.h"
#include "error.h"
#include "list.h"
#include "lock.h"
#include "tests.h"
#include "io.h"
#include "options.h"
#include "tyche.h"

/* Define a few things, mostly for sanity checking later. */
#define INITIAL_RAW_RATIO  80    // 80%

/* Make the options stuct shared. */
Options opts;



/* main
 * Initial logic to start tyche.
 */
int main(int argc, char **argv) {
  /* Get options & verify them.  Will terminate if errors, so no checking here. */
  options__process(argc, argv);

  /* Initialize the locker. */
  lock__initialize();

  /* Build the two lists we're going to use, then set their options. */
  List *raw_list;
  create_listset(&raw_list);

  /* Get a list of the pages we have to work with.  Build array large enough to store all, even if opts->dataset_max is set. */
  io__get_page_count();
  char *pages[opts.page_count];
  opts.max_locks = (uint32_t)(opts.page_count / opts.lock_ratio);
  lock__initialize();
  io__build_pages_array(pages);

  /* If a test was specified, run it instead of tyche and then leave. */
  if (opts.test != NULL) {
    tests__run_test(pages);
    show_error(E_GENERIC, "A test (-t %s) was specified.  It should have ran by now.  Quitting non-zero for safety.", opts.test);
  }

  printf("Tyche finished, shutting down.\n");
  return 0;
}


/* create_listset
 * A convenience function that will combine two lists and set their initial values.
 */
void create_listset(List **raw_list) {
  /* Initialize the lists. */
  *raw_list = list__initialize();
  if (*raw_list == NULL)
    show_error(E_GENERIC, "Couldn't create the raw list.  This is fatal.");
  List *comp_list = list__initialize();
  if (comp_list == NULL)
    show_error(E_GENERIC, "Couldn't create the compressed list.  This is fatal.");

  /* Join the lists to eachother. */
  (*raw_list)->offload_to = comp_list;
  comp_list->restore_to = *raw_list;

  /* Set the memory sizes for both lists. */
  (*raw_list)->max_size = opts.max_memory * (opts.fixed_ratio > 0 ? opts.fixed_ratio : INITIAL_RAW_RATIO) / 100;
  comp_list->max_size = opts.max_memory - (*raw_list)->max_size;

  /* All done. */
  return;
}
