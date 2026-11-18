/*
 * Copyright 2026 Abhay Byte
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/libcl/libcl.h"
#include "compiler/shader_enums.h"
#include "poly/geometry.h"
#include "poly/tessellator.h"

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
   /* v9 JM, unused (0) elsewhere: index budget of a chunk. panlib_tess_draw() keeps
    * the longest run of the chunk's patches whose indices fit it (at least
    * one patch) and hands the rest back to the next chunk. */
   uint32_t idx_limit;

   uint64_t total_patches;
   uint64_t next_patch;
#if defined(PAN_ARCH) && PAN_ARCH == 9
   /* TCS patches statistic, counted by panlib_tess_draw() with idx_limit. */
   uint64_t tcs_stat;
#endif
} PACKED;

/* Vertex stride of a chunk window and what it shares with the next one, for
 * the topologies whose chunks can be cut by vertex range: whole primitives
 * (align), strip vertices repeated by the next chunk (overlap) and vertices
 * per primitive start (div).
 */
static inline void
panlib_chunk_topology(uint32_t prim, uint32_t *align, uint32_t *overlap,
                      uint32_t *div)
{
   *align = 1;
   *overlap = 0;
   *div = 1;

   switch (prim) {
   case MESA_PRIM_LINES:
      *align = *div = 2;
      break;
   case MESA_PRIM_TRIANGLES:
      *align = *div = 3;
      break;
   case MESA_PRIM_LINES_ADJACENCY:
      *align = *div = 4;
      break;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      *align = *div = 6;
      break;
   case MESA_PRIM_LINE_STRIP:
      *overlap = 1;
      break;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      *overlap = 3;
      break;
   case MESA_PRIM_TRIANGLE_STRIP:
      *align = *overlap = 2;
      break;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      *align = 2;
      *overlap = 4;
      *div = 2;
      break;
   default:
      break;
   }
}

/* State of a non-tessellated gpu_prerast draw that the CS loop runs in
 * chunks, see panlib_chunk_setup/step. The first part is written at record
 * time, the rest by the kernels on every execution.
 */
/* The lowering kernels run one workgroup per (vertex, instance): the G615
 * loses the device on grids with thousands of instances (4 x 8192 hangs, 4 x
 * 4000 works), so a chunk holds no more.
 */
#define PANLIB_CHUNK_MAX_INSTANCES 2048u

struct panlib_chunk_draw {
   uint64_t index_buffer;
   /* 0 when the primitives generated query is not counted here. */
   uint64_t prims_generated;

   uint32_t indexed;
   uint32_t max_verts;
   uint32_t prim;
   /* Strip topology with primitive restart. */
   uint32_t restart;
   uint32_t index_size;
   uint32_t index_count_el;
   uint32_t view_count;

   /* Vk[Indexed]DrawIndirectCommand of the API draw, and of this chunk. */
   uint32_t in_draw[5];
   uint32_t draw[5];

   uint32_t next_inst;
   uint32_t next_vertex;
   uint32_t prims_done;
   /* Primitives before this chunk within its instance, GS primitive ID base. */
   uint32_t prim_base;
   uint32_t more;
   /* Chunk start within the API draw, see panvk_gpu_prerast_params. */
   uint32_t chunk_vertex;
   uint32_t chunk_instance;
   /* Copy of "more" stored by the compute CS for the vertex/tiler CS. */
   uint32_t more_vt;
   /* Instances per chunk, 0 for PANLIB_CHUNK_MAX_INSTANCES. v9 JM, which
    * lowered whole draws before, keeps whole instance ranges. */
   uint32_t max_insts;
#if defined(PAN_ARCH) && PAN_ARCH == 9
   /* v9 JM: a strip window whose last restart index comes in
    * its first half cuts the strip after it instead, so that every window
    * moves on by at least half a step and the CPU can bound the chunks. */
   uint32_t cut_tail;
#endif
} PACKED;
