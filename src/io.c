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
#include "error.h"
#include "io.h"


/* Extern Error Codes */
extern const int E_GENERIC;


/* io__build_pages_array
 * Uses the io__scan_for_pages function to first enumerate all the available pages on disk, then stores them in an array of
 * pointers for use by the caller.  Return value is the size of the first page found.
 */
void io__build_pages_array(char *dir_name, char *pages[]) {
  PageFilespec *head = NULL;
  PageFilespec *current = NULL;
  int i = 0;

  // Start the recursive scan.
  io__scan_for_pages(dir_name, &head);

  // Loop through the list and save values to the array.
  current = head;
  for(;;) {
    pages[i] = current->filespec;
    i++;
    free(current);
    current = current->next;
    if (current == head)
      break;
  }
  return;
}


/* io__get_page_count
 * Scans the dir_name passed to get a count of the files.  This... sucks cuz it requires me to scan the dir twice, but I cannot for
 * the life of me figure out how to get the values assigned to an "array" via a double-pointer + malloc in the scanning function.
 */
uint io__get_page_count(char *dir_name) {
  PageFilespec *head = NULL;
  PageFilespec *current = NULL;
  int count = 0;

  // Start the recursive scan.
  io__scan_for_pages(dir_name, &head);

  // Build a count of the number of elements so we can build our array below.
  if (head == NULL)
    show_error("Head is still null which means we found no pages in the root directory.\n", E_GENERIC);
  current = head;
  for(;;) {
    count++;
    free(current);
    current = current->next;
    if (current == head)
      break;
  }
  return count;
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