/*
 * Copyright © 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

/* Build 115 driver-side performance changes (perf review 2026-10-26,
 * package P-A). Each change has an environment knob that brings the 114
 * behaviour back with "<name>=0". The knobs are resolved once per process,
 * at the first vkCreateInstance (panvk_debug_init()), so hot paths only test
 * a bit of panvk_perf_knobs. Like PANVK_DEBUG, they are read with
 * os_get_option(): on Android a debug.mesa.* property works too.
 */

#ifndef PANVK_PERF_KNOBS_H
#define PANVK_PERF_KNOBS_H

#include <stdint.h>

#include "util/macros.h"

enum panvk_perf_knob {
   /* A1, PANVK_CSF_FS_IDLE_CLEAN: a CSF draw that does not run its FS (the
    * depth bounds stand-in of a depth-only pipeline, or an FS with nothing
    * to write) leaves the FS state clean instead of dirtying it again. */
   PANVK_PERF_FS_IDLE_CLEAN = 1 << 0,
   /* C1, PANVK_DB_ALT: v9+ fragment shaders with the depth bounds lowering
    * get an unlowered compile, drawn while the test is off (jm-v7 076). */
   PANVK_PERF_DB_ALT = 1 << 1,
   /* C1, PANVK_DEPTH_BOUNDS: the depthBounds feature is exposed. */
   PANVK_PERF_DEPTH_BOUNDS = 1 << 2,
   /* PANVK_CSF_INDIRECT_REEMIT: a CSF draw after an indirect draw that
    * repeated the VS tables or push uniforms rebuilds them. */
   PANVK_PERF_CSF_INDIRECT_REEMIT = 1 << 3,
   /* A2, PANVK_CSF_DESC_DIRTY: CSF descriptor binds and pushes dirty only
    * the stages that use the changed sets. */
   PANVK_PERF_CSF_DESC_DIRTY = 1 << 4,
   /* A4, PANVK_VB_DIRTY_CMP: vkCmdBindVertexBuffers2 dirties the vertex
    * buffers only when a binding's address or size changes. */
   PANVK_PERF_VB_DIRTY_CMP = 1 << 5,
   /* A5, PANVK_PRERAST_FAST_SKIP: a CSF draw that gpu_prerast does not
    * want skips the other gpu_prerast checks. */
   PANVK_PERF_PRERAST_FAST_SKIP = 1 << 6,
   /* M3, PANVK_DESC_POOL_HINT: descriptor set allocation starts the free
    * slot search at the first word that can hold a free set, and a pool
    * reset only visits the allocated sets. */
   PANVK_PERF_DESC_POOL_HINT = 1 << 7,
   /* Package P-C. C2 (a), PANVK_WSI_SW_CACHED: the sw WSI reads frames
    * back through a cached memory type plus an invalidate instead of the
    * uncached coherent one. */
   PANVK_PERF_WSI_SW_CACHED = 1 << 8,
   /* C2 (b), PANVK_WSI_SW_DEFER_REPLY: the X11 event thread collects the
    * reply to each sw MIT-SHM put; the present thread does not block on
    * the round trip. */
   PANVK_PERF_WSI_SW_DEFER_REPLY = 1 << 9,
   /* K1, PANVK_ANDROID_PRESENT_FENCE: the PanVK Android swapchain waits
    * for the presented image's fence instead of the whole device. */
   PANVK_PERF_ANDROID_PRESENT_FENCE = 1 << 10,
   /* D13, PANVK_ANDROID_PRESENT_THREAD: the PanVK Android swapchain copies
    * and posts frames on its own thread; vkQueuePresentKHR only queues. */
   PANVK_PERF_ANDROID_PRESENT_THREAD = 1 << 11,
   /* C4, PANVK_PRELOAD_BARRIER_TRACK: on CSF, the preload barrier of a
    * LOAD pass waits for earlier fragment jobs only if one of them may
    * have written what the pass loads. */
   PANVK_PERF_PRELOAD_BARRIER_TRACK = 1 << 12,
   /* Build 116. PANVK_LOGICOP_FPK: logic ops that ignore the destination
    * (CLEAR, COPY, COPY_INVERTED, SET) keep forward pixel kill. */
   PANVK_PERF_LOGICOP_FPK = 1 << 13,
   /* PANVK_V9_JM_MERGE_WG: v9 JM compute jobs (vkCmdDispatch, libpan
    * kernels, gpu_prerast) set "Allow merging workgroups" from the shader,
    * as CSF does. */
   PANVK_PERF_V9_JM_MERGE_WG = 1 << 14,
   /* PANVK_PRERAST_MERGE_WG: v9+ gpu_prerast kernels (lowered VS/TES, GS,
    * polygon), one invocation per workgroup, are compiled for and
    * dispatched with "Allow merging workgroups". */
   PANVK_PERF_PRERAST_MERGE_WG = 1 << 15,
};

extern uint32_t panvk_perf_knobs;

#define PANVK_PERF(knob) likely(panvk_perf_knobs & PANVK_PERF_##knob)

void panvk_perf_knobs_init(void);

#endif /* PANVK_PERF_KNOBS_H */
