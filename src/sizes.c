/*
 * sizes.c
 *
 *  Created on: Jun 24, 2015
 *      Author: administrator
 */

#include <stdio.h>
#include <pthread.h>
#include "buffer.h"
#include "list.h"
#include "manager.h"


int main() {
  // -- Manager Information
  printf("\n");
  /* Identifier(s) & Lists */
  printf("Size of Manager->id             : %3zu Bytes\n", sizeof((Manager *)0)->id);
  printf("Size of Manager->raw_list       : %3zu Bytes\n", sizeof((Manager *)0)->raw_list);
  printf("Size of Manager->comp_list      : %3zu Bytes\n", sizeof((Manager *)0)->comp_list);
  /* Page Information */
  printf("Size of Manager->pages          : %3zu Bytes\n", sizeof((Manager *)0)->pages);
  /* Manager Control */
  printf("Size of Manager->runnable       : %3zu Bytes\n", sizeof((Manager *)0)->runnable);
  printf("Size of Manager->run_duration   : %3zu Bytes\n", sizeof((Manager *)0)->run_duration);
  printf("Size of Manager->lock           : %3zu Bytes\n", sizeof((Manager *)0)->lock);
  /* Workers and Their Aggregate Data */
  printf("Size of Manager->workers        : %3zu Bytes\n", sizeof((Manager *)0)->workers);
  printf("Size of Manager->hits           : %3zu Bytes\n", sizeof((Manager *)0)->hits);
  printf("Size of Manager->misses         : %3zu Bytes\n", sizeof((Manager *)0)->misses);
  printf("-------------------------------------------\n");
  printf("Size of Manager                   %3zu Bytes\n", sizeof(Manager));


  // -- Worker Information
  printf("\n");
  /* ID & Tracking */
  printf("Size of Worker->id              : %3zu Bytes\n", sizeof((Worker *)0)->id);
  printf("Size of Worker->misses          : %3zu Bytes\n", sizeof((Worker *)0)->misses);
  printf("Size of Worker->hits            : %3zu Bytes\n", sizeof((Worker *)0)->hits);
  printf("-------------------------------------------\n");
  printf("Size of Worker                    %3zu Bytes\n", sizeof(Worker));


  // -- List Information
  printf("\n");
  /* Size and Counter Members */
  printf("Size of List->count             : %3zu Bytes\n", sizeof((List *)0)->count);
  printf("Size of List->current_size      : %3zu Bytes\n", sizeof((List *)0)->current_size);
  printf("Size of List->max_size          : %3zu Bytes\n", sizeof((List *)0)->max_size);
  /* Locking, Reference Counters, and Similar Members */
  printf("Size of List->lock              : %3zu Bytes\n", sizeof((List *)0)->lock);
  printf("Size of List->lock_owner        : %3zu Bytes\n", sizeof((List *)0)->lock_owner);
  printf("Size of List->lock_depth        : %3zu Bytes\n", sizeof((List *)0)->lock_depth);
  printf("Size of List->writer_condition  : %3zu Bytes\n", sizeof((List *)0)->writer_condition);
  printf("Size of List->reader_condition  : %3zu Bytes\n", sizeof((List *)0)->reader_condition);
  printf("Size of List->ref_count         : %3zu Bytes\n", sizeof((List *)0)->ref_count);
  printf("Size of List->pending_writers   : %3zu Bytes\n", sizeof((List *)0)->pending_writers);
  /* Management and Administration Members */
  printf("Size of List->offload_to        : %3zu Bytes\n", sizeof((List *)0)->offload_to);
  printf("Size of List->restore_to        : %3zu Bytes\n", sizeof((List *)0)->restore_to);
  printf("Size of List->sweep_goal        : %3zu Bytes\n", sizeof((List *)0)->sweep_goal);
  printf("Size of List->sweeps            : %3zu Bytes\n", sizeof((List *)0)->sweeps);
  printf("Size of List->sweep_cost        : %3zu Bytes\n", sizeof((List *)0)->sweep_cost);
  printf("Size of List->offloads          : %3zu Bytes\n", sizeof((List *)0)->offloads);
  printf("Size of List->restorations      : %3zu Bytes\n", sizeof((List *)0)->restorations);
  /* Management of Nodes for Skiplist and Buffers */
  printf("Size of List->head              : %3zu Bytes\n", sizeof((List *)0)->head);
  printf("Size of List->clock_hand        : %3zu Bytes\n", sizeof((List *)0)->clock_hand);
  printf("Size of List->indexes           : %3zu Bytes\n", sizeof((List *)0)->indexes);
  printf("Size of List->levels            : %3zu Bytes\n", sizeof((List *)0)->levels);
  printf("-------------------------------------------\n");
  printf("Size of List                      %3zu Bytes\n", sizeof(List));


  // -- SkiplistNode Information
  printf("\n");
  /* Directions for Traversal.  Left-To-Right Mentality. */
  printf("Size of SkiplistNode->right     : %3zu Bytes\n", sizeof((SkiplistNode *)0)->right);
  printf("Size of SkiplistNode->down      : %3zu Bytes\n", sizeof((SkiplistNode *)0)->down);
  /* Buffer Reference to the List Item Itself */
  printf("Size of SkiplistNode->target    : %3zu Bytes\n", sizeof((SkiplistNode *)0)->target);
  printf("-------------------------------------------\n");
  printf("Size of SkiplistNode              %3zu Bytes\n", sizeof(SkiplistNode));


  // -- Buffer Information
  printf("\n");
  /* Attributes for typical buffer organization and management. */
  printf("Size of Buffer->id              : %3zu Bytes\n", sizeof((Buffer *)0)->id);
  printf("Size of Buffer->ref_count       : %3zu Bytes\n", sizeof((Buffer *)0)->ref_count);
  printf("Size of Buffer->popularity      : %3zu Bytes\n", sizeof((Buffer *)0)->popularity);
  printf("Size of Buffer->victimized      : %3zu Bytes\n", sizeof((Buffer *)0)->victimized);
  printf("Size of Buffer->lock            : %3zu Bytes\n", sizeof((Buffer *)0)->lock);
  printf("Size of Buffer->condition       : %3zu Bytes\n", sizeof((Buffer *)0)->condition);
  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  printf("Size of Buffer->comp_cost       : %3zu Bytes\n", sizeof((Buffer *)0)->comp_cost);
  printf("Size of Buffer->io_cost         : %3zu Bytes\n", sizeof((Buffer *)0)->io_cost);
  printf("Size of Buffer->comp_hits       : %3zu Bytes\n", sizeof((Buffer *)0)->comp_hits);
  /* The actual payload we want to cache (i.e.: the page). */
  printf("Size of Buffer->data_length     : %3zu Bytes\n", sizeof((Buffer *)0)->data_length);
  printf("Size of Buffer->comp_length     : %3zu Bytes\n", sizeof((Buffer *)0)->comp_length);
  printf("Size of Buffer->data            : %3zu Bytes\n", sizeof((Buffer *)0)->data);
  /* Tracking for the list we're part of. */
  printf("Size of Buffer->next            : %3zu Bytes\n", sizeof((Buffer *)0)->next);
  printf("-------------------------------------------\n");
  printf("Size of Buffer                    %3zu Bytes\n", sizeof(Buffer));


  printf("\n\n%10s%10s\n", "PAGE_SIZE", "Overhead");
  float size = 0.0;
  for (int i=64; i<=65536; i*=2) {
    size = (100.0f * sizeof(Buffer)) / (sizeof(Buffer) + i);
    printf("%10i%9.3f%%\n", i, size);
  }

  return 0;
}
