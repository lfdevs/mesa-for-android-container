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
   uint32_t indexed;
   uint32_t draw_index;
   uint32_t capacity;
   uint32_t inst_attr_mask;
   uint32_t gs_invocations;
   uint32_t _pad;
};
#endif
