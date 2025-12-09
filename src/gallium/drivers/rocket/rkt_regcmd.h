/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef RKT_REGCMD_H
#define RKT_REGCMD_H

#include "rkt_ml.h"

void rkt_fill_regcmd(struct rkt_ml_subgraph *subgraph,
                     const struct rkt_operation *operation,
                     struct util_dynarray *regs, unsigned task_num);

#endif /* RKT_REGCMD_H */
