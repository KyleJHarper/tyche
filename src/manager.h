/*
 * manager.h
 *
 *  Created on: Dec 8, 2015
 *      Author: Kyle Harper
 * Description: Tyche's main logic runs inside of a manager.  This mostly avoids cramming all the logic into main() and/or tyche.c.
 *              It also allows me to build multiple managers in the future if necessary.
 */

#ifndef SRC_MANAGER_H_
#define SRC_MANAGER_H_


/* Build the worker struct. */
typedef uint32_t workerid_t;
typedef struct worker Worker;
struct worker {
  workerid_t id;      // ID of the worker.
  uint64_t misses;    // Count of buffer misses for the life of this worker.
  uint64_t hits;      // Count of buffer hits for the life of this worker.
};

/* Build the manager struct. */
typedef uint8_t managerid_t;
typedef struct manager Manager;
struct manager {
  /* Identifier(s) & Lists */
  managerid_t id;         // The ID of the manager, in case we have multiple in the future.
  List *raw_list;         // The raw list of buffers.
  List *comp_list;        // The compressed list of buffers.

  /* Page Information */
  char **pages;           // The list of pages we can ask for.

  /* Manager Control */
  uint8_t runnable;       // Pointer to the integer that indicates if we should still be running.
  uint32_t run_duration;  // Time spent, in ms, running this manager.
  pthread_mutex_t lock;   // Lock for operations on the manager which require atomicity.

  /* Workers and Their Aggregate Data */
  Worker *workers;        // The pool of workers this manager has assigned to it.
  uint64_t hits;          // Total hits for all workers.
  uint64_t misses;        // Total misses for all workers.
};


/* Prototypes */
Manager* manager__initialize(managerid_t id, char **pages);
int manager__start(Manager *mgr);
void manager__timer(Manager *mgr);
void manager__spawn_worker(Manager *mgr);
void manager__assign_worker_id(workerid_t *referring_id_ptr);
int manager__destroy(Manager *mgr);


#endif /* SRC_MANAGER_H_ */
