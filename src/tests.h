/*
 * tests.h
 *
 *  Created on: Jul 14, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_TESTS_H_
#define SRC_TESTS_H_

void tests__move_buffers(const uint PAGE_COUNT, char *pages[]);
void tests__io(char *pages[], const int PAGE_COUNT);
void tests__compression();
void tests__synchronized_readwrite();
void tests__wake_up(List *raw_list);
void tests__read(List *raw_list);
void tests__chaos(List *raw_list);
void tests__elements();

#endif /* SRC_TESTS_H_ */
