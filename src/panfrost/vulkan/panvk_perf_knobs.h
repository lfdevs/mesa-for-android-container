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
};

extern uint32_t panvk_perf_knobs;

#define PANVK_PERF(knob) likely(panvk_perf_knobs & PANVK_PERF_##knob)

void panvk_perf_knobs_init(void);

#endif /* PANVK_PERF_KNOBS_H */
