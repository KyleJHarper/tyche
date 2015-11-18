/*
 * io.h
 *
 *  Created on: Sep 23, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_IO_H_
#define SRC_IO_H_

/* Includes */
#include <stdint.h>

/* A struct to hold the io pages that exist on-disk. */
typedef struct pagefilespec PageFilespec;
struct pagefilespec {
  /* Treat this like a list to avoid double-scanning. */
  char *filespec;      /* The textual path to the page. */
  uint *page_size;     /* Size, in bytes, of this file. */
  PageFilespec *next;  /* Pointer to the next item in the list. */
};


/* Function prototypes */
uint io__get_page_count(char *dir_name);
void io__build_pages_array(char *dir_name, char *pages[], uint64_t dataset_max, uint64_t *dataset_size, uint32_t *page_count);
void io__scan_for_pages(char *data_dir, PageFilespec **head);


#endif /* SRC_IO_H_ */
