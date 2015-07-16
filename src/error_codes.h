/*
 * error_codes.h
 *
 *  Created on: Jun 25, 2015
 *      Author: Codes are shared amongst the entire program.  They should extern them as-needed.
 */

#ifndef SRC_ERROR_CODES_H_
#define SRC_ERROR_CODES_H_

/* Global Error Codes
 *         0  == Ok
 *   1 - 100  == Errors
 * 101 - 199  == Warnings and recoverable situations
 */
const int E_FAILURE_THRESHOLD  = 100;
const int E_WARNING_THRESHOLD  = 200;

const int E_OK                 =   0;
const int E_GENERIC            =   1;
const int E_TRY_AGAIN          = 101;
const int E_BUFFER_POOFED      = 121;


#endif /* SRC_ERROR_CODES_H_ */
