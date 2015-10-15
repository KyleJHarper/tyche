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
#include <unistd.h>       /* for getopt */
#include <stdlib.h>       /* for exit() */
#include "error_codes.h"
#include "error.h"
#include "list.h"
#include "lock.h"
#include "tests.h"
#include "io.h"
#include "main.h"

/* Extern error codes */
extern const int E_OK;
extern const int E_GENERIC;


/* main
 * Initial logic to start tyche.
 */
int main(int argc, char **argv) {
  /* Get options & verify them. */
  char *data_dir = NULL;
  get_options(argc, argv, &data_dir);

  /* Get a list of the pages we have to work with. */
  const uint PAGE_COUNT = io__get_page_count(data_dir);
  char *pages[PAGE_COUNT];
  io__build_pages_array(data_dir, pages);

  /* Initialize the locker. */
  lock__initialize();

  tests__compression();
//  Buffer *buf = buffer__initialize(205, NULL);
//  buf->data = (unsigned char *)malloc(100);
//  unsigned char *raw_data = "1234567890abcde";
//  buf->data_length = 10;
//  memcpy(buf->data, raw_data, buf->data_length);
//  printf("Buf's memcpy'd data is: '%s'\n", buf->data);
//
//  buffer__compress(buf);
//  printf("Buf's compressed data is: '%s'\n", buf->data);
//  FILE *fh = fopen("/tmp/rawrc", "wb");
//  fwrite(buf->data, 1, buf->data_length, fh);
//  fclose(fh);
//
//  buffer__decompress(buf);
//  printf("Buf's decompressed data is: '%s'\n", buf->data);
//  FILE *fhd = fopen("/tmp/rawrd", "wb");
//  fwrite(buf->data, 1, buf->data_length, fhd);
//  fclose(fhd);

  printf("Main finished.\n");
  return 0;
}


/* get_options
 * A snippet from main() to get all the options sent via CLI, then verifies them.
 */
void get_options(int argc, char **argv, char **data_dir) {
  // Shamelessly copied from gcc example docs.  No need to get fancy.
  int c = 0, index = 0;
  opterr = 0;
  while ((c = getopt(argc, argv, "d:ht:")) != -1) {
    switch (c) {
      case 'd':
        *data_dir = optarg;
        break;
      case 'h':
        show_help();
        exit(E_OK);
        break;
      case '?':
        show_help();
        if (optopt == 'd')
          fprintf(stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf(stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
        exit(E_GENERIC);
      default:
        show_help();
        exit(E_GENERIC);
    }
  }

  /* Pre-flight Checks */
  // -- A directory is always required.
  if (*data_dir == NULL)
    show_error("You must specify a data directory.\n", E_GENERIC);
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
