/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "poly/cl/tessellator.h"

KERNEL(1)
libkk_tess_isoline(constant struct poly_tess_params *p,
                   enum poly_tess_mode tess_mode)
{
   uint patch = cl_global_id.x;
   poly_tess_isoline_process(p, patch, tess_mode);
}

KERNEL(1)
libkk_tess_tri(constant struct poly_tess_params *p,
               enum poly_tess_mode tess_mode)
{
   uint patch = cl_global_id.x;
   poly_tess_tri_process(p, patch, tess_mode);
}

KERNEL(1)
libkk_tess_quad(constant struct poly_tess_params *p,
                enum poly_tess_mode tess_mode)
{
   uint patch = cl_global_id.x;
   poly_tess_quad_process(p, patch, tess_mode);
}
