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
static uint
libkk_predicate_draw(global uint32_t *out, constant uint32_t *in,
                     uint32_t out_stride_el, uint32_t in_stride_el,
                     uint draw_id, bool enabled)
{
   out += draw_id * out_stride_el;
   in += draw_id * in_stride_el;

   /* Copy enabled draws, zero predicated draws. */
   for (uint i = 0; i < out_stride_el; ++i) {
      out[i] = enabled ? in[i] : 0;
   }
}

/*
 * Indirect Draw predicate: value > draw_id
 */
KERNEL(32)
libkk_predicate_indirect_gt_draw_id(global uint32_t *out,
                                    constant uint32_t *in,
                                    constant uint32_t *value,
                                    uint32_t out_stride_el,
                                    uint32_t in_stride_el)
{
   uint draw_id = cl_global_id.x;
   bool enabled = *value > draw_id;

   libkk_predicate_draw(out, in, out_stride_el, in_stride_el, draw_id, enabled);
}

/*
 * Indirect Draw predicate: value == 0
 */
KERNEL(32)
libkk_predicate_indirect_eq_zero(global uint32_t *out, constant uint32_t *in,
                                 constant uint32_t *value,
                                 uint32_t out_stride_el, uint32_t in_stride_el)
{
   uint draw_id = cl_global_id.x;
   bool enabled = *value == 0;

   libkk_predicate_draw(out, in, out_stride_el, in_stride_el, draw_id, enabled);
}

/*
 * Indirect Draw predicate: value != 0
 */
KERNEL(32)
libkk_predicate_indirect_neq_zero(global uint32_t *out, constant uint32_t *in,
                                  constant uint32_t *value,
                                  uint32_t out_stride_el,
                                  uint32_t in_stride_el)
{
   uint draw_id = cl_global_id.x;
   bool enabled = *value != 0;

   libkk_predicate_draw(out, in, out_stride_el, in_stride_el, draw_id, enabled);
}

KERNEL(1024)
libkk_unroll_geometry(
   uint64_t index_buffer, global struct poly_heap *heap,
   constant uint32_t *in_draw, global uint32_t *out_draw,
   uint32_t in_draw_stride_el, uint32_t restart_index,
   uint32_t index_buffer_size_el, uint32_t in_el_size_B,
   uint32_t out_el_size_B, uint32_t flatshade_first, uint32_t mode)
{
   uint gid = cl_group_id.x;
   in_draw += gid * in_draw_stride_el;
   out_draw += gid * 5;

   POLY_DECL_UNROLL_RESTART_SCRATCH(scratch, 1024);
   poly_unroll_geometry(out_draw, heap, in_draw, index_buffer,
                        index_buffer_size_el, in_el_size_B, out_el_size_B,
                        restart_index, flatshade_first, mode, scratch);
}
