/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/libcl/libcl.h"
#include "poly/geometry.h"
#include "poly/tessellator.h"

struct panlib_tess_sync {
   uint32_t seqno;
   uint32_t error;
};

/* Per-draw tessellation state shared by the CS loop and the panlib_tess_*
 * kernels. Each loop iteration either starts a new chunk of patches (VS, TCS,
 * tessellator) or draws the next index range of the current chunk.
 */
struct panlib_tess_draw {
   struct poly_tess_params p;
   struct poly_heap heap;

   /* Vk[Indexed]DrawIndirectCommand of the API draw. */
   uint32_t in_draw[5];
   /* The part of the API draw the VS runs over for the current chunk. */
   uint32_t vs_draw[5];
   uint32_t tcs_grid[3];
   /* Indexed draw of this iteration's range of tessellator indices. */
   uint32_t draw[5];

   uint32_t input_patch_size;
   uint32_t max_patches;
   uint32_t indexed;
   /* Max indices drawn per iteration, 0 for a whole chunk. */
   uint32_t range;
   /* Vertices per output primitive. */
   uint32_t verts_per_prim;

   uint32_t patches_per_instance;
   uint32_t instances;
   uint32_t new_chunk;
   uint32_t chunk_patches;
   /* First patch of the chunk within its instance, the primitive ID base. */
   uint32_t prim_base;
   uint32_t idx_total;
   uint32_t idx_cursor;
   /* Output primitives drawn before this iteration, GS primitive ID base. */
   uint32_t gs_prim_base;
   uint32_t prims_done;
   uint32_t more;
   /* Chunk start within the API draw, see panvk_gpu_prerast_params. */
   uint32_t chunk_vertex;
   uint32_t chunk_instance;
   uint32_t _pad;

   uint64_t total_patches;
   uint64_t next_patch;

   struct panlib_tess_sync ready;
   struct panlib_tess_sync free;
} PACKED;
