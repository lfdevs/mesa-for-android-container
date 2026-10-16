/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "compiler/shader_enums.h"
#include "genxml/gen_macros.h"

#if PAN_ARCH >= 10

/* An indirect fan lowered in one pass (not chunked): clamps its counts to
 * what the arena holds, so the triangles past it are dropped instead of
 * drawn from stale records and indices. The words are the lowered VS
 * params (vertex count, instance count, vertices per instance), its
 * dispatch grid and the generated draw.
 */
KERNEL(1)
panlib_fan_clamp_draw(global uint32_t *vertex_count,
                      global uint32_t *instance_count,
                      global uint32_t *verts_per_instance,
                      global uint32_t *dispatch, global uint32_t *gen_draw,
                      uint32_t cap)
{
   uint32_t n = *vertex_count, k = *instance_count;

   if ((uint64_t)n * k <= cap)
      return;

   n = min(n, cap);
   k = min(k, cap / n);

   *vertex_count = n;
   *instance_count = k;
   *verts_per_instance = n;
   dispatch[0] = n;
   dispatch[1] = k;
   gen_draw[0] = n;
   gen_draw[1] = k;
}

#endif
