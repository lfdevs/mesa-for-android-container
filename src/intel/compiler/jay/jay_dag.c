/*
 * Copyright 2026 Intel Corporation
 * Copyright 2019 Broadcom
 * SPDX-License-Identifier: MIT
 */

#include "jay_dag.h"
#include <stdint.h>
#include "util/ralloc.h"
#include "util/u_dynarray.h"

void
jay_dag_init(struct jay_dag *dag, void *memctx, uint32_t node_count)
{
   assert(node_count >= 1 && "node 0 is reserved and always present");

   *dag = (struct jay_dag) {
      .adjacency = rzalloc_array(memctx, uint32_t, node_count),
      .parent_counts = rzalloc_array(memctx, uint32_t, node_count),
      .node_count = node_count,
      .node = 1,
   };

   util_dynarray_init(&dag->heads, memctx);
   util_dynarray_init(&dag->edges, memctx);
}

void
jay_dag_add_edge(struct jay_dag *dag, uint32_t child)
{
   if (child && child != dag->node) {
      assert(child < dag->node_count);

      /* We have to prune degenerate or duplicate edges */
      for (uint32_t i = (dag->node > 0 ? dag->adjacency[dag->node - 1] : 0);
           i < util_dynarray_num_elements(&dag->edges, uint32_t); ++i) {
         if (*util_dynarray_element(&dag->edges, uint32_t, i) == child)
            return;
      }

      util_dynarray_append(&dag->edges, child);
      dag->parent_counts[child]++;
   }
}

void
jay_dag_next_node(struct jay_dag *dag)
{
   assert(dag->node < dag->node_count);

   dag->adjacency[dag->node++] =
      util_dynarray_num_elements(&dag->edges, uint32_t);
}

void
jay_dag_finalize(struct jay_dag *dag, uint32_t first_node)
{
   for (uint32_t i = dag->node - 1; i >= first_node; --i) {
      if (dag->parent_counts[i] == 0) {
         util_dynarray_append(&dag->heads, i);
      }
   }
}

/**
 * Removes a DAG head from the graph, and moves any new dag heads into the
 * heads list.
 */
void
jay_dag_prune_head(struct jay_dag *dag, uint32_t head)
{
   assert(!dag->parent_counts[head]);
   util_dynarray_delete_unordered(&dag->heads, uint32_t, head);
   uint32_t first = head > 0 ? dag->adjacency[head - 1] : 0;

   for (unsigned i = first; i < dag->adjacency[head]; ++i) {
      uint32_t *it = util_dynarray_element(&dag->edges, uint32_t, i);

      if ((--dag->parent_counts[*it]) == 0) {
         util_dynarray_append(&dag->heads, *it);
      }
   }
}
