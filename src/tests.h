/*
 * tests.h
 *
 *  Created on: Jul 14, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_TESTS_H_
#define SRC_TESTS_H_

#include "options.h"


void tests__show_available();
void tests__run_test(List *raw_list, char *pages[]);
void tests__options();
void tests__move_buffers(List *raw_list, char *pages[]);
void tests__io(char *pages[]);
void tests__compression();
void tests__synchronized_readwrite(List *raw_list);
void tests__wake_up(List *raw_list);
void tests__read(List *raw_list);
void tests__chaos(List *raw_list);
void tests__elements(List *raw_list);

#endif /* SRC_TESTS_H_ */
