/*
 * options.h
 *
 *  Created on: Nov 18, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_OPTIONS_H_
#define SRC_OPTIONS_H_

/* Includes */
#include <stdint.h>


/* A Convenient Struct to hold our options and settings. */
typedef struct options Options;
struct options {
  /* Page Information */
  char *page_directory;    // The root directory for pages to find pages.
  uint32_t page_count;     // The number of pages found when scanning for data to use.
  uint32_t page_limit;     // The limit imposed when scanning page_directory for pages.
  uint16_t smallest_page;  // Size of the smallest page found while scanning.
  uint16_t biggest_page;   // Size of the largest page found while scanning.
  uint64_t dataset_size;   // Total bytes in the pages scanned, respecting of page_limit.
  uint64_t dataset_max;    // The maximum size, in bytes, for all pages in the dataset.

  /* Resource Control */
  uint64_t max_memory;     // Maximum amount of memory tyche can use for buffers.
  int8_t fixed_ratio;      // If non-negative, enforce raw list to this ratio when balancing.
  uint16_t workers;        // Number of worker threads to use simultaneously.
  uint8_t lock_ratio;      // Number of buffers sharing the same lock.
  uint32_t max_locks;      // Number of actual locks to create to meet the ratio.

  /* Tyche Management */
  uint16_t duration;       // Amount of time for each worker to run, in seconds (s).
  int8_t hit_ratio;        // Minimum hit ratio to attain when testing.

  /* Run Test? */
  char *test;              // Name of a test to run.
};


/* Prototypes, for science! */
void options__process(int argc, char **argv);
void options__show_help();


#endif /* SRC_OPTIONS_H_ */
