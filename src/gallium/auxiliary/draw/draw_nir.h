/**************************************************************************
 *
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#ifndef DRAW_NIR_H
#define DRAW_NIR_H

#include "compiler/nir/nir.h"

bool
draw_nir_lower_opcodes(struct nir_shader *shader);

#endif
