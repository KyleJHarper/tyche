/*
 * buffer.h
 *
 *  Created on: Jun 18, 2015
 *      Author: Kyle Harper
 * Description: Exposes the structures and whatnot for a buffer.  Each buffer holds some basic tracking information as well as the
 *              data field itself that we will call a Page.
 */

#ifndef SRC_BUFFER_H_
#define SRC_BUFFER_H_

/* Include necessary headers here. */
#include <stdint.h>  /* Used for the uint_ types */
#include <stdbool.h> /* For bool types. */


/* Globals to help track limits. */
#define MAX_POPULARITY  UINT16_MAX
#define MAX_WINDOWS              4
#define SKIPLIST_MAX            32
#define BUFFER_ID_MAX   UINT32_MAX

/* Enumerator for bit-flags in the buffer.  Keep it under 16 flags because we use uint16_t. */
enum buffer_flags {
  // These control CoW (copy-on-write) synchronization.
  dirty         = 1 <<  0,   //   1
  pending_sweep = 1 <<  1,   //   2
  updating      = 1 <<  2,   //   4
  removing      = 1 <<  3,   //   8
  removed       = 1 <<  4,   //  16
  // When sweeping we need to mark the buffer as 'compressing' so list__update() doesn't flake out (deadlock).
  compressing   = 1 <<  5,   //  32
  compressed    = 1 <<  6,   //  64
  // LIMIT is << 15
};
/* We need 0 and non-zero specifically for 'locking' so we define those separately. */
#define UNLOCKED     0
#define LOCKED       1
#define CAS_USLEEP  10 // Not used yet... maybe ever.

/* Build the typedef and structure for a Buffer
 * The size and alignment of members is of some import.  Aligning on 64-bit boundaries should provide decent space use with little
 * to no wasted space.
 * Note: The Flexible Array Member is going to track ->next pointers for the skiplist.  The coin-toss has a 50/50 probability which
 * will result in a geometric series which converges absolutely like so:
 * 1/2 + 1/4 + 1/8 + 1/16 + ... ==> yields a limit of 1.
 * We must also add 1 because every node will have a link at level 0, so the average size of the FAM will be 1 + 1 == 2.
 * The FAM will be assumed to use a full word for the pointer(s) so the average FAM is going to be 2 words make the entire struct
 * consume an average of 7 words, or 56 bytes.
 *
 * (The above assumes 64-bit architecture of course.  32 and 16 bit would align on 64 bit as well since it's a common multiple).
 */
typedef uint32_t bufferid_t;
typedef struct buffer Buffer;
struct buffer {
  /* The payload we want to cache (i.e.: the page). */
  // -- Word 1
  void *data;                     /* Pointer to the memory holding the page data, whether raw or compressed. */
  // -- Word 2
  uint32_t data_length;           /* Number of bytes originally in *data. */
  uint32_t comp_length;           /* Number of bytes in *data if it was compressed.  Set to 0 when not used. */

  /* Attributes for typical buffer organization and management. */
  // -- Word 3
  bufferid_t id;                  /* Identifier of the page. Should come from the system providing the data itself. */
  uint16_t flags;                 /* Holds 16 bit flags.  See enum above for details. */
  uint8_t overhead;               /* The overhead of each buffer.  It varies because of ->nexts below. */
  uint8_t padding;                /* Useless padding, forcing alignment. */
  // -- Word 4
  uint16_t windows[MAX_WINDOWS];  /* Array of windows for tracking popularity. */
  // -- Word 5
  uint32_t comp_cost;             /* Time spent, in ns, to compress and decompress a page.  Using clock_gettime(3) */
  uint16_t ref_count;             /* Number of references currently holding this buffer. */
  uint8_t cas_lock;               /* The light-weight CAS lock we use for buffer protection in our Skiplist. */

  /* The actual Skiplist elements, via Flexible Array Member (C99) support. */
  uint8_t sl_levels;              /* Number of levels in the logical skiplist we possess. */
  // -- Words 6+ (avg 2, see above)
  Buffer *nexts[];                /* The "next" pointers for our skiplist structure. */
};


/* Prototypes */
int buffer__initialize(Buffer **buf, uint8_t sl_levels, bufferid_t id, uint32_t size, void *data, char *page_filespec);
void buffer__destroy(Buffer *buf, const bool destroy_data);
void buffer__lock(Buffer *buf);
void buffer__unlock(Buffer *buf);
void buffer__release_pin(Buffer *buf);
int buffer__compress(Buffer *buf, void **compressed_data, int compressor_id, int compressor_level);
int buffer__decompress(Buffer *buf, int compressor_id);
void buffer__copy(Buffer *src, Buffer *dst, bool copy_data);


#endif /* SRC_BUFFER_H_ */
