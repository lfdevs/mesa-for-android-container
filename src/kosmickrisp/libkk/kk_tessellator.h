/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "poly/tessellator.h"

#define libkk_tessellate(context, grid, barrier, prim, mode, state)            \
   if (prim == TESS_PRIMITIVE_QUADS) {                                         \
      libkk_tess_quad(context, grid, barrier, state, mode);                    \
   } else if (prim == TESS_PRIMITIVE_TRIANGLES) {                              \
      libkk_tess_tri(context, grid, barrier, state, mode);                     \
   } else {                                                                    \
      assert(prim == TESS_PRIMITIVE_ISOLINES);                                 \
      libkk_tess_isoline(context, grid, barrier, state, mode);                 \
   }
