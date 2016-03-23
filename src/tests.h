/*
 * tests.h
 *
 *  Created on: Jul 14, 2015
 *      Author: Kyle Harper
 */

#ifndef SRC_TESTS_H_
#define SRC_TESTS_H_

#include <stdint.h>
#include "options.h"

/* This structure exists to share and synchronize items for the readwrite test. */
typedef struct readwriteopts ReadWriteOpts;
struct readwriteopts {
  List *list;
  uint32_t list_count;
  uint32_t worker_count;
  uint32_t chaos_monkeys;
  uint32_t reads_per_worker;
  uint32_t list_floor;
  uint32_t sleep_delay;
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

#endif /* SRC_TESTS_H_ */
