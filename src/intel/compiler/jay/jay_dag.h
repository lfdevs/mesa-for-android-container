/*
 * Copyright 2026 Intel Corporation
 * Copyright 2019 Broadcom
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/u_dynarray.h"

struct jay_dag {
   struct util_dynarray heads, edges;
   uint32_t *parent_counts;
   uint32_t *adjacency;
   uint32_t node, node_count;
};

void jay_dag_init(struct jay_dag *dag, void *memctx, uint32_t node_count);
void jay_dag_prune_head(struct jay_dag *dag, uint32_t head);
void jay_dag_add_edge(struct jay_dag *dag, uint32_t child);
void jay_dag_finalize(struct jay_dag *dag, uint32_t first_node);
void jay_dag_next_node(struct jay_dag *dag);
