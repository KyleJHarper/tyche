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
#include <inttypes.h>
#include "list.h"
#include "buffer.h"
#include "options.h"
#include "tests.h"
#include "lz4.h"
#include "error.h"


/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L
#define MILLION    1000000L

extern const int E_OK;
extern const int E_BAD_CLI;
extern const int E_BUFFER_POOFED;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_IS_VICTIMIZED;
extern const int E_GENERIC;

extern const int BUFFER_OVERHEAD;

/* Make the options stuct shared. */
extern Options opts;


void tests__show_available() {
  printf("Available Tests (case-sensitive)\n");
  printf("                   all :  Run all tests.\n");
  printf("           compression :  Test basic compression and buffer compression.\n");
  printf("              elements :  Basic building of Buffer elements and adding/removing from a list.\n");
  printf("                    io :  Read pages from disk and store information in Buffers.\n");
  printf("          move_buffers :  Purposely puts lists into conditions that trigger sweeping/pushing/popping.\n");
  printf("               options :  Shows the value of all options; great for debugging CLI issues.\n");
  printf("synchronized_readwrite :  Extensive test proving asynchronous behavior is safe.\n");
  printf("\n");
  return;
}


void tests__run_test(List *raw_list, char *pages[]) {
  /* Start Timer */
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  int ran_test = 0;

  /* If 'help' is sent, show options. */
  if(strcmp(opts.test, "help") == 0) {
    tests__show_available();
    return;
  }
  /* ALL tests... */
  if(strcmp(opts.test, "all") == 0) {
    printf("RUNNING TEST: tests__compression\n");
    tests__compression();
    printf("RUNNING TEST: tests__elements\n");
    tests__elements(raw_list);
    printf("RUNNING TEST: tests__io\n");
    tests__io(pages);
    printf("RUNNING TEST: tests__move_buffers\n");
    tests__move_buffers(raw_list, pages);
    printf("RUNNING TEST: tests__options\n");
    tests__options(opts);
    printf("RUNNING TEST: tests__synchronized_readwrite\n");
    tests__synchronized_readwrite(raw_list);
    ran_test++;
  }

  /* tests__compression */
  if(strcmp(opts.test, "compression") == 0) {
    printf("RUNNING TEST: tests__compression\n");
    tests__compression();
    ran_test++;
  }
  /* tests__elements */
  if(strcmp(opts.test, "elements") == 0) {
    printf("RUNNING TEST: tests__elements\n");
    tests__elements(raw_list);
    ran_test++;
  }
  /* tests__io */
  if(strcmp(opts.test, "io") == 0) {
    printf("RUNNING TEST: tests__io\n");
    tests__io(pages);
    ran_test++;
  }
  /* tests__move_buffers */
  if(strcmp(opts.test, "move_buffers") == 0) {
    printf("RUNNING TEST: tests__move_buffers\n");
    tests__move_buffers(raw_list, pages);
    ran_test++;
  }
  /* tests__options */
  if (strcmp(opts.test, "options") == 0) {
    printf("RUNNING TEST: tests__options\n");
    tests__options(opts);
    ran_test++;
  }
  /* tests__synchronized_readwrite */
  if(strcmp(opts.test, "synchronized_readwrite") == 0) {
    printf("RUNNING TEST: tests__synchronized_readwrite\n");
    tests__synchronized_readwrite(raw_list);
    ran_test++;
  }

  /* Stop Timer and Leave */
  clock_gettime(CLOCK_MONOTONIC, &end);
  int test_ms = (BILLION *(end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec) / MILLION;
  printf("Test Time: %d ms\n", test_ms);
  if (ran_test == 0) {
    tests__show_available();
    show_error(E_BAD_CLI, "You sent a test name (-t %s) for a test that doesn't exist: tests__%s.", opts.test, opts.test);
  }
  return;
}


void tests__synchronized_readwrite(List *raw_list) {
  // Set up the default options for the readwrite test.
  ReadWriteOpts rwopts;
  rwopts.raw_list = raw_list;
  rwopts.chaos_monkeys    =   10;
  rwopts.list_count       = 1000;
  rwopts.list_floor       =  500;
  rwopts.reads_per_worker = 5000;
  rwopts.sleep_delay      =  123;
  rwopts.worker_count     = 5000;
  // This test can use extended test options (-X).  Read them here, if provided.
  if(opts.extended_test_options != NULL && strcmp(opts.extended_test_options, "") != 0) {
    printf("Extended options were found; updating test values with options specified: %s\n", opts.extended_test_options);
    char *token = NULL;
    token = strtok(opts.extended_test_options, ","); if(token != NULL) rwopts.chaos_monkeys    = atoi(token);
    token = strtok(NULL, ",");                       if(token != NULL) rwopts.list_count       = atoi(token);
    token = strtok(NULL, ",");                       if(token != NULL) rwopts.list_floor       = atoi(token);
    token = strtok(NULL, ",");                       if(token != NULL) rwopts.reads_per_worker = atoi(token);
    token = strtok(NULL, ",");                       if(token != NULL) rwopts.sleep_delay      = atoi(token);
    token = strtok(NULL, ",");                       if(token != NULL) rwopts.worker_count     = atoi(token);
    if (rwopts.chaos_monkeys    == 0 || rwopts.list_count  == 0 || rwopts.list_floor   == 0 ||
        rwopts.reads_per_worker == 0 || rwopts.sleep_delay == 0 || rwopts.worker_count == 0)
      show_error(E_GENERIC, "One or more of the extended options passed in ended up 0, this means you sent a 0 or bad input:\n"
                            "Chaos Monkeys: %d\nList Count: %d\nList Floor: %d\nReads Per Worker: %d\nSleep Delay: %d\nWorker Count: %d",
                            rwopts.chaos_monkeys, rwopts.list_count, rwopts.list_floor, rwopts.reads_per_worker, rwopts.sleep_delay, rwopts.worker_count);
    if(rwopts.list_floor < rwopts.chaos_monkeys)
      show_error(E_GENERIC, "The list floor (%d) cannot be lower than chaos monkeys (%d) because of race conditions.", rwopts.list_floor, rwopts.chaos_monkeys);
    if(rwopts.list_count <= rwopts.list_floor)
      show_error(E_GENERIC, "The list count (%d) cannot be less than or equal to the list floor (%d)", rwopts.list_count, rwopts.list_floor);
  }
  raw_list->max_size = 100 * 1024 * 1024;
  Buffer *temp = NULL, *current = NULL;
  char *sample_data = "some text, hooray for me";
  // Change the lock count so it's reasonable.
  opts.max_locks = rwopts.list_count / 2;
  lock__initialize();

  // Create LIST_COUNT buffers with some data in them.
  for (bufferid_t i=1; i<=rwopts.list_count; i++) {
    temp = buffer__initialize(i, NULL);
    temp->data = malloc(strlen(sample_data) + 1);
    strcpy(temp->data, sample_data);
    temp->data_length = strlen(temp->data) + 1;
    list__add(raw_list, temp);
  }

  // Start worker threads which will try to read data at the same time.
  pthread_t workers[rwopts.worker_count];
  for (int i=0; i<rwopts.worker_count; i++)
    pthread_create(&workers[i], NULL, (void *) &tests__read, &rwopts);
sleep(3);
printf("Rawr\n");
  // Start up a chaos monkeys for insanity.
  pthread_t chaos_monkeys[rwopts.chaos_monkeys];
  for (int i=0; i<rwopts.chaos_monkeys; i++)
    pthread_create(&chaos_monkeys[i], NULL, (void *) &tests__chaos, &rwopts);

  // Wait for them to finish.
  for (int i=0; i<rwopts.worker_count; i++)
    pthread_join(workers[i], NULL);
  for (int i=0; i<rwopts.chaos_monkeys; i++)
    pthread_join(chaos_monkeys[i], NULL);

  int has_failures = 0;
  current = raw_list->head;
  while(current->next != raw_list->head) {
    current = current->next;
    if (current->ref_count != 0) {
      printf("Buffer ID number %d has non-zero ref_count: %d  (lock_id %d)\n", current->id, current->ref_count, current->lock_id);
      has_failures++;
    }
  }
  if (has_failures > 0)
    show_error(E_GENERIC, "Test 'synchronized_readwrite' has failures :(\n");
  if (raw_list->count > rwopts.list_floor || raw_list->count < (rwopts.list_floor - rwopts.chaos_monkeys))
    show_error(E_GENERIC, "Test 'synchronized_readwrite' didn't reduce the list count (%d) to LIST_FLOOR (%d) as expected.", raw_list->count, rwopts.list_floor);

  setlocale(LC_NUMERIC, "");
  printf("All done.  I used %d workers performing a combined %'d reads with %d chaos workers taking buffers from %d to %d (true final count is %d, due to known race condition with chaos workers)\n", rwopts.worker_count, rwopts.worker_count * rwopts.reads_per_worker, rwopts.chaos_monkeys, rwopts.list_count, rwopts.list_floor, raw_list->count);

  printf("Test 'synchronized_readwrite': all passed\n");
  return;
}
void tests__read(ReadWriteOpts *rwopts) {
  // Pick random buffers in the list and pin them for reading.  The only way to pick randomly from a list that shrinks is to search
  // over the list itself, which is kinda magoo but it suits the purpose.
  srand(time(0));
  int rv = 0;
  bufferid_t id_to_get = 0;
  Buffer *selected = NULL;
printf("whee\n");
  for (int i=0; i<rwopts->reads_per_worker; i++) {
    for(;;) {
      id_to_get = rand() % rwopts->list_count;
printf("About to search for %"PRIu32"\n", id_to_get);
      rv = list__search(rwopts->raw_list, &selected, id_to_get);
printf("Done searching.\n");
      if (rv == E_OK)
        break;
      if (rv == E_BUFFER_NOT_FOUND || rv == E_BUFFER_POOFED || E_BUFFER_IS_VICTIMIZED)
        continue;
      printf("We should never hit this (rv is %d).\n", rv);
    }
    usleep(rand() % rwopts->sleep_delay);  // This just emulates some random time the reader will use this buffer.
printf("1\n");
    rv = buffer__lock(selected);
    if (rv == E_OK || rv == E_BUFFER_IS_VICTIMIZED) {
      buffer__update_ref(selected, -1);
      buffer__unlock(selected);
    }
  }
  pthread_exit(0);
}
void tests__chaos(ReadWriteOpts *rwopts) {
  // Remove a buffer from the list every so often until we're down to LIST_FLOOR, just because.
  Buffer *temp = NULL;
  int rv = 0;
  bufferid_t id_to_remove = 0;
  // Loop through and remove stuff until we reach LIST_FLOOR.  This predicate cannot be (reasonably) made safe.
  while(rwopts->raw_list->count >= rwopts->list_floor) {
    id_to_remove = rand() % rwopts->list_count;
    rv = list__search(rwopts->raw_list, &temp, id_to_remove);
    if (rv == E_BUFFER_NOT_FOUND)
      continue;
    // List search gave us a ref_count, need to decrement ourself.
    buffer__lock(temp);
    buffer__update_ref(temp, -1);
    buffer__unlock(temp);
    rv = list__remove(rwopts->raw_list, id_to_remove);
    usleep(rwopts->sleep_delay);
  }
  pthread_exit(0);
}


/* tests__elements
 * Simply creates buffers and assigns them to the list.  Should update the counts and so forth.
 */
void tests__elements(List *raw_list) {
  Buffer *buf = NULL;
  int rv = E_OK;
  int element_count = 5000;
  if(opts.extended_test_options != NULL && strcmp(opts.extended_test_options, "") != 0) {
    printf("Extended options were found; updating test values with options specified: %s\n", opts.extended_test_options);
    char *token = NULL;
    token = strtok(opts.extended_test_options, ",");
    if(token != NULL)
      element_count = atoi(token);
    if (element_count  == 0)
      show_error(E_GENERIC, "One or more of the extended options passed in ended up 0, this means you sent a 0 or bad input:\n");
  }
  opts.max_memory = BUFFER_OVERHEAD * element_count;
  raw_list->max_size = opts.max_memory;

  // Add all the buffers.
  printf("Step 1.  Adding %d dummy buffers to the list.\n", element_count);
  for(int i=0; i<element_count; i++) {
    buf = buffer__initialize(i, NULL);
    list__add(raw_list, buf);
  }

  // Display the statistics of the list.
  printf("\nStep 2.  Showing list statistics.\n");
  tests__list_structure(raw_list);

  // Search for an item just to prove it works.
  printf("\nStep 3.  Searching for a buffer, just to prove it works.\n");
  buf = NULL;
  rv = list__search(raw_list, &buf, 1);
  if (rv != E_OK)
    show_error(E_GENERIC, "Failed to search for a buffer which should have existed.  rv was %d\n", rv);
  printf("Got the buffer, it's ref count is %"PRIu16".\n", buf->ref_count);
  buffer__lock(buf);
  buffer__update_ref(buf, -1);
  buffer__unlock(buf);

  // Remove the buffers.
  printf("\nStep 4.  Removing all the dummy buffers.\n");
  while(raw_list->head->next != raw_list->head)
    list__remove(raw_list, raw_list->head->next->id);

  // Display the statistics of the list again.
  printf("\nStep 5.  Showing list statistics.\n");
  tests__list_structure(raw_list);

  printf("Test 'elements': All Passed\n");
  return;
}


/*
 * IO either works or doesn't.  You'll get an error on CLI if you mess up options or provide bad data.  This function will simply
 * attempt to read a file into a buffer with buffer__initialize(valid_id, valid_path).  Requires a pointer to the pages array and
 * the page_count.
 */
void tests__io(char *pages[]) {
  int id_to_get = 128;
  while (id_to_get >= opts.page_count && id_to_get != 0)
    id_to_get >>= 1;
  if (id_to_get == 0)
    show_error(E_GENERIC, "The tests__io function reached id_to_get value of 0... are you sure you pointed tyche to a directory with pages?");

  // Looks like we have a valid ID to get.  Let's see if it actually works...
  Buffer *buf = buffer__initialize(id_to_get, pages[id_to_get - 1]);
  printf("Found a buffer and loaded it.  ID is %d, data length is %d, and io_cost is %"PRIu32".\n", buf->id, buf->data_length, buf->io_cost);
  buffer__victimize(buf);
  buffer__destroy(buf);

  printf("Test 'io': All Passed\n");
  return;
}


/*
 * Ensures basic compression and decompression works.  We should NOT read buffers from the disk.  It would be a more comprehensive
 * test but it would preclude us from testing JUST compression, which is the point here.
 */
void tests__compression() {
  // Set up some basic stuff
  void *src = "Lorem ipsum dolor. Sit amet amet mollis vitae posuere egestas iaculis aptent. Ante ac molestie laoreet et ut. "
              "Tristique aptent egestas purus lorem mattis. Pharetra ultricies risus. Eget scelerisque augue. Fames iaculis donec. "
              "Pellentesque donec tristique at libero vulputate metus morbi lectus. Eu quam in nibh tellus wisi. At aliquam "
              "sagittis aenean sit accumsan. Cupidatat gravida facilisis gravida imperdiet inceptos lacus ultricies dignissim "
              "fringilla nunc sed magna mollis quisque purus semper tempor. A velit in suspendisse curabitur ut sollicitudin mi "
              "adipiscing. A pellentesque sociosqu exercitationem sit pede. Vestibulum sed sed. Odio nulla lectus. Convallis quam "
              "egestas magnis est sit. Metus porta tellus. Scelerisque sollicitudin auctor non dictum dolor condimentum ipsum "
              "adipiscing ornare a nec. Tempor felis urna. Placerat proin elementum rem litora praesent ut semper et. Eleifend ac "
              "vel nam praesent mauris libero non conubia quis id gravida. Vel est augue. Vel non lacus magna lorem nisl diam sit "
              "eu. Vestibulum placerat molestie congue bibendum vulputate metus ultrices sollicitudin. Convallis arcu mollitia. "
              "Purus id magna. Volutpat erat vitae mi donec id nonummy et pellentesque. Orci ipsum diam. Sollicitudin congue "
              "viverra. Erat massa elit commodo dui mi sit purus convallis enim diam magna varius tempor consectetuer ac amet "
              "nulla et ultrices in in ut est ac leo orci et mauris volutpat suspendisse exercitation diam. Eros aenean pulvinar. "
              "Maecenas interdum aliquam. Ante sapien donec donec nam sit lectus phasellus nullam vestibulum lacus non. Aliquam "
              "blandit nulla. Duis et gravida velit quam nascetur turpis nam in. Eleifend ac sodales sed nisl integer sollicitudin "
              "mauris orci aliquam duis mauris. Posuere dui nec. Nunc tristique nascetur. Dignissim velit malesuada tincidunt cras "
              "morbi morbi at mauris. Eget nec erat. Sed euismod faucibus. Accumsan fermentum eget. Nec faucibus curabitur pede "
              "dictum morbi. Dapibus erat reprehenderit. Natoque corporis cras in risus nam. Ac rhoncus in eros feugiat eros aut "
              "fames magna malesuada maecenas amet. Integer aliquam erat nisl neque amet. Ac dolor tellus dolor at perferendis. "
              "Risus dolor ultricies. Justo at ultrices aenean tempus magna amet ac eget at turpis in. Nunc habitant blandit et "
              "purus semper. Adipiscing voluptatem porttitor. Sapien elementum justo pellentesque duis ligula lorem ultrices "
              "ultricies. Nam lectus euismod ultricies praesent et error sed eleifend. Con dolor morbi. Aenean est sit. Nec "
              "interdum nonummy eu leo sit. Pellentesque consequat morbi. Lacus augue vitae. Et sit wisi. Dolor ante placerat. "
              "Netus commodo proin. Gravida facilisis bibendum metus non eget. Consectetuer libero eu sit vehicula turpis "
              "suspendisse wisi netus. Aenean massa faucibus neque sodales urna. In dictum aliquam. Amet suscipit neque. In "
              "auctor lectus purus sagittis vestibulum tristique vestibulum et. Condimentum leo at. Lorem ante scelerisque. At "
              "non maecenas totam risus nibh. Massa lorem venenatis torquent gravida libero. Elementum id placerat. Nulla "
              "suspendisse dolore. Tellus necessitatibus vitae praesent sed per. Pellentesque egestas rhoncus aenean aliquam "
              "molestie. Arcu sociis tincidunt nisl consequat semper magna quisque justo. Feugiat condimentum eget nec sed "
              "iaculis et mi facilisis dis sit cursus odio pellentesque mauris volutpat orci massa dolor eget et. Pede nulla "
              "urna. Sollicitudin sodales nisl. Eget ut nulla. A id vestibulum. Egestas ac risus. In iaculis nascetur fermentum "
              "sociosqu cras rutrum vestibulum vehicula a aliquam rhoncus ornare nulla et. Mauris sed condimentum. Sem ut felis "
              "sed per enim malesuada magna quam. Con maecenas dui. Habitasse feugiat purus. Volutpat lectus orci. Est etiam "
              "justo. Odio nec consequat. Posuere et et. Condimentum nunc faucibus viverra amet sapien urna iste eget suspendisse "
              "eget quam. Vulputate nullam porta tortor quis aenean. Ut blandit augue. Nulla ligula quam. Non in mauris. Egestas "
              "laoreet tincidunt. Aliquam duis at congue lacus est nascetur velit lorem proin neque egestas. Tortor vitae "
              "bibendum. Parturient neque lacus. Pede a ultrices leo lacus vivamus.";
  int src_size = strlen(src);
  int dst_max_size = LZ4_compressBound(src_size);
  void *dst = (void *)malloc(dst_max_size);
  int rv = 0;
  void *new_src = (void *)malloc(src_size);

  /* Test 1:  make sure the LZ4_* functions can compress and decompress data. */
  rv = LZ4_compress_default(src, dst, src_size, dst_max_size);
  if (rv < 1)
    show_error(E_GENERIC, "The rv was negative when attempting basic compression of a void* dataset: %d\n", rv);
  rv = LZ4_decompress_fast(dst, new_src, src_size);
  if (rv < 0)
    show_error(E_GENERIC, "The rv was negative when decompressing a char pointer, indicating an error: %d\n", rv);
  if (memcmp(src, new_src, src_size) != 0)
    show_error(E_GENERIC, "src and new_src don't match from test 1.\n");
  printf("Test 1: passed\n");

  /* Test 2:  the LZ4 compression function needs to return the size of the compressed data. */
  // This is automatically done by the function as noted by rv above.
  printf("Test 2: passed\n");

  /* Test 3:  The compression and decompression functions should work on a buffer->data element. */
  Buffer *buf = buffer__initialize(205, NULL);
  buf->data = (void *)malloc(src_size);
  buf->data_length = src_size;
  memcpy(buf->data, src, buf->data_length);
  rv = LZ4_compress_default(src, buf->data, buf->data_length, dst_max_size);
  if (rv < 0)
    show_error(E_GENERIC, "The rv was negative when compressing a buffer element, indicating an error: %d\n", rv);
  memset(new_src, 0, src_size);
  rv = LZ4_decompress_fast(buf->data, new_src, buf->data_length);
  if (rv < 0)
    show_error(E_GENERIC, "The rv was negative when decompressing a buffer element, indicating an error: %d\n", rv);
  if (memcmp(src, new_src, src_size) != 0)
    show_error(E_GENERIC, "src and new_src don't match from test 3.\n");
  printf("Test 3: passed\n");

  /* Test 4:  Same as test 3, but we'll use buffer__compress and buffer__decompress.  Should get comp_time updated. */
  free(buf->data);
  buf->data = (void *)malloc(src_size);
  buf->data_length = src_size;
  memcpy(buf->data, src, buf->data_length);
  rv = buffer__compress(buf);
  if (rv != 0)
    show_error(E_GENERIC, "The rv was non-zero, indicating an error from buffer__compress: %d\n", rv);
  printf("Compression gave an OK response.    comp_time is %d ns, comp_hits is %d, data_legnth is %d, and comp_length is %d bytes\n", buf->comp_cost, buf->comp_hits, buf->data_length, buf->comp_length);
  memset(new_src, 0, src_size);
  rv = buffer__decompress(buf);
  if (rv != 0)
    show_error(E_GENERIC, "The rv was non-zero, indicating an error from buffer__decompress: %d\n", rv);
  if (memcmp(src, buf->data, src_size) != 0)
    show_error(E_GENERIC, "src and new_src don't match from test 4.\n");
  printf("Decompression gave an OK response.  comp_time is %d ns, comp_hits is %d, data_legnth is %d, and comp_length is %d bytes\n", buf->comp_cost, buf->comp_hits, buf->data_length, buf->comp_length);
  printf("Test 4: passed\n");

  /* All Done */
  printf("Test 'compression': all passed!\n");

  return;
}


/*
 * Make sure that we can create a list, try to put too many buffers in it, and have it offload things as expected.
 */
void tests__move_buffers(List *raw_list, char *pages[]) {
  raw_list->sweep_goal = 30;
  Buffer *buf = NULL;
  uint total_bytes = 0;

  // -- TEST 1:  Do sizes match up like they're supposed to when moving items into a list?
  /* Figure out how much data we have in the pages[] elements' files. */
  for (uint i = 0; i < opts.page_count; i++) {
    buf = buffer__initialize(i, pages[i]);
    total_bytes += buf->data_length + BUFFER_OVERHEAD;
    buffer__victimize(buf);
    buffer__destroy(buf);
  }
  /* Now add them all to the list and see if the list size matches.  Give padding, because we want to avoid offloading for now. */
  raw_list->max_size = total_bytes + (1024 * 1024);
  for (uint i = 0; i < opts.page_count; i++) {
    buf = buffer__initialize(i, pages[i]);
    list__add(raw_list, buf);
  }
  if (total_bytes != raw_list->current_size)
    show_error(E_GENERIC, "Calculated a total size of %d, and raw_list->current_size is %"PRIu64"\n", total_bytes, raw_list->current_size);
  printf("Total bytes measured in buffers matches the list size, success!\n");
  while(raw_list->head->next != raw_list->head)
    list__remove(raw_list, raw_list->head->next->id);
  while(raw_list->offload_to->head->next != raw_list->offload_to->head)
    list__remove(raw_list->offload_to, raw_list->offload_to->head->next->id);
  printf("Test 1 Passed:  Does the list size match the known size of data read from disk?\n\n");

  // -- TEST 2:  Can we offload from raw to a compressed list?
  /* Alright, let's purposely set the list to a value smaller than we know we need and ensure offloading happens. */
  raw_list->max_size = total_bytes >> 1;
  raw_list->offload_to->max_size = total_bytes;
  for (uint i = 0; i < opts.page_count; i++) {
    buf = buffer__initialize(i, pages[i]);
    buf->popularity = MAX_POPULARITY/(i+1);
    list__add(raw_list, buf);
  }
  printf("All done.  Raw list has %d buffers using %"PRIu64" bytes.  Comp list has %d buffers using %"PRIu64" bytes.\n", raw_list->count, raw_list->current_size, raw_list->offload_to->count, raw_list->offload_to->current_size);
  while(raw_list->head->next != raw_list->head)
    list__remove(raw_list, raw_list->head->next->id);
  while(raw_list->offload_to->head->next != raw_list->offload_to->head)
    list__remove(raw_list->offload_to, raw_list->offload_to->head->next->id);
  printf("Test 2 Passed:  Will items overflow from the raw list to the compressed one as needed?\n\n");

  // -- TEST 3:  Will offloading properly pop the compressed buffer?
  /* Now let's do the same test again, but shrink the comp_list to ensure data has to be popped off the end. */
  raw_list->max_size = total_bytes >> 1;
  raw_list->offload_to->max_size = total_bytes >> 3;
  for (uint i = 0; i < opts.page_count; i++) {
    buf = buffer__initialize(i, pages[i]);
    buf->popularity = MAX_POPULARITY/(i+1);
    list__add(raw_list, buf);
  }
  printf("All done.  Raw list has %d buffers using %"PRIu64" bytes.  Comp list has %d buffers using %"PRIu64" bytes.\n", raw_list->count, raw_list->current_size, raw_list->offload_to->count, raw_list->offload_to->current_size);
  while(raw_list->head->next != raw_list->head)
    list__remove(raw_list, raw_list->head->next->id);
  while(raw_list->offload_to->head->next != raw_list->offload_to->head)
    list__remove(raw_list->offload_to, raw_list->offload_to->head->next->id);
  printf("Test 3 Passed:  Will the compressed list pop buffers when out of room?\n\n");

  // -- TEST 4:  Can we move items back to the raw list after they've been compressed?
  raw_list->max_size = total_bytes >> 1;
  raw_list->offload_to->max_size = total_bytes >> 3;
  for (uint i = 0; i < opts.page_count; i++) {
    buf = buffer__initialize(i, pages[i]);
    buf->popularity = MAX_POPULARITY/(i+1);
    list__add(raw_list, buf);
  }
  /* Pick a random buffer from the offload list and do a search for it.  The result should be it getting moved to the raw list. */
  if (raw_list->offload_to->count == 0)
    show_error(E_GENERIC, "The comp_list doesn't have anything in the pool.  Can't do this test.");
  Buffer *test4_buf = NULL, *test4_current = NULL;
  bufferid_t test4_sample_id = raw_list->offload_to->head->next->id;
  printf("About to start searching for a buffer which should be found in comp list.\n");
  if (list__search(raw_list, &test4_buf, test4_sample_id) != E_OK)
    show_error(E_GENERIC, "list__search says it failed to find our buffer.  Boo.");
  printf("The list search buffer returned gave id of %d, the id we wanted was %d.\n", test4_buf->id, test4_sample_id);
  int found_in_raw = 0;
  test4_current = raw_list->head;
  while(test4_current->next != raw_list->head) {
    test4_current = test4_current->next;
    if(test4_current->id == test4_sample_id)
      found_in_raw++;
  }
  if (found_in_raw == 0)
    show_error(E_GENERIC, "The id we searched for didn't show up in the raw list... this means we failed to restore it.");
  if (found_in_raw > 1)
    show_error(E_GENERIC, "The found_in_raw variable is higher than 1, this is bad.\n");
  printf("Test 4 Passed:  Can we restore items from the offload list when searching finds them there?\n\n");

  printf("Test 'move_buffers': all passed\n");
  return;
}


/* tests__options
 * Simple test to make sure options get set correctly.  I'm not sure this will ever be useful.
 */
void tests__options() {
  // Just print them out to show what they ended up looking like.
  printf("opts->page_directory = %s\n",        opts.page_directory);
  printf("opts->page_count     = %"PRIu32"\n", opts.page_count);
  printf("opts->page_limit     = %"PRIu32"\n", opts.page_limit);
  printf("opts->smallest_page  = %"PRIu16"\n", opts.smallest_page);
  printf("opts->biggest_page   = %"PRIu16"\n", opts.biggest_page);
  printf("opts->dataset_size   = %"PRIu64"\n", opts.dataset_size);
  printf("opts->dataset_max    = %"PRIu64"\n", opts.dataset_max);
  /* Resource Control */
  printf("opts->max_memory     = %"PRIu64"\n", opts.max_memory);
  printf("opts->fixed_ratio    = %"PRIi8"\n",  opts.fixed_ratio);
  printf("opts->workers        = %"PRIu16"\n", opts.workers);
  /* Test Management */
  printf("opts->duration       = %"PRIu16"\n", opts.duration);
  printf("opts->hit_ratio      = %"PRIi8"\n",  opts.hit_ratio);

  return;
}

/* tests__list_structure
 * Spits out a bunch of information about a list.  Mostly for debugging.  Might delete later.
 */
void tests__list_structure(List *list) {
  // List attributes
  /* Size and Counter Members */
  printf("          count: %"PRIu32"\n", list->count);
  printf("   current_size: %"PRIu64"\n", list->current_size);
  printf("       max_size: %"PRIu64"\n", list->max_size);
  /* Locking, Reference Counters, and Similar Members */
  printf("      ref_count: %"PRIu32"\n", list->ref_count);
  printf("pending_writers: %"PRIu8"\n", list->pending_writers);
  /* Management and Administration Members */
  printf("     sweep_goal: %"PRIu8"\n", list->sweep_goal);
  printf("         sweeps: %"PRIu32"\n", list->sweeps);
  printf("     sweep_cost: %"PRIu64"\n", list->sweep_cost);
  /* Management of Nodes for Skiplist and Buffers */
  printf("         levels: %"PRIu8"\n", list->levels);

  // Skiplist Index information.
  int count = 0, out_of_order = 0;
  SkiplistNode *slnode = NULL;
  for(int i=0; i<list->levels; i++) {
    count = 0;
    out_of_order = 0;
    slnode = list->indexes[i];
    while(slnode->right != NULL) {
      slnode = slnode->right;
      count++;
      if((slnode->right != NULL) && (slnode->target->id >= slnode->right->target->id))
        out_of_order++;
    }
    printf("Index %02d:  in order - %s, count %d (%3.1f%%)\n", i, out_of_order == 0 ? "yes" : "no", count, 100 * (double)count/list->count);
  }
  printf("Indexes %02d - %02d are all 0 / 0.0%%\n", list->levels, SKIPLIST_MAX);

}
