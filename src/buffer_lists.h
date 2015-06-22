/*
 * buffer_lists.h
 *
 *  Created on: Jun 21, 2015
 *      Author: administrator
 */

#ifndef SRC_BUFFER_LISTS_H_
#define SRC_BUFFER_LISTS_H_

/* Define some fun stuff.  Just cuz. */
const char *LIST__BAD_INITIALIZE_MSG = "Error initializing the following list: ";

/* Function prototypes.  Not required, but whatever. */
int list__initialize();
int list__add();
int list__remove();



#endif /* SRC_BUFFER_LISTS_H_ */
