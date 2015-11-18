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
#include <sys/sysctl.h>   /* for sysconf */
#include "error_codes.h"
#include "error.h"
#include "list.h"
#include "lock.h"
#include "tests.h"
#include "io.h"
#include "tyche.h"

/* Define a few things, mostly for sanity checking later. */
#define MIN_MEMORY            1048576    // 1 MB
#define INITIAL_RAW_RATIO          80    // 80%
#define MAX_WORKERS        UINT16_MAX    // 2^16, 65535
#define MAX_DURATION       UINT16_MAX    // 2^16, 65535
#define MAX_DATASET_MAX    UINT64_MAX    // 2^64, <big>


/* Extern error codes */
extern const int E_OK;
extern const int E_BAD_CLI;


/* main
 * Initial logic to start tyche.
 */
int main(int argc, char **argv) {
  /* Get options & verify them.  Will terminate if errors, so no checking here. */
  Options opts;
  get_options(argc, argv, &opts);

  /* Initialize the locker. */
  lock__initialize();

  /* Build the two lists we're going to use, then set their options. */
  List *raw_list;
  create_listset(&raw_list, &opts);

  /* Get a list of the pages we have to work with.  Build array large enough to store all, even if opts->dataset_max is set. */
  opts->page_count = io__get_page_count(opts->page_directory);
  char *pages[opts->page_count];
  io__build_pages_array(opts->page_directory, pages, opts->dataset_max, &opts->dataset_size, &opts->page_count);



  printf("Tyche finished, shutting down.\n");
  return 0;
}


/* get_options
 * A snippet from main() to get all the options sent via CLI, then verifies them.
 */
void get_options(int argc, char **argv, Options *opts) {
  /* Reset everything since there isn't an initialization function for Options structs. */
  /* Page Information */
  opts->page_directory = NULL;
  opts->page_count = 0;
  opts->page_limit = 0;
  opts->smallest_page = UINT16_MAX;
  opts->biggest_page = 0;
  opts->dataset_size = 0;
  opts->dataset_max = MAX_DATASET_MAX;
  /* Resource Control */
  opts->max_memory = 0;
  opts->fixed_ratio = -1;
  opts->workers = sysconf(_SC_NPROCESSORS_ONLN) > 0 ? (uint16_t)sysconf(_SC_NPROCESSORS_ONLN) : 1;
  /* Test Management */
  opts->duration = 5;
  opts->hit_ratio = -1;

  /* Process everything passed from CLI now. */
  int c = 0;
  opterr = 0;
  while ((c = getopt(argc, argv, "b:d:f:hm:p:r:w:")) != -1) {
    switch (c) {
      case 'b':
        opts->dataset_max = (uint64_t)atoll(optarg);
        break;
      case 'd':
        opts->duration = (uint16_t)atoi(optarg);
        if (atoi(optarg) > MAX_DURATION)
          opts->duration = MAX_DURATION;
        break;
      case 'f':
        opts->fixed_ratio = (int8_t)atoi(optarg);
        break;
      case 'h':
        show_help();
        exit(E_OK);
        break;
      case 'm':
        opts->max_memory = (uint64_t)atoll(optarg);
        break;
      case 'p':
        opts->page_directory = optarg;
        break;
      case 'r':
        opts->hit_ratio = (int8_t)atoi(optarg);
        break;
      case 'w':
        opts->workers = (uint16_t)atoi(optarg);
        if (atoi(optarg) > MAX_WORKERS)
          opts->workers = MAX_WORKERS;
        break;
      case '?':
        show_help();
        if (optopt == 'b' || optopt == 'd' || optopt == 'f' || optopt == 'm' || optopt == 'p' || optopt == 'r' || optopt == 'w')
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
  // -- A page directory is always required.  If it's an invalid path, the io__* functions will catch it.
  if (opts->page_directory == NULL)
    show_error(E_BAD_CLI, "You must specify a directory to search for pages for the test (-p).");
  // -- Memory needs to be at least MIN_MEMORY and less than the installed physical memory.
  const size_t PHYSICAL_MEMORY = (size_t)sysconf(_SC_PHYS_PAGES) * (size_t)sysconf(_SC_PAGESIZE);
  if (opts->max_memory < MIN_MEMORY)
    show_error(E_BAD_CLI, "The memory argument you supplied (-m) is too low.  You sent %"PRIu64", but a minimum of %"PRIu64" is required.", opts->max_memory, MIN_MEMORY);
  if (PHYSICAL_MEMORY == 0)
    show_error(E_GENERIC, "Unable to discern the amount of memory this system has.  Can't be sure we have enough memory to do this test.");
  if (opts->max_memory > PHYSICAL_MEMORY)
    show_error(E_BAD_CLI, "The memory argument you supplied (-m) is too high.  You sent %"PRIu64", but your system maximum physical memory is %d.", opts->max_memory, PHYSICAL_MEMORY);
  // -- Fixed ratio should be -1 (not fixed) or 1 to 100.  Zero is either an atoi() error or nonsensical (can't have just a compressed list and 0% raw.
  if (opts->fixed_ratio == 0)
    show_error(E_BAD_CLI, "The fixed ratio (-f) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; fixed size of 0 would mean 0% for raw buffers which is nonsensical.");
  if (opts->fixed_ratio < -1)
    show_error(E_BAD_CLI, "The fixed ratio (-f) cannot be negative... that's just weird.  Why did you send %d?", opts->fixed_ratio);
  if (opts->fixed_ratio > 100)
    show_error(E_BAD_CLI, "The fixed ratio (-f) cannot be over 100... you can't have more than 100% of your memory assigned to something.  You sent %d.", opts->fixed_ratio);
  // -- Workers must be 1+.  Will be 0 if atoi() fails or user is derp.
  if (opts->workers == 0)
    show_error(E_BAD_CLI, "The worker count (-w) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option.  You need at least 1 worker to, ya know, do work.");
  if (opts->workers == MAX_WORKERS)
    show_error(E_BAD_CLI, "You specified more workers (-w) than allowed (max: %d).", MAX_WORKERS);
  // -- Duration must be non-zero and less than MAX_DURATION.
  if (opts->duration == 0)
    show_error(E_BAD_CLI, "The duration (-d) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option.  The test must run for at least 1 second.");
  if (opts->duration == MAX_DURATION)
    show_error(E_BAD_CLI, "You specified a duration (-d) greater than the max allowed (%d).", MAX_DURATION);
  // -- Hit ratio can't be 0, nor can it be more than 100.
  if (opts->hit_ratio == 0)
    show_error(E_BAD_CLI, "The target hit ratio (-r) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; if you intended to keep the hit ratio as close to zero as possible, simply set the max memory (-m) to the minimum value (%d).", MIN_MEMORY);
  if (opts->hit_ratio < -1)
    show_error(E_BAD_CLI, "The target hit ratio (-r) cannot be negative.  You send %d.", opts->hit_ratio);
  if (opts->hit_ratio > 100)
    show_error(E_BAD_CLI, "The target hit ratio (-r) cannot be over 100... that's just weird.");
  // -- Dataset max cannot be 0.  Other than that... shrugs.
  if (opts->dataset_max == 0)
    show_error(E_BAD_CLI, "The maximum dataset bytes (-b) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; it limits the number of bytes the scan functions will find before moving on with the test.");

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


/* create_listset
 * A convenience function that will combine two lists and set their initial values.
 */
void create_listset(List **raw_list, Options *opts) {
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
  (*raw_list)->max_size = opts->max_memory * (opts->fixed_ratio > 0 ? opts->fixed_ratio : INITIAL_RAW_RATIO) / 100;
  comp_list->max_size = opts->max_memory - raw_list->max_size;

  /* All done. */
  return;
}
