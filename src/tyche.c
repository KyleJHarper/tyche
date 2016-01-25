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
#include "manager.h"
#include "tyche.h"

/* Make the options stuct shared. */
Options opts;


/* main
 * Initial logic to start tyche.
 */
int main(int argc, char **argv) {
  /* Get options & verify them.  Will terminate if errors, so no checking here. */
  options__process(argc, argv);

  /* Get a list of the pages we have to work with.  Build array large enough to store all, even if opts->dataset_max is set. */
  io__get_page_count();
  char *pages[opts.page_count];
  opts.max_locks = (uint32_t)(opts.page_count / opts.lock_ratio);
  io__build_pages_array(pages);

  /* Initialize the locker pool and then build the Manager to work with.  Fire an srand() for tests__* just in case. */
  srand(time(NULL));
  lock__initialize();
  Manager *mgr = manager__initialize(0, pages);
  /* If a test was specified, run it instead of the manager(s) and then leave. */
  if (opts.test != NULL) {
    tests__run_test(mgr->raw_list, pages);
    fprintf(stderr, "A test (-t %s) was specified so we ran it.  All done.  Quitting non-zero for safety.\n", opts.test);
    exit(E_GENERIC);
  }

  /* Run the managers. */
  manager__start(mgr);

  printf("Tyche finished, shutting down.\n");
  return 0;
}


