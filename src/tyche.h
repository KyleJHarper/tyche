/*
 * tyche.h
 *
 *  Created on: Sep 1, 2015
 *      Author: administrator
 */

#ifndef SRC_TYCHE_H_
#define SRC_TYCHE_H_

/* Includes */
#include <stdint.h>


/* A Convenient Struct to hold our options and settings. */
typedef struct options Options;
struct options {
  /* Page Information */
  char *page_directory;   // The root directory for pages to find pages.
  uint32_t page_count;    // The number of pages found when scanning for data to use.
  uint32_t page_limit;    // The limit imposed when scanning page_directory for pages.
  uint16_t biggest_page;  // Size of the largest page found while scanning.
  uint64_t dataset_size;  // Total bytes in the pages scanned, respecting of page_limit.
  uint64_t dataset_max;   // The maximum size, in bytes, for all pages in the dataset.

  /* Resource Control */
  uint64_t max_memory;    // Maximum amount of memory tyche can use for buffers.
  uint8_t fixed_ratio;    // If non-zero, enforce raw list to this ratio when balancing.
  uint16_t workers;       // Number of worker threads to use simultaneously.


  /* Test Management */
  uint16_t duration;      // Amount of time for each worker to run, in seconds (s).
  uint8_t hit_ratio;      // Minimum hit ratio to attain when testing.
};

/* Prototypes, because reasons! */
int main(int argc, char **argv);
void get_options(int argc, char **argv, Options *opts);
void show_help();

#endif /* SRC_TYCHE_H_ */
