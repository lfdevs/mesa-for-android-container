/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_ENCODE_H
#define ETHOSU_ENCODE_H

#include "ethosu_ml.h"
#ifdef __cplusplus
extern "C" {
#endif

bool
weight_cache_lookup(struct ethosu_ml_device *device,
                    const struct ethosu_operation *operation,
                    const uint8_t *input_weights,
                    long input_weights_size,
                    uint8_t **weights, long *weights_size);

void
weight_cache_insert(struct ethosu_ml_device *device,
                    const struct ethosu_operation *operation,
                    const uint8_t *input_weights,
                    long input_weights_size,
                    const uint8_t *weights, long weights_size);

void
ml_reorder_encode_weights(struct ethosu_subgraph *subgraph,
                          struct ethosu_operation *operation,
                          const uint8_t *input_weights,
                          long input_weights_size,
                          uint8_t **weights,
                          long *weights_size);

void
ethosu_weight_cache_destroy(struct ethosu_ml_device *device);

#ifdef __cplusplus
}
#endif

#endif /* ETHOSU_ENCODE_H */
