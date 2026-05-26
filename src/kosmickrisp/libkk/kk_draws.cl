/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl_vk.h"

#include "poly/cl/restart.h"

/*
 * To implement drawIndirectCount generically, we dispatch a kernel to
 * clone-and-patch the indirect buffer, predicating out draws as appropriate.
 */
KERNEL(32)
libkk_predicate_indirect(global uint32_t *out, constant uint32_t *in,
                         constant uint32_t *draw_count, uint32_t out_stride_el,
                         uint32_t in_stride_el)
{
   uint draw = cl_global_id.x;
   bool enabled = draw < *draw_count;
   out += draw * out_stride_el;
   in += draw * in_stride_el;

   /* Copy enabled draws, zero predicated draws. */
   for (uint i = 0; i < out_stride_el; ++i) {
      out[i] = enabled ? in[i] : 0;
   }
}

KERNEL(1024)
libkk_unroll_geometry(
   uint64_t index_buffer, global struct poly_heap *heap,
   constant uint32_t *in_draw, global uint32_t *out_draw,
   uint32_t restart_index, uint32_t index_buffer_size_el, uint32_t in_el_size_B,
   uint32_t out_el_size_B, uint32_t flatshade_first, uint32_t mode)
{
   POLY_DECL_UNROLL_RESTART_SCRATCH(scratch, 1024);
   poly_unroll_geometry(out_draw, heap, in_draw, index_buffer,
                        index_buffer_size_el, in_el_size_B, out_el_size_B,
                        restart_index, flatshade_first, mode, scratch);
}
