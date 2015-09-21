/*
 * main.c
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
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>    /* for getopt */
#include "error_codes.h"
#include "error.h"
#include "list.h"
#include "lock.h"
#include "tests.h"
#include "main.h"

/* Extern error codes */
extern const int E_OK;
extern const int E_GENERIC;


/* main
 * Initial logic to start tyche.
 */
int main(int argc, char **argv) {
  /* Get options, then verify them. */
  char *pages_directory = NULL;
  int c = 0, index = 0;
  opterr = 0;
  while ((c = getopt(argc, argv, "d:h")) != -1) {
    switch (c) {
      case 'd':
        pages_directory = optarg;
        break;
      case 'h':
        show_help();
        return E_OK;
        break;
      case '?':
        show_help();
        if (optopt == 'd')
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);
        return E_GENERIC;
      default:
        show_help();
        return E_GENERIC;
    }
  }
  /* Pre-flight Checks */

  /* Initialize locker and lists here. */
  lock__initialize();

  printf("Main finished.\n");
  return 0;
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
