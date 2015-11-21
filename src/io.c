/*
 * io.c
 *
 *  Created on:      Sep 23, 2015
 *      Author:      Kyle Harper
 *      Description: Handles Input/Output operations (disk IO).  Though technically, tyche only ever reads buffers because write
 *                   performance isn't part of the theory we're testing.
 */

/* Headers */
#include <dirent.h>      /* for opendir() */
#include <errno.h>
#include <stdlib.h>      /* for NULL */
#include <stdio.h>       /* for printf() */
#include <string.h>      /* for strcmp(), strlen(), strcpy() */
#include <sys/stat.h>    /* for stat() */
#include "error.h"
#include "options.h"
#include "io.h"


/* Extern Error Codes */
extern const int E_GENERIC;

/* We need access to the global options. */
extern Options opts;


/* io__build_pages_array
 * Uses the io__scan_for_pages function to first enumerate all the available pages on disk, then stores them in an array of
 * pointers for use by the caller.  Also respects dataset_max and provides feedback for dataset_size, page_count, and largest
 * detected page size.
 */
void io__build_pages_array(char *pages[]) {
  PageFilespec *head = NULL;
  PageFilespec *current = NULL;
  PageFilespec *next = NULL;
  int i = 0;

  // Start the recursive scan.
  io__scan_for_pages(opts.page_directory, &head);

  // Loop through the list and save values to the array.
  /* Page count comes in non-zero because of an earlier scan; when we fix that this line can go away. */
  opts.page_count = 0;
  current = head;
  for(;;) {
    if(((opts.dataset_size + current->page_size) < opts.dataset_max) && (opts.page_count < opts.page_limit)) {
      // We haven't hit our limits (if any was even specified).  Add it up!
      opts.dataset_size += current->page_size;
      opts.page_count++;
      pages[i] = current->filespec;
      i++;
      // Update the smallest/biggest settings.
      if (opts.biggest_page < current->page_size)
        opts.biggest_page = current->page_size;
      if (opts.smallest_page > current->page_size)
        opts.smallest_page = current->page_size;
    }
    // Even if we didn't add it above, we still need to free the memory and update to the next item.
    next = current->next;
    // We don't free(current->filespec) because we assigned the memory address to pages[] above.
    free(current);
    current = next;
    if (current == head)
      break;
  }

  return;
}


/* io__get_page_count
 * Scans the dir_name passed to get a count of the files.  This... sucks cuz it requires me to scan the dir twice, but I cannot for
 * the life of me figure out how to get the values assigned to an "array" via a double-pointer + malloc in the scanning function.
 * This function will respect the ->page_limit member of the options struct.
 */
void io__get_page_count() {
  PageFilespec *head = NULL;
  PageFilespec *current = NULL;
  PageFilespec *next = NULL;

  // Start the recursive scan.
  io__scan_for_pages(opts.page_directory, &head);

  // Build a count of the number of elements so we can build our array below.
  if (head == NULL)
    show_error(E_GENERIC, "Head is still null which means we found no pages in the root directory.\n");
  current = head;
  for(;;) {
    if(opts.page_count < opts.page_limit)
      opts.page_count++;
    next = current->next;
    free(current->filespec);
    free(current);
    current = next;
    if (current == head)
      break;
  }
  return;
}


/* io__scan_for_pages
 * Accepts a directory to serve as a root for recursively scanning for pages to be used for our processing.
 * Void return type because if this fails, we're toast and will show_error() out.
 */
void io__scan_for_pages(char *dir_name, PageFilespec **head) {
  // Open the data directory.
  DIR *dir = opendir(dir_name);
  if (errno != 0) {
    show_file_error(dir_name, errno);
    exit(E_GENERIC);
  }

  // Directory is opened, begin scanning for files.
  struct dirent *entry = NULL;
  struct stat st;
  for(;;) {
    // Start scanning.
    errno = 0;
    if ((entry = readdir(dir)) == NULL)
      return;
    // We have a valid entry.  If this entry is a directory, recurse.  If it's '.' or '..', skip.
    if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)
      continue;
    if (entry->d_type == DT_DIR) {
      char *child_dir = (char *)malloc(strlen(dir_name) + strlen(entry->d_name) + 1 + 1);
      strcpy(child_dir, dir_name);
      strcat(child_dir, "/");
      strcat(child_dir, entry->d_name);
      io__scan_for_pages(child_dir, head);
      free(child_dir);
      child_dir = NULL;
      continue;
    }
    // Our entry is a file, malloc a new Page and update pointers.
    PageFilespec *new_page = (PageFilespec *)malloc(sizeof(PageFilespec));
    new_page->filespec = (char *)malloc(strlen(dir_name) + strlen(entry->d_name) + 1 + 1);
    strcpy(new_page->filespec, dir_name);
    strcat(new_page->filespec, "/");
    strcat(new_page->filespec, entry->d_name);
    stat(new_page->filespec, &st);
    new_page->page_size = (uint)st.st_size;
    if (*head == NULL) {
      *head = new_page;
      (*head)->next = new_page;
    }
    new_page->next = (*head)->next;
    (*head)->next = new_page;
  }

  // Close the directory.  We don't need to free() *new_page because it's part of a chain led by **head.
  closedir(dir);
}
