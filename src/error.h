/*
 * error.h
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_ERROR_H_
#define SRC_ERROR_H_

/* Function Prototypes */
void show_error(int exit_code, char *format, ...);
void show_file_error(char *filespec, int error_code);


#endif /* SRC_ERROR_H_ */
