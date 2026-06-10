/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl_vk.h"
#include "poly/geometry.h"
#include "poly/tessellator.h"

KERNEL(1024)
libkk_prefix_sum_tess(global struct poly_tess_params *p)
{
   local uint scratch[32];
   poly_prefix_sum(scratch, p->counts, p->nr_patches, 1 /* words */,
                   0 /* word */, 1024);

   /* After prefix summing, we know the total # of indices, so allocate the
    * index buffer now. Elect a thread for the allocation.
    */
   barrier(CLK_LOCAL_MEM_FENCE);
   if (cl_local_id.x != 0)
      return;

   /* The last element of an inclusive prefix sum is the total sum */
   uint total = p->nr_patches > 0 ? p->counts[p->nr_patches - 1] : 0;

   /* Allocate 4-byte indices */
   uint32_t elsize_B = sizeof(uint32_t);
   uint32_t size_B = total * elsize_B;
   uint alloc_B = poly_heap_alloc_offs(p->heap, size_B);
   p->index_buffer = (global uint32_t *)(((uintptr_t)p->heap->base) + alloc_B);

   /* ...and now we can generate the API indexed draw */
   global uint32_t *desc = p->out_draws;

   desc[0] = total;              /* count */
   desc[1] = 1;                  /* instance_count */
   desc[2] = alloc_B / elsize_B; /* start */
   desc[3] = 0;                  /* index_bias */
   desc[4] = 0;                  /* start_instance */
}
