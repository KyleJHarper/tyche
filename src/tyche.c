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
#include <ctype.h>
#include <inttypes.h>     /* for PRIuXX format codes */
#include <stdlib.h>       /* for exit() */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>       /* for getopt */
#include "error_codes.h"
#include "error.h"
#include "list.h"
#include "lock.h"
#include "tests.h"
#include "io.h"
#include "tyche.h"

/* Define a few things, mostly for sanity checking later. */
#define MIN_MEMORY 1048576    /* 1 MB */
#define INITIAL_RAW 80        /* 80% */


/* Extern error codes */
extern const int E_OK;
extern const int E_BAD_CLI;


/* main
 * Initial logic to start tyche.
 */
int main(int argc, char **argv) {
  /* Get options & verify them. */
  char *DATA_DIR = NULL;
  uint64_t MAX_MEMORY = 0;
  get_options(argc, argv, &DATA_DIR, &MAX_MEMORY);

  /* Build the two lists we're going to use. */
  List *raw_list = list__initialize();
  List *comp_list = list__initialize();
  raw_list->max_size = MAX_MEMORY * INITIAL_RAW / 100;
  comp_list->max_size = MAX_MEMORY * (100 - INITIAL_RAW) / 100;
  raw_list->offload_to = comp_list;
  comp_list->restore_to = raw_list;

  /* Get a list of the pages we have to work with. */
  const uint PAGE_COUNT = io__get_page_count(DATA_DIR);
  char *pages[PAGE_COUNT];
  io__build_pages_array(DATA_DIR, pages);

  /* Initialize the locker. */
  lock__initialize();

  tests__move_buffers(PAGE_COUNT, pages);
  //tests__synchronized_readwrite();

  printf("Tyche finished, shutting down.\n");
  return 0;
}


/* get_options
 * A snippet from main() to get all the options sent via CLI, then verifies them.
 */
void get_options(int argc, char **argv, char **data_dir, uint64_t *max_memory) {
  // Shamelessly copied from gcc example docs.  No need to get fancy.
  int c = 0;
  opterr = 0;
  while ((c = getopt(argc, argv, "d:hm:")) != -1) {
    switch (c) {
      case 'd':
        *data_dir = optarg;
        break;
      case 'h':
        show_help();
        exit(E_OK);
        break;
      case 'm':
        if (optarg == NULL)
          show_error(E_BAD_CLI, "You specified -m but didn't actually provide an argument... tsk tsk");
        *max_memory = atoi(optarg);
        break;
      case '?':
        show_help();
        if (optopt == 'd')
          fprintf(stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        exit(E_BAD_CLI);
      default:
        show_help();
        exit(E_BAD_CLI);
    }
  }

  /* Pre-flight Checks */
  // -- A directory is always required.
  if (*data_dir == NULL)
    show_error(E_BAD_CLI, "You must specify a data directory.");
  // -- Memory needs to be at least MIN_MEMORY
  if (*max_memory < MIN_MEMORY)
    show_error(E_BAD_CLI, "The memory argument you supplied (-m) is too low.  You sent %"PRIu64", but a minimum of %"PRIu64" is required.", *max_memory, MIN_MEMORY);

  return;
}


/* show_help
 * Spits out the basic syntax for tyche.
 */
void show_help() {
  fprintf(stderr, "\n");
  fprintf(stderr, "tyche - Example Program for the Adaptive Compressed Cache Replacement Strategy (ACCRS)\n");
  fprintf(stderr, "        This is an implementation of ACCRS and is NOT intended as a tool or API!\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "  Usage: tyche <-d page_dir> [-h]....more I'm sure\n");
  fprintf(stderr, "     ex: tyche -d /data/pages/8k ... more I'm sure\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "  Options:\n");
  fprintf(stderr, "    %3s    %s", "-d", "The directory to scan for pages when testing.  (Recursive of course)\n");
  fprintf(stderr, "    %3s    %s", "-h", "Show this help.  Exit 0.\n");
  fprintf(stderr, "    %3s    %s", "XX", "More I'm Sure\n");
  fprintf(stderr, "\n");
  return;
}
