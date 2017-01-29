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
#include "error.h"
#include "list.h"
#include "io.h"
#include "options.h"
#include "manager.h"
#include "globals.h"
#include "tyche.h"

/* Make the options stuct shared. */
Options opts;


/* main
 * Initial logic to start tyche.
 */
int main(int argc, char **argv) {
  /* Get options & verify them.  Will terminate if errors, so no checking here. */
  options__process(argc, argv);

  /* Get a list of the pages we have to work with, respecting any limits specified in opts. */
  char **pages = NULL;
  io__get_pages(&pages);
  srand(time(NULL));
  Manager *mgr = manager__initialize(0, pages);

  /* Run the managers. */
  manager__start(mgr);

  /* Clean up and send final notice. */
  manager__destroy(mgr);
  for(uint32_t i=0; i<opts.page_count; i++)
    free(pages[i]);
  free(pages);
  printf("Tyche finished, shutting down.\n");
  return 0;
}
