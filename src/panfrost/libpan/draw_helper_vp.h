/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PANLIB_VP_RUN_HEADER_BYTES 12u
#define PANLIB_VP_RUN_BYTES 12u
#define PANLIB_VP_MAX_VIEWPORTS 16u
#define PANLIB_VP_RESTART 0xffffffffu
/* pack_prim_id: run index = record | (primitive ID & 0xffff) << 16. Records
 * are capped at 65536 (PANVK_GPU_PRERAST_MAX_INVOCATIONS). Run draws have
 * primitive restart off, so any value is a vertex.
 */
#define PANLIB_VP_PRIM_ID_SHIFT 16u
#define PANLIB_VP_PACKED_RECORD_MASK 0xffffu
#define PANLIB_VP_MAX_INPUT_INDICES (1u << 24)
#define PANLIB_VP_MAX_OUT_INDICES (1u << 24)
#define PANLIB_VP_MAX_RUNS ((UINT32_MAX - PANLIB_VP_RUN_HEADER_BYTES) / PANLIB_VP_RUN_BYTES)

struct panlib_vp_run {
   uint32_t viewport;
   uint32_t first_index;
   uint32_t index_count;
};

struct panlib_vp_run_header {
   uint32_t index_count;
   uint32_t run_count;
   uint32_t overflow;
};

/* Selected-viewport depth clamp, two little-endian floats. The run loop
 * walks this with an immediate subtract: v11 cannot add a shifted
 * viewport to a base address.
 */
struct panlib_vp_clamp {
   float z_min;
   float z_max;
};

/* The kernel argument struct is emitted by the libpan shader generator
 * from panlib_vp_runs_launch. outputs_written is the GS mask before the
 * passthrough drops the viewport bit.
 */

static inline bool
panlib_vp_run_bytes(uint32_t run_capacity, uint64_t *bytes)
{
   if (run_capacity > PANLIB_VP_MAX_RUNS)
      return false;

   *bytes = PANLIB_VP_RUN_HEADER_BYTES +
            (uint64_t)run_capacity * PANLIB_VP_RUN_BYTES;
   return true;
}
