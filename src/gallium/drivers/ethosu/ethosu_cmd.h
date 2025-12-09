/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#ifndef ETHOSU_CMD_H
#define ETHOSU_CMD_H

#include "ethosu_ml.h"

void ethosu_emit_cmdstream(struct ethosu_subgraph *subgraph);

#endif /* ETHOSU_CMD_H */
