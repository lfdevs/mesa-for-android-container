/*
 * Copyright 2026 XclipseTools
 * SPDX-License-Identifier: MIT
 *
 * Ported from JimVulkan/mesa-unified-drivers (Bifrost geometry stage
 * emulation) by biblioklept.
 */

#ifndef PANVK_GS_H
#define PANVK_GS_H

/* Geometry stage emulation for Bifrost (v6/v7). Arch-independent on
 * purpose: the lowering is pure NIR, and this header only carries the
 * contract between it and the JM draw path.
 */

#include <stdbool.h>
#include <stdint.h>

#include "compiler/shader_enums.h"

struct nir_shader;
struct nir_xfb_info;

/* Mali has no geometry stage, so a geometry shader is compiled into a
 * COMPUTE shader that runs between the vertex job and the tiler job: it
 * reads the varying buffers the vertex shader wrote and writes a fresh set
 * for the tiler, plus an index buffer expanding the strips it emitted into
 * independent primitives. Output is sized for the worst case, see
 * panvk_nir_lower_gs.c.
 */
#define PANVK_GS_WORKGROUP_SIZE 64
#define PANVK_GS_SLOT_UNUSED    UINT32_MAX

/* An indirect draw's parameters exist only on the GPU, so the emulation is
 * dispatched for this many input primitives (times gl_InvocationID) and each
 * invocation reads the real count from the draw's argument buffer.
 * Primitives past the cap are dropped.
 */
#define PANVK_GS_INDIRECT_MAX_PRIMS 4096

/* How the draw's vertices assemble into the input primitives. Lists, and
 * the list-with-adjacency topologies, are LIST.
 */
enum panvk_gs_topology {
   PANVK_GS_TOPO_LIST = 0,
   /* primitive p is vertices p .. p + n - 1; triangle strips swap the last
    * two on odd p */
   PANVK_GS_TOPO_STRIP = 1,
   /* triangle p is vertices p + 1, p + 2, 0 */
   PANVK_GS_TOPO_FAN = 2,
   /* triangle strip with adjacency, see gs_input_record() */
   PANVK_GS_TOPO_TRI_STRIP_ADJ = 3,
};

/* Push-constant block the lowered shader reads its addresses from. Field
 * order is load-bearing: panvk_nir_lower_gs.c addresses it with offsetof().
 */
struct panvk_gs_push {
   uint64_t in_pos;
   uint64_t in_general;
   uint64_t out_pos;
   uint64_t out_general;
   uint64_t out_index;
   /* slot -> byte offset in the generic buffer, indexed by gl_varying_slot,
    * PANVK_GS_SLOT_UNUSED for a slot nothing reads. The input offsets
    * belong to the vertex shader's layout and the output offsets to the
    * fragment shader's, neither is known when the GS is compiled.
    */
   uint64_t in_offsets;
   uint64_t out_offsets;
   uint32_t in_general_stride;
   uint32_t out_general_stride;
   uint32_t num_input_prims;
   uint32_t pad;
   /* The viewport transform, applied by the emulation on the way out (and
    * undone on the way in when the vertex shader already applied it).
    * Padded to vec4. These cannot come from nir_load_viewport_*: this runs
    * as COMPUTE with a compute sysval block.
    */
   float viewport_scale[3];
   float pad2;
   float viewport_offset[3];
   float pad3;
   /* Input assembly. The vertex shader wrote one varying record per vertex
    * it ran, each instance's records at instance_stride after the previous
    * one. index_buf is the draw's first index, or 0 for a plain draw.
    */
   uint64_t index_buf;
   uint32_t index_size;
   uint32_t topology;
   uint32_t prims_per_instance;
   uint32_t instance_stride;
   /* Indirect draws (indirect_cmd != 0): the Vk*IndirectCommand, the vertex
    * job's varying buffer descriptors as the draw helper patched them, its
    * DRAW section (Instance Size is the per-instance record stride), and for
    * an indexed draw the smallest index (the vertex job starts at it).
    * index_buf is then the index buffer base. num_input_prims is the cap.
    */
   uint64_t indirect_cmd;
   uint64_t in_pos_desc;
   uint64_t in_general_desc;
   uint64_t vertex_dcd;
   uint64_t index_min;
   /* The tiler job's PRIMITIVE index count word: the emulation writes the
    * real count there.
    */
   uint64_t tiler_index_count;
   /* Transform feedback, 0 when the draw is not capturing: one primitive
    * count per compute invocation, and one staging record per output vertex
    * slot in panvk_gs_xfb_table() order.
    */
   uint64_t xfb_counts;
   uint64_t xfb_staging;
   /* The polygon-mode geometry shader culls faces itself (PANVK_GS_CULL_*),
    * since the tiler only sees the lines or points it emits.
    */
   uint32_t polygon_cull;
   /* POSITION holds screen-space xyz and 1/w (the vertex shader was compiled
    * for the tiler): the emulation undoes the viewport transform.
    */
   uint32_t in_screen_space;
   /* A shader writing gl_ViewportIndex: PANVK_GS_MAX_VIEWPORTS entries of
    * PANVK_GS_VIEWPORT_STRIDE bytes, {scale xyz, offset xyz, scissor
    * min x, min y, max x, max y (floats, max exclusive)}. 0 otherwise.
    */
   uint64_t viewports;
   /* Tessellation: the draw's struct poly_tess_params. */
   uint64_t tess_params;
   /* When non-zero, where the real number of input primitives is:
    * min(*num_prims_addr, num_input_prims) run.
    */
   uint64_t num_prims_addr;
   /* Vertices per input primitive when compiled for a variable count (the
    * tess control stage: patchControlPoints).
    */
   uint32_t patch_size;
   /* Leave output positions in clip space (tess eval feeding a GS). */
   uint32_t out_clip_space;
   /* geometryStreams: every stream owns a copy of the output vertex slots
    * (xfb_stream_slots per stream) and its own index list and counts; only
    * stream 0 is rasterized. xfb_stream_index holds the lists of streams
    * 1-3, xfb_stream_idx entries each, and the counts of stream s start at
    * s * xfb_invocations in xfb_counts.
    */
   uint64_t xfb_stream_index;
   uint32_t xfb_stream_slots;
   uint32_t xfb_stream_idx;
   uint32_t xfb_invocations;
   uint32_t pad4;
   /* Pipeline statistics (jm-v7 016), 0 when no query counts: four u64
    * counter addresses, each 0 when not counted, that every live invocation
    * adds to: geometry shader invocations (1), geometry shader primitives
    * (all streams), clipping invocations and clipping primitives (stream 0).
    */
   uint64_t pstats;
   /* Per-viewport rasterization (jm-v7 031): when the selectable viewports'
    * depth ranges differ, the tiler cannot clamp/clip with a union. vp_split
    * viewports then each get a copy of the index list (index_region entries
    * apart, after the full list at out_index) holding only their primitives,
    * and a tiler job of their own. out_viewport holds the viewport index of
    * every output vertex slot; the tiler jobs are tiler_job_stride bytes
    * apart. vp_split 0: one tiler job with the union (jm-v7 017).
    */
   uint64_t out_viewport;
   uint32_t vp_split;
   uint32_t index_region;
   uint32_t tiler_job_stride;
   /* gl_PointSize (jm-v7 033): the vertex shader's fp16 point sizes,
    * in_psiz_stride bytes apart (in_psiz 0: it writes none, reads 1.0; an
    * indirect draw's buffer is in the patched descriptor in_psiz_desc), and
    * the emulation's own for point output (0: none).
    */
   uint32_t in_psiz_stride;
   uint64_t in_psiz;
   uint64_t in_psiz_desc;
   uint64_t out_psiz;
};

/* gl_ViewportIndex, carried from the emulation to the fragment shader in a
 * slot Vulkan never uses and pan lays out as generic.
 */
#define PANVK_GS_VIEWPORT_INDEX_SLOT VARYING_SLOT_CLIP_VERTEX
#define PANVK_GS_MAX_VIEWPORTS       16
#define PANVK_GS_VIEWPORT_STRIDE     40

#define PANVK_GS_CULL_FRONT (1u << 0)
#define PANVK_GS_CULL_BACK  (1u << 1)
#define PANVK_GS_FRONT_CCW  (1u << 2)

/* Transform feedback is captured from the emulation. Every captured 32-bit
 * component is one dword of a per-vertex staging record; table entry j says
 * where record dword j goes: xfb buffer (bits 16+) and dword inside that
 * buffer's vertex (bits 0-15).
 */
#define PANVK_XFB_MAX_RECORD_DWORDS 128

/* Fills table (PANVK_XFB_MAX_RECORD_DWORDS entries) and returns the record
 * size in dwords, 0 for a shader that captures nothing, -1 for one this
 * emulation cannot capture.
 */
int panvk_gs_xfb_table(const struct nir_xfb_info *xfb, uint32_t *table);

/* Transform feedback without a geometry shader: a vertex shader with xfb
 * outputs runs through the geometry emulation with a generated passthrough
 * geometry shader that emits every input primitive as it is, so the capture
 * path above is the only one. The vertex shader keeps what the passthrough
 * needs to rebuild its nir_xfb_info (struct panvk_xfb_output) and one
 * passthrough exists per input primitive class.
 */
struct panvk_xfb_output {
   uint8_t buffer;
   uint8_t location;
   uint8_t component_mask;
   uint8_t pad;
   uint16_t offset;
};

enum panvk_xfb_gs_class {
   PANVK_XFB_GS_POINTS,
   PANVK_XFB_GS_LINES,
   PANVK_XFB_GS_TRIANGLES,
   PANVK_XFB_GS_LINES_ADJ,
   PANVK_XFB_GS_TRIANGLES_ADJ,
   PANVK_XFB_GS_CLASS_COUNT,
};

/* The passthrough class of a draw topology, -1 for one with no class
 * (patches: a tessellated draw picks the class of its output primitives).
 */
static inline int
panvk_xfb_gs_class(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:
      return PANVK_XFB_GS_POINTS;
   case MESA_PRIM_LINES:
   case MESA_PRIM_LINE_STRIP:
      return PANVK_XFB_GS_LINES;
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_TRIANGLE_FAN:
      return PANVK_XFB_GS_TRIANGLES;
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      return PANVK_XFB_GS_LINES_ADJ;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return PANVK_XFB_GS_TRIANGLES_ADJ;
   default:
      return -1;
   }
}

struct panvk_gs_lower_options {
   unsigned input_verts_per_prim;
   unsigned output_verts_per_prim;
   unsigned max_output_verts;
   unsigned max_output_prims;
   /* gl_InvocationID count: every (input primitive, invocation) pair is its
    * own compute invocation with its own output slots.
    */
   unsigned invocations;

   unsigned pos_stride;

   /* Where panvk_gs_push lives in the push-constant/FAU space. */
   unsigned params_base;

   /* Transform feedback: stage a capture record at every EmitVertex. */
   const struct nir_xfb_info *xfb;
   /* The vertex streams the shader emits on. */
   unsigned stream_mask;

   /* multiViewport: with gl_ViewportIndex written, the output slot that
    * gets the selected viewport's scissor edge distances (times clip w, so
    * that perspective interpolation keeps their sign), 0 for none.
    */
   unsigned scissor_slot;
};

bool panvk_nir_lower_gs(struct nir_shader *nir,
                        const struct panvk_gs_lower_options *opts);

/* Tessellation (jm-v7 014). The tess control shader runs as a compute
 * shader after the vertex job, one workgroup per patch (workgroup_id.x the
 * patch, .y the instance), one invocation per output vertex. The libpoly
 * tessellator kernels count, prefix sum and write the domain point indices,
 * and the tess eval shader runs as the vertex shader of a non-indexed draw
 * over them: vertex i is tessellator index i. A geometry shader or transform
 * feedback after it goes through the geometry emulation of that draw.
 *
 * The tess control output, the per-patch counts and the tessellator output
 * live in one device arena (panvk_device::tess_arena), reused by every
 * tessellated draw: each draw's first arena write is a barrier job.
 */
#define PANVK_TESS_HEAP_SIZE (160u << 20)
/* Tessellator indices one draw keeps, at most. Whole patches past it are
 * dropped (panlib_v7_tess_prefix).
 */
#define PANVK_TESS_MAX_INDICES (1u << 23)
/* Patches one draw tessellates, at most: the tessellator grid and the serial
 * prefix sum are sized for it.
 */
#define PANVK_TESS_MAX_PATCHES 8192u
/* With a geometry shader or transform feedback after the tess eval shader,
 * the geometry emulation of the generated draw is sized for this many input
 * primitives (instead of PANVK_GS_INDIRECT_MAX_PRIMS).
 */
#define PANVK_TESS_GS_MAX_PRIMS (1u << 21)
#define PANVK_TESS_UNSET 0xff

/* What the draw path needs from a tess control or tess eval shader. The
 * domain, spacing, winding and point mode may come from either stage.
 */
struct panvk_tess_info {
   /* 0 triangles, 1 quads, 2 isolines, PANVK_TESS_UNSET. */
   uint8_t domain;
   /* enum poly_tess_partitioning, PANVK_TESS_UNSET. */
   uint8_t spacing;
   uint8_t ccw;
   uint8_t point_mode;
   /* Tess control only. */
   uint8_t out_patch_size;
   uint8_t patch_constants;
   uint16_t stride_el;
   uint64_t per_vertex_outputs;
};

/* opts: input_verts_per_prim 0 (panvk_gs_push::patch_size), pos_stride,
 * params_base.
 */
bool panvk_nir_lower_tcs_inputs(struct nir_shader *nir,
                                const struct panvk_gs_lower_options *opts);

/* nir_load_tess_param_buffer_poly -> panvk_gs_push::tess_params. */
bool panvk_nir_lower_tess_params(struct nir_shader *nir,
                                 const struct panvk_gs_lower_options *opts);

#endif /* PANVK_GS_H */
