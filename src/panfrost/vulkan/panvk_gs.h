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
   /* A shader writing gl_ViewportIndex: PANVK_GS_MAX_VIEWPORTS x
    * {scale xyz, offset xyz}. 0 otherwise.
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
};

/* gl_ViewportIndex, carried from the emulation to the fragment shader in a
 * slot Vulkan never uses and pan lays out as generic.
 */
#define PANVK_GS_VIEWPORT_INDEX_SLOT VARYING_SLOT_CLIP_VERTEX
#define PANVK_GS_MAX_VIEWPORTS       16

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
};

bool panvk_nir_lower_gs(struct nir_shader *nir,
                        const struct panvk_gs_lower_options *opts);

/* Tessellation: the tess control shader runs as a compute shader after the
 * vertex job, one workgroup per patch (workgroup_id.x the patch, .y the
 * instance), one invocation per output vertex. The tess eval shader becomes
 * a point-in, point-out geometry shader run once per tessellator index.
 */
#define PANVK_TESS_HEAP_SIZE   (64u << 20)
#define PANVK_TESS_MAX_INDICES (1u << 18)

/* The tessellator's grid for an indirect draw: each thread walks the
 * patches at this stride.
 */
#define PANVK_TESS_INDIRECT_GRID 1024

/* opts: input_verts_per_prim 0 (panvk_gs_push::patch_size), pos_stride,
 * params_base.
 */
bool panvk_nir_lower_tcs_inputs(struct nir_shader *nir,
                                const struct panvk_gs_lower_options *opts);

/* nir_load_tess_param_buffer_poly -> panvk_gs_push::tess_params. */
bool panvk_nir_lower_tess_params(struct nir_shader *nir,
                                 const struct panvk_gs_lower_options *opts);

/* A tess eval shader whose inputs src/poly already lowered, into the
 * geometry emulation's input form: one point in, one point out.
 */
void panvk_nir_tes_to_gs(struct nir_shader *nir);

#endif /* PANVK_GS_H */
