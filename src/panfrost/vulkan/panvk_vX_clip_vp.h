/*
 * Copyright © 2026 panvk-kbase contributors
 * SPDX-License-Identifier: MIT
 */

/* Clip/cull distance and ViewportIndex plumbing between the pre-raster
 * stages and the FS (panvk_vX_shader.c). The helpers are defined at the end
 * of panvk_vX_shader.c.
 */

#ifndef PANVK_VX_CLIP_VP_H
#define PANVK_VX_CLIP_VP_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "vk_util.h"

#include "nir_builder.h"

struct panvk_dist_elem;
struct panvk_gpu_prerast_gs_info;

#if PAN_ARCH >= 10
/* Generic slots Vulkan never uses. CULL_DIST0/1 keep the raw cull
 * distances (an FS may read them); the per-vertex "cull[i] < 0" flags that
 * the FS kill interpolates go in their own two slots. A passthrough that
 * selects the viewport drops ViewportIndex (it is no HW varying here) and
 * writes a flat copy for the FS instead.
 */
#define PANVK_CULL_FLAG_SLOT0 VARYING_SLOT_TEX0
#define PANVK_CULL_FLAG_SLOT1 VARYING_SLOT_TEX1
#define PANVK_VP_INDEX_SLOT   VARYING_SLOT_TEX2

/* What the FS kill tests in place of CULL_DIST0/1. */
#define PANVK_FS_CULL_KILL_SLOT0 PANVK_CULL_FLAG_SLOT0
#define PANVK_FS_CULL_KILL_SLOT1 PANVK_CULL_FLAG_SLOT1
#define PANVK_FS_CULL_BITS                                                     \
   (VARYING_BIT_CULL_DIST0 | VARYING_BIT_CULL_DIST1 |                          \
    BITFIELD64_BIT(PANVK_CULL_FLAG_SLOT0) |                                    \
    BITFIELD64_BIT(PANVK_CULL_FLAG_SLOT1))

/* GS records keep cull apart from clip (panvk_unmerge_cull_vars()). */
#define PANVK_GS_XFB_NCLIP(nir) ((nir)->info.clip_distance_array_size)
#else
#define PANVK_FS_CULL_KILL_SLOT0 VARYING_SLOT_CULL_DIST0
#define PANVK_FS_CULL_KILL_SLOT1 VARYING_SLOT_CULL_DIST1
#define PANVK_FS_CULL_BITS (VARYING_BIT_CULL_DIST0 | VARYING_BIT_CULL_DIST1)
#define PANVK_GS_XFB_NCLIP(nir) 0
#endif

static void panvk_store_cull_flags(nir_builder *b, nir_shader *nir,
                                   const struct panvk_dist_elem *cull,
                                   nir_def *const *cull_v, unsigned ncull);
static nir_def *panvk_fs_cull_value(nir_builder *b, nir_shader *nir,
                                    uint64_t prev_outputs, unsigned idx);
static uint64_t panvk_fs_dist_defaults(nir_shader *nir);
static void panvk_unmerge_cull_vars(nir_shader *nir);
static unsigned panvk_vp_index_copy(nir_builder *b, nir_def *vp_index,
                                    unsigned base);
static nir_shader *
panvk_fixup_gs_passthrough(const struct panvk_gpu_prerast_gs_info *gs,
                           const nir_shader *gs_nir, nir_shader *pt);

#endif /* PANVK_VX_CLIP_VP_H */
