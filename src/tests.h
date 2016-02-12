/*
 * tests.h
 *
 *  Created on: Jul 14, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_TESTS_H_
#define SRC_TESTS_H_

#include "options.h"

/* This structure exists to share and synchronize items for the readwrite test. */
typedef struct readwriteopts ReadWriteOpts;
struct readwriteopts {
  List *raw_list;
  int list_count;
  int worker_count;
  int chaos_monkeys;
  int reads_per_worker;
  int list_floor;
  int sleep_delay;
};

void tests__show_available();
void tests__run_test(List *raw_list, char **pages);
void tests__options();
void tests__move_buffers(List *raw_list, char **pages);
void tests__io(char **pages);
void tests__compression();
void tests__synchronized_readwrite(List *raw_list);
void tests__wake_up(List *raw_list);
void tests__read(ReadWriteOpts *rwopts);
void tests__chaos(ReadWriteOpts *rwopts);
void tests__elements(List *raw_list);
void tests__list_structure(List *list);

#endif /* SRC_TESTS_H_ */
