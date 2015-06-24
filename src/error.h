/*
 * error.h
 *
 *  Created on: Jun 21, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_ERROR_H_
#define SRC_ERROR_H_

/* Function Prototypes */
void show_err(char *message, int exit_code);

/* Global Error Codes */
const int E_OK          = 0;
const int E_GENERIC     = 1;
const int E_TRY_AGAIN = 101;

#endif /* SRC_ERROR_H_ */
