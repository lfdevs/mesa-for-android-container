/*
 * Copyright © 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */
#ifndef _BI_SWIZZLES_H_
#define _BI_SWIZZLES_H_

#include "bi_opcodes.h"
#include "compiler.h"

/* Bitset of supported bi_swizzle for each src of each instruction.
 *
 * These are bifrost-only. Supported swizzles on valhall are determined by the
 * flags in va_src_info.
 *
 * Generated in bi_swizzles.c.py */
extern uint32_t bi_op_swizzles[BI_NUM_OPCODES][BI_MAX_SRCS];

#endif
