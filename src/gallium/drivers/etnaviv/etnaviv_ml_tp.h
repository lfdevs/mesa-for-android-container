/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "etnaviv_ml.h"

void
etna_ml_lower_transpose(struct etna_ml_subgraph *subgraph,
                        const struct pipe_tensor *input_tensor,
                        struct etna_operation *operation);

void
etna_ml_lower_detranspose(struct etna_ml_subgraph *subgraph,
                          const struct pipe_tensor *output_tensor,
                          struct etna_operation *operation);

void
etna_ml_lower_reshuffle(struct etna_ml_subgraph *subgraph,
                        const struct pipe_ml_operation *first_operation,
                        struct etna_operation *operation);

void
etna_ml_lower_pad(struct etna_ml_subgraph *subgraph,
                  const struct pipe_ml_operation *pad,
                  struct etna_operation *operation);

void
etna_ml_lower_relu(struct etna_ml_subgraph *subgraph,
                   const struct pipe_ml_operation *operation,
                   const struct pipe_tensor *input_tensor,
                   struct etna_operation *relu);

void
etna_ml_lower_absolute(struct etna_ml_subgraph *subgraph,
                       const struct pipe_ml_operation *pad,
                       struct etna_operation *operation);

void
etna_ml_lower_logistic(struct etna_ml_subgraph *subgraph,
                       const struct pipe_ml_operation *pad,
                       struct etna_operation *operation);

void
etna_ml_compile_operation_tp(struct etna_ml_subgraph *subgraph,
                             const struct etna_operation *operation,
                             struct etna_vip_instruction *instruction);

void
etna_ml_emit_operation_tp(struct etna_ml_subgraph *subgraph,
                          struct etna_vip_instruction *operation,
                          unsigned idx);