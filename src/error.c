/*
 * error.c
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* show_error
 * Displays an error to stderr and aborts if exit_code is non-zero.  This function should basically only be used when we want to
 * exit after error message.
 */
void show_error(char *message, int exit_code) {
  fprintf(stderr, "%s\nABORTING\n", message);
  if (exit_code != 0) exit(exit_code);
}


/* show_file_error
 * Looks up the error_code sent to send a useful message.
 */
void show_file_error(char *filespec, int error_code) {
  switch (error_code) {
    case 0:
      fprintf(stderr, "show_file_error() was called with an error_code of 0, this shouldn't have happened.\n");
      break;
    case ENOENT:
      fprintf(stderr, "File/directory not found: %s\n", filespec);
      break;
    case EACCES:
      fprintf(stderr, "Access denied to directory: %s\n", filespec);
      break;
    case ELOOP:
      fprintf(stderr, "Path specified appears to be a symbolic link that loops: %s\n", filespec);
      break;
    case ENAMETOOLONG:
      fprintf(stderr, "File/directory name is too long for this platform: %s\n", filespec);
      break;
    case ENFILE:
      fprintf(stderr, "Too many file concurrently open by the system, cannot open filespec: %s\n", filespec);
      break;
    case EMFILE:
      fprintf(stderr, "Too many open files by this process, cannot open filespec: %s\n", filespec);
      break;
    case ENOMEM:
      fprintf(stderr, "No available memory to open this file/directory: %s\n", filespec);
      break;
    default:
      fprintf(stderr, "Untrapped error code trying to access %s, code is: %d\n", filespec, errno);
      break;
  }
}
