/*
 * Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_SCHED_H
#define ETHOSU_SCHED_H

#include "ethosu_ml.h"

void ethosu_sched_operation(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation);

#endif /* ETHOSU_SCHED_H */
