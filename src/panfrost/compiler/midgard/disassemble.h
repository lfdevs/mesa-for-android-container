/*
 * Copyright (C) 2019 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void disassemble_midgard(FILE *fp, const void *code, size_t size,
                         uint64_t gpu_id, bool verbose);
