/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/libcl/libcl.h"
#include "compiler/shader_enums.h"

#include "util/bitscan.h"
#include "util/u_math.h"

#ifdef __OPENCL_VERSION__
#include "compiler/libcl/libcl_vk.h"
#endif

#pragma once

#define POLY_MAX_SO_BUFFERS     4
#define POLY_MAX_VERTEX_STREAMS 4

enum poly_gs_shape {
   /* Indexed, where indices are encoded as:
    *
    *    round_to_pot(max_indices) * round_to_pot(input_primitives) *
    *                              * instance_count
    *
    * invoked for max_indices * input_primitives * instance_count indices.
    *
    * This is used with any dynamic topology. No hardware instancing used.
    */
   POLY_GS_SHAPE_DYNAMIC_INDEXED,

   /* Indexed with a static index buffer. Indices ranges up to max_indices.
    * Hardware instance count = input_primitives * software instance count.
    */
   POLY_GS_SHAPE_STATIC_INDEXED,

   /* Non-indexed. Dispatched as:
    *
    *    (max_indices, input_primitives * instance count).
    */
   POLY_GS_SHAPE_STATIC_PER_PRIM,

   /* Non-indexed. Dispatched as:
    *
    *    (max_indices * input_primitives, instance count).
    */
   POLY_GS_SHAPE_STATIC_PER_INSTANCE,
};

static inline unsigned
poly_gs_rast_vertices(enum poly_gs_shape shape, unsigned max_indices,
                      unsigned input_primitives, unsigned instance_count)
{
   switch (shape) {
   case POLY_GS_SHAPE_DYNAMIC_INDEXED:
      return max_indices * input_primitives * instance_count;

   case POLY_GS_SHAPE_STATIC_INDEXED:
   case POLY_GS_SHAPE_STATIC_PER_PRIM:
      return max_indices;

   case POLY_GS_SHAPE_STATIC_PER_INSTANCE:
      return max_indices * input_primitives;
   }

   UNREACHABLE("invalid shape");
}

static inline unsigned
poly_gs_rast_instances(enum poly_gs_shape shape, unsigned max_indices,
                       unsigned input_primitives, unsigned instance_count)
{
   switch (shape) {
   case POLY_GS_SHAPE_DYNAMIC_INDEXED:
      return 1;

   case POLY_GS_SHAPE_STATIC_INDEXED:
   case POLY_GS_SHAPE_STATIC_PER_PRIM:
      return input_primitives * instance_count;

   case POLY_GS_SHAPE_STATIC_PER_INSTANCE:
      return instance_count;
   }

   UNREACHABLE("invalid shape");
}

static inline bool
poly_gs_indexed(enum poly_gs_shape shape)
{
   return shape == POLY_GS_SHAPE_DYNAMIC_INDEXED ||
          shape == POLY_GS_SHAPE_STATIC_INDEXED;
}

static inline unsigned
poly_gs_index_size(enum poly_gs_shape shape)
{
   switch (shape) {
   case POLY_GS_SHAPE_DYNAMIC_INDEXED:
      return 4;
   case POLY_GS_SHAPE_STATIC_INDEXED:
      return 1;
   default:
      return 0;
   }
}

/* Heap to allocate from. */
struct poly_heap {
   DEVICE(uchar) base;
   uint32_t bottom, size;
} PACKED;
static_assert(sizeof(struct poly_heap) == 4 * 4);

#ifdef __OPENCL_VERSION__
static inline uint
poly_heap_alloc_offs(global struct poly_heap *heap, uint size_B)
{
   size_B = align(size_B, 16);

   uint offs =
      atomic_fetch_add((volatile atomic_uint *)(&heap->bottom), size_B);

   /* Use printf+abort because assert is stripped from release builds. */
   if (heap->bottom >= heap->size) {
      printf(
         "FATAL: GPU heap overflow, allocating size %u, at offset %u, heap size %u!",
         size_B, offs, heap->size);

      abort();
   }

   return offs;
}

static inline global void *
poly_heap_alloc(global struct poly_heap *heap, uint size_B)
{
   return heap->base + poly_heap_alloc_offs(heap, size_B);
}

uint64_t nir_load_ro_sink_address_poly(void);

static inline uint64_t
poly_index_buffer(uint64_t index_buffer, uint size_el, uint offset_el,
                  uint elsize_B)
{
   if (offset_el < size_el)
      return index_buffer + (offset_el * elsize_B);
   else
      return nir_load_ro_sink_address_poly();
}
#endif

/** Parameters that feed a vertex (or tessellation evaluation) shader.
 *
 * From the perspective of libpoly, vertex and tessellation evaluation shaders
 * are identical.  One just fets fed by the hardware's input assmebly (which
 * may be emulated by the driver) and the other gets fed from the tessellator.
 * However, from the perspective of a geometry dispatch, they are identical.
 */
struct poly_vertex_params {
   /* Index buffer if present. */
   uint64_t index_buffer;

   /* Size of an index in the index buffer, in bytes */
   uint32_t index_size_B;

   /* Size of the bound index buffer for bounds checking */
   uint32_t index_buffer_range_el;

   /* Number of vertices per instance. Written by CPU for direct draw, indirect
    * setup kernel for indirect. This is used for VS->GS and VS->TCS indexing.
    */
   uint32_t verts_per_instance;

   /* Within an indirect VS draw, the grids used to dispatch the VS written
    * out by the GS indirect setup kernel or the CPU for a direct draw. This is
    * the "indirect local" format: first 3 is in threads, second 3 is in grid
    * blocks. This lets us use nontrivial workgroups with indirect draws without
    * needing any predication.
    */
   uint32_t grid[6];

   uint32_t _pad;

   /* Output buffer for vertex data */
   uint64_t output_buffer;

   /* Mask of outputs present in the output buffer */
   uint64_t outputs;
} PACKED;
static_assert(sizeof(struct poly_vertex_params) == 16 * 4);

static inline void
poly_vertex_params_init(struct poly_vertex_params *p,
                        uint64_t outputs, const uint32_t wg_size[3])
{
   *p = (struct poly_vertex_params) {
      .outputs = outputs,
      .grid = {
         0, 0, 1, /* x/y are set by poly_vertex_params_set_draw() */
         wg_size[0], wg_size[1], wg_size[2],
      },
   };
}

static inline void
poly_vertex_params_set_draw(struct poly_vertex_params *p,
                            uint32_t vertex_count, uint32_t instance_count)
{
   /* Invoke VS as (vertices, instances) */
   p->verts_per_instance = vertex_count;
   p->grid[0] = vertex_count;
   p->grid[1] = instance_count;
}

static inline uint
poly_index_buffer_range_el(uint size_el, uint offset_el)
{
   return offset_el < size_el ? (size_el - offset_el) : 0;
}

/* This must match VkDraw[Indexed]IndirectCommand
 *
 * The vertex/index_count and first_vertex/index fields line up, as does
 * instance_count.  The only ones that don't are vertexOffset and
 * firstInstance but we always set those to zero.
 */
struct poly_indirect_draw {
   union {
      uint32_t vertex_count;
      uint32_t index_count;
   };
   uint32_t instance_count;
   union {
      uint32_t first_vertex;
      uint32_t first_index;
   };
   uint32_t zeros[2];
};
static_assert(sizeof(struct poly_indirect_draw) == 5 * 4);

struct poly_geometry_params {
   /* Address of count buffer. For an indirect draw, this will be written by the
    * indirect setup kernel.
    */
   DEVICE(uint) count_buffer;

   /* Address of the primitives generated counters */
   DEVICE(uint) prims_generated_counter[POLY_MAX_VERTEX_STREAMS];
   DEVICE(uint) xfb_prims_generated_counter[POLY_MAX_VERTEX_STREAMS];
   DEVICE(uint) xfb_overflow[POLY_MAX_VERTEX_STREAMS];
   DEVICE(uint) xfb_any_overflow;

   /* Pointers to transform feedback buffer offsets in bytes */
   DEVICE(uint) xfb_offs_ptrs[POLY_MAX_SO_BUFFERS];

   /* Output index buffer, allocated by pre-GS. */
   DEVICE(uint) output_index_buffer;

   /* Address of transform feedback buffer in general, supplied by the CPU. */
   DEVICE(uchar) xfb_base_original[POLY_MAX_SO_BUFFERS];

   /* Address of transform feedback for the current primitive. Written by pre-GS
    * program.
    */
   DEVICE(uchar) xfb_base[POLY_MAX_SO_BUFFERS];

   /* Location-indexed mask of flat outputs, used for lowering GL edge flags. */
   uint64_t flat_outputs;

   uint32_t xfb_size[POLY_MAX_SO_BUFFERS];

   /* Number of vertices emitted by transform feedback per stream. Written by
    * the pre-GS program.
    */
   uint32_t xfb_verts[POLY_MAX_VERTEX_STREAMS];

   /* Within an indirect GS draw, the grids used to dispatch the GS written
    * out by the GS indirect setup kernel or the CPU for a direct draw. This is
    * the "indirect local" format: first 3 is in threads, second 3 is in grid
    * blocks. This lets us use nontrivial workgroups with indirect draws without
    * needing any predication.
    */
   uint32_t grid[6];

   /* Indirect draw command */
   struct poly_indirect_draw draw;

   /* Number of input primitives across all instances, calculated by the CPU for
    * a direct draw or the GS indirect setup kernel for an indirect draw.
    */
   uint32_t input_primitives;

   /* Number of input primitives per instance, rounded up to a power-of-two and
    * with the base-2 log taken. This is used to partition the output vertex IDs
    * efficiently.
    */
   uint32_t primitives_log2;

   /* Number of bytes output by the GS count shader per input primitive (may be
    * 0), written by CPU and consumed by indirect draw setup shader for
    * allocating counts.
    */
   uint32_t count_buffer_stride;

   /* Dynamic input topology. Must be compatible with the geometry shader's
    * layout() declared input class.
    */
   uint32_t input_topology;
} PACKED;
static_assert(sizeof(struct poly_geometry_params) == 79 * 4);

static inline void
poly_geometry_params_init(struct poly_geometry_params *p,
                          enum mesa_prim prim, const uint32_t wg_size[3])
{
   *p = (struct poly_geometry_params) {
      .input_topology = prim,
      .grid = {
         0, 0, 1, /* x/y are set by poly_geometry_params_set_draw() */
         wg_size[0], wg_size[1], wg_size[2],
      },
   };
}

static inline void
poly_geometry_params_set_draw(struct poly_geometry_params *p,
                              enum mesa_prim prim,
                              enum poly_gs_shape shape, uint32_t max_indices,
                              uint32_t vertex_count, uint32_t instance_count)
{
   /* Calculate number of primitives input into the GS */
   const uint32_t prim_per_instance =
      u_decomposed_prims_for_vertices(prim, vertex_count);

   /* Invoke GS as (primitives, instances) */
   p->grid[0] = prim_per_instance;
   p->grid[1] = instance_count;

   p->input_primitives = prim_per_instance * instance_count;
   p->primitives_log2 = util_logbase2_ceil(prim_per_instance);

   p->draw.index_count = poly_gs_rast_vertices(
      shape, max_indices, prim_per_instance, instance_count);
   p->draw.instance_count = poly_gs_rast_instances(
      shape, max_indices, prim_per_instance, instance_count);
}

/* TCS shared memory layout:
 *
 *    vec4 vs_outputs[VERTICES_IN_INPUT_PATCH][TOTAL_VERTEX_OUTPUTS];
 *
 * TODO: compact.
 */
static inline uint
poly_tcs_in_offs_el(uint vtx, gl_varying_slot location,
                    uint64_t crosslane_vs_out_mask)
{
   uint base = vtx * util_bitcount64(crosslane_vs_out_mask);
   uint offs = util_bitcount64(crosslane_vs_out_mask &
                               (((uint64_t)(1) << location) - 1));

   return base + offs;
}

static inline uint
poly_tcs_in_size(uint32_t vertices_in_patch, uint64_t crosslane_vs_out_mask)
{
   return vertices_in_patch * util_bitcount64(crosslane_vs_out_mask) * 16;
}

/*
 * TCS out buffer layout, per-patch:
 *
 *    float tess_level_outer[4];
 *    float tess_level_inner[2];
 *    vec4 patch_out[MAX_PATCH_OUTPUTS];
 *    vec4 vtx_out[OUT_PATCH_SIZE][TOTAL_VERTEX_OUTPUTS];
 *
 * Vertex out are compacted based on the mask of written out. Patch
 * out are used as-is.
 *
 * Bounding boxes are ignored.
 */
static inline uint
poly_tcs_out_offs_el(uint vtx_id, gl_varying_slot location, uint nr_patch_out,
                     uint64_t vtx_out_mask)
{
   uint off = 0;
   if (location == VARYING_SLOT_TESS_LEVEL_OUTER)
      return off;

   off += 4;
   if (location == VARYING_SLOT_TESS_LEVEL_INNER)
      return off;

   off += 2;
   if (location >= VARYING_SLOT_PATCH0)
      return off + (4 * (location - VARYING_SLOT_PATCH0));

   /* Anything else is a per-vtx output */
   off += 4 * nr_patch_out;
   off += 4 * vtx_id * util_bitcount64(vtx_out_mask);

   uint idx = util_bitcount64(vtx_out_mask & (((uint64_t)(1) << location) - 1));
   return off + (4 * idx);
}

static inline uint
poly_tcs_out_stride_el(uint nr_patch_out, uint out_patch_size,
                       uint64_t vtx_out_mask)
{
   return poly_tcs_out_offs_el(out_patch_size, VARYING_SLOT_POS, nr_patch_out,
                               vtx_out_mask);
}

static inline uint
poly_tcs_out_stride(uint nr_patch_out, uint out_patch_size,
                    uint64_t vtx_out_mask)
{
   return poly_tcs_out_stride_el(nr_patch_out, out_patch_size, vtx_out_mask) *
          4;
}

/* In a tess eval shader, stride for hw vertex ID */
#define POLY_TES_PATCH_ID_STRIDE 8192

static inline uint
poly_compact_prim(enum mesa_prim prim)
{
   static_assert(MESA_PRIM_QUAD_STRIP == MESA_PRIM_QUADS + 1);
   static_assert(MESA_PRIM_POLYGON == MESA_PRIM_QUADS + 2);

#ifndef __OPENCL_VERSION__
   assert(prim != MESA_PRIM_QUADS);
   assert(prim != MESA_PRIM_QUAD_STRIP);
   assert(prim != MESA_PRIM_POLYGON);
   assert(prim != MESA_PRIM_PATCHES);
#endif

   return (prim >= MESA_PRIM_QUADS) ? (prim - 3) : prim;
}

static inline enum mesa_prim
poly_uncompact_prim(uint packed)
{
   if (packed >= MESA_PRIM_QUADS)
      return (enum mesa_prim)(packed + 3);

   return (enum mesa_prim)packed;
}

/*
 * Write a strip into a 32-bit index buffer. This is the sequence:
 *
 *    (b, b + 1, b + 2, ..., b + n - 1, -1) where -1 is the restart index
 *
 * For points, we write index buffers without restart just for remapping.
 */
static inline void
_poly_write_strip(GLOBAL uint32_t *index_buffer, uint32_t index_offset,
                  uint32_t vertex_offset, uint32_t verts_in_prim,
                  uint32_t stream, uint32_t stream_multiplier, uint32_t n)
{
   bool restart = n > 1;
   if (verts_in_prim < n)
      return;

   GLOBAL uint32_t *out = &index_buffer[index_offset];

   /* Write out indices for the strip */
   for (uint32_t i = 0; i < verts_in_prim; ++i) {
      out[i] = (vertex_offset + i) * stream_multiplier + stream;
   }

   if (restart)
      out[verts_in_prim] = -1;
}

static inline unsigned
poly_decomposed_prims_for_vertices_with_tess(enum mesa_prim prim, int vertices,
                                             unsigned verts_per_patch)
{
   if (prim >= MESA_PRIM_PATCHES) {
      return vertices / verts_per_patch;
   } else {
      return u_decomposed_prims_for_vertices(prim, vertices);
   }
}

#ifdef __OPENCL_VERSION__
/*
 * Returns (work_group_scan_inclusive_add(x), work_group_sum(x)). Implemented
 * manually with subgroup ops and local memory since Mesa doesn't do those
 * lowerings yet.
 */
static inline uint2
poly_work_group_scan_inclusive_add(uint x, local uint *scratch)
{
   uint sg_id = get_sub_group_id();

   /* Partial prefix sum of the subgroup */
   uint sg = sub_group_scan_inclusive_add(x);

   /* Reduction (sum) for the subgroup */
   uint sg_sum = sub_group_broadcast(sg, 31);

   /* Write out all the subgroups sums */
   barrier(CLK_LOCAL_MEM_FENCE);
   scratch[sg_id] = sg_sum;
   barrier(CLK_LOCAL_MEM_FENCE);

   /* Read all the subgroup sums. Thread T in subgroup G reads the sum of all
    * threads in subgroup T.
    */
   uint other_sum = scratch[get_sub_group_local_id()];

   /* Exclusive sum the subgroup sums to get the total before the current group,
    * which can be added to the total for the current group.
    */
   uint other_sums = sub_group_scan_exclusive_add(other_sum);
   uint base = sub_group_broadcast(other_sums, sg_id);
   uint prefix = base + sg;

   /* Reduce the workgroup using the prefix sum we already did */
   uint reduction = sub_group_broadcast(other_sums + other_sum, 31);

   return (uint2)(prefix, reduction);
}

static inline void
poly_prefix_sum(local uint *scratch, global uint *buffer, uint len, uint words,
                uint word, uint wg_count)
{
   uint tid = cl_local_id.x;

   /* Main loop: complete workgroups processing multiple values at once */
   uint i, count = 0;
   uint len_remainder = len % wg_count;
   uint len_rounded_down = len - len_remainder;

   for (i = tid; i < len_rounded_down; i += wg_count) {
      global uint *ptr = &buffer[(i * words) + word];
      uint value = *ptr;
      uint2 sums = poly_work_group_scan_inclusive_add(value, scratch);

      *ptr = count + sums[0];
      count += sums[1];
   }

   /* The last iteration is special since we won't have a full subgroup unless
    * the length is divisible by the subgroup size, and we don't advance count.
    */
   global uint *ptr = &buffer[(i * words) + word];
   uint value = (tid < len_remainder) ? *ptr : 0;
   uint scan = poly_work_group_scan_inclusive_add(value, scratch)[0];

   if (tid < len_remainder) {
      *ptr = count + scan;
   }
}

static inline void
poly_increment_counters(global uint32_t *a, global uint32_t *b,
                        global uint32_t *c, uint count)
{
   global uint32_t *ptr[] = {a, b, c};

   for (uint i = 0; i < 3; ++i) {
      if (ptr[i]) {
         *(ptr[i]) += count;
      }
   }
}

static inline void
poly_increment_ia(global uint32_t *ia_vertices, global uint32_t *ia_primitives,
                  global uint32_t *vs_invocations, global uint32_t *c_prims,
                  global uint32_t *c_invs, constant uint32_t *draw,
                  enum mesa_prim prim, unsigned verts_per_patch)
{
   poly_increment_counters(ia_vertices, vs_invocations, NULL,
                           draw[0] * draw[1]);

   uint prims = poly_decomposed_prims_for_vertices_with_tess(prim, draw[0],
                                                             verts_per_patch) *
                draw[1];

   poly_increment_counters(ia_primitives, c_prims, c_invs, prims);
}

static inline void
poly_gs_setup_indirect(uint64_t index_buffer, constant uint *draw,
                       global struct poly_vertex_params *vp /* output */,
                       global struct poly_geometry_params *p /* output */,
                       global struct poly_heap *heap,
                       uint64_t vs_outputs /* Vertex (TES) output mask */,
                       uint32_t index_size_B /* 0 if no index bffer */,
                       uint32_t index_buffer_range_el,
                       uint32_t prim /* Input primitive type, enum mesa_prim */,
                       int is_prefix_summing, uint max_indices,
                       enum poly_gs_shape shape)
{
   /* Determine the (primitives, instances) grid size. */
   uint vertex_count = draw[0];
   uint instance_count = draw[1];

   poly_vertex_params_set_draw(vp, vertex_count, instance_count);
   poly_geometry_params_set_draw(p, prim, shape, max_indices,
                                 vertex_count, instance_count);

   /* If indexing is enabled, the third word is the offset into the index buffer
    * in elements. Apply that offset now that we have it. For a hardware
    * indirect draw, the hardware would do this for us, but for software input
    * assembly we need to do it ourselves.
    */
   if (index_size_B) {
      vp->index_buffer = poly_index_buffer(index_buffer, index_buffer_range_el,
                                           draw[2], index_size_B);

      vp->index_buffer_range_el =
         poly_index_buffer_range_el(index_buffer_range_el, draw[2]);
   }

   /* We need to allocate VS and GS count buffers, do so now */
   uint vertex_buffer_size =
      poly_tcs_in_size(vertex_count * instance_count, vs_outputs);

   if (is_prefix_summing) {
      p->count_buffer = poly_heap_alloc(
         heap, p->input_primitives * p->count_buffer_stride);
   }

   vp->output_buffer = (uintptr_t)poly_heap_alloc(heap, vertex_buffer_size);

   vp->outputs = vs_outputs;

   if (shape == POLY_GS_SHAPE_DYNAMIC_INDEXED) {
      const uint32_t index_offset =
         poly_heap_alloc_offs(heap, p->draw.index_count * 4);
      p->draw.first_index = index_offset / 4;
      p->output_index_buffer = (global uint *)(heap->base + index_offset);
   }
}

static uint
poly_load_index(uintptr_t index_buffer, uint32_t index_buffer_range_el, uint id,
                uint index_size)
{
   bool oob = id >= index_buffer_range_el;

   /* If the load would be out-of-bounds, load the first element which is
    * assumed valid. If the application index buffer is empty with robustness2,
    * index_buffer will point to a zero sink where only the first is valid.
    */
   if (oob) {
      id = 0;
   }

   uint el;
   if (index_size == 1) {
      el = ((constant uint8_t *)index_buffer)[id];
   } else if (index_size == 2) {
      el = ((constant uint16_t *)index_buffer)[id];
   } else {
      el = ((constant uint32_t *)index_buffer)[id];
   }

   /* D3D robustness semantics. TODO: Optimize? */
   if (oob) {
      el = 0;
   }

   return el;
}

static void
poly_store_index(uintptr_t index_buffer, uint index_size_B, uint id, uint value)
{
   global uint32_t *out_32 = (global uint32_t *)index_buffer;
   global uint16_t *out_16 = (global uint16_t *)index_buffer;
   global uint8_t *out_8 = (global uint8_t *)index_buffer;

   if (index_size_B == 4)
      out_32[id] = value;
   else if (index_size_B == 2)
      out_16[id] = value;
   else
      out_8[id] = value;
}

#endif
