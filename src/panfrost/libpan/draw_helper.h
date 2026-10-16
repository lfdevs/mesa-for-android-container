/*
 * Copyright 2025 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <stdatomic.h>
#include <stdint.h>
#include "compiler/libcl/libcl.h"
#include "genxml/gen_macros.h"

#pragma once

#if (PAN_ARCH == 6 || PAN_ARCH == 7)
#include "poly/geometry.h"
#include "poly/tessellator.h"
#endif

#if (PAN_ARCH == 6 || PAN_ARCH == 7)
/* Keep in sync with panvk_varying_buf_id */
enum panlib_varying_buf_id {
   PANLIB_VARY_BUF_GENERAL,
   PANLIB_VARY_BUF_POSITION,
   PANLIB_VARY_BUF_PSIZ,

   /* Keep last */
   PANLIB_VARY_BUF_MAX,
};

struct libpan_draw_helper_index_min_max_result {
    uint32_t min;
    uint32_t max;
};

struct libpan_draw_helper_attrib_buf_info {
    uint32_t divisor;
    uint32_t stride;
    bool per_instance;
};

struct libpan_draw_helper_attrib_info {
    uint32_t base_offset;

    /* When the attribute is per instance, the stride otherwise 0 */
    uint32_t stride;
};

/* Transform feedback captured from the geometry shader emulation, one per
 * capturing draw. The draw path fills everything above `start`;
 * panlib_xfb_scan fills the rest, panlib_xfb_copy reads it.
 */
struct libpan_xfb_draw {
    uint64_t buffer[4];      /* bound address */
    uint32_t size[4];        /* bound size in bytes */
    uint32_t stride[4];      /* the shader's xfb stride, 0 = not captured to */
    uint64_t offsets_in;     /* uint32_t[4]: write offsets before this draw */
    uint64_t offsets_out;    /* uint32_t[4]: after it */
    uint64_t counts;         /* uint32_t per emulation invocation: primitives */
    uint64_t first;          /* uint32_t per invocation: its first primitive */
    uint64_t indices;        /* the emulation's index buffer */
    uint64_t staging;        /* record_dwords per output vertex slot */
    uint64_t table;          /* uint32_t per record dword: buffer << 16 | dword */
    uint32_t invocations;    /* emulation invocations dispatched */
    uint32_t idx_per_invocation;
    uint32_t vpp;            /* vertices per output primitive */
    uint32_t record_dwords;
    uint32_t start[4];       /* scan: byte offset this draw writes from */
    uint32_t written;        /* scan: primitives captured */
    uint32_t needed;         /* scan: primitives emitted */
};

/* Tessellation, one per tessellated draw (jm-v7 014). The draw path fills
 * the parameters, panlib_v7_tess_setup/_prefix and the tessellator the
 * rest. The buffers p points at live in the device's tess arena.
 */
struct panlib_v7_tess {
   struct poly_tess_params p;
   struct poly_heap heap;
   /* VkDrawIndirectCommand of the tess eval draw. */
   uint32_t draw[4];
   uint32_t input_patch_size;
   uint32_t out_patch_size;
   uint32_t max_patches;
   uint32_t max_indices;
   /* struct libpan_draw_helper_varying_buf_info the tess eval draw
    * allocates its varyings from, and what one output vertex takes there
    * (all layers), plus a margin left free: the index count is cut so that
    * the draw fits. 0 for no limit.
    */
   uint64_t varying_info;
   uint32_t varying_bytes;
   uint32_t varying_margin;
};
#endif

#if PAN_ARCH == 9
/* panlib_v9_prerast_indirect() parameters. Addresses are 0 when unused. */
struct panlib_v9_prerast_patch {
   /* VkDraw(Indexed)IndirectCommand and the vkCmdDraw*IndirectCount count. */
   uint64_t cmd;
   uint64_t draw_count;
   /* panvk_gpu_prerast_params: &vertex_count (instance_count and
    * first_instance follow, index_offset is 5 words on) and
    * &vertex.verts_per_instance. */
   uint64_t counts;
   uint64_t verts_per_instance;
   /* Lowered VS COMPUTE job and its first vertex / base instance sysvals. */
   uint64_t vs_job;
   uint64_t first_vertex_sysval;
   uint64_t base_instance_sysval;
   /* VS attribute descriptors and {offset, stride, divisor} of the
    * instance-rate attributes with a divisor, see
    * panlib_v9_draw_indirect_patch(). */
   uint64_t vs_attribs;
   uint64_t inst_attr_info;
   /* GS kernel COMPUTE job and panvk_gpu_prerast_gs_params::vertex_count. */
   uint64_t gs_job;
   uint64_t gs_counts;
   /* VkDrawIndexedIndirectCommand of the generated draw (VS only). */
   uint64_t gen_draw;
   /* {vertex, instance} start of a tessellation chunk, copied to
    * panvk_gpu_prerast_params::chunk_vertex/_instance, or 0. */
   uint64_t chunk;
   uint32_t indexed;
   uint32_t draw_index;
   uint32_t capacity;
   uint32_t inst_attr_mask;
   uint32_t gs_invocations;
   uint32_t _pad;
};
#endif
