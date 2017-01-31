/*
 * sizes.c
 *
 *  Created on: Jun 24, 2015
 *      Author: administrator
 */

#include <pthread.h>
#include <stdio.h>
#include "buffer.h"
#include "list.h"
#include "manager.h"


int main() {
  // -- Manager Information
  printf("\n");
  /* Identifier(s) & Lists */
  printf("Size of Manager->id                           : %5zu Bytes\n", sizeof((Manager *)0)->id);
  printf("Size of Manager->list                         : %5zu Bytes\n", sizeof((Manager *)0)->list);
  /* Page Information */
  printf("Size of Manager->pages                        : %5zu Bytes\n", sizeof((Manager *)0)->pages);
  /* Manager Control */
  printf("Size of Manager->runnable                     : %5zu Bytes\n", sizeof((Manager *)0)->runnable);
  printf("Size of Manager->run_duration                 : %5zu Bytes\n", sizeof((Manager *)0)->run_duration);
  printf("Size of Manager->lock                         : %5zu Bytes\n", sizeof((Manager *)0)->lock);
  /* Workers and Their Aggregate Data */
  printf("Size of Manager->workers                      : %5zu Bytes\n", sizeof((Manager *)0)->workers);
  printf("Size of Manager->hits                         : %5zu Bytes\n", sizeof((Manager *)0)->hits);
  printf("Size of Manager->misses                       : %5zu Bytes\n", sizeof((Manager *)0)->misses);
  printf("-----------------------------------------------------------\n");
  printf("Size of Manager                                 %5zu Bytes\n", sizeof(Manager));


  // -- Worker Information
  printf("\n");
  /* ID & Tracking */
  printf("Size of Worker->id                            : %5zu Bytes\n", sizeof((Worker *)0)->id);
  printf("Size of Worker->misses                        : %5zu Bytes\n", sizeof((Worker *)0)->misses);
  printf("Size of Worker->hits                          : %5zu Bytes\n", sizeof((Worker *)0)->hits);
  printf("-----------------------------------------------------------\n");
  printf("Size of Worker                                  %5zu Bytes\n", sizeof(Worker));


  // -- List Information
  printf("\n");
  /* Size and Counter Members */
  printf("Size of List->raw_count                       : %5zu Bytes\n", sizeof((List *)0)->raw_count);
  printf("Size of List->comp_count                      : %5zu Bytes\n", sizeof((List *)0)->comp_count);
  printf("Size of List->current_raw_size                : %5zu Bytes\n", sizeof((List *)0)->current_raw_size);
  printf("Size of List->max_raw_size                    : %5zu Bytes\n", sizeof((List *)0)->max_raw_size);
  printf("Size of List->current_comp_size               : %5zu Bytes\n", sizeof((List *)0)->current_comp_size);
  printf("Size of List->max_comp_size                   : %5zu Bytes\n", sizeof((List *)0)->max_comp_size);
  /* Locking, Reference Counters, and Similar Members */
  printf("Size of List->lock                            : %5zu Bytes\n", sizeof((List *)0)->lock);
  printf("Size of List->lock_owner                      : %5zu Bytes\n", sizeof((List *)0)->lock_owner);
  printf("Size of List->lock_depth                      : %5zu Bytes\n", sizeof((List *)0)->lock_depth);
  printf("Size of List->writer_condition                : %5zu Bytes\n", sizeof((List *)0)->writer_condition);
  printf("Size of List->reader_condition                : %5zu Bytes\n", sizeof((List *)0)->reader_condition);
  printf("Size of List->sweeper_condition               : %5zu Bytes\n", sizeof((List *)0)->sweeper_condition);
  printf("Size of List->ref_count                       : %5zu Bytes\n", sizeof((List *)0)->ref_count);
  printf("Size of List->pending_writers                 : %5zu Bytes\n", sizeof((List *)0)->pending_writers);
  /* Management and Administration Members */
  printf("Size of List->sweep_goal                      : %5zu Bytes\n", sizeof((List *)0)->sweep_goal);
  printf("Size of List->sweeps                          : %5zu Bytes\n", sizeof((List *)0)->sweeps);
  printf("Size of List->sweep_cost                      : %5zu Bytes\n", sizeof((List *)0)->sweep_cost);
  printf("Size of List->restorations                    : %5zu Bytes\n", sizeof((List *)0)->restorations);
  printf("Size of List->compressions                    : %5zu Bytes\n", sizeof((List *)0)->compressions);
  /* Management of Nodes for Skiplist and Buffers */
  printf("Size of List->head                            : %5zu Bytes\n", sizeof((List *)0)->head);
  printf("Size of List->clock_hand                      : %5zu Bytes\n", sizeof((List *)0)->clock_hand);
  printf("Size of List->indexes                         : %5zu Bytes\n", sizeof((List *)0)->indexes);
  printf("Size of List->levels                          : %5zu Bytes\n", sizeof((List *)0)->levels);
  /* Compressor Pool Management */
  printf("Size of List->jobs_lock                       : %5zu Bytes\n", sizeof((List *)0)->jobs_lock);
  printf("Size of List->jobs_cond                       : %5zu Bytes\n", sizeof((List *)0)->jobs_cond);
  printf("Size of List->jobs_parent_cond                : %5zu Bytes\n", sizeof((List *)0)->jobs_parent_cond);
  printf("Size of List->comp_victims                    : %5zu Bytes\n", sizeof((List *)0)->comp_victims);
  printf("Size of List->comp_victims_index              : %5zu Bytes\n", sizeof((List *)0)->comp_victims_index);
  printf("Size of List->victims                         : %5zu Bytes\n", sizeof((List *)0)->victims);
  printf("Size of List->victims_index                   : %5zu Bytes\n", sizeof((List *)0)->victims_index);
  printf("Size of List->victims_compressor_index        : %5zu Bytes\n", sizeof((List *)0)->victims_compressor_index);
  printf("Size of List->active_compressors              : %5zu Bytes\n", sizeof((List *)0)->active_compressors);
  printf("Size of List->compressor_threads              : %5zu Bytes\n", sizeof((List *)0)->compressor_threads);
  printf("Size of List->compressor_pool                 : %5zu Bytes\n", sizeof((List *)0)->compressor_pool);
  printf("Size of List->compressor_id                   : %5zu Bytes\n", sizeof((List *)0)->compressor_id);
  printf("Size of List->compressor_level                : %5zu Bytes\n", sizeof((List *)0)->compressor_level);
  printf("Size of List->compressor_count                : %5zu Bytes\n", sizeof((List *)0)->compressor_count);
  printf("-----------------------------------------------------------\n");
  printf("Size of List                                    %5zu Bytes\n", sizeof(List));


  // -- Compressor Information
  printf("\n");
  printf("Size of Compressor->worker                    : %5zu Bytes\n", sizeof((Compressor *)0)->worker);
  printf("Size of Compressor->jobs_lock                 : %5zu Bytes\n", sizeof((Compressor *)0)->jobs_lock);
  printf("Size of Compressor->jobs_cond                 : %5zu Bytes\n", sizeof((Compressor *)0)->jobs_cond);
  printf("Size of Compressor->jobs_parent_cond          : %5zu Bytes\n", sizeof((Compressor *)0)->jobs_parent_cond);
  printf("Size of Compressor->acive_compressors         : %5zu Bytes\n", sizeof((Compressor *)0)->active_compressors);
  printf("Size of Compressor->runnable                  : %5zu Bytes\n", sizeof((Compressor *)0)->runnable);
  printf("Size of Compressor->victims                   : %5zu Bytes\n", sizeof((Compressor *)0)->victims);
  printf("Size of Compressor->victims_index             : %5zu Bytes\n", sizeof((Compressor *)0)->victims_index);
  printf("Size of Compressor->victims_compressor_index  : %5zu Bytes\n", sizeof((Compressor *)0)->victims_compressor_index);
  printf("Size of Compressor->compressor_id             : %5zu Bytes\n", sizeof((Compressor *)0)->compressor_id);
  printf("Size of Compressor->compressor_level          : %5zu Bytes\n", sizeof((Compressor *)0)->compressor_level);
  printf("-----------------------------------------------------------\n");
  printf("Size of Compressor                              %5zu Bytes\n", sizeof(Compressor));


  // -- SkiplistNode Information
  printf("\n");
  /* Directions for Traversal.  Left-To-Right Mentality. */
  printf("Size of SkiplistNode->right                   : %5zu Bytes\n", sizeof((SkiplistNode *)0)->right);
  printf("Size of SkiplistNode->down                    : %5zu Bytes\n", sizeof((SkiplistNode *)0)->down);
  /* Buffer Reference to the List Item Itself */
  printf("Size of SkiplistNode->target                  : %5zu Bytes\n", sizeof((SkiplistNode *)0)->target);
  printf("-----------------------------------------------------------\n");
  printf("Size of SkiplistNode                            %5zu Bytes\n", sizeof(SkiplistNode));


  // -- Buffer Information
  printf("\n");
  /* Attributes for typical buffer organization and management. */
  printf("Size of Buffer->id                            : %5zu Bytes\n", sizeof((Buffer *)0)->id);
  printf("Size of Buffer->ref_count                     : %5zu Bytes\n", sizeof((Buffer *)0)->ref_count);
  printf("Size of Buffer->pending_sweep                 : %5zu Bytes\n", sizeof((Buffer *)0)->pending_sweep);
  printf("Size of Buffer->popularity                    : %5zu Bytes\n", sizeof((Buffer *)0)->popularity);
  printf("Size of Buffer->victimized                    : %5zu Bytes\n", sizeof((Buffer *)0)->victimized);
  printf("Size of Buffer->is_ephemeral                  : %5zu Bytes\n", sizeof((Buffer *)0)->is_ephemeral);
  printf("Size of Buffer->lock                          : %5zu Bytes\n", sizeof((Buffer *)0)->lock);
  printf("Size of Buffer->reader_cond                   : %5zu Bytes\n", sizeof((Buffer *)0)->reader_cond);
  printf("Size of Buffer->writer_cond                   : %5zu Bytes\n", sizeof((Buffer *)0)->writer_cond);
  printf("Size of Buffer->pending_writers               : %5zu Bytes\n", sizeof((Buffer *)0)->pending_writers);
  /* Cost values for each buffer when pulled from disk or compressed/decompressed. */
  printf("Size of Buffer->comp_cost                     : %5zu Bytes\n", sizeof((Buffer *)0)->comp_cost);
  printf("Size of Buffer->io_cost                       : %5zu Bytes\n", sizeof((Buffer *)0)->io_cost);
  printf("Size of Buffer->comp_hits                     : %5zu Bytes\n", sizeof((Buffer *)0)->comp_hits);
  /* The actual payload we want to cache (i.e.: the page). */
  printf("Size of Buffer->data_length                   : %5zu Bytes\n", sizeof((Buffer *)0)->data_length);
  printf("Size of Buffer->comp_length                   : %5zu Bytes\n", sizeof((Buffer *)0)->comp_length);
  printf("Size of Buffer->data                          : %5zu Bytes\n", sizeof((Buffer *)0)->data);
  /* Tracking for the list we're part of. */
  printf("Size of Buffer->next                          : %5zu Bytes\n", sizeof((Buffer *)0)->next);
  printf("-----------------------------------------------------------\n");
  printf("Size of Buffer                                  %5zu Bytes\n", sizeof(Buffer));


  printf("\n\nQuick Summary Table\n");
  printf("+---------------+-------------+\n");
  printf("| Manager       | %5zu Bytes |\n", sizeof(Manager));
  printf("| Worker        | %5zu Bytes |\n", sizeof(Worker));
  printf("| List          | %5zu Bytes |\n", sizeof(List));
  printf("| Compressor    | %5zu Bytes |\n", sizeof(Compressor));
  printf("| SkiplistNode  | %5zu Bytes |\n", sizeof(SkiplistNode));
  printf("| Buffer        | %5zu Bytes |\n", sizeof(Buffer));
  printf("+---------------+-------------+\n");
  printf("(Note: Size may be slightly off due to alignment and/or pointers-to-alloc'd spaces.)\n");


  printf("\n\n%10s%10s\n", "PAGE_SIZE", "Overhead");
  float size = 0.0;
  for (int i=1024; i<=65536; i*=2) {
    size = (100.0f * sizeof(Buffer)) / (sizeof(Buffer) + i);
    printf("%10i%9.3f%%\n", i, size);
  }

  return 0;
}
