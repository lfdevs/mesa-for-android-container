/*
 * Copyright 2026 Intel Corporation
 * Copyright 2019 Broadcom
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/u_dynarray.h"

struct jay_dag {
   struct util_dynarray edges;
   uint32_t *adjacency;
   uint32_t node, node_count;
};

struct jay_dag_iterator {
   const struct jay_dag *dag;
   struct util_dynarray heads;
   uint32_t *parent_counts;
};

static inline void
jay_dag_init(struct jay_dag *dag, void *memctx, uint32_t node_count)
{
   assert(node_count >= 1 && "node 0 is reserved and always present");

   *dag = (struct jay_dag) {
      .adjacency = rzalloc_array(memctx, uint32_t, node_count),
      .node_count = node_count,
      .node = 1,
   };

   util_dynarray_init(&dag->edges, memctx);
}

static inline void
jay_dag_transpose(struct jay_dag *out, const struct jay_dag *in)
{
   jay_dag_init(out, in->edges.mem_ctx, in->node_count);

   /* Initialize the output edge array so we can do random access writes */
   memset(util_dynarray_grow_bytes(&out->edges, in->edges.size, 1), 0,
          in->edges.size);

   /* Determine the number of edges for each node after transpose */
   uint32_t *count = calloc(in->node_count, sizeof(uint32_t));
   util_dynarray_foreach(&in->edges, uint32_t, edge) {
      count[*edge]++;
   }

   /* Prefix sum to get the layout of adjacency[] */
   unsigned n = 0;
   for (unsigned i = 0; i < in->node_count; ++i) {
      n += count[i];
      out->adjacency[i] = n;
   }

   /* Now copy the edges into place */
   for (uint32_t i = 0; i < in->node_count; ++i) {
      uint32_t first_adj = i > 0 ? in->adjacency[i - 1] : 0;

      for (unsigned j = first_adj; j < in->adjacency[i]; ++j) {
         uint32_t *node = util_dynarray_element(&in->edges, uint32_t, j);
         assert(*node && count[*node] > 0 && "exact calculations");
         count[*node]--;
         uint32_t idx = out->adjacency[(*node) - 1] + count[*node];
         *util_dynarray_element(&out->edges, uint32_t, idx) = i;
      }
   }
}

static inline void
jay_dag_iterator_init(struct jay_dag_iterator *it, const struct jay_dag *dag)
{
   *it = (struct jay_dag_iterator) {
      .dag = dag,
      .parent_counts =
         rzalloc_array(dag->edges.mem_ctx, uint32_t, dag->node_count),
   };

   util_dynarray_init(&it->heads, dag->edges.mem_ctx);
}

static inline void
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
   }
}

static inline void
jay_dag_next_node(struct jay_dag *dag)
{
   assert(dag->node < dag->node_count);

   dag->adjacency[dag->node++] =
      util_dynarray_num_elements(&dag->edges, uint32_t);
}

static inline void
jay_dag_iterate(struct jay_dag_iterator *it, uint32_t first, uint32_t last)
{
   assert(it->heads.size == 0 && "must be zeroed on entry");
   uint32_t first_adj = first > 0 ? it->dag->adjacency[first - 1] : 0;

   for (unsigned i = first_adj; i < it->dag->adjacency[last]; ++i) {
      uint32_t *node = util_dynarray_element(&it->dag->edges, uint32_t, i);
      it->parent_counts[*node]++;
   }

   for (uint32_t i = last; i >= first; --i) {
      if (it->parent_counts[i] == 0) {
         util_dynarray_append(&it->heads, i);
      }
   }
}

/**
 * Removes a DAG head and moves any new dag heads into the heads list.
 */
static inline void
jay_dag_take_head(struct jay_dag_iterator *it, uint32_t head)
{
   assert(!it->parent_counts[head]);
   util_dynarray_delete_unordered(&it->heads, uint32_t, head);
   uint32_t first = head > 0 ? it->dag->adjacency[head - 1] : 0;

   for (unsigned i = first; i < it->dag->adjacency[head]; ++i) {
      uint32_t *node = util_dynarray_element(&it->dag->edges, uint32_t, i);

      if ((--it->parent_counts[*node]) == 0) {
         util_dynarray_append(&it->heads, *node);
      }
   }
}

static inline void
jay_dag_iterator_reset(struct jay_dag_iterator *it)
{
   while (it->heads.size) {
      jay_dag_take_head(it, util_dynarray_top(&it->heads, uint32_t));
   }
}

static inline void
jay_dag_print(struct jay_dag *dag)
{
   for (unsigned i = 0; i < dag->node_count; ++i) {
      uint32_t first = i > 0 ? dag->adjacency[i - 1] : 0;
      for (unsigned j = first; j < dag->adjacency[i]; ++j) {
         uint32_t *it = util_dynarray_element(&dag->edges, uint32_t, j);
         printf("%u->%u\n", i, *it);
      }
   }
}

#define jay_dag_foreach_edge(dag, head, it)                                    \
   for (uint32_t *it = ((uint32_t *) (dag)->edges.data) +                      \
                       ((head) > 0 ? (dag)->adjacency[(head) - 1] : 0);        \
        it < ((uint32_t *) (dag)->edges.data) + ((dag)->adjacency[(head)]);    \
        ++it)
