/*
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "poly/geometry.h"
#include "poly/cl/tessellator.h"

#if PAN_ARCH >= 6

/*
 * Mali does not expose fixed-function tessellation.  These kernels are the
 * hardware-independent libpoly tessellator entry points used by PanVK's
 * tessellation-as-compute path.
 */
KERNEL(64)
panlib_tess_isoline(constant struct poly_tess_params *p,
                    enum poly_tess_mode mode__2)
{
   if (cl_global_id.x >= p->nr_patches)
      return;
   poly_tess_isoline_process(p, cl_global_id.x, mode__2);
}

KERNEL(64)
panlib_tess_tri(constant struct poly_tess_params *p,
                enum poly_tess_mode mode__2)
{
   if (cl_global_id.x >= p->nr_patches)
      return;
   poly_tess_tri_process(p, cl_global_id.x, mode__2);
}

KERNEL(64)
panlib_tess_quad(constant struct poly_tess_params *p,
                 enum poly_tess_mode mode__2)
{
   if (cl_global_id.x >= p->nr_patches)
      return;
   poly_tess_quad_process(p, cl_global_id.x, mode__2);
}

KERNEL(1024)
panlib_prefix_sum_tess(global struct poly_tess_params *p)
{
   local uint scratch[32];
   poly_prefix_sum(scratch, p->counts, p->nr_patches, 1, 0, 1024);

   barrier(CLK_LOCAL_MEM_FENCE);
   if (cl_local_id.x != 0)
      return;

   const uint total = p->nr_patches ? p->counts[p->nr_patches - 1] : 0;
   const uint32_t elsize_B = sizeof(uint32_t);
   const uint alloc_B = poly_heap_alloc_offs(p->heap, total * elsize_B);

   p->index_buffer =
      (global uint32_t *)(((uintptr_t)p->heap->base) + alloc_B);

   global uint32_t *draw = p->out_draws;
   draw[0] = total;
   draw[1] = 1;
   draw[2] = alloc_B / elsize_B;
   draw[3] = 0;
   draw[4] = 0;
}

#endif

#if PAN_ARCH == 9
/* Mali-G57 (v9) can't run the 1024-wide prefix-sum workgroup (its barrier
 * never completes).  Single-thread serial version with identical results:
 * inclusive scan of counts, index allocation and the indexed indirect draw. */
KERNEL(1)
panlib_prefix_sum_tess_serial(global struct poly_tess_params *p)
{
   uint32_t sum = 0;
   for (uint32_t i = 0; i < p->nr_patches; i++) {
      sum += p->counts[i];
      p->counts[i] = sum;
   }

   const uint total = sum;
   const uint32_t elsize_B = sizeof(uint32_t);
   const uint alloc_B = poly_heap_alloc_offs(p->heap, total * elsize_B);

   p->index_buffer =
      (global uint32_t *)(((uintptr_t)p->heap->base) + alloc_B);

   global uint32_t *draw = p->out_draws;
   draw[0] = total;
   draw[1] = 1;
   draw[2] = alloc_B / elsize_B;
   draw[3] = 0;
   draw[4] = 0;
}
#endif
