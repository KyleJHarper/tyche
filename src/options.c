/*
 * options.c
 *
 *  Created on: Nov 18, 2015
 *      Author: Kyle Harper
 */


/* Headers, for fun */
#include <ctype.h>
#include <inttypes.h>     /* for PRIuXX format codes */
#include <unistd.h>       /* for getopt */
#include <sys/sysctl.h>   /* for sysconf */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "error.h"
#include "options.h"


/* Definitions to match most of the options. */
#define MIN_MEMORY                1048576    // 1 MB
#define MAX_LOCK_RATIO         UINT8_MAX     // 2^8,  255
#define MAX_WORKERS            UINT16_MAX    // 2^16, 65535
#define MAX_DURATION           UINT16_MAX    // 2^16, 65535
#define MAX_DATASET_MAX        UINT64_MAX    // 2^64, really big...
#define MAX_PAGE_LIMIT         UINT32_MAX    // 2^32, 4.3 billion


/* Extern error codes */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BAD_CLI;

/* Make the options stuct shared. */
extern Options opts;




/* options__process
 * A snippet from main() to get all the options sent via CLI, then verifies them.
 */
void options__process(int argc, char **argv) {
  /* Reset everything since there isn't an initialization function for Options structs. */
  /* Page Information */
  opts.page_directory = NULL;
  opts.page_count = 0;
  opts.page_limit = MAX_PAGE_LIMIT;
  opts.smallest_page = UINT16_MAX;
  opts.biggest_page = 0;
  opts.dataset_size = 0;
  opts.dataset_max = MAX_DATASET_MAX;
  /* Resource Control */
  opts.max_memory = 0;
  opts.fixed_ratio = -1;
  opts.workers = sysconf(_SC_NPROCESSORS_ONLN) > 0 ? (uint16_t)sysconf(_SC_NPROCESSORS_ONLN) : 1;
  opts.lock_ratio = 1;
  opts.max_locks = 0;
  /* Tyche Management */
  opts.duration = 5;
  opts.hit_ratio = -1;
  /* Run Test? */
  opts.test = NULL;

  /* Process everything passed from CLI now. */
  int c = 0;
  opterr = 0;
  while ((c = getopt(argc, argv, "b:d:f:hl:m:n:p:r:t:w:")) != -1) {
    switch (c) {
      case 'b':
        opts.dataset_max = (uint64_t)atoll(optarg);
        break;
      case 'd':
        opts.duration = (uint16_t)atoi(optarg);
        if (atoi(optarg) > MAX_DURATION)
          opts.duration = MAX_DURATION;
        break;
      case 'f':
        opts.fixed_ratio = (int8_t)atoi(optarg);
        break;
      case 'h':
        options__show_help();
        exit(E_OK);
        break;
      case 'l':
        opts.lock_ratio = (uint8_t)atoi(optarg);
        if(atoi(optarg) > MAX_LOCK_RATIO)
          opts.lock_ratio = MAX_LOCK_RATIO;
        break;
      case 'm':
        opts.max_memory = (uint64_t)atoll(optarg);
        break;
      case 'n':
        opts.page_limit = (uint32_t)atoll(optarg);
        break;
      case 'p':
        opts.page_directory = optarg;
        break;
      case 'r':
        opts.hit_ratio = (int8_t)atoi(optarg);
        break;
      case 't':
        if (opts.test != NULL) {
          fprintf(stderr, "You cannot specify the -t option more than once.\n");
          exit(E_BAD_CLI);
        }
        opts.test = optarg;
        break;
      case 'w':
        opts.workers = (uint16_t)atoi(optarg);
        if (atoi(optarg) > MAX_WORKERS)
          opts.workers = MAX_WORKERS;
        break;
      case '?':
        options__show_help();
        if (optopt == 'b' || optopt == 'd' || optopt == 'f' || optopt == 'l' || optopt == 'm' || optopt == 'n' || optopt == 'p' || optopt == 'r' || optopt == 'w')
          fprintf(stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        exit(E_BAD_CLI);
      default:
        options__show_help();
        exit(E_BAD_CLI);
    }
  }

  /* Pre-flight Checks */
  // -- A page directory is always required.  If it's an invalid path, the io__* functions will catch it.
  if (opts.page_directory == NULL)
    show_error(E_BAD_CLI, "You must specify a directory to search for pages for the test (-p).");
  // -- Memory needs to be at least MIN_MEMORY and less than the installed physical memory.
  const size_t PHYSICAL_MEMORY = (size_t)sysconf(_SC_PHYS_PAGES) * (size_t)sysconf(_SC_PAGESIZE);
  if (opts.max_memory < MIN_MEMORY)
    show_error(E_BAD_CLI, "The memory argument you supplied (-m) is too low.  You sent %"PRIu64", but a minimum of %"PRIu64" is required.", opts.max_memory, MIN_MEMORY);
  if (PHYSICAL_MEMORY == 0)
    show_error(E_GENERIC, "Unable to discern the amount of memory this system has.  Can't be sure we have enough memory to do this test.");
  if (opts.max_memory > PHYSICAL_MEMORY)
    show_error(E_BAD_CLI, "The memory argument you supplied (-m) is too high.  You sent %"PRIu64", but your system maximum physical memory is %d.", opts.max_memory, PHYSICAL_MEMORY);
  // -- Fixed ratio should be -1 (not fixed) or 1 to 100.  Zero is either an atoi() error or nonsensical (can't have just a compressed list and 0% raw.
  if (opts.fixed_ratio == 0)
    show_error(E_BAD_CLI, "The fixed ratio (-f) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; fixed size of 0 would mean 0% for raw buffers which is nonsensical.");
  if (opts.fixed_ratio < -1)
    show_error(E_BAD_CLI, "The fixed ratio (-f) cannot be negative... that's just weird.  Why did you send %"PRIi8"?", opts.fixed_ratio);
  if (opts.fixed_ratio > 100)
    show_error(E_BAD_CLI, "The fixed ratio (-f) cannot be over 100... you can't have more than 100%% of your memory assigned to something.  You sent %"PRIi8".", opts.fixed_ratio);
  // -- Workers must be 1+.  Will be 0 if atoi() fails or user is derp.
  if (opts.workers == 0)
    show_error(E_BAD_CLI, "The worker count (-w) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option.  You need at least 1 worker to, ya know, do work.");
  if (opts.workers == MAX_WORKERS)
    show_error(E_BAD_CLI, "You specified more workers (-w) than allowed (max: %d).", MAX_WORKERS);
  // -- Duration must be non-zero and less than MAX_DURATION.
  if (opts.duration == 0)
    show_error(E_BAD_CLI, "The duration (-d) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option.  The test must run for at least 1 second.");
  if (opts.duration == MAX_DURATION)
    show_error(E_BAD_CLI, "You specified a duration (-d) greater than the max allowed (%d).", MAX_DURATION);
  // -- Hit ratio can't be 0, nor can it be more than 100.
  if (opts.hit_ratio == 0)
    show_error(E_BAD_CLI, "The target hit ratio (-r) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; if you intended to keep the hit ratio as close to zero as possible, simply set the max memory (-m) to the minimum value (%d).", MIN_MEMORY);
  if (opts.hit_ratio < -1)
    show_error(E_BAD_CLI, "The target hit ratio (-r) cannot be negative.  You send %d.", opts.hit_ratio);
  if (opts.hit_ratio > 100)
    show_error(E_BAD_CLI, "The target hit ratio (-r) cannot be over 100... that's just weird.");
  // -- Dataset max cannot be 0.  Other than that... shrugs.
  if (opts.dataset_max == 0)
    show_error(E_BAD_CLI, "The maximum dataset bytes (-b) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; it limits the number of bytes the scan functions will find before moving on with the test.");
  // -- Page limit cannot be 0.
  if (opts.page_limit == 0)
    show_error(E_BAD_CLI, "The page limit (-n) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; it limits the number of pages the scan functions will find before moving on with the test.");
  // -- Locker pool size limit cannot be 0.  It defaults to 1.
  if (opts.lock_ratio == 0)
    show_error(E_BAD_CLI, "The lock ratio (-l) is 0.  You either sent invalid input (atoi() failed), or you misunderstood the option; it sets the number of buffers sharing each buffer lock.");
  if (opts.lock_ratio == MAX_LOCK_RATIO)
    show_error(E_BAD_CLI, "The lock ratio (-l) is too high.  Maximum allowed value is %"PRIu8, MAX_LOCK_RATIO);

  return;
}


/* show_help
 * Spits out the basic syntax for tyche.
 */
void options__show_help() {
  fprintf(stderr, "\n");
  fprintf(stderr, "tyche - Example Program for the Adaptive Compressed Cache Replacement Strategy (ACCRS)\n");
  fprintf(stderr, "        This is an implementation of ACCRS and is NOT intended as a tool or API!\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "  Usage: tyche <-p pages_directory> <-m memory_size> [-bdfhlnrtw]\n");
  fprintf(stderr, "     ex: tyche -d /data/pages/8k -m 10000000\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "  Options:\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-b", "<number>",  "Maximum number of bytes to use from the data pages.  Default: unlimited.\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-d", "<number>",  "Duration to run tyche, in seconds.  Default: 5 sec\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-f", "1 - 100",   "Fixed ratio.  Percentage RAM guaranteed for the raw buffer list.  Default: disabled (-1)\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-h", "",          "Show this help.\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-l", "1 - 255",   "Number of buffers sharing each buffer lock.  Defaults to 1.\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-m", "<number>",  "Maximum number of bytes (RAM) to use for all buffers.  Default: 1 MB.\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-n", "<number>",  "Maximum number of pages to use from the sample data pages.  Default: unlimited.\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-p", "/some/dir", "The directory to scan for pages of sample data.  (Recursive of course).\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-r", "1 - 100",   "Hit Ratio to ensure as a minimum (by searching raw list when too low).  Default: disabled (-1)\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-t", "test_name", "Run an internal test.  Specify 'help' to see available tests.  (For debugging).\n");
  fprintf(stderr, "    %2s   %-10s   %s", "-w", "<number>",  "Number of workers (threads) to use while testing.  Defaults to CPU count.\n");
  fprintf(stderr, "\n");

  return;
}
