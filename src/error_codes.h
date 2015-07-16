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

const int E_OK                 =   0;  // All good.
const int E_GENERIC            =   1;  // General errors which don't specifically mean any regular condition/error.
const int E_TRY_AGAIN          = 101;  // Conditions may exist (e.g. threading) requiring a caller the intended action again.
const int E_BUFFER_POOFED      = 121;  // Some operations cause buffers to *poof* as we swap them around.  Handle appropriately.


#endif /* SRC_ERROR_CODES_H_ */
