/*
 * buffer_lists.h
 *
 *  Created on: Jun 21, 2015
 *      Author: administrator
 */

#ifndef SRC_BUFFER_LISTS_H_
#define SRC_BUFFER_LISTS_H_

/* Includes */
#include "buffer.h"

/* Function prototypes.  Not required, but whatever. */
void list__initialize();
void list__add(Buffer *list);
void list__remove(Buffer *buf, char *list_name);



#endif /* SRC_BUFFER_LISTS_H_ */
