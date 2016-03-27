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
const int E_FAILURE_THRESHOLD  =   1;
const int E_WARNING_THRESHOLD  = 100;

/* OK Situation */
const int E_OK                              =   0;  // All good.
/* Failure Situations */
const int E_GENERIC                         =   1;  // General errors which don't specifically mean any regular condition/error.
const int E_BAD_CLI                         =   2;  // User sent invalid CLI args.
const int E_BAD_CONF                        =   3;  // User provided a config file with bad options.  (Not in use yet)
/* Warnings and Recoverable Situations */
const int E_TRY_AGAIN                       = 101;  // Conditions may exist (e.g. threading) requiring a caller the intended action again.
const int E_BUFFER_NOT_FOUND                = 120;  // When searching for a buffer, throw this when not found.  Let caller handle.
const int E_BUFFER_IS_VICTIMIZED            = 121;  // When buffers are victimized, callers need to know this.  For ex: lowering ref counts.
const int E_BUFFER_ALREADY_EXISTS           = 122;  // When trying to add a buffer to a list, we return this if it already exists.
const int E_BUFFER_MISSING_DATA             = 123;  // Operations attempting to read data from a buffer might find it has none at the time.
const int E_BUFFER_ALREADY_COMPRESSED       = 124;  // Not sure this condition ever really exists but we'll set a code.
const int E_BUFFER_ALREADY_DECOMPRESSED     = 125;  // If 2 threads are attempting to restore a buffer this condition can occur.


#endif /* SRC_ERROR_CODES_H_ */
