/*
 * grr.c
 *
 *  Created on: Aug 23, 2015
 *      Author: Kyle Harper
 */


#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>  /* Used for the uint_ types */
#include <assert.h>


typedef uint32_t bufferid_t;
typedef uint16_t removal_index_t;
typedef struct buffer Buffer;
struct buffer {
  /* Attributes for typical buffer organization and management. */
  bufferid_t id;                  /* Identifier of the page. Should come from the system providing the data itself (e.g.: inode). */
  uint16_t ref_count;             /* Number of references currently holding this buffer. */
  uint8_t popularity;             /* Rapidly decaying counter used for victim selection with clock sweep.  Ceiling of MAX_POPULARITY. */
  uint8_t victimized;             /* If the buffer has been victimized this is set non-zero.  Prevents incrementing of ref_count. */
  uint16_t lock_id;               /* Lock ID from the locker_pool[], rather than having a pthread mutex & cond for each Buffer. */
  removal_index_t removal_index;  /* When a buffer is victimized we need to compare it's removal index (higher == newer/fresher). */

  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  uint32_t comp_cost;             /* Time spent, in ns, to compress and decompress a page during a polling period.  Using clock_gettime(3) */
  uint32_t io_cost;               /* Time spent, in ns, to read this buffer from the disk.  Using clock_gettime(3) */
  uint16_t comp_hits;             /* Number of times reclaimed from the compressed table during a polling period. */

  /* The actual payload we want to cache (i.e.: the page). */
  uint16_t data_length;           /* Number of bytes in data.  For raw tables, always PAGE_SIZE.  Compressed will vary. */
  char *data;                     /* Pointer to the character array holding the page data. */
};
typedef struct list List;
struct list {
  uint32_t count;                   /* Number of buffers in the list. */
  pthread_mutex_t lock;             /* For operations requiring exclusive locking of the list (writing to it). */
  //pthread_cond_t writer_condition;  /* The condition variable for writers to wait for when attempting to drain a list of refs. */
  //pthread_cond_t reader_condition;  /* The condition variable for readers to wait for when attempting to increment ref count. */
  uint32_t ref_count;               /* Number of threads pinning this list (searching it) */
  uint8_t pending_writers;          /* Value to indicate how many writers are waiting to edit the list. */
  Buffer *pool[1];   /* Array of pointers to Buffers since we're avoiding the linked list. */
};


/* Prototypes because reasons */
void starting_routine(List *list);
void break_crap(List *list, Buffer **buf);

int main() {
  const int WORKERS = 5;

  List *list = (List *)malloc(sizeof(List));
  list->count = 0;
  list->ref_count = 0;
  list->pending_writers = 0;
  pthread_mutex_init(&list->lock, NULL);

  Buffer *new_buffer = (Buffer *)malloc(sizeof(Buffer));
  new_buffer->id = 123;
  new_buffer->lock_id = 456;
  new_buffer->ref_count = 0;
  new_buffer->removal_index = 0;
  new_buffer->comp_cost = 0;
  new_buffer->comp_hits = 0;
  new_buffer->victimized = 0;
  new_buffer->popularity = 0;
  new_buffer->data_length = 0;
  new_buffer->io_cost = 0;
  new_buffer->removal_index = 0;
  new_buffer->data = NULL;
  new_buffer->data = "some text, hooray for me";

  list->pool[0] = new_buffer;
  printf("list->pool[0] has address %d\n", &list->pool[0]);

  pthread_t workers[WORKERS];
  for (int i=0; i<WORKERS; i++)
    pthread_create(&workers[i], NULL, (void *) &starting_routine, list);

  for (int i=0; i<WORKERS; i++)
    pthread_join(workers[i], NULL);

  printf("All done.\n");
}


void starting_routine(List *list) {
  // Starting point a thread will use.
  Buffer *local_buf_ptr;
  printf("%d : Thread starting up.  local_buf_ptr is currently %d, ", pthread_self(), local_buf_ptr);
  local_buf_ptr = &list->pool[0];
  printf("and is now %d\n", local_buf_ptr);
  break_crap(list, local_buf_ptr);
  pthread_exit(0);
}


void break_crap(List *list, Buffer **buf) {
  // Emulate the buffer removal.
  pthread_mutex_lock(&list->lock);
  printf("%d : checking to see if *buf (ptr address %d) is null\n", pthread_self(), *buf);
  sleep(1);
  if (*buf == NULL) {
    printf("%d : *buf is null so I'm leaving.\n", pthread_self());
    pthread_mutex_unlock(&list->lock);
    return;
  }
  printf("%d : *buf is not null and has ID=%d and lock_id=%d, free()ing and NULL-ing\n", pthread_self(), (*buf)->id, (*buf)->lock_id);
  free(*buf);
  *buf = NULL;
  assert(*buf == NULL);
  pthread_mutex_unlock(&list->lock);
  return;
}
