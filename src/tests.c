/*
 * tests.c
 *
 *  Created on: Jul 14, 2015
 *      Author: Kyle Harper
 */

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include "list.h"
#include "buffer.h"
#include "tests.h"
#include "lz4.h"


extern const int E_OK;
extern const int E_BUFFER_POOFED;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_IS_VICTIMIZED;





/*
 * Ensures basic compression and decompression works.
 */
void tests__compression() {
  /* Test 1:  make sure the LZ4_* functions can compress and decompress data. */
  // -- Basic stuff
  unsigned char *src = "1234567890abcdef";
  unsigned char *dst = (unsigned char *)malloc(10000);
  int src_size = 16;
  int dst_max_size = 10000;
  int rv = 0;
  unsigned char *new_src = (unsigned char *)malloc(src_size);
    // -- Compress
  FILE *fh_c = fopen("/tmp/char_ptr_c", "wb");
  FILE *fh_d = fopen("/tmp/char_ptr_d", "wb");
  FILE *fh_raw = fopen("/tmp/char_ptr_raw", "wb");
  fwrite(src, 1, src_size, fh_raw);
  fclose(fh_raw);
  rv = LZ4_compress_default(src, dst, src_size, dst_max_size);
  if (rv < 0) {
    printf("The rv was negative when compressing a char pointer, indicating an error: %d\n", rv);
    exit(rv);
  }
  fwrite(dst, 1, rv, fh_c);
  fclose(fh_c);
  // -- Decompress
  rv = LZ4_decompress_fast(dst, new_src, src_size);
  if (rv < 0) {
    printf("The rv was negative when decompressing a char pointer, indicating an error: %d\n", rv);
    exit(rv);
  }
  fwrite(new_src, 1, src_size, fh_d);
  fclose(fh_d);

  /* Test 2:  the LZ4 compression function needs to return the size of the compressed data. */
  // This is automatically done by the function as noted by rv above.

  /* Test 3:  The compression and decompression functions should work on a buffer->data element. */
  Buffer *buf = buffer__initialize(205, NULL);
  buf->data = (unsigned char *)malloc(src_size);
  buf->data_length = src_size;
  memcpy(buf->data, src, buf->data_length);
  // -- Compress
  FILE *fh_buf_c = fopen("/tmp/buf_member_c", "wb");
  FILE *fh_buf_d = fopen("/tmp/buf_member_d", "wb");
  rv = LZ4_compress_default(src, buf->data, buf->data_length, dst_max_size);
  if (rv < 0) {
    printf("The rv was negative when compressing a buffer element, indicating an error: %d\n", rv);
    exit(rv);
  }
  fwrite(buf->data, 1, rv, fh_buf_c);
  fclose(fh_buf_c);
  // -- Decompress
  new_src = (char *)malloc(buf->data_length);
  rv = LZ4_decompress_fast(buf->data, new_src, buf->data_length);
  if (rv < 0) {
    printf("The rv was negative when decompressing a buffer element, indicating an error: %d\n", rv);
    exit(rv);
  }
  fwrite(new_src, 1, buf->data_length, fh_buf_d);
  fclose(fh_buf_d);

  // Print the notice that tests 1 - 3 are done.
  printf("Tests 1 - 3 are done.  Compare MD5 sums in /tmp/*\n");

  /* Test 4:  using the buffer__compress/buffer__decompress functions should work and store comp_time. */
  Buffer *buf2 = buffer__initialize(123, NULL);
  buf2->data = (unsigned char *)malloc(src_size);
  buf2->data_length = src_size;
  memcpy(buf2->data, src, buf2->data_length);
  // -- Compress
  rv = buffer__compress(buf2);
  if (rv != E_OK) {
    printf("buffer__compress gave a result of of: %d\n", rv);
    exit(rv);
  }
  printf("Compression gave an OK response.  Comp time is %d ns, and size is %d bytes\n", buf2->comp_cost, buf2->data_length);
  //TODO finish the buffer__decompress function so we can wrap up this test.

  return;
}





































































/* ------------------------------------------------------------------------------------------------------------------------------
 * Older Tests Below Here
   ------------------------------------------------------------------------------------------------------------------------------*/
// A global for testing cuz I'm bad
const int LIST_COUNT       =   1000;
const int WORKER_COUNT     =   5000;
const int CHAOS_MONKIES    =     10;
const int READS_PER_WORKER =   5000;
const int LIST_FLOOR       =    975;
const int SLEEP_DELAY      =    123;


void tests__synchronized_readwrite() {
  List *raw_list = list__initialize();
  Buffer *temp;
  // Create LIST_COUNT buffers with some data in them.
  for (bufferid_t i=1; i<=LIST_COUNT; i++) {
    temp = buffer__initialize(i, NULL);
    temp->data = "some text, hooray for me";
    list__add(raw_list, temp);
  }

  // Start worker threads which will try to read data at the same time.
  pthread_t workers[WORKER_COUNT];
  for (int i=0; i<WORKER_COUNT; i++)
    pthread_create(&workers[i], NULL, (void *) &tests__read, raw_list);

  // Start up a chaos monkies for insanity.
  pthread_t chaos_monkies[CHAOS_MONKIES];
  for (int i=0; i<CHAOS_MONKIES; i++)
    pthread_create(&chaos_monkies[i], NULL, (void *) &tests__chaos, raw_list);

  // Wait for them to finish.
  for (int i=0; i<WORKER_COUNT; i++)
    pthread_join(workers[i], NULL);
  for (int i=0; i<CHAOS_MONKIES; i++)
    pthread_join(chaos_monkies[i], NULL);

  for (int i=0; i<raw_list->count; i++) {
    if (raw_list->pool[i]->ref_count != 0)
      printf("Buffer ID number %d has non-zero ref_count: %d\n", raw_list->pool[i]->id, raw_list->pool[i]->ref_count);
  }
  list__destroy(raw_list);

  setlocale(LC_NUMERIC, "");
  printf("All done.  I used %d workers performing a combined %'d reads with %d chaos workers taking buffers from %d to %d\n", WORKER_COUNT, WORKER_COUNT * READS_PER_WORKER, CHAOS_MONKIES, LIST_COUNT, LIST_FLOOR);
}
void tests__read(List *raw_list) {
  // Pick random buffers in the list and pin them for reading.  The only way to pick randomly from a list that shrinks is to search
  // over the list itself, which is kinda magoo but it suits the purpose.
  srand(time(0));
  int rv = 0;
  bufferid_t id_to_get = 0;
  Buffer *selected;
  for (int i=0; i<READS_PER_WORKER; i++) {
    for(;;) {
      id_to_get = rand() % LIST_COUNT;
      rv = list__search(raw_list, &selected, id_to_get);
      if (rv == 0)
        break;
      if (rv == E_BUFFER_NOT_FOUND || rv == E_BUFFER_POOFED || E_BUFFER_IS_VICTIMIZED)
        continue;
      printf("We should never hit this (rv is %d).\n", rv);
    }
    usleep(rand() % SLEEP_DELAY);  // This just emulates some random time the reader will use this buffer.
    rv = buffer__lock(selected);
    if (rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED) {
      buffer__update_ref(selected, -1);
      buffer__unlock(selected);
    }
  }
  pthread_exit(0);
}
void tests__chaos(List *raw_list) {
  // Remove a buffer from the list every so often until we're down to LIST_FLOOR, just because.
  Buffer *temp = NULL;
  int rv = 0;
  bufferid_t id_to_remove = 0;
  // This predicate will be unsafe (non-atomic) but that's ok for this testing.
  while(raw_list->count > LIST_FLOOR) {
    id_to_remove = rand() % LIST_COUNT;
    rv = list__search(raw_list, &temp, id_to_remove);
    if (rv == E_BUFFER_NOT_FOUND)
      continue;
    // List search gave us a ref_count, need to decrement ourself.
    buffer__lock(temp);
    buffer__update_ref(temp, -1);
    buffer__unlock(temp);
    rv = list__remove(raw_list, temp, id_to_remove);
    usleep(SLEEP_DELAY);
  }
  pthread_exit(0);
}


void tests__elements() {
  List *raw_list = list__initialize();
  List *comp_list = list__initialize();
  Buffer *elem1 = buffer__initialize(1, NULL);  list__add(raw_list, elem1);
  Buffer *elem2 = buffer__initialize(2, NULL);  list__add(raw_list, elem2);
  Buffer *elem3 = buffer__initialize(3, NULL);  list__add(raw_list, elem3);
  Buffer *rawr1 = buffer__initialize(10, NULL);  list__add(comp_list, rawr1);
  Buffer *rawr2 = buffer__initialize(11, NULL);  list__add(comp_list, rawr2);

  printf("Number of raw  elements: %d\n", raw_list->count);
  printf("Number of comp elements: %d\n", comp_list->count);

  list__remove(raw_list, elem1, elem1->id);
  list__remove(raw_list, elem3, elem3->id);
  list__remove(comp_list, rawr1, rawr1->id);

  printf("Number of raw  elements: %d\n", raw_list->count);
  printf("Number of comp elements: %d\n", comp_list->count);
}

