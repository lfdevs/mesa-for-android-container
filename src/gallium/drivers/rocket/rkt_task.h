/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef RKT_TASK_H
#define RKT_TASK_H

#include "rkt_ml.h"

void rkt_split_tasks(struct rkt_ml_subgraph *subgraph,
                     struct rkt_operation *operation);

#endif /* RKT_TASK_H */