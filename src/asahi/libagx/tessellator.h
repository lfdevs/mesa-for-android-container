/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "poly/tessellator.h"

#define libagx_tessellate(context, grid, barrier, prim, mode, state)           \
   if (prim == TESS_PRIMITIVE_QUADS) {                                         \
      libagx_tess_quad(context, grid, barrier, state, mode);                   \
   } else if (prim == TESS_PRIMITIVE_TRIANGLES) {                              \
      libagx_tess_tri(context, grid, barrier, state, mode);                    \
   } else {                                                                    \
      assert(prim == TESS_PRIMITIVE_ISOLINES);                                 \
      libagx_tess_isoline(context, grid, barrier, state, mode);                \
   }
