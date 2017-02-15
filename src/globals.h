/*
 *globals.h
 *
 *  Created on: Jun 25, 2015
 *      Author: Kyle Harper
 * Description: Values shared amongst the entire program.  They should extern them as-needed.
 */

#ifndef SRC_GLOBALS_H_
#define SRC_GLOBALS_H_




/* Define the compressor functions.  This should probably be an enum. */
const int NO_COMPRESSOR_ID   = 0;
const int LZ4_COMPRESSOR_ID  = 1;
const int ZLIB_COMPRESSOR_ID = 2;
const int ZSTD_COMPRESSOR_ID = 3;



/* Handy Variables for Lists and Buffers */
const int NEED_PIN      = 0;
const int HAVE_PIN      = 1;
const int KEEP_DATA     = 0;
const int DESTROY_DATA  = 1;


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
const int E_BUFFER_COMPRESSION_PROBLEM      = 126;  // The code we send when lz4/zlib/zstd gives us a code of their own.
const int E_BUFFER_MISSING_A_PIN            = 127;  // If an action requires a pin (list__update) and caller doesn't have one.
const int E_BUFFER_IS_DIRTY                 = 128;  // If someone tries to update a dirty buffer, they aren't allowed.
const int E_LIST_CANNOT_BALANCE             = 140;  // If we can't balance a list, throw this code.
const int E_LIST_REMOVAL                    = 141;  // Cannot remove a buffer from the list.
const int E_NO_MEMORY                       = 150;  // Any time an alloc() fails, we send this rather than bailing out via exit().
const int E_BAD_ARGS                        = 190;  // Caller sent bad arguments.  Whatever was supposed to be done, isn't, or is incomplete.


#endif /* SRC_GLOBALS_H_ */
