/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef RKT_COEFS_H
#define RKT_COEFS_H

#include "rkt_ml.h"

struct pipe_resource *
rkt_fill_weights(struct rkt_ml_subgraph *subgraph,
                 const struct pipe_ml_operation *poperation);

struct pipe_resource *
rkt_fill_biases(struct rkt_ml_subgraph *subgraph,
                const struct pipe_ml_operation *poperation,
                unsigned *truncate_bits);

#endif /* RKT_COEFS_H */