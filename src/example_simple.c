/*
 * example_simple.c
 *
 *   Created on:  Feb 3, 2017
 *       Author:  Kyle Harper
 *  Description:  Simple example program demonstrating how to create a list (buffer pool, whatever you want to call it).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>   // For memcpy() below, just for this example.
#include "list.h"     // Include this.  Gives you access to List structures.
#include "buffer.h"   // Include this.  Gives you access to Buffer structures.
#include "globals.h"  // Include this.  Gives you access to error codes and constants.


// Compressor ID.  We use LZ4 in this example.
extern const int LZ4_COMPRESSOR_ID;
// Error codes.
extern const int E_OK;
extern const int E_BUFFER_NOT_FOUND;
// Tags and Flags
extern const int NEED_PIN;


/*
 * The following main() will read like a lifecycle: an application will create, find, update, and then remove some data
 * from a list.  Then it will destroy the list when the application dies or is done with it.
 */
int main() {
  // Create a pointer to a list structure for use later.  You can create this pointer anywhere as long as you don't lose scope to it.
  List *list = NULL;
  // We will also create a Buffer object, which is a container (struct) for your data.  You'd have one of these for each data item you wanted managed.
  Buffer *buf = NULL;

  // In this example, your application creates the following data and wants it stored in a self-managed pool (the list*).
  // Your app must know data size, and MUST malloc the space!  Otherwise, free() will fail later because we assign YOUR pointer.
  int your_data_size = 89;
  char *your_data = malloc(your_data_size);
  memcpy(your_data, "This is data from your application.  It can be text, binary, whatever... it's irrelevant.", your_data_size);
  bufferid_t your_data_id = 42;  // The ID your application uses to reference this piece of data.  E.g.: inode, page number, et cetera.
  int rv = 0;  // A simple integer to hold return values for error handling.


  // Step 1)
  // Use list__initialize() to allocate memory for your list pointer and set initial values and start sub-processes running.
  rv = list__initialize(&list, 1, LZ4_COMPRESSOR_ID, 1, 1000000);
  if (rv != E_OK) {
    printf("Failed to initialize the list.  Error code is %d.\n", rv);
    exit(rv);  // Or throw it to your caller...
  }
  printf("Got my initialized list.  Compressor threads and management threads are now running.\n");

  // Step 2)
  // Initialize a buffer object with your data attached, then add it to the list.
  buffer__initialize(&buf, your_data_id, your_data_size, your_data, NULL);
  rv = list__add(list, buf, NEED_PIN);
  if (rv != E_OK) {
    printf("Uh oh, I didn't get to add my data :(.  Return value is: %d\n", rv);
    exit(rv);  // Or throw it to your caller.
  }
  // The list now contains a buffer linked to *your_data.  Do NOT free(your_data).  Do NOT modify *your_data.
  printf("Our list now has %"PRIu32" raw and %"PRIu32" compressed buffers, using %"PRIu64" bytes.\n", list->raw_count, list->comp_count, list->current_raw_size + list->current_comp_size);


  // Step 3)
  // Search for the data we just stored.  We'll set our buf pointer to NULL to prove this works.
  buf = NULL;
  rv = list__search(list, &buf, your_data_id, NEED_PIN);
  if (rv != E_OK) {
    // We didn't find the buffer :(  Do whatever your app logic needs from here.
    // It's common to get E_BUFFER_NOT_FOUND if it doesn't exist.
    exit(rv);
  }
  printf("Yay!  We found our data inside the list:\n    %s\n", (char *)buf->data);
  // Searching gives a "pin" on the buffer that prevents other threads from changing or deleting it.  We also use CoW
  // (Copy-on-Write) so your threads won't block except in extreme cases of resource exhaustion.
  // NOTE:  Normally you call buffer__remove_pin(buf) to release your pin; but we're updating this buffer below, which requires a pin anyway.


  // Step 4)
  // Update the data by assigning your new value to it.  A Copy-on-Write process will protect the original Buffer for other threads.
  // You MUST find (list__searcch()) and keep your pin on a buffer THEN call list__update().
  int new_data_size = 60;
  char *new_data = malloc(new_data_size);
  memcpy(new_data, "This is your new data that you want assigned to your buffer.", new_data_size);
  rv = list__update(list, &buf, new_data, new_data_size, NEED_PIN);
  if (rv != E_OK) {
    // The update failed.  Possibly because the buffer is 'dirty' from another thread updating it before us.  Code for that is E_BUFFER_IS_DIRTY.
    exit(rv);
  }
  printf("Data updated, it's now %"PRIu32" bytes long.\n    %s\n", buf->data_length, (char *)buf->data);
  printf("Our list now has %"PRIu32" raw and %"PRIu32" compressed buffers, using %"PRIu64" bytes.\n", list->raw_count, list->comp_count, list->current_raw_size + list->current_comp_size);
  // Once again we would normally release our pin with buffer__remove_pin(buf), but in this example we're removing it below, which requires a pin again.


  // Step 5)
  // Remove the buffer from the list explicitly.  Removal is also a non-blocking operation.
  rv = list__remove(list, buf);
  if (rv != E_OK && rv != E_BUFFER_NOT_FOUND) {
    printf("Failed to remove the buffer.\n");
    exit(rv);
  }
  printf("Removed the buffer, list now has %"PRIu32" raw and %"PRIu32" compressed buffers, using %"PRIu64" bytes.\n", list->raw_count, list->comp_count, list->current_raw_size + list->current_comp_size);
  // Removal automatically releases THIS thread's pin to avoid a deadlock.  No need to call buffer__release_pin().


  // Step 6)
  // Our application is done, yay!  You should always destroy the list explicitly.
  list__destroy(list);
  return 0;
}
