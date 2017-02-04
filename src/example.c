/*
 * example.c
 *
 *  Created on:  Feb 3, 2017
 *      Author:  Kyle Harper
 *  Description: An example of how to use the ACCRS logic contained within list.h/c and buffer.h/c.
 *               You DO NOT have to use these exact functions!  These are simple an example you can follow.
 *
 *               This is a SIMPLE example, designed for hand-holding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>       // For memcpy()
#include "list.h"         // Include this.  Gives you access to List struct.
#include "buffer.h"       // Include this.  Gives you access to bufferid_t and Buffer struct.
#include "globals.h"      // Include this.  Gives you access to error codes and constants.

// Compressor ID
extern const int LZ4_COMPRESSOR_ID;
// Error codes.
extern const int E_OK;
extern const int E_BUFFER_ALREADY_EXISTS;
extern const int E_BUFFER_NOT_FOUND;


// Prototypes because I'm too lazy to write a header.
int main();
int ex__add(   bufferid_t your_data_id, void *your_data, int your_data_size);
int ex__update(bufferid_t your_data_id, void *new_data,  int new_data_size);
int ex__find(  bufferid_t your_data_id, void **your_data_pointer);
int ex__remove(bufferid_t your_data_id);


// Create a pointer to a list structure for use later.  Doing this with global scope just for example.
// You can create this pointer anywhere as long as you don't lose scope to it.  If you lose the pointer... RIP memory.
List *list = NULL;


/*
 * The following main() will read like a lifecycle: an application will create, find, update, and then remove some data
 * from a list.  Then it will destroy the list when the application dies or is done with it.
 */
int main() {
  // Your application creates the following data and wants it stored in a self-managed pool (the list*).
  // Your app must know data size, and MUST malloc the space!  Otherwise, free() will fail later because we pass YOUR pointer.
  int your_data_size = 89;
  char *your_data = malloc(your_data_size);
  memcpy(your_data, "This is data from your application.  It can be text, binary, whatever... it's irrelevant.", your_data_size);
  bufferid_t your_data_id = 42;  // The ID your application uses to reference this piece of data.  E.g.: inode, page number, et cetera.
  int rv = 0;  // A simple integer to hold return values for error handling.


  // Step 1)
  // Use list__initialize() to allocate memory for your list pointer and set initial values and start sub-processes running.
  rv = list__initialize(&list, 1, LZ4_COMPRESSOR_ID, 1, 10000000);
  if (rv != E_OK) {
    printf("Failed to initialize the list.  Error code is %d.\n", rv);
    exit(rv);  // Or throw it to your caller...
  }
  printf("Got my initialized list.  Compressor threads and management threads now running.\n");


  // Step 2)
  // Add your data to a buffer and store that in your list.
  rv = ex__add(your_data_id, your_data, your_data_size);
  if (rv != E_OK) {
    printf("Uh oh, I didn't get to add my data :(.  Return value is: %d\n", rv);
    exit(rv);  // Or throw it to your caller.
  }
  // The list now contains a buffer linked to *your_data.  Do NOT free(your_data).  Do NOT modify *your_data.
  printf("Our list now has %"PRIu32" raw and %"PRIu32" compressed buffers, using %"PRIu64" bytes.\n", list->raw_count, list->comp_count, list->current_raw_size + list->current_comp_size);

  // Step 3)
  // Search for some data.  It'll get copied to your local pointer.
  void *found_data = NULL;
  rv = ex__find(your_data_id, &found_data);
  if (rv != E_OK) {
    // We didn't find the buffer :(  Do whatever your app logic needs from here.
    exit(1);
  }
  // Hooray, we found the data.  *found_data now has a COPY of your original data.
  printf("Yay!  We found our data inside the list:\n    %s\n", (char *)found_data);
  // It is YOUR responsibility to free *found_data when you're done with it.
  free(found_data);


  // Step 4)
  // Update the data by assigning a new value to it.
  int new_data_size = 60;
  char *new_data = malloc(new_data_size);
  memcpy(new_data, "This is your new data that you want assigned to your buffer.", new_data_size);
  rv = ex__update(your_data_id, new_data, new_data_size);
  if (rv != E_OK) {
    // The update failed.  Possibly because the buffer wasn't in the list.  Handle this appropriately in your app.
    exit(1);
  }
  // Going to find() it again so just to print it out to prove it changed.
  ex__find(your_data_id, &found_data);
  printf("Data updated, it's now %zu bytes long.\n    %s\n", strlen(found_data), (char *)found_data);
  free(found_data);

  // Step 5)
  // Remove the buffer from the list now that we're done with it.
  rv = ex__remove(your_data_id);
  if (rv != E_OK) {
    // We failed to remove it.  Should almost never happen.
    exit(1);
  }


  // Step 6)
  // Our application is done, yay!  You should always destroy the list explicitly.
  list__destroy(list);
  return 0;
}


int ex__add(bufferid_t your_data_id, void *your_data, int your_data_size) {
  // Create your local pointer to a new buffer to assign your data to.
  Buffer *buf = NULL;
  // Allocate the memory and initializes some internal attributes for your buffer.
  int rv = buffer__initialize(&buf, your_data_id, your_data_size, your_data, NULL);
  if (rv != E_OK) {
    printf("Failed to create a buffer... error code is %d.\n", rv);
    exit(rv);  // Or throw it to your caller...
  }

  // Put the buffer in the list so you can stop tracking it.
  rv = list__add(list, buf, 0);          // Adds your buffer to your list.
  if (rv == E_BUFFER_ALREADY_EXISTS)     // We don't need this buffer if it already exists; it's a duplicate.
    buffer__destroy(buf, DESTROY_DATA);  // buffer__destroy() will free(your_data) and all of its internal parts (no memory leak).  Don't call free()!

  // Leave.  Send rv if you want your caller to know the list__add status.
  return rv;
}


int ex__update(bufferid_t your_data_id, void *new_data, int new_data_size) {
  // Updating a buffer doesn't require working with the list itself.  Just the buffer.
  // First, use list__search() to get a reference to the buffer you want to update.
  Buffer *buf = NULL;
  int rv = list__search(list, &buf, your_data_id, NEED_LIST_PIN);
  if (rv != E_OK)  // It wasn't found, so you can't update it.
    return rv;

  // Use buffer__update() to set up the new data.
  rv = buffer__update(buf, new_data_size, new_data);
  return rv;
}


int ex__find(bufferid_t your_data_id, void **your_data_pointer) {
  // Searching is quite simple.  Create a buffer pointer and use list__search() to try to find it.
  Buffer *buf = NULL;
  int rv = list__search(list, &buf, your_data_id, NEED_LIST_PIN);
  // If we didn't find it, there's nothing to clean up.
  if (rv != E_OK)
    return rv;
  // We did find it, yay!  We've either been given a copy, or a reference to the original data.
  if (buf->is_ephemeral) {
    // We were given an ephemeral copy.  We can simply link to that data, destroy the copy, and move on.
    *your_data_pointer = buf->data;
    buffer__destroy(buf, KEEP_DATA);
    return rv;
  }
  // The buffer wasn't ephemeral.  We only have a reference to the real buffer.  Make a copy of it in this simple example.
  *your_data_pointer = malloc(buf->data_length);
  memcpy(*your_data_pointer, buf->data, buf->data_length);
  // Searching gives you a "pin" on the buffer when it's non-ephemeral.  Remove that pin now that we have our own copy.
  buffer__update_ref(buf, -1);
  return rv;
}


int ex__remove(bufferid_t your_data_id) {
  // Removing a buffer is easy, just pass in the ID.  If it exists, it'll get removed.
  // Note: it's quite rare to call removal directly because the list manages itself.  The only real use case for this is when
  //       your application wants to remove (not update, remove) a chunk of data persistently (off disk, then out of RAM).
  int rv = list__remove(list, your_data_id);
  if (rv == E_OK)
    return rv;
  // It's common for people to call list__remove() blindly because their app can't be sure what's still in the list at any given
  // time.  If the buffer is NOT FOUND, that's almost always OK; the calling app simply wants to make sure it doesn't exist.
  if (rv == E_BUFFER_NOT_FOUND)
    return E_OK;
  // If the rv wasn't OK or NOT FOUND, you should let the caller know.
  return rv;
}

