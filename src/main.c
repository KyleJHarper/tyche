/*
 * main.c
 *
 *  Created on: Jun 18, 2015
 *      Author: Kyle Harper
 * Description: This is a simple program designed to test Harper's Cache Replacement Strategy (HCRS).  This is an implementation
 *              of HCRS, not a library or API to be copied or used wholesale.  The basics of this program include maintaining
 *              circular lists of raw and compressed buffers.  Read up on HCRS for more details on the theory.
 *
 *              Important Note!
 *              The design, organization, and peak optimization of every nuance of this program isn't the target.  Obviously I
 *              don't want massive performance problems (e.g.: lock contentions).  The point of tyche is to build a reasonably
 *              performant benchmarking tool to identify trends in an HCRS implementation.
 */

/* Headers */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "error_codes.h"
#include "error.h"
#include "list.h"
#include "lock.h"
#include "tests.h"

int main(int argc, char *argv[]) {
  /* Initialize locker and lists here. */
  lock__initialize();

  //tests__elem();
  tests__synchronized_read();
  return 0;
}

