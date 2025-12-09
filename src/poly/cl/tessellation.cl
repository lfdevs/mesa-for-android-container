/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "poly/geometry.h"
#include "poly/tessellator.h"

uint
poly_tcs_patch_vertices_in(constant struct poly_tess_params *p)
{
   return p->input_patch_size;
}

uint
poly_tes_patch_vertices_in(constant struct poly_tess_params *p)
{
   return p->output_patch_size;
}

uint
poly_tcs_unrolled_id(constant struct poly_tess_params *p, uint3 wg_id)
{
   return (wg_id.y * p->patches_per_instance) + wg_id.x;
}

/*
 * Helper to lower indexing for a tess eval shader ran as a compute shader. This
 * handles the tess+geom case. This is simpler than the general input assembly
 * lowering, as we know:
 *
 * 1. the index buffer is U32
 * 2. the index is in bounds
 *
 * Therefore we do a simple load. No bounds checking needed.
 */
uint32_t
poly_load_tes_index(constant struct poly_tess_params *p, uint32_t index)
{
   /* Swap second and third vertices of each triangle to flip winding order
    * dynamically if needed.
    */
   if (p->ccw) {
      uint id = index % 3;

      if (id == 1)
         index++;
      else if (id == 2)
         index--;
   }

   return p->index_buffer[index];
}

uintptr_t
poly_tcs_out_address(constant struct poly_tess_params *p, uint patch_id,
                     uint vtx_id, gl_varying_slot location, uint nr_patch_out,
                     uint out_patch_size, uint64_t vtx_out_mask)
{
   uint stride_el =
      poly_tcs_out_stride_el(nr_patch_out, out_patch_size, vtx_out_mask);

   uint offs_el =
      poly_tcs_out_offs_el(vtx_id, location, nr_patch_out, vtx_out_mask);

   offs_el += patch_id * stride_el;

   /* Written to match the AGX addressing mode */
   return (uintptr_t)(p->tcs_buffer) + (((uintptr_t)offs_el) << 2);
}

static uint
tes_unrolled_patch_id(uint raw_id)
{
   return raw_id / POLY_TES_PATCH_ID_STRIDE;
}

uint
poly_tes_patch_id(constant struct poly_tess_params *p, uint raw_id)
{
   return tes_unrolled_patch_id(raw_id) % p->patches_per_instance;
}

static uint
tes_vertex_id_in_patch(uint raw_id)
{
   return raw_id % POLY_TES_PATCH_ID_STRIDE;
}

float2
poly_load_tess_coord(constant struct poly_tess_params *p, uint raw_id)
{
   uint patch = tes_unrolled_patch_id(raw_id);
   uint vtx = tes_vertex_id_in_patch(raw_id);

   global struct poly_tess_point *t =
      &p->patch_coord_buffer[p->coord_allocs[patch] + vtx];

   /* Written weirdly because NIR struggles with loads of structs */
   uint2 fixed = *((global uint2 *)t);

   /* Convert fixed point to float */
   return convert_float2(fixed) / (1u << 16);
}

uintptr_t
poly_tes_in_address(constant struct poly_tess_params *p, uint raw_id, uint vtx_id,
                    gl_varying_slot location)
{
   uint patch = tes_unrolled_patch_id(raw_id);

   return poly_tcs_out_address(p, patch, vtx_id, location,
                               p->tcs_patch_constants, p->output_patch_size,
                               p->tcs_per_vertex_outputs);
}

float4
poly_tess_level_outer_default(constant struct poly_tess_params *p)
{
   return vload4(0, p->tess_level_outer_default);
}

float2
poly_tess_level_inner_default(constant struct poly_tess_params *p)
{
   return vload2(0, p->tess_level_inner_default);
}
