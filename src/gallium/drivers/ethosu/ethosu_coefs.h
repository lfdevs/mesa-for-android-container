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
           int32_t *bias_data,
           uint8_t *weight_data,
           unsigned weight_size);

void
fill_lut(struct ethosu_subgraph *subgraph,
         struct ethosu_operation *operation,
         void *lut);

#endif /* ETHOSU_COEFS_H */
