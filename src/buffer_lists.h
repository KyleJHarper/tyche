/*
 * buffer_lists.h
 *
 *  Created on: Jun 21, 2015
 *      Author: administrator
 */

#ifndef SRC_BUFFER_LISTS_H_
#define SRC_BUFFER_LISTS_H_

/* Includes */
#include <stdint.h>
#include "buffer.h"

/* Function prototypes.  Not required, but whatever. */
void list__initialize();
Buffer* list__add(Buffer *list);
void list__remove(Buffer **buf, char *list_name);
uint32_t list__count(Buffer *list);


#endif /* SRC_BUFFER_LISTS_H_ */
