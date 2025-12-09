/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_COEFS_H
#define ETHOSU_COEFS_H

#include "ethosu_ml.h"

void
fill_coefs(struct ethosu_subgraph *subgraph,
           struct ethosu_operation *operation,
           struct pipe_resource *bias_rsrc,
           struct pipe_resource *weight_rsrc);

#endif /* ETHOSU_COEFS_H */
