/*
 * list.c
 *
 *  Created on: Jun 19, 2015
 *      Author: Kyle Harper
 * Description: Builds the buffer lists and functions for them.
 *
 *              Clock sweep is the method this implementation uses for victim selection in the raw buffer list.
 */

/* Include necessary headers here. */
#include <pthread.h>
#include <jemalloc/jemalloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h> /* For bool types. */
#include <assert.h>
#include <inttypes.h>
#include <time.h>      /* for clock_gettime() */
#include <math.h>
#include <errno.h>
#include "buffer.h"
#include "error.h"
#include "list.h"


#include <locale.h> /* Remove me after debugging... probably */
#include <unistd.h> //Also remove after debug.


/* We need to know what one billion is for clock timing. */
#define BILLION 1000000000L
#define MILLION    1000000L

/* Specify the default raw ratio to start with. */
#define INITIAL_RAW_RATIO   80    // 80%
/* Specify the default CoW ratio and defaults. */
#define INITIAL_COW_RATIO    5    //  %
#define COW_NAP_TIME         3    //  seconds



/* Extern the error codes we'll use. */
extern const int E_OK;
extern const int E_GENERIC;
extern const int E_BUFFER_NOT_FOUND;
extern const int E_BUFFER_ALREADY_EXISTS;
extern const int E_BUFFER_ALREADY_COMPRESSED;
extern const int E_BUFFER_ALREADY_DECOMPRESSED;
extern const int E_BUFFER_COMPRESSION_PROBLEM;
extern const int E_BUFFER_MISSING_A_PIN;
extern const int E_BUFFER_IS_DIRTY;
extern const int E_LIST_CANNOT_BALANCE;
extern const int E_LIST_REMOVAL;
// No mem errors.
extern const int E_NO_MEMORY;
extern const int E_BAD_ARGS;
// Warnings


/* Store the overhead of a Buffer.  Since our skiplist will have 1 node per buffer (probabilistically) we include that. */
const int BUFFER_OVERHEAD = sizeof(Buffer) + sizeof(SkiplistNode);

/* We use the have/don't have data flags. */
extern const int HAVE_PIN;
extern const int NEED_PIN;

// Create a global tracker for compressor IDs.  This is because I can't have circular things:
// struct list { ... Compressor *compressors; }
// struct compressor { ... List *list; }
int next_compress_worker_id = 0;



/*
 * +--------------------+
 * | Core Functionality |
 * +--------------------+
 */

/* list__initialize
 * Creates the actual list that we're being given a pointer to.  We will also create the head of it as a reference point.
 */
int list__initialize(List **list, int compressor_count, int compressor_id, int compressor_level, uint64_t max_memory) {
  /* Quick error checking, then initialize the list.  We don't need to lock it because it's synchronous. */
  int rv = E_OK;
  *list = (List *)malloc(sizeof(List));
  if (*list == NULL)
    return E_NO_MEMORY;

  /* Size and Counter Members */
  (*list)->raw_count = 0;
  (*list)->comp_count = 0;
  (*list)->current_raw_size = 0;
  (*list)->max_raw_size = 0;
  (*list)->current_comp_size = 0;
  (*list)->max_comp_size = 0;
  list__balance(*list, INITIAL_RAW_RATIO, max_memory);

  /* Locking, Reference Counters, and Similar Members */
  if (pthread_mutex_init(&(*list)->lock, NULL) != 0)
    return E_GENERIC;
  (*list)->lock_owner = 0;
  (*list)->lock_depth = 0;
  if (pthread_cond_init(&(*list)->writer_condition, NULL) != 0)
    return E_GENERIC;
  if (pthread_cond_init(&(*list)->reader_condition, NULL) != 0)
    return E_GENERIC;
  if (pthread_cond_init(&(*list)->sweeper_condition, NULL) != 0)
    return E_GENERIC;
  (*list)->ref_count = 0;
  (*list)->pending_writers = 0;

  /* Management and Administration Members */
  (*list)->active = 1;
  pthread_create(&(*list)->sweeper_thread, NULL, (void *) &list__sweeper_start, (*list));
  (*list)->sweep_goal = 5;
  (*list)->sweeps = 0;
  (*list)->sweep_cost = 0;
  (*list)->restorations = 0;
  (*list)->compressions = 0;
  (*list)->evictions = 0;

  /* Head Nodes of the List and Skiplist (Index). Make the Buffer list head a dummy buffer. */
  rv = buffer__initialize(&(*list)->head, BUFFER_ID_MAX, 0, (void*)0, NULL);
  if (rv != E_OK)
    return rv;
  (*list)->head->next = (*list)->head;
  (*list)->clock_hand = (*list)->head;
  (*list)->levels = 1;
  SkiplistNode *slnode = NULL;
  for(int i=0; i<SKIPLIST_MAX; i++) {
    rv = list__initialize_skiplistnode(&slnode, (*list)->head);
    if (rv != E_OK)
      return rv;
    if(i != 0)
      slnode->down = (*list)->indexes[i-1];
    // Assign it to the correct index.
    (*list)->indexes[i] = slnode;
  }

  /* Compressor Pool Management */
  pthread_mutex_init(&(*list)->jobs_lock, NULL);
  pthread_cond_init(&(*list)->jobs_cond, NULL);
  pthread_cond_init(&(*list)->jobs_parent_cond, NULL);
  (*list)->compressor_threads = calloc(compressor_count, sizeof(pthread_t));
  if((*list)->compressor_threads == NULL)
    return E_NO_MEMORY;
  for(int i=0; i<VICTIM_BATCH_SIZE; i++)
    (*list)->comp_victims[i] = NULL;
  (*list)->comp_victims_index = 0;
  for(int i=0; i<VICTIM_BATCH_SIZE; i++)
    (*list)->victims[i] = NULL;
  (*list)->victims_index = 0;
  (*list)->victims_compressor_index = 0;
  (*list)->active_compressors = 0;
  (*list)->compressor_pool = calloc(compressor_count, sizeof(Compressor));
  if((*list)->compressor_pool == NULL)
    return E_NO_MEMORY;
  for(int i=0; i<compressor_count; i++) {
    (*list)->compressor_pool[i].jobs_cond = &(*list)->jobs_cond;
    (*list)->compressor_pool[i].jobs_lock = &(*list)->jobs_lock;
    (*list)->compressor_pool[i].jobs_parent_cond = &(*list)->jobs_parent_cond;
    (*list)->compressor_pool[i].active_compressors = &(*list)->active_compressors;
    (*list)->compressor_pool[i].runnable = 0;
    (*list)->compressor_pool[i].victims = (*list)->victims;
    (*list)->compressor_pool[i].victims_index = &(*list)->victims_index;
    (*list)->compressor_pool[i].victims_compressor_index = &(*list)->victims_compressor_index;
    (*list)->compressor_pool[i].compressor_id = compressor_id;
    (*list)->compressor_pool[i].compressor_level = compressor_level;
    pthread_create(&(*list)->compressor_threads[i], NULL, (void*) &list__compressor_start, (*list));
  }
  (*list)->compressor_id = compressor_id;
  (*list)->compressor_level = compressor_level;
  (*list)->compressor_count = compressor_count;

  /* Copy-On-Write Space (of Buffers) */
  (*list)->cow_max_size = INITIAL_COW_RATIO * max_memory / 100;
  pthread_mutex_init(&(*list)->cow_lock, NULL);
  pthread_cond_init(&(*list)->cow_killer_cond, NULL);
  pthread_cond_init(&(*list)->cow_waiter_cond, NULL);
  buffer__initialize(&(*list)->cow_head, BUFFER_ID_MAX, 0, NULL, NULL);
  (*list)->cow_head->next = (*list)->cow_head;
  pthread_create(&(*list)->slaughter_house_thread, NULL, (void *) &list__slaughter_house, (*list));

  return E_OK;
}


/* list__initialize_skiplistnode
 * Simply builds an empty skiplist node.
 */
int list__initialize_skiplistnode(SkiplistNode **slnode, Buffer *buf) {
  *slnode = (SkiplistNode *)malloc(sizeof(SkiplistNode));
  if(*slnode == NULL)
    return E_NO_MEMORY;
  (*slnode)->down = NULL;
  (*slnode)->right = NULL;
  (*slnode)->target = buf;
  (*slnode)->buffer_id = buf->id;
  return E_OK;
}


/* list__acquire_write_lock
 * Drains the list of references so we don't modify the list while another thread is scanning it.  The combination of conditions
 * and mutexes should provide perfect consistency/synchronization.  Readers all use list__update_ref which respects these same
 * precepts.
 */
int list__acquire_write_lock(List *list) {
  /* Check to see if the current lock owner is us; this is free of race conditions because it's only a race when we own it... */
  if (pthread_equal(list->lock_owner, pthread_self())) {
    list->lock_depth++;
    return E_OK;
  }

  /* Block until we get the list lock, then we can safely update pending writers. */
  pthread_mutex_lock(&list->lock);
  if (list->lock_depth != 0)
    return E_GENERIC;
  list->pending_writers++;
  /* Begin a predicate check while under the protection of the mutex.  Block if the predicate remains true. */
  while(list->ref_count != 0)
    pthread_cond_wait(&list->writer_condition, &list->lock);
  /* We now have the lock again and our predicate is guaranteed protected (it respects the list lock). */
  list->pending_writers--;
  /* Set ourself to the lock owner in case future paths function calls try to ensure this thread has the list locked. */
  list->lock_owner = pthread_self();
  list->lock_depth++;
  return E_OK;
}


/* list__release_write_lock
 * Unlocks the mutex for a list.  Begin the broadcast chain for either writesr or readers, depending on the predicate others are
 * blocking on.  Readers all use list__update_ref which respects these same precepts.
 */
int list__release_write_lock(List *list) {
  /* Decrement the lock depth.  If the lock depth is more than 0 we aren't done with the lock yet. */
  list->lock_depth--;
  if (list->lock_depth != 0)
    return E_OK;

  /* Determine if we need to notify the readers, avoids spurious wake ups. */
  if(list->pending_writers == 0)
    pthread_cond_broadcast(&list->reader_condition);

  /* Remove ourself as the lock owner so another can take our place.  Don't have to... just NULL-ing for safety. */
  list->lock_owner = 0;
  /* Release the lock now that the proper broadcast is out there.  Releasing AFTER a broadcast is supported and safe. */
  pthread_mutex_unlock(&list->lock);
  return E_OK;
}


/* list__add
 * Adds a node to the list specified.  Buffer must be created by caller.  We use localized (buffer) locking to aid with insertion
 * performance, but it's a minor improvement.  The real benefit is that readers can continue searching (list__search()) while this
 * function is running.
 */
int list__add(List *list, Buffer *buf, uint8_t list_pin_status) {
  /* Initialize a few basic values. */
  int rv = E_OK;
  int slnode_rv = E_OK;

  /* Grab the list lock so we can handle sweeping processes and signaling correctly.  Small race will allow exceeding max, but that's ok. */
  if(list->current_raw_size > list->max_raw_size) {
    // We're about to wake up the sweeper, which means we need to remove this threads list pin if the caller has one.
    if(list_pin_status == HAVE_PIN)
      list__update_ref(list, -1);
    pthread_mutex_lock(&list->lock);
    while(list->current_raw_size > list->max_raw_size) {
      pthread_cond_broadcast(&list->sweeper_condition);
      pthread_cond_wait(&list->reader_condition, &list->lock);
    }
    pthread_mutex_unlock(&list->lock);
    // Now put this threads list pin back in place, making the caller never-aware it lost it's pin; if applicable.
    if(list_pin_status == HAVE_PIN)
      list__update_ref(list, 1);
  }

  // Add a list pin if the caller didn't provide one.
  if(list_pin_status == NEED_PIN)
    list__update_ref(list, 1);

  // Decide how many levels we're willing to set the node upon.
  int levels = 0;
  while((levels < SKIPLIST_MAX) && (levels < list->levels) && (rand() % 2 == 0))
    levels++;

  // Build a local stack based on the main list->indexes[] to build breadcrumbs.  Lock each buffer as we descend the skiplist tree.
  // until we have the whole chain.  Since scanning always down-and-forward we're safe.
  SkiplistNode *slstack[SKIPLIST_MAX];
  Buffer *locked_buffers[SKIPLIST_MAX];
  bufferid_t last_lock_id = BUFFER_ID_MAX - 1;
  int locked_ids_index = -1;
  for(int i = 0; i < SKIPLIST_MAX; i++)
    slstack[i] = list->indexes[i];

  // Traverse the list to find the ideal location at each level.  Since we're searching, use list->levels as the start height.
  for(int i = list->levels - 1; i >= 0; i--) {
    for(;;) {
      // Scan forward until we are as close as we can get.
      while(slstack[i]->right != NULL && slstack[i]->right->buffer_id <= buf->id)
        slstack[i] = slstack[i]->right;
      // Lock the buffer pointed to (if we haven't already) to effectively lock this SkiplistNode so we can test it.
      if(slstack[i]->buffer_id != last_lock_id)
        buffer__lock(slstack[i]->target);
      // If right is NULL or the ->right member is still bigger, we're as far over as we can go and should have a lock.
      if(slstack[i]->right == NULL || slstack[i]->right->buffer_id > buf->id) {
        if(slstack[i]->buffer_id != last_lock_id) {
          last_lock_id = slstack[i]->buffer_id;
          locked_ids_index++;
          locked_buffers[locked_ids_index] = slstack[i]->target;
        }
        break;
      }
      // Otherwise, someone inserted while we acquired this lock.  Release and try moving forward again.
      buffer__unlock(slstack[i]->target);
    }
    // If the buffer already exists, flag it with rv.  We'll release any locks we acquired before we leave.
    if(slstack[i]->buffer_id == buf->id) {
      rv = E_BUFFER_ALREADY_EXISTS;
      break;
    }
    // Modify the next slstack node to look at the more-forward position we just jumped to.  If we're at index 0, skip it, we're done.
    if(i != 0)
      slstack[i-1] = slstack[i]->down;
  }

  // Continue searching the list from slstack[0] to ensure it doesn't already exist.  Then add to the list.
  if (rv == E_OK) {
    Buffer *nearest_neighbor = slstack[0]->target;
    // Move right in the buffer list.  ->head is always max, so no need to check anything but ->id.
    while(nearest_neighbor->next->id <= buf->id)
      nearest_neighbor = nearest_neighbor->next;
    if(nearest_neighbor->id == buf->id) {
      rv = E_BUFFER_ALREADY_EXISTS;
    } else {
      buf->next = nearest_neighbor->next;
      nearest_neighbor->next = buf;
    }
  }

  // Loop through the slstack and begin linking Skiplist Nodes together everything is still E_OK.
  if (rv == E_OK) {
    SkiplistNode *slnode = NULL;
    for(int i = 0; i < levels; i++) {
      // Create a new Skiplist Node for each level we'll be inserting at and insert it into that index.
      slnode_rv = list__initialize_skiplistnode(&slnode, buf);
      if (slnode_rv != E_OK)
        return slnode_rv;
      slnode->right = slstack[i]->right;
      slstack[i]->right = slnode;
    }
    // Now that the Nodes all exist (if any) and our slstack's ->right members point to them, we can set their ->down members.
    for(int i = levels - 1; i > 0; i--)
      slstack[i]->right->down = slstack[i-1]->right;
  }

  // Unlock any buffers we locked along the way.
  for(int i = locked_ids_index; i >= 0; i--)
    buffer__unlock(locked_buffers[i]);

  // Remove the list pin we set if the caller didn't provide one.
  if(list_pin_status == NEED_PIN)
    list__update_ref(list, -1);

  // If everything worked, grab the list lock whenever it's available and increment counters.
  if (rv == E_OK) {
    pthread_mutex_lock(&list->lock);
    if(levels == list->levels)
      list->levels++;
    list->raw_count++;
    list->current_raw_size += BUFFER_OVERHEAD + buf->data_length;
    pthread_mutex_unlock(&list->lock);
  }

  return rv;
}


/* list__remove
 * Removes the buffer from the list's pool while respecting the list lock and readers.  Caller must grab and pass buffer_id before
 * unlocking its reference to the buffer; this way we can rely on finding the ID even if the buffer is removed by another thread.
 *
 * In the event multiple threads try to remove the same buffer, the first (race condition) will win and the others will simply have
 * their pins removed after the removal is done.
 *
 * Caller MUST have a pin on the buffer!
 * Upon successful completion the buffer will be moved to a copy-on-write space (pending deletion).
 */
int list__remove(List *list, Buffer *buf) {
  /* Caller has to have a pin, so even though ref_count is a dirty read it will always be 1+ if the caller did their job right. */
  if(buf->ref_count < 1)
    return E_BUFFER_MISSING_A_PIN;
  /* Use atomics/locks to compare and/or set the dirty flag to prevent multiple updates at once. */
  pthread_mutex_lock(&buf->lock);
  if(buf->flags & removing) {
    pthread_mutex_unlock(&buf->lock);
    while(buf->flags & removing); //spin
    // Since another thread was already removing it, it's now in the slaughter house.  Just remove our pin so it'll flush later.
    __sync_fetch_and_add(&buf->ref_count, -1);
    return E_OK;
  }
  // Looks like no one else beat us to the update.  Flip some bits and have a party.  We'll mark it dirty after we're done.
  buf->flags |= removing;
  pthread_mutex_unlock(&buf->lock);

  /* Get a read lock to ensure the sweeper doesn't run (or that it's the sweeper who actually called us). */
  list__update_ref(list, 1);
  int rv = E_BUFFER_NOT_FOUND;

  // Build a local stack based on the main list->indexes[] to build breadcrumbs.  Lock each buffer as we descend the skiplist tree.
  // until we have the whole chain.  Since scanning is always down-and-forward we're safe.
  SkiplistNode *slstack[SKIPLIST_MAX];
  Buffer *locked_buffers[SKIPLIST_MAX];
  bufferid_t last_lock_id = BUFFER_ID_MAX - 1;
  int locked_ids_index = -1;
  for(int i = 0; i < SKIPLIST_MAX; i++)
    slstack[i] = list->indexes[i];

  // Traverse the list to find the ideal location at each level.  Since we're searching, use list->levels as the start height.
  int levels = 0;
  for(int i = list->levels - 1; i >= 0; i--) {
    for(;;) {
      // Scan forward until we are as close as we can get.
      while(slstack[i]->right != NULL && slstack[i]->right->buffer_id < buf->id)
        slstack[i] = slstack[i]->right;
      // Lock the buffer pointed to (if we haven't already) to effectively lock this SkiplistNode so we can test it.
      if(slstack[i]->buffer_id != last_lock_id)
        buffer__lock(slstack[i]->target);
      // If right is NULL or the ->right member is still bigger, we're as far over as we can go and should have a lock.
      if(slstack[i]->right == NULL || slstack[i]->right->buffer_id >= buf->id) {
        if(slstack[i]->buffer_id != last_lock_id) {
          last_lock_id = slstack[i]->buffer_id;
          locked_ids_index++;
          locked_buffers[locked_ids_index] = slstack[i]->target;
        }
        break;  // Breaks the forward scanning.
      }
      // Otherwise, someone inserted while we acquired this lock.  Release and try moving forward again.
      buffer__unlock(slstack[i]->target);
    }
    // Modify the next slstack node to look at the more-forward position we just jumped to.  If we're at index 0, skip it, we're done.
    if(i != 0)
      slstack[i-1] = slstack[i]->down;
    // If ->right exists and its id matches increment levels so we can modify the skiplist levels later.
    if(slstack[i]->right != NULL && slstack[i]->right->buffer_id == buf->id)
      levels++;
  }

  // Now find the nearest neighbor.
  Buffer *nearest_neighbor = slstack[0]->target;
  // Move right in the buffer list.  ->head is always max, so no need to check anything but ->id.
  while(nearest_neighbor->next->id < buf->id)
    nearest_neighbor = nearest_neighbor->next;
  // This should NEVER happen because the caller has a pin... but we'll throw it for debugging.
  if(nearest_neighbor->next->id != buf->id) {
    list__update_ref(list, -1);
    return E_BUFFER_NOT_FOUND;
  }

  // We should be close-as-can-be.  If ->next matches, we're on the right track.  Otherwise we're still E_BUFFER_NOT_FOUND.
  rv = E_OK;
  const uint32_t BUFFER_SIZE = BUFFER_OVERHEAD + (buf->comp_length == 0 ? buf->data_length : buf->comp_length);
  // Update the list metrics and move the clock hand if it's pointing at the same address as buf.
  if(list->clock_hand == buf)
    list->clock_hand = buf->next;
  if(buf->flags & compressed) {
    __sync_fetch_and_sub(&list->current_comp_size, BUFFER_SIZE);
    __sync_fetch_and_sub(&list->comp_count, 1);
  } else {
    __sync_fetch_and_sub(&list->current_raw_size, BUFFER_SIZE);
    __sync_fetch_and_sub(&list->raw_count, 1);
  }

  // Now change our ->next pointer for nearest_neighbor to drop this from the list, then destroy the slnodes.
  nearest_neighbor->next = buf->next;
  if(rv == E_OK) {
    SkiplistNode *slnode = NULL;
    for(int i = levels - 1; i >= 0; i--) {
      // Each of these levels was already found to have the node, so ->right->right has to exist or at least be NULL.
      slnode = slstack[i]->right;
      slstack[i]->right = slstack[i]->right->right;
      free(slnode);
      // If the list's skip-index at this level is empty, drop the list levels height.
      if(list->indexes[i]->right == NULL)
        list->levels--;
    }
  }

  // Unlock any buffers we locked along the way.
  for(int i = locked_ids_index; i >= 0; i--)
    buffer__unlock(locked_buffers[i]);

  /* Flip bits and let go of the list pin we held.  Then send the buffer off. */
  pthread_mutex_lock(&buf->lock);
  buf->flags |= dirty;
  buf->flags &= (~removing);
  pthread_mutex_unlock(&buf->lock);
  // Remove the pin the caller came in with.
  __sync_fetch_and_add(&buf->ref_count, -1);
  list__add_cow(list, buf);
  list__update_ref(list, -1);

  return rv;
}


/* list__search
 * Searches for a buffer in the list specified so it can be sent back as a double pointer.  We need to pin the list so we can
 * search it.  When successfully found, we increment ref_count.  Caller MUST get a list pin first!
 */
int list__search(List *list, Buffer **buf, bufferid_t id, uint8_t list_pin_status) {
  /* Since searching can cause restorations and ultimately exceed max size, check for it.  This is a dirty read but OK. */
  if(list->current_raw_size > list->max_raw_size) {
    // We're about to wake up the sweeper, which means we need to remove this threads list pin if the caller has one.
    if(list_pin_status == HAVE_PIN)
      list__update_ref(list, -1);
    pthread_mutex_lock(&list->lock);
    while(list->current_raw_size > list->max_raw_size) {
      pthread_cond_broadcast(&list->sweeper_condition);
      pthread_cond_wait(&list->reader_condition, &list->lock);
    }
    pthread_mutex_unlock(&list->lock);
    // Now put this threads list pin back in place, making the caller never-aware it lost it's pin; if applicable.
    if(list_pin_status == HAVE_PIN)
      list__update_ref(list, 1);
  }

  /* If the caller doesn't provide a list pin, add one. */
  if(list_pin_status == NEED_PIN)
    list__update_ref(list, 1);

  /* Begin searching the list at the highest level's index head. */
  int rv = E_BUFFER_NOT_FOUND;
  SkiplistNode *slnode = list->indexes[list->levels];
  while(rv == E_BUFFER_NOT_FOUND) {
    // Move right until we can't go farther.  Try to let the system know to prefetch this, as this is the hottest spot in the code.
    while(slnode->right != NULL && slnode->right->buffer_id <= id) {
      slnode = slnode->right;
      __builtin_prefetch(slnode->right, 0, 1);
    }
    // If the node matches, we're done!  Try to update the ref and assign everything appropriately.
    if(slnode->buffer_id == id) {
      *buf = slnode->target;
      __sync_fetch_and_add(&(*buf)->ref_count, 1);
      rv = E_OK;
    }
    // If we can't move down, leave.
    if(slnode->down == NULL)
      break;
    slnode = slnode->down;
  }

  /* If we're still E_BUFFER_NOT_FOUND, scan the nearest_neighbor until we find it. */
  if(rv == E_BUFFER_NOT_FOUND) {
    Buffer *nearest_neighbor = slnode->target;
    while(nearest_neighbor->next->id <= id)
      nearest_neighbor = nearest_neighbor->next;
    // If we got a match, score.  Our nearest_neighbor is now the match.  Update ref and assign things.
    if(nearest_neighbor->id == id) {
      // Assign it.
      *buf = nearest_neighbor;
      __sync_fetch_and_add(&(*buf)->ref_count, 1);
      rv = E_OK;
    }
  }

  /* If the buffer was found and is compressed, we need to decompress it. */
  if(rv == E_OK && ((*buf)->flags & compressed)) {
    // The only protection we need is the buffer's lock.  We already have a pin, so it can't poof and there can't be any readers.
    pthread_mutex_lock(&(*buf)->lock);
    // First check above was a dirty read, do it again
    if((*buf)->comp_length != 0) {
      // No one else decompressed it before us, so let's move forward.
      int decompress_rv = E_OK;
      uint16_t comp_length = (*buf)->comp_length;
      decompress_rv = buffer__decompress(*buf, list->compressor_id);
      if (decompress_rv != E_OK && decompress_rv != E_BUFFER_ALREADY_DECOMPRESSED) {
        pthread_mutex_unlock(&(*buf)->lock);
        return E_BUFFER_COMPRESSION_PROBLEM;
      }
      // Update counters for the list now by forcibly grabbing the mutex, while still holding our pin.
      pthread_mutex_lock(&list->lock);
      list->raw_count++;
      list->comp_count--;
      list->current_comp_size -= (BUFFER_OVERHEAD + comp_length);
      list->current_raw_size += (BUFFER_OVERHEAD + (*buf)->data_length);
      list->restorations++;
      pthread_mutex_unlock(&list->lock);
    }
    // Clear the compressed flag.
    (*buf)->flags &= (~compressed);
    pthread_mutex_unlock(&(*buf)->lock);
  }

  /* If the caller didn't provide a pin, remove the one we set above. */
  if(list_pin_status == NEED_PIN)
    list__update_ref(list, -1);

  return rv;
}


/* list__update
 * Updates a buffer with the data and size specified.  We require the caller to have a pin from list__search().
 * If the page is clean, the update marks the existing buffer dirty and swaps in a new one.
 * If the page is dirty, we refuse the update and send back a warning.  (Caller needs to refresh and re-process).
 *
 * In the event multiple threads try to update the same buffer, the first (race condition) will win and the others will be given a
 * blocking operation contingent the Buffer's `updating` flag.
 *
 * Caller MUST have a pin on the buffer!
 * Upon successful completion the original buffer will be moved to a copy-on-write space (pending deletion), and the caller's buf
 * will be linked to the NEW buffer.  In other words, you get the new/updated buffer back so you don't have to search for it again.
 */
int list__update(List *list, Buffer **callers_buf, void *data, uint32_t size, uint8_t list_pin_status) {
  /* Caller has to have a pin, so even though ref_count is a dirty read it will always be 1+ if the caller did their job right. */
  Buffer *buf = *callers_buf;
  if(buf->ref_count < 1)
    return E_BUFFER_MISSING_A_PIN;
  /* Use atomics/locks to compare and/or set the dirty flag to prevent multiple updates at once. */
  pthread_mutex_lock(&buf->lock);
  if((buf->flags & dirty) || (buf->flags & updating)) {
    pthread_mutex_unlock(&buf->lock);
    while(buf->flags & updating); //spin
    return E_BUFFER_IS_DIRTY;
  }
  // Looks like no one else beat us to the update.  Flip some bits and have a party.  We'll mark it dirty after we're done.
  buf->flags |= updating;
  pthread_mutex_unlock(&buf->lock);

  /* Grab the list lock so we can handle sweeping processes and signaling correctly.  Small race will allow exceeding max, but that's ok. */
  if((buf->flags & compressing) == 0 && (list->current_raw_size > list->max_raw_size)) {
    // We're about to wake up the sweeper, which means we need to remove this threads list pin if the caller has one.
    if(list_pin_status == HAVE_PIN)
      list__update_ref(list, -1);
    pthread_mutex_lock(&list->lock);
    while(list->current_raw_size > list->max_raw_size) {
      pthread_cond_broadcast(&list->sweeper_condition);
      pthread_cond_wait(&list->reader_condition, &list->lock);
    }
    pthread_mutex_unlock(&list->lock);
    // Now put this threads list pin back in place, making the caller never-aware it lost it's pin; if applicable.
    if(list_pin_status == HAVE_PIN)
      list__update_ref(list, 1);
  }

  // Add a list pin if the caller didn't provide one.
  if(list_pin_status == NEED_PIN)
    list__update_ref(list, 1);

  // Since we're just updating a buffer in place, we don't need an slstack.  Just find the topmost slnode.
  SkiplistNode *topmost_slnode = NULL;
  // Build a local stack based on the main list->indexes[] to build breadcrumbs.  Lock each buffer as we descend the skiplist tree.
  // until we have the whole chain.  Since scanning is always down-and-forward we're safe.
  SkiplistNode *slstack[SKIPLIST_MAX];
  Buffer *locked_buffers[SKIPLIST_MAX];
  bufferid_t last_lock_id = BUFFER_ID_MAX - 1;
  int locked_ids_index = -1;
  for(int i = 0; i < SKIPLIST_MAX; i++)
    slstack[i] = list->indexes[i];

  // Traverse the list to find the topmost slnode.
  for(int i = list->levels - 1; i >= 0; i--) {
    for(;;) {
      // Scan forward until we are as close as we can get.
      while(slstack[i]->right != NULL && slstack[i]->right->buffer_id < buf->id)
        slstack[i] = slstack[i]->right;
      // If the ->right->target matches our buffer, we found the topmost.
      if(topmost_slnode == NULL && slstack[i]->right != NULL && slstack[i]->right->target == buf)
        topmost_slnode = slstack[i]->right;
      // Lock the buffer pointed to (if we haven't already) to effectively lock this SkiplistNode so we can test it.
      if(slstack[i]->buffer_id != last_lock_id)
        buffer__lock(slstack[i]->target);
      // If right is NULL or the ->right member is still bigger, we're as far over as we can go and should have a lock.
      if(slstack[i]->right == NULL || slstack[i]->right->buffer_id >= buf->id) {
        if(slstack[i]->buffer_id != last_lock_id) {
          last_lock_id = slstack[i]->buffer_id;
          locked_ids_index++;
          locked_buffers[locked_ids_index] = slstack[i]->target;
        }
        break;
      }
      // Otherwise, someone inserted while we acquired this lock.  Release and try moving forward again.
      buffer__unlock(slstack[i]->target);
    }
    // Modify the next slstack node to look at the more-forward position we just jumped to.  If we're at index 0, skip it, we're done.
    if(i != 0)
      slstack[i-1] = slstack[i]->down;
  }

  // Now find the nearest neighbor.
  Buffer *nearest_neighbor = slstack[0]->target;
  // Move right in the buffer list.  ->head is always max, so no need to check anything but ->id.
  while(nearest_neighbor->next->id < buf->id) {
    nearest_neighbor = nearest_neighbor->next;
  }
  // This should NEVER happen because the caller has a pin... but we'll throw it.
  if(nearest_neighbor->next->id != buf->id)
    exit(3);

  // Make a copy of the buffer.  The new buffer will only have ONE (1) ref, the updater!  Remove its pin from buf.
  Buffer *new_buffer;
  buffer__initialize(&new_buffer, buf->id, size, data, NULL);
  buffer__copy(buf, new_buffer, false);
  new_buffer->ref_count = 1;
  new_buffer->data_length = size;
  new_buffer->comp_length = 0;
  // If the update is working with a compressing buffer, update sizes properly or we'll have skewed accounting.
  if(buf->flags & compressing) {
    new_buffer->data_length = buf->data_length;
    new_buffer->comp_length = size;
  }
  __sync_fetch_and_add(&buf->ref_count, -1);

  // Update all the linking.  Update the new_buffer first!
  new_buffer->next = buf->next;
  nearest_neighbor->next = new_buffer;
  while(topmost_slnode != NULL && topmost_slnode->target == buf) {
    topmost_slnode->target = new_buffer;
    if(topmost_slnode->down == NULL)
      break;
    topmost_slnode = topmost_slnode->down;
  }

  // Unlock any buffers we locked along the way.
  for(int i = locked_ids_index; i >= 0; i--)
    buffer__unlock(locked_buffers[i]);

  // Remove the list pin we set if the caller didn't provide one.
  if(list_pin_status == NEED_PIN)
    list__update_ref(list, -1);

  // Now that we're done reading buf, we need to change the caller's buf via indirection so they see the new buffer.
  *callers_buf = new_buffer;

  // Check to see if this was just a raw-to-raw update.  Compressors update list size on their own.
  if((buf->flags & compressing) == 0)
    // Coerce to allow a negative value to the atomic; otherwise an underflow can be sent.
    __sync_fetch_and_add(&list->current_raw_size, (int)(size - buf->data_length));

  // Mark the buffer dirty, remove the updating flag, and throw it in the dirty pool for future eviction.
  pthread_mutex_lock(&buf->lock);
  buf->flags |= dirty;
  buf->flags &= (~updating);
  pthread_mutex_unlock(&buf->lock);
  list__add_cow(list, buf);

  return E_OK;
}


/* list__update_ref
 * Edits the reference count of threads currently pinning the list.  Pinning happens for searching the list.  Delta should only be
 * 1 or -1, ever.  Typically a worker should call this once and then respect (dirty-read) pending_writers.
 */
int list__update_ref(List *list, int delta) {
  /* Do we own the lock as a writer?  If so, we don't need to mess with reference counting.  Avoids deadlocking. */
  if (pthread_equal(list->lock_owner, pthread_self()))
    return E_OK;

  /* Lock the list and check pending writers.  If non-zero and we're incrementing, wait on the reader condition. */
  pthread_mutex_lock(&list->lock);
  if (delta > 0 && list->pending_writers > 0) {
    while(list->pending_writers > 0)
      pthread_cond_wait(&list->reader_condition, &list->lock);
  }
  list->ref_count += delta;

  /* When writers are waiting and we are decrementing, we need to broadcast to the writer condition that it's safe to proceed. */
  if (delta < 0 && list->pending_writers != 0 && list->ref_count == 0)
    pthread_cond_broadcast(&list->writer_condition);

  /* Release the lock, which will also finally unblock any threads we woke up with broadcast above.  Then leave happy. */
  pthread_mutex_unlock(&list->lock);
  return E_OK;
}


/* list__sweep
 * Attempts to run the sweep algorithm on the list to find space to free up.
 * Note:  We attempt to free a percentage of ->current_size, NOT ->max_size!  There are pros/cons to both; in normal usage the
 * current size should always be high enough to avoid errors because sweeping shouldn't be called until we're low on memory.
 */
uint64_t list__sweep(List *list, uint8_t sweep_goal) {
  // Variables and tracking data.  We only start the time when we drain readers with list__acquire_write_lock() below.
  struct timespec start, end;
  Buffer *victim = NULL;
  uint64_t bytes_freed = 0;
  uint64_t comp_bytes_added = 0;
  uint32_t total_victims = 0;
  const uint64_t BYTES_NEEDED = (list->current_raw_size > list->max_raw_size ? list->current_raw_size - list->max_raw_size : 0) + (list->max_raw_size * sweep_goal / 100);

  // Loop forever to free up memory.  Memory checks happen near the end of the loop.
  if(BYTES_NEEDED != 0 && list->current_raw_size > list->max_raw_size) {
    while(1) {
      // Scan until we find a buffer to remove.  Popularity is halved until a victim is found.  Skip head matches.
      while(1) {
        list->clock_hand = list->clock_hand->next;
        if (list->clock_hand->popularity == 0 && list->clock_hand != list->head) {
          // If the buffer is already pending for sweep operations, we can't reuse it.  Skip.
          if(list->clock_hand->flags & pending_sweep)
            continue;
          // If it's compressed, just update the comp_victims array (if possible) and continue.
          if (list->clock_hand->flags & compressed) {
            if(list->comp_victims_index < MAX_COMP_VICTIMS) {
              list->comp_victims[list->comp_victims_index] = list->clock_hand;
              list->comp_victims_index++;
              list->clock_hand->flags |= pending_sweep;
            }
            continue;
          }
          // We found a raw buffer victim.
          victim = list->clock_hand;
          victim->flags |= pending_sweep;
          break;
        }
        list->clock_hand->popularity >>= 1;
      }
      // We only reach this when an unpopular raw victim id is found.  Update space we'll free and start tracking victim.
      bytes_freed += BUFFER_OVERHEAD + victim->data_length;
      list->victims[list->victims_index] = victim;
      list->victims_index++;
      total_victims++;

      // If the victim pool is full or we've found enough memory to free, flush everything and reset counters.
      if(list->victims_index == VICTIM_BATCH_SIZE || BYTES_NEEDED <= bytes_freed) {
        // Grab the jobs lock and rely on our condition to tell us when compressor_jobs is empty.
        pthread_mutex_lock(&list->jobs_lock);
        while(list->active_compressors > 0 || list->victims_index > list->victims_compressor_index) {
          pthread_cond_broadcast(&list->jobs_cond);
          pthread_cond_wait(&list->jobs_parent_cond, &list->jobs_lock);
        }
        pthread_mutex_unlock(&list->jobs_lock);
        for(int i=0; i<list->victims_index; i++) {
          comp_bytes_added += BUFFER_OVERHEAD + list->victims[i]->comp_length;
          list->victims[i]->flags &= (~pending_sweep);
          list->victims[i] = NULL;
        }
        list->victims_index = 0;
        list->victims_compressor_index = 0;
        // Check to see if we're done scanning.  This prevents double checking via while() with every loop iteration.
        if(BYTES_NEEDED <= bytes_freed)
          break;
      }
    }
  }
  // Finally, remove all pending_sweep flags from the compressed victims now that we're done scanning.
  for(int i=0; i<list->comp_victims_index; i++)
    list->comp_victims[i]->flags &= (~pending_sweep);


  // We freed up enough raw space.  If comp space is too large start freeing up space.  Update some counters under write protection.
  clock_gettime(CLOCK_MONOTONIC, &start);
  list__acquire_write_lock(list);
  list->compressions += total_victims;
  list->raw_count -= total_victims;
  list->comp_count += total_victims;
  list->current_raw_size -= bytes_freed;
  list->current_comp_size += comp_bytes_added;
  if(list->current_comp_size > list->max_comp_size) {
    for(int i=0; i<list->comp_victims_index && list->current_comp_size > list->max_comp_size; i++) {
      if((list->comp_victims[i]->flags & compressed) == 0)
        continue;
      // We add a pin because list__remove requires it (buffers usually come list__search).
      __sync_fetch_and_add(&list->comp_victims[i]->ref_count, 1);
      list__remove(list, list->comp_victims[i]);
      list->evictions++;
      list->comp_victims[i] = NULL;
    }
    // If we still haven't freed enough comp space, start clockhand motion again and take more comp victims.
    while(list->current_comp_size > list->max_comp_size) {
      while(1) {
        list->clock_hand = list->clock_hand->next;
        if(list->clock_hand->popularity == 0 && list->clock_hand != list->head && (list->clock_hand->flags & compressed)) {
          __sync_fetch_and_add(&list->clock_hand->ref_count, 1);
          list__remove(list, list->clock_hand);
          list->evictions++;
          break;
        }
        list->clock_hand->popularity >>= 1;
      }
    }
  }
  // Wrap up and leave.
  clock_gettime(CLOCK_MONOTONIC, &end);
  list->comp_victims_index = 0;
  if(bytes_freed > 0 || comp_bytes_added > 0)
    list->sweeps++;
  list->sweep_cost += BILLION * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
  list__release_write_lock(list);

  return bytes_freed;
}


/* list__sweeper_start
 * This is the asynchronous thread that will call the sweeping logic when necessary.
 */
void list__sweeper_start(List *list) {
  while(1) {
    pthread_mutex_lock(&list->lock);
    while(list->current_raw_size < list->max_raw_size && list->current_comp_size < list->max_comp_size && list->active != 0) {
      pthread_cond_broadcast(&list->reader_condition);
      pthread_cond_wait(&list->sweeper_condition, &list->lock);
    }
    pthread_mutex_unlock(&list->lock);
    if(list->active == 0) {
      pthread_cond_broadcast(&list->reader_condition);
      break;
    }
    list__sweep(list, list->sweep_goal);
  }

  // Perform a final sweep.  This is to solve the edge case where a reader (list__add or list__search) is stuck waiting because its
  // last wake-up failed the predicate because others add/restores pushed it over the limit again.  Ergo, they're hung at shutdown.
  list__sweep(list, list->sweep_goal);

  return;
}


/* list__balance
 * Redistributes memory between a list and it's offload target.  The list__sweep() and list__pop() functions will handle the
 * buffer migration while respecting the new boundaries.
 */
int list__balance(List *list, uint32_t ratio, uint64_t max_memory) {
  // As always, be safe.
  if (list == NULL)
    return E_BAD_ARGS;
  list__acquire_write_lock(list);

  // Set the memory values according to the ratio.
  list->max_raw_size = max_memory * ratio / 100;
  list->max_comp_size = max_memory - list->max_raw_size;

  // Call list__sweep to clean up any needed raw space and remove any compressed buffers, if necessary.
  const uint8_t MINIMUM_SWEEP_GOAL = list->current_raw_size > list->max_raw_size ? 101 - (100 * list->max_raw_size / list->current_raw_size) : 1;
  if (MINIMUM_SWEEP_GOAL > 99)
    return E_LIST_CANNOT_BALANCE;
  list__sweep(list, MINIMUM_SWEEP_GOAL > list->sweep_goal ? MINIMUM_SWEEP_GOAL : list->sweep_goal);

  // All done.  Release our lock and go home.
  list__release_write_lock(list);
  return E_OK;
}


/* list__destroy
 * Frees the data held by a list.
 */
int list__destroy(List *list) {
  int rv = E_OK;
  // Stop the sweeper.
  list->active = 0;
  pthread_mutex_lock(&list->lock);
  pthread_cond_broadcast(&list->sweeper_condition);
  pthread_mutex_unlock(&list->lock);
  pthread_join(list->sweeper_thread, NULL);

  // Destroy all the buffers, including head.
  while(list->head->next != list->head) {
    __sync_fetch_and_add(&list->head->next->ref_count, 1);
    rv = list__remove(list, list->head->next);
    if(rv != E_OK)
      return E_LIST_REMOVAL;
  }
  __sync_fetch_and_add(&list->head->next->ref_count, 1);
  list__remove(list, list->head);

  // Nuke all the skiplist items.
  for(int i=0; i<SKIPLIST_MAX; i++)
    free(list->indexes[i]);

  // Stop all the compressors.
  for(int i=0; i<list->compressor_count; i++)
    list->compressor_pool[i].runnable = 1;
  for(int i=0; i<list->compressor_count; i++) {
    pthread_mutex_lock(&list->jobs_lock);
    pthread_cond_broadcast(&list->jobs_cond);
    pthread_mutex_unlock(&list->jobs_lock);
  }
  for(int i=0; i<list->compressor_count; i++)
    pthread_join(list->compressor_threads[i], NULL);
  free(list->compressor_pool);
  free(list->compressor_threads);

  // Stop the cow killer.
  pthread_mutex_lock(&list->cow_lock);
  pthread_cond_broadcast(&list->cow_killer_cond);
  pthread_mutex_unlock(&list->cow_lock);
  pthread_join(list->slaughter_house_thread, NULL);

  // Destroy the list object itself.
  free(list);
  return E_OK;
}


/* list__compressor_start
 * The is the initialization point for a compressor via pthread_create().
 */
void list__compressor_start(List *list) {
  // Figure out which index we are.
  pthread_mutex_lock(&list->lock);
  int my_worker_id = next_compress_worker_id;
  Compressor *comp = &list->compressor_pool[my_worker_id];
  next_compress_worker_id++;
  pthread_mutex_unlock(&list->lock);

  // Try to do work forever.  We need to start off assuming we're an active worker.  It self-regulates within the loop.
  Buffer *work_me[COMPRESSOR_BATCH_SIZE];
  void *compressed_data = NULL;
  int work_me_count = 0;
  int rv = E_OK;
  pthread_mutex_lock(comp->jobs_lock);
  (*comp->active_compressors)++;
  pthread_mutex_unlock(comp->jobs_lock);

  while(1) {
    // Secure the lock to test the predicate.  If there's no work to do, do some signaling and wait.
    pthread_mutex_lock(comp->jobs_lock);
    if((*comp->victims_index) == (*comp->victims_compressor_index)) {
      // There's no work to do.  Remove our active pin, notify the parent if pin count is 0, and then wait to be woken up again.
      (*comp->active_compressors)--;
      if(*comp->active_compressors == 0)
        pthread_cond_broadcast(comp->jobs_parent_cond);
      while((*comp->victims_index) == (*comp->victims_compressor_index) && comp->runnable == 0)
        pthread_cond_wait(comp->jobs_cond, comp->jobs_lock);
      // Someone woke us up and we have work to do.  Increment active counter.
      (*comp->active_compressors)++;
    }

    // We have a lock and our predicate indicates there's a job to do.  Try to grab it and start working, if we're allowed.
    if(comp->runnable != 0) {
      (*comp->active_compressors)--;
      pthread_cond_broadcast(comp->jobs_cond);
      pthread_mutex_unlock(comp->jobs_lock);
      break;
    }
    // Yep, we have work to do!  Grab some items and release the lock so others can have it.
    work_me_count = 0;
    while(work_me_count < COMPRESSOR_BATCH_SIZE && (*comp->victims_index) > (*comp->victims_compressor_index)) {
      work_me[work_me_count] = comp->victims[(*comp->victims_compressor_index)];
      work_me_count++;
      (*comp->victims_compressor_index)++;
    }
    pthread_cond_broadcast(comp->jobs_cond);
    pthread_mutex_unlock(comp->jobs_lock);

    for(int i = 0; i < work_me_count; i++) {
      // Compress the buffer's data.  Lean on list__update() for the heavy lifting and CoW work.
      if(work_me[i]->flags & compressed)
        continue;
      rv = buffer__compress(work_me[i], &compressed_data, comp->compressor_id, comp->compressor_level);
      if(rv == E_BUFFER_ALREADY_COMPRESSED)
        continue;
      // List update requires a pin.
      __sync_fetch_and_add(&work_me[i]->ref_count, 1);
      // We are the only ones who ever set or release the compressing flag so it's ok.
      work_me[i]->flags |= compressing;
      rv = list__update(list, &work_me[i], compressed_data, work_me[i]->comp_length, HAVE_PIN);
      // Removal of the compressing flag doesn't matter because of CoW.  But the new buffer pointed to by work_me[i] should get a flag.
      work_me[i]->flags |= compressed;
      __sync_fetch_and_add(&work_me[i]->ref_count, -1);
      work_me[i] = NULL;
    }
  }
  return;
}


/* tests__list_structure
 * Spits out a bunch of information about a list.  Mostly for debugging.
 */
void list__show_structure(List *list) {
  // List attributes
  /* Size and Counter Members */
  printf("\n");
  printf("List Statistics\n");
  printf("===============\n");
  printf("Buffer counts   : %'"PRIu32" raw, %'"PRIu32" compressed.\n", list->raw_count, list->comp_count);
  printf("Current sizes   : %'"PRIu64" bytes raw, %'"PRIu64" bytes compressed.\n", list->current_raw_size, list->current_comp_size);
  printf("Maximum sizes   : %'"PRIu64" bytes raw, %'"PRIu64" bytes compressed.\n", list->max_raw_size, list->max_comp_size);
  /* Locking, Reference Counters, and Similar Members */
  printf("Reference pins  : %"PRIu32".  This should be 0 at program end.\n", list->ref_count);
  printf("Pending writers : %"PRIu8".  This should be 0 at program end.\n", list->pending_writers);
  printf("CoW space used  : %"PRIu64".  This should be 0 at program end.\n", list->cow_current_size);
  /* Management and Administration Members */
  printf("Sweep goal      : %"PRIu8"%%.\n", list->sweep_goal);
  printf("Sweeps performed: %'"PRIu64".\n", list->sweeps);
  printf("Time sweeping   : %'"PRIu64" ns.  (Cannot search during this time.  More == bad)\n", list->sweep_cost);
  /* Management of Nodes for Skiplist and Buffers */
  printf("Skiplist Levels : %"PRIu8"\n", list->levels);

  // Skiplist Index information.
  char *header_format    = "| %-5s | %-8s | %-11s | %-9s | %-44s |\n";
  char *row_format       = "| %5d | %8s | %11s | %9s | %'9d  (%7.4f%% : %7.4f%% : %8.4f%%) |\n";
  char *header_separator = "+-------------------------------------------------------------------------------------------+\n";
  printf("\n");
  printf("Skiplist Statistics\n");
  printf("===================\n");
  printf("%s", header_separator);
  printf(header_format, "", "", "Down", "Target", "[Node Statistics]");
  printf(header_format, "Index", "In Order", "Pointers OK", "IDs Match", "Count      (Coverage :  Optimal :     Delta)");
  printf("%s", header_separator);
  int count = 0, out_of_order = 0, downs_wrong = 0, downs = 0, non_zero_refs = 0, target_ids_wrong = 0, pending_sweeps = 0, compressed = 0, raw = 0;
  int total_skiplistnodes = 0;
  SkiplistNode *slnode = NULL, *sldown = NULL;
  // Step 1:  For each level...
  for(int i=0; i<list->levels; i++) {
    count = 0;
    out_of_order = 0;
    downs_wrong = 0;
    target_ids_wrong = 0;
    slnode = list->indexes[i];
    // Step 2:  For each slnode moving rightward...
    while(slnode->right != NULL) {
      downs = 0;
      sldown = slnode;
      total_skiplistnodes++;
      if(slnode->buffer_id != slnode->target->id) {
        target_ids_wrong++;
        printf("slnode in index %d has buffer_id of %u but target->id is %u\n", i, slnode->buffer_id, slnode->target->id);
      }
      // Step 3:  For each slnode looking downward...
      while(sldown->down != NULL) {
        if(sldown->buffer_id == sldown->down->buffer_id)
          downs++;
        sldown = sldown->down;
      }
      if(downs != i)
        downs_wrong++;
      slnode = slnode->right;
      count++;
      if((slnode->right != NULL) && (slnode->buffer_id >= slnode->right->buffer_id))
        out_of_order++;
    }
    printf(row_format, i, out_of_order == 0 ? "yes" : "no", downs_wrong == 0 ? "yes" : "no", target_ids_wrong == 0 ? "yes" : "no", count, 100 * (double)count/(list->raw_count + list->comp_count), 100.0 / pow(2,i+1), (100 * (double)count/(list->raw_count + list->comp_count)) - (100.0 / pow(2,i+1)));
    if(out_of_order != 0) {
      printf("Index was out of order displaying: ");
      slnode = list->indexes[i];
      while(slnode->right != NULL) {
        slnode = slnode->right;
        printf(" %"PRIu32, slnode->buffer_id);
      }
      printf("\n");
    }
  }
  printf("%s", header_separator);
  printf("Indexes %02d - %02d are all 0 / 0.0%%\n", list->levels, SKIPLIST_MAX);
  out_of_order = 0;
  Buffer *nearest_neighbor = list->head;
  while(nearest_neighbor->next != list->head) {
    nearest_neighbor = nearest_neighbor->next;
    if(nearest_neighbor->id >= nearest_neighbor->next->id)
      out_of_order++;
    if(nearest_neighbor->ref_count != 0)
      non_zero_refs++;
    if(nearest_neighbor->flags & pending_sweep)
      pending_sweeps++;
    if(nearest_neighbor->comp_length == 0)
      raw++;
    if(nearest_neighbor->comp_length != 0)
      compressed++;
  }
  printf("Total number of SkiplistNodes   : %d (%7.4f%% coverage, optimal %8.4f%%, delta %.4f%%)\n", total_skiplistnodes, 100.0 * total_skiplistnodes / (list->raw_count + list->comp_count), 100.0, 100.0 * total_skiplistnodes / (list->raw_count + list->comp_count) - 100.0);
  printf("\n");
  printf("Buffer Statistics\n");
  printf("===================\n");
  printf("Buffers in order from head      : %s\n", out_of_order == 0 ? "yes" : "no");
  printf("Buffers with non-zero ref counts: %d (should be 0)\n", non_zero_refs);
  printf("Buffers pending sweeps          : %d (should be 0)\n", pending_sweeps);
  printf("Buffers raw (uncompressed)      : %'d\n", raw);
  printf("Buffers compressed              : %'d\n", compressed);
  printf("Buffers evicted                 : %'"PRIu64"\n", list->evictions);
  printf("\n");
}


/* list__dump_structure
 * Dumps the entire structure of the list specified.  Starts with Skiplist Nodes and then prints the buffers.
 */
void list__dump_structure(List *list) {
  SkiplistNode *slnode = NULL;
  const int MAX_ENTRIES = 50;
  int entries = 0, segment = 1;
  printf("\n");
  printf("Skiplist Structure Dump\n");
  printf("=======================\n");
  printf("Format is|   Index#-Segment: ...\n");
  printf("Example  |   0-0001: 2 3 5 18 29 ...(%d entries per segment, for readability)\n", MAX_ENTRIES);
  for(int i=0; i<list->levels; i++) {
    slnode = list->indexes[i];
    entries = MAX_ENTRIES;
    segment = 1;
    while(slnode->right != NULL) {
      slnode = slnode->right;
      entries++;
      if(entries >= MAX_ENTRIES) {
        printf("\n%02d-%07d:", i, segment);
        entries = 1;
        segment++;
      }
      printf(" %"PRIu32, slnode->buffer_id);
    }
    printf("\n");
  }

  printf("\nBuffer list dump:");
  Buffer *current = list->head;
  entries = MAX_ENTRIES;
  segment = 1;
  while(current->next != list->head) {
    current = current->next;
    entries++;
    if(entries >= MAX_ENTRIES) {
      printf("\nBuffers-%07d:", segment);
      entries = 1;
      segment++;
    }
    printf(" %"PRIu32, current->id);
  }
  printf("\n");

  return;
}


/* list__add_cow
 * Marks a buffer as dirty and appends it to a list for future cleaning.
 */
void list__add_cow(List *list, Buffer *buf) {
  /* If the buffer doesn't have any pins just kill it. */
  if(buf->ref_count == 0) {
    buffer__destroy(buf, true);
    return;
  }

  /* Looks like it has pins.  Try to prepend it to the list.  If the cow space is full, we wait. */
  pthread_mutex_lock(&list->cow_lock);
  while((list->cow_current_size + buf->data_length) > list->cow_max_size) {
    pthread_cond_broadcast(&list->cow_killer_cond);
    pthread_cond_wait(&list->cow_waiter_cond, &list->cow_lock);
  }
  // Space should be free, add our buffer.
  buf->next = list->cow_head->next;
  list->cow_head->next = buf;
  list->cow_current_size += BUFFER_OVERHEAD + (buf->comp_length == 0 ? buf->data_length : buf->comp_length);
  pthread_mutex_unlock(&list->cow_lock);
  return;
}


/* list__slaughter_house
 * Kills cows... yep.
 * Ok, it purges the Copy-On-Write buffers that are no longer pinned.
 */
void list__slaughter_house(List *list) {
  Buffer *current = NULL;
  Buffer *next = NULL;
  struct timespec ts;
  int wait_rv = 0;

  /* Let's murder some cows D2 style! */
  while(list->active) {
    // Always get a lock to control the cow list.
    pthread_mutex_lock(&list->cow_lock);
    // Scan through the list and remove anything with no pins.
    current = list->cow_head;
    next = current->next;
    while(next != list->cow_head) {
      // If the ref is zero, unlink it and destroy it.
      if(next->ref_count == 0) {
        current->next = next->next;
        list->cow_current_size = list->cow_current_size - BUFFER_OVERHEAD - (next->comp_length == 0 ? next->data_length : next->comp_length);
        buffer__destroy(next, true);
      }
      // Ref wasn't zero.  Move forward.
      current = current->next;
      next = current->next;
    }

    /* Now go to sleep until someone wakes us up or enough time has passed. */
    ts.tv_sec = time(NULL) + COW_NAP_TIME;
    ts.tv_nsec = 0;
    pthread_cond_broadcast(&list->cow_waiter_cond);
    wait_rv = pthread_cond_timedwait(&list->cow_killer_cond, &list->cow_lock, &ts);
    // If we timed out, we have to unlock because the mutex was given back to us.
    if(wait_rv == ETIMEDOUT)
      pthread_mutex_unlock(&list->cow_lock);
  }

  // Upon exit, destroy anything remaining in the slaughter house.
  while(list->cow_head->next != list->cow_head) {
    next = list->cow_head->next;
    list->cow_head->next = list->cow_head->next->next;
    list->cow_current_size = list->cow_current_size - BUFFER_OVERHEAD - next->data_length;
    buffer__destroy(next, true);
  }

  return;
}


