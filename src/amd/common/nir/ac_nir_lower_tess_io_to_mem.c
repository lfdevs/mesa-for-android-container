/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_gpu_info.h"
#include "ac_nir.h"
#include "ac_nir_helpers.h"
#include "ac_shader_util.h"
#include "nir_builder.h"
#include "nir_tcs_info.h"
#include "util/u_math.h"

/*
 * These NIR passes are used to lower NIR cross-stage I/O intrinsics into the
 * memory accesses that actually happen on the HW.
 *
 * Each input and output has a 16-byte (4 dwords) slot reserved for it, and
 * can have up to 4 components. Each component is 32 bits.
 *
 * ## VS-TCS-TES I/O - Terminology:
 *
 * * patch - Group of vertices, used instead of primitives in tessellation
 * * per-vertex - input or output which can be different for every vertex.
 * * per-patch - input output which applies to a patch (a group of vertices)
 *
 * ## VS-TCS-TES I/O - How it works:
 *
 * ```
 * SW model:    SW VS         SW TCS    tessellator    SW TES
 *                ┊             ┊             ┊          ┊
 *              ┌────┐        ┌────┐        ┌────┐    ┌─────┐
 * HW pipeline: │ LS │─╮   ╭─>│ HS │─╮   ╭─>│ FF │ ╭─>│VS/ES│
 *              └────┘ │   │  └────┘ │   │  └────┘ │  └─────┘
 * Memory:             ╰─>LDS<──╯    ╰─>VRAM───────╯
 * ```
 *
 * * SW VS runs as a HW LS (Local Shader, merged into HS on GFX9+),
 *   and SW TCS runs as HW HS (Hull Shader).
 *   SW TES runs as either HW VS or HW ES (Export Shader).
 * * LS and HS share the same LDS space.
 * * LS (SW VS) stores outputs to LDS to be read by HS (SW TCS).
 * * HS (SW TCS) stores outputs in LDS if the HS (SW TCS) reads them.
 * * HS (SW TCS) stores outputs in VRAM if the next stage (SW TES) reads them.
 *
 * Side note: some old HW supports having TES read from the same LDS space where LS/HS write, but
 * Mesa always stores HS outputs to VRAM to avoid forcing TES waves to run on the same CU as the LS/HS waves.
 *
 * ### Passing VS-TCS I/O in registers
 *
 * On GPUs that run SW VS and  SW TCS on the same HW stage (HS on GFX9+),
 * IO can be passed through registers instead of LDS when the following conditions are met:
 *
 * 1. TCS input and output patch size match
 * 2. Floating point execution modes in SW VS and SW TCS match
 * 3. The SW VS output is not written indirectly, and the corresponding SW TCS input is not read indirectly
 *
 * Some HS outputs could be passed through registers to, but this is a TODO.
 *
 * ### LDS layout used by VS-TCS:
 *
 * ```
 * TCS per-vertex inputs for patch 0  <─── 0
 * TCS per-vertex inputs for patch 1
 * TCS per-vertex inputs for patch 2  <─── hs_per_vertex_input_lds_offset (rel_patch_id = 2)
 * ...
 * TCS per-vertex outputs for patch 0 <─── hs_output_lds_offset (rel_patch_id = 0, per-vertex)
 * TCS per-patch outputs for patch 0  <─── hs_output_lds_offset (rel_patch_id = 0, per-patch)
 * TCS per-vertex outputs for patch 1
 * TCS per-patch outputs for patch 1
 * TCS per-vertex outputs for patch 2 <─── hs_output_lds_offset (rel_patch_id = 2, per-vertex)
 * TCS per-patch outputs for patch 2  <─── hs_output_lds_offset (rel_patch_id = 2, per-patch)
 * ...
 * ```
 *
 * ### VRAM layout used by TCS-TES I/O:
 *
 * ```
 * attr 0 of patch 0 vertex 0   <─── "off-chip LDS" offset, aligned to >= 4K
 * attr 0 of patch 0 vertex 1
 * attr 0 of patch 0 vertex 2
 * ...
 * attr 0 of patch 1 vertex 0
 * attr 0 of patch 1 vertex 1
 * attr 0 of patch 1 vertex 2   <─── hs_per_vertex_output_vmem_offset (attribute slot = 0, rel_patch_id = 1, vertex index = 2)
 * ...
 * attr 0 of patch 2 vertex 0
 * attr 0 of patch 2 vertex 1
 * attr 0 of patch 2 vertex 2
 * ...
 * [pad to 256B]
 * attr 1 of patch 0 vertex 0   <─── aligned to 256B
 * attr 1 of patch 0 vertex 1
 * attr 1 of patch 0 vertex 2
 * ...
 * ...
 * [pad to 256B]
 * per-patch attr 0 of patch 0  <─── hs_out_patch_data_offset_amd, aligned to 256B
 * per-patch attr 0 of patch 1
 * per-patch attr 0 of patch 2  <─── hs_per_patch_output_vmem_offset (attribute slot = 0, rel_patch_id = 2)
 * ...
 * [pad to 256B]
 * per-patch attr 1 of patch 0  <─── aligned to 256B
 * per-patch attr 1 of patch 1
 * per-patch attr 1 of patch 2
 * ...
 * ```
 *
 */

typedef struct {
   /* Which hardware generation we're dealing with */
   enum amd_gfx_level gfx_level;
   unsigned wave_size;
   nir_tcs_info tcs_info;
   ac_nir_tess_io_info io_info;

   /* I/O semantic -> real location used by lowering. */
   ac_nir_map_io_driver_location map_io;

   /* Bit mask of TCS per-vertex inputs (VS outputs) which are passed via temporaries (VGPRs)
    * from VS to TCS because they are read using gl_InvocationIndex as the vertex index.
    *
    * If TCS cross-invocation reads or indirect reads of these inputs are present, they don't
    * prevent fast access via gl_InvocationIndex because those are just different ways of reading
    * the same values.
    *
    * An example where a TCS input is indexed by gl_InvocationIndex and some other index is
    * Unigine Heaven where the position input is used for patch culling (with cross-invocation
    * access) and also read with gl_InvocationIndex to forward it to TES.
    *
    * Passing TCS inputs in VGPRs is only possible when:
    * - VS+TCS are merged (GFX9+).
    * - Input and output patch sizes are the same.
    */
   uint64_t tcs_inputs_via_temp;

   /* Bit mask of TCS per-vertex inputs (VS outputs) which are passed via LDS for cross-invocation
    * reads or indirect reads.
    */
   uint64_t tcs_inputs_via_lds;

   /* True if the output patch fits the subgroup, so all TCS outputs are always written in the same
    * subgroup that reads them.
    */
   bool tcs_out_patch_fits_subgroup;

   /* TCS output values, 8 channels per slot. The last 4 channels are high 16 bits of the first 4 channels.
    * Output values that are not stored with cross-invocation access and indirect indexing are stored here.
    * Output values stored with cross-invocation access or indirect indexing are stored in LDS.
    * All outputs are loaded from LDS or VGPRs and written to memory at the end of the shader.
    */
   nir_variable *tcs_per_vertex_outputs[VARYING_SLOT_MAX][8];
   /* Max. 4 channels, always 32 bits per channel. */
   uint8_t tcs_per_vertex_output_vmem_chan_mask[VARYING_SLOT_MAX];

   /* Same, but for tess levels. LDS isn't used if only invocation 0 writes and reads tess levels or
    * if all invocations write tess levels.
    */
   nir_variable *tcs_tess_level[2]; /* outer, inner */
   /* We can't use uint8_t due to a buggy gcc warning. */
   uint16_t tcs_tess_level_chan_mask[2]; /* outer, inner */

   /* Same, but for per-patch outputs. */
   nir_variable *tcs_per_patch_outputs[MAX_VARYING][8];
   uint8_t tcs_per_patch_output_vmem_chan_mask[MAX_VARYING];
} lower_tess_io_state;

typedef struct {
   nir_def *outer;
   nir_def *inner;
} tess_levels;

#define TESS_LVL_MASK (VARYING_BIT_TESS_LEVEL_OUTER | VARYING_BIT_TESS_LEVEL_INNER)

void
ac_nir_get_tess_io_info(const nir_shader *tcs, const nir_tcs_info *tcs_info, uint64_t tes_inputs_read,
                        uint32_t tes_patch_inputs_read, ac_nir_map_io_driver_location map_io,
                        bool remapped_outputs_include_tess_levels, ac_nir_tess_io_info *io_info)
{
   io_info->vram_output_mask = tcs->info.tess.tcs_outputs_read_by_tes & tes_inputs_read;
   io_info->vram_patch_output_mask = tcs->info.tess.tcs_patch_outputs_read_by_tes & tes_patch_inputs_read;

   /* These shouldn't occur in TCS. */
   io_info->vram_output_mask &= ~(VARYING_BIT_LAYER | VARYING_BIT_VIEWPORT |
                                  VARYING_BIT_PRIMITIVE_ID | VARYING_BIT_PRIMITIVE_SHADING_RATE);

   /* Convert tess levels from 2-bit masks to 32-bit varying slot masks. */
   uint32_t tess_levels_defined_by_all_invoc =
      (uint32_t)tcs_info->tess_levels_defined_by_all_invoc << VARYING_SLOT_TESS_LEVEL_OUTER;
   uint32_t tess_levels_only_written_by_invoc0 =
      (uint32_t)tcs_info->tess_levels_only_written_by_invoc0 << VARYING_SLOT_TESS_LEVEL_OUTER;
   uint32_t tess_levels_only_read_by_invoc0 =
      (uint32_t)tcs_info->tess_levels_only_read_by_invoc0 << VARYING_SLOT_TESS_LEVEL_OUTER;

   /* Per-patch outputs and tess levels don't need LDS if:
    * - There is no indirect indexing
    * AND
    *    - only written by invocation 0 and never read or only read by invocation 0
    *      (always true when the number of output patch vertices is 1)
    *    OR
    *    - written by all invocations in all execution paths (so that output reads can always
    *      return values from VGPRs instead of LDS)
    */
   uint32_t tess_levels_written = tcs->info.outputs_written & TESS_LVL_MASK;
   uint32_t tess_levels_dont_need_lds =
      tess_levels_written & ~tcs->info.outputs_read_indirectly & ~tcs->info.outputs_written_indirectly &
      ((tess_levels_only_written_by_invoc0 & ~tcs->info.outputs_read) |
       (tess_levels_only_written_by_invoc0 & tess_levels_only_read_by_invoc0) |
       tess_levels_defined_by_all_invoc);

   uint32_t patch_outputs_dont_need_lds =
      tcs->info.patch_outputs_written & ~tcs->info.patch_outputs_read_indirectly &
      ~tcs->info.patch_outputs_written_indirectly &
      ((tcs_info->patch_outputs_only_written_by_invoc0 & ~tcs->info.patch_outputs_read) |
       (tcs_info->patch_outputs_only_written_by_invoc0 & tcs_info->patch_outputs_only_read_by_invoc0) |
       tcs_info->patch_outputs_defined_by_all_invoc);

   /* Determine which outputs use LDS. */
   io_info->lds_output_mask = (((tcs->info.outputs_read & tcs->info.outputs_written) |
                                tcs->info.tess.tcs_cross_invocation_outputs_written |
                                tcs->info.outputs_written_indirectly) & ~TESS_LVL_MASK) |
                              (tess_levels_written & ~tess_levels_dont_need_lds);
   io_info->lds_patch_output_mask = tcs->info.patch_outputs_written & ~patch_outputs_dont_need_lds;

   /* Determine which outputs hold their values in VGPRs. */
   io_info->vgpr_output_mask = (tcs->info.outputs_written &
                                ~(tcs->info.tess.tcs_cross_invocation_outputs_written |
                                  tcs->info.outputs_written_indirectly) & ~TESS_LVL_MASK) |
                               (tess_levels_written &
                                (tess_levels_defined_by_all_invoc | tess_levels_only_written_by_invoc0));
   io_info->vgpr_patch_output_mask = tcs->info.patch_outputs_written &
                                     ~tcs->info.patch_outputs_written_indirectly &
                                     (tcs_info->patch_outputs_defined_by_all_invoc |
                                      tcs_info->patch_outputs_only_written_by_invoc0);

   /* Each output must have at least 1 bit in vgpr_output_mask or lds_output_mask or both. */
   assert(tcs->info.outputs_written == (io_info->vgpr_output_mask | io_info->lds_output_mask));
   assert(tcs->info.patch_outputs_written == (io_info->vgpr_patch_output_mask | io_info->lds_patch_output_mask));

   io_info->highest_remapped_vram_output = 0;
   io_info->highest_remapped_vram_patch_output = 0;

   if (map_io) {
      u_foreach_bit64(i, io_info->vram_output_mask & ~TESS_LVL_MASK) {
         unsigned index = map_io(i);
         io_info->highest_remapped_vram_output = MAX2(io_info->highest_remapped_vram_output, index + 1);
      }

      u_foreach_bit(i, io_info->vram_patch_output_mask) {
         unsigned index = map_io(VARYING_SLOT_PATCH0 + i);
         io_info->highest_remapped_vram_patch_output = MAX2(io_info->highest_remapped_vram_patch_output, index + 1);
      }

      if (remapped_outputs_include_tess_levels) {
         u_foreach_bit64(i, io_info->vram_output_mask & TESS_LVL_MASK) {
            unsigned index = map_io(i);
            io_info->highest_remapped_vram_patch_output = MAX2(io_info->highest_remapped_vram_patch_output, index + 1);
         }
      }
   } else {
      io_info->highest_remapped_vram_output = util_bitcount64(io_info->vram_output_mask & ~TESS_LVL_MASK);
      io_info->highest_remapped_vram_patch_output = util_bitcount(io_info->vram_patch_output_mask);

      if (remapped_outputs_include_tess_levels) {
         io_info->highest_remapped_vram_patch_output +=
            util_bitcount64(io_info->vram_output_mask & TESS_LVL_MASK);
      }
   }
}

static bool
tcs_output_needs_vmem(nir_intrinsic_instr *intrin,
                      nir_shader *shader,
                      lower_tess_io_state *st)
{
   const unsigned loc = nir_intrinsic_io_semantics(intrin).location;
   const bool per_vertex = intrin->intrinsic == nir_intrinsic_store_per_vertex_output ||
                           intrin->intrinsic == nir_intrinsic_load_per_vertex_output;

   if (per_vertex) {
      return st->io_info.vram_output_mask & ~TESS_LVL_MASK & BITFIELD64_BIT(loc);
   } else if (loc == VARYING_SLOT_TESS_LEVEL_OUTER || loc == VARYING_SLOT_TESS_LEVEL_INNER) {
      return false;
   } else {
      return st->io_info.vram_patch_output_mask & BITFIELD_BIT(loc - VARYING_SLOT_PATCH0);
   }
}

static bool
tcs_output_needs_lds(nir_intrinsic_instr *intrin,
                     nir_shader *shader,
                     lower_tess_io_state *st)
{
   const unsigned loc = nir_intrinsic_io_semantics(intrin).location;
   const bool per_vertex = intrin->intrinsic == nir_intrinsic_store_per_vertex_output ||
                           intrin->intrinsic == nir_intrinsic_load_per_vertex_output;

   if (per_vertex) {
      return st->io_info.lds_output_mask & ~TESS_LVL_MASK & BITFIELD64_BIT(loc);
   } else if (loc == VARYING_SLOT_TESS_LEVEL_OUTER || loc == VARYING_SLOT_TESS_LEVEL_INNER) {
      return st->io_info.lds_output_mask & TESS_LVL_MASK & BITFIELD64_BIT(loc);
   } else {
      return st->io_info.lds_patch_output_mask & BITFIELD_BIT(loc - VARYING_SLOT_PATCH0);
   }
}

static bool
lower_ls_output_store(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      void *state)
{
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   /* The ARB_shader_viewport_layer_array spec contains the
    * following issue:
    *
    *    2) What happens if gl_ViewportIndex or gl_Layer is
    *    written in the vertex shader and a geometry shader is
    *    present?
    *
    *    RESOLVED: The value written by the last vertex processing
    *    stage is used. If the last vertex processing stage
    *    (vertex, tessellation evaluation or geometry) does not
    *    statically assign to gl_ViewportIndex or gl_Layer, index
    *    or layer zero is assumed.
    *
    * So writes to those outputs in VS-as-LS are simply ignored.
    */
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   if (io_sem.location == VARYING_SLOT_LAYER || io_sem.location == VARYING_SLOT_VIEWPORT) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   lower_tess_io_state *st = (lower_tess_io_state *) state;

   /* When a VS output isn't read by TCS, don't emit anything. */
   if ((io_sem.no_varying ||
        !((st->tcs_inputs_via_temp | st->tcs_inputs_via_lds) & BITFIELD64_BIT(io_sem.location)))) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   if (st->tcs_inputs_via_lds & BITFIELD64_BIT(io_sem.location)) {
      b->cursor = nir_before_instr(&intrin->instr);

      nir_def *vertex_idx = nir_load_local_invocation_index(b);
      nir_def *base_off_var = nir_imul(b, vertex_idx, nir_load_lshs_vertex_stride_amd(b));

      unsigned mapped = ac_nir_map_io_location(io_sem.location, st->tcs_inputs_via_lds, st->map_io);
      nir_def *io_off = ac_nir_calc_io_off(b, nir_intrinsic_component(intrin),
                                           nir_get_io_offset_src(intrin)->ssa,
                                           nir_imm_int(b, 16u), 4u, mapped);
      unsigned write_mask = nir_intrinsic_write_mask(intrin);

      nir_def *off = nir_iadd_nuw(b, base_off_var, io_off);

      /* The beginning of LDS is reserved for the tess level group vote. */
      off = nir_iadd_imm_nuw(b, off, AC_TESS_LEVEL_VOTE_LDS_BYTES);

      AC_NIR_STORE_IO(b, intrin->src[0].ssa, 0, write_mask, io_sem.high_16bits,
                      nir_store_shared, off, .write_mask = store_write_mask, .base = store_const_offset);
   }

   /* The store_output intrinsic on GFX9+ is used to pass the output to TCS via VGPRs. */
   if (!(st->tcs_inputs_via_temp & BITFIELD64_BIT(io_sem.location)))
      nir_instr_remove(&intrin->instr);

   return true;
}

static bool
filter_load_tcs_per_vertex_input(const nir_instr *instr,
                                 UNUSED const void *state)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   lower_tess_io_state *st = (lower_tess_io_state *) state;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_load_per_vertex_input)
      return false;

   nir_src *off_src = nir_get_io_offset_src(intrin);
   nir_src *vertex_index_src = nir_get_io_arrayed_index_src(intrin);
   nir_instr *vertex_index_instr = nir_def_instr(vertex_index_src->ssa);
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   /* If this is accessed via gl_InvocationIndex, don't use LDS if tcs_inputs_via_temp is also set,
    * which indicates that VS and TCS have the same number of patch vertices and the input can be
    * read from VGPRs.
    */
   if (st->tcs_inputs_via_temp & BITFIELD64_BIT(io_sem.location) &&
       nir_src_is_const(*off_src) && /* array indexing */
       vertex_index_instr->type == nir_instr_type_intrinsic &&
       nir_instr_as_intrinsic(vertex_index_instr)->intrinsic == nir_intrinsic_load_invocation_id)
      return false;

   return true;
}

static nir_def *
hs_per_vertex_input_lds_offset(nir_builder *b,
                               lower_tess_io_state *st,
                               nir_intrinsic_instr *instr)
{
   nir_def *tcs_in_vtxcnt = nir_load_patch_vertices_in(b);
   nir_def *rel_patch_id = nir_load_tess_rel_patch_id_amd(b);
   nir_def *vertex_index = nir_get_io_arrayed_index_src(instr)->ssa;

   nir_def *stride = nir_load_lshs_vertex_stride_amd(b);
   nir_def *tcs_in_patch_stride = nir_imul(b, tcs_in_vtxcnt, stride);
   nir_def *vertex_index_off = nir_imul(b, vertex_index, stride);

   nir_def *tcs_in_current_patch_offset = nir_imul(b, rel_patch_id, tcs_in_patch_stride);

   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(instr);
   const unsigned mapped = ac_nir_map_io_location(io_sem.location, st->tcs_inputs_via_lds, st->map_io);
   nir_def *io_offset = ac_nir_calc_io_off(b, nir_intrinsic_component(instr),
                                           nir_get_io_offset_src(instr)->ssa,
                                           nir_imm_int(b, 16u), 4u, mapped);
   nir_def *lds_offset = nir_iadd_nuw(b, nir_iadd_nuw(b, tcs_in_current_patch_offset, vertex_index_off), io_offset);

   /* The beginning of LDS is reserved for the tess level group vote. */
   return nir_iadd_imm_nuw(b, lds_offset, AC_TESS_LEVEL_VOTE_LDS_BYTES);
}

static unsigned
hs_output_lds_map_io_location(nir_shader *shader,
                              const bool per_vertex,
                              const unsigned loc,
                              lower_tess_io_state *st)
{
   if (!per_vertex) {
      const uint64_t tf_mask = st->io_info.lds_output_mask & TESS_LVL_MASK;
      if (loc == VARYING_SLOT_TESS_LEVEL_INNER || loc == VARYING_SLOT_TESS_LEVEL_OUTER) {
         assert(tf_mask & BITFIELD64_BIT(loc));
         return util_bitcount64(tf_mask & BITFIELD64_MASK(loc));
      }

      const uint32_t patch_out_mask = st->io_info.lds_patch_output_mask;
      assert(patch_out_mask & BITFIELD_BIT(loc - VARYING_SLOT_PATCH0));
      return util_bitcount64(tf_mask) +
             util_bitcount(patch_out_mask & BITFIELD_MASK(loc - VARYING_SLOT_PATCH0));
   } else {
      const uint64_t per_vertex_mask = st->io_info.lds_output_mask & ~TESS_LVL_MASK;
      assert(per_vertex_mask & BITFIELD64_BIT(loc));
      return util_bitcount64(per_vertex_mask & BITFIELD64_MASK(loc));
   }
}

static unsigned
get_lds_output_vertex_size(const ac_nir_tess_io_info *io_info)
{
   return util_bitcount64(io_info->lds_output_mask & ~TESS_LVL_MASK) * 16;
}

static unsigned
get_lds_pervertex_output_patch_size(const ac_nir_tess_io_info *io_info, unsigned tcs_vertices_out)
{
   return tcs_vertices_out * get_lds_output_vertex_size(io_info);
}

static unsigned
get_lds_output_patch_stride(const ac_nir_tess_io_info *io_info, unsigned tcs_vertices_out)
{
   unsigned lds_perpatch_output_patch_size = (util_bitcount64(io_info->lds_output_mask & TESS_LVL_MASK) +
                                              util_bitcount(io_info->lds_patch_output_mask)) * 16;
   /* Add 4 to the output patch size to minimize LDS bank conflicts. */
   return get_lds_pervertex_output_patch_size(io_info, tcs_vertices_out) +
          lds_perpatch_output_patch_size + 4;
}

static nir_def *
hs_output_lds_offset(nir_builder *b, lower_tess_io_state *st, unsigned location, unsigned component,
                     nir_def *vertex_index, nir_def *io_offset)
{
   unsigned tcs_vertices_out = b->shader->info.tess.tcs_vertices_out;
   nir_def *off = NULL;

   if (io_offset) {
      const unsigned mapped = hs_output_lds_map_io_location(b->shader, vertex_index != NULL,
                                                            location, st);
      off = ac_nir_calc_io_off(b, component, io_offset,
                               nir_imm_int(b, 16u), 4, mapped);
   } else {
      off = nir_imm_int(b, 0);
   }

   nir_def *rel_patch_id = nir_load_tess_rel_patch_id_amd(b);
   nir_def *patch_offset = nir_imul_imm(b, rel_patch_id,
                                        get_lds_output_patch_stride(&st->io_info, tcs_vertices_out));

   nir_def *tcs_in_vtxcnt = nir_load_patch_vertices_in(b);
   nir_def *tcs_num_patches = nir_load_tcs_num_patches_amd(b);
   nir_def *input_patch_size = nir_imul(b, tcs_in_vtxcnt, nir_load_lshs_vertex_stride_amd(b));
   nir_def *output_patch0_offset = nir_imul(b, input_patch_size, tcs_num_patches);
   nir_def *output_patch_offset = nir_iadd_nuw(b, patch_offset, output_patch0_offset);

   if (vertex_index)
      off = nir_iadd_nuw(b, off, nir_imul_imm(b, vertex_index, get_lds_output_vertex_size(&st->io_info)));
   else
      off = nir_iadd_imm_nuw(b, off, get_lds_pervertex_output_patch_size(&st->io_info, tcs_vertices_out));

   nir_def *lds_offset = nir_iadd_nuw(b, off, output_patch_offset);

   /* The beginning of LDS is reserved for the tess level group vote. */
   return nir_iadd_imm_nuw(b, lds_offset, AC_TESS_LEVEL_VOTE_LDS_BYTES);
}

static unsigned
hs_output_vram_map_io_location(nir_shader *shader,
                               const bool per_vertex,
                               const unsigned loc,
                               lower_tess_io_state *st)
{
   /* Unlinked shaders:
    * We are unaware of TES inputs while lowering TCS outputs.
    * The driver needs to pass a callback to map varyings to a fixed location.
    */
   if (st->map_io)
      return st->map_io(loc);

   /* Linked shaders:
    * Take advantage of having knowledge of TES inputs while lowering TCS outputs.
    * Map varyings to a prefix sum of the IO mask to save space in VRAM.
    */
   if (!per_vertex) {
      const uint64_t tf_mask = st->io_info.vram_output_mask & TESS_LVL_MASK;
      if (loc == VARYING_SLOT_TESS_LEVEL_INNER || loc == VARYING_SLOT_TESS_LEVEL_OUTER) {
         assert(tf_mask & BITFIELD64_BIT(loc));
         return util_bitcount64(tf_mask & BITFIELD64_MASK(loc));
      }

      const uint32_t patch_out_mask = st->io_info.vram_patch_output_mask;
      assert(patch_out_mask & BITFIELD_BIT(loc - VARYING_SLOT_PATCH0));
      return util_bitcount64(tf_mask) +
             util_bitcount(patch_out_mask & BITFIELD_MASK(loc - VARYING_SLOT_PATCH0));
   } else {
      const uint64_t per_vertex_mask = st->io_info.vram_output_mask & ~TESS_LVL_MASK;
      assert(per_vertex_mask & BITFIELD64_BIT(loc));
      return util_bitcount64(per_vertex_mask & BITFIELD64_MASK(loc));
   }
}

static nir_def *
hs_per_vertex_output_vmem_offset(nir_builder *b, lower_tess_io_state *st, unsigned location,
                                 unsigned component, nir_def *vertex_index, nir_def *io_offset,
                                 nir_def *patch_offset)
{
   nir_def *out_vertices_per_patch = b->shader->info.stage == MESA_SHADER_TESS_CTRL
                                         ? nir_imm_int(b, b->shader->info.tess.tcs_vertices_out)
                                         : nir_load_patch_vertices_in(b);
   nir_def *attr_stride = nir_load_tcs_mem_attrib_stride(b);
   nir_def *off =
      ac_nir_calc_io_off(b, component, io_offset, attr_stride, 4u,
                         hs_output_vram_map_io_location(b->shader, true, location, st));

   if (!patch_offset) {
      patch_offset = nir_imul(b, nir_load_tess_rel_patch_id_amd(b),
                              nir_imul_imm(b, out_vertices_per_patch, 16u));
   }

   nir_def *vertex_index_off = nir_imul_imm(b, vertex_index, 16u);

   return nir_iadd_nuw(b, nir_iadd_nuw(b, patch_offset, vertex_index_off), off);
}

static nir_def *
hs_per_patch_output_vmem_offset(nir_builder *b, lower_tess_io_state *st, unsigned location,
                                unsigned component, nir_def *io_offset, nir_def *patch_offset)
{
   nir_def *tcs_num_patches = nir_load_tcs_num_patches_amd(b);
   nir_def *per_patch_data_offset = nir_load_hs_out_patch_data_offset_amd(b);
   /* Align the stride to 256B. */
   nir_def *attr_stride = nir_align_imm(b, nir_imul_imm(b, tcs_num_patches, 16u), 256);

   nir_def *off =
      ac_nir_calc_io_off(b, component, io_offset, attr_stride, 4u,
                         hs_output_vram_map_io_location(b->shader, false, location, st));

   if (!patch_offset)
      patch_offset = nir_imul_imm(b, nir_load_tess_rel_patch_id_amd(b), 16u);

   off = nir_iadd_nuw(b, off, per_patch_data_offset);
   return nir_iadd_nuw(b, off, patch_offset);
}

static nir_def *
lower_hs_per_vertex_input_load(nir_builder *b,
                               nir_instr *instr,
                               void *state)
{
   lower_tess_io_state *st = (lower_tess_io_state *) state;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   nir_def *off = hs_per_vertex_input_lds_offset(b, st, intrin);
   nir_def *load = NULL;

   AC_NIR_LOAD_IO(load, b, intrin->def.num_components, intrin->def.bit_size, io_sem.high_16bits,
                  nir_load_shared, off);

   return load;
}

static nir_variable *
get_or_create_output_variable(nir_builder *b, nir_variable **var, unsigned bit_size)
{
   /* Create the local variable if needed. */
   if (!*var) {
      *var = nir_local_variable_create(b->impl, bit_size == 16 ? &glsl_type_builtin_float16_t :
                                                                 &glsl_type_builtin_float, NULL);
   }
   return *var;
}

static void
store_output_variable(nir_builder *b, nir_def *store_val, unsigned write_mask, unsigned component,
                      bool high_16bits, nir_variable **slot)
{
   u_foreach_bit(i, write_mask << component) {
      assert(!slot[i] ||
             glsl_base_type_bit_size(glsl_get_base_type(slot[i]->type)) == store_val->bit_size);
      assert((store_val->bit_size == 16 &&
              (!slot[4 + i] ||
               glsl_base_type_bit_size(glsl_get_base_type(slot[4 + i]->type)) == store_val->bit_size)) ||
             (store_val->bit_size == 32 && !slot[4 + i]));

      nir_store_var(b, get_or_create_output_variable(b, &slot[i + high_16bits * 4], store_val->bit_size),
                    nir_channel(b, store_val, i - component), 0x1);
   }
}

static nir_def *
lower_hs_output_store(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      lower_tess_io_state *st)
{
   assert(intrin->intrinsic == nir_intrinsic_store_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_store_output);

   nir_io_semantics semantics = nir_intrinsic_io_semantics(intrin);
   const unsigned component = nir_intrinsic_component(intrin);
   nir_def *store_val = intrin->src[0].ssa;
   const unsigned write_mask = nir_intrinsic_write_mask(intrin);
   const bool per_vertex = intrin->intrinsic == nir_intrinsic_store_per_vertex_output;
   const bool write_to_vmem = tcs_output_needs_vmem(intrin, b->shader, st);
   const bool write_to_lds =  tcs_output_needs_lds(intrin, b->shader, st);

   assert(store_val->bit_size & (16 | 32));

   if (write_to_vmem) {
      if (per_vertex) {
         for (unsigned slot = 0; slot < semantics.num_slots; slot++)
            st->tcs_per_vertex_output_vmem_chan_mask[semantics.location + slot] |= write_mask << component;
      } else {
         assert(semantics.location >= VARYING_SLOT_PATCH0 && semantics.location <= VARYING_SLOT_PATCH31);
         unsigned index = semantics.location - VARYING_SLOT_PATCH0;

         for (unsigned slot = 0; slot < semantics.num_slots; slot++)
            st->tcs_per_patch_output_vmem_chan_mask[index + slot] |= write_mask << component;
      }
   }

   if (write_to_lds) {
      nir_def *vertex_index = per_vertex ? nir_get_io_arrayed_index_src(intrin)->ssa : NULL;
      nir_def *lds_off = hs_output_lds_offset(b, st, semantics.location, component,
                                              vertex_index, nir_get_io_offset_src(intrin)->ssa);
      AC_NIR_STORE_IO(b, store_val, 0, write_mask, semantics.high_16bits,
                      nir_store_shared, lds_off, .write_mask = store_write_mask, .base = store_const_offset);
   }

   /* Store per-vertex outputs to temp variables. The outputs will be stored to memory at the end of the shader. */
   if (write_to_vmem && per_vertex &&
       st->io_info.vgpr_output_mask & BITFIELD64_BIT(semantics.location)) {
      assert(semantics.location < ARRAY_SIZE(st->tcs_per_vertex_outputs));
      assert(semantics.num_slots == 1);

      store_output_variable(b, store_val, write_mask, component, semantics.high_16bits,
                            st->tcs_per_vertex_outputs[semantics.location]);
   }

   if (write_to_vmem && !per_vertex) {
      assert(semantics.location >= VARYING_SLOT_PATCH0 && semantics.location <= VARYING_SLOT_PATCH31);
      unsigned index = semantics.location - VARYING_SLOT_PATCH0;

      if (st->io_info.vgpr_patch_output_mask & BITFIELD_BIT(index)) {
         assert(semantics.num_slots == 1);
         store_output_variable(b, store_val, write_mask, component, semantics.high_16bits,
                               st->tcs_per_patch_outputs[index]);
      }
   }

   /* Save tess levels that don't need to be stored in LDS into local variables. */
   if (semantics.location == VARYING_SLOT_TESS_LEVEL_INNER ||
       semantics.location == VARYING_SLOT_TESS_LEVEL_OUTER) {
      unsigned i = semantics.location - VARYING_SLOT_TESS_LEVEL_OUTER;

      st->tcs_tess_level_chan_mask[i] |= write_mask << component;

      if (st->io_info.vgpr_output_mask & BITFIELD64_BIT(semantics.location))
         ac_nir_store_var_components(b, st->tcs_tess_level[i], store_val, component, write_mask);
   }

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

static nir_def *
lower_hs_output_load(nir_builder *b,
                     nir_intrinsic_instr *intrin,
                     lower_tess_io_state *st)
{
   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   const unsigned component = nir_intrinsic_component(intrin);

   if ((io_sem.location == VARYING_SLOT_TESS_LEVEL_INNER ||
        io_sem.location == VARYING_SLOT_TESS_LEVEL_OUTER) &&
       !tcs_output_needs_lds(intrin, b->shader, st)) {
      const unsigned num_components = intrin->def.num_components;
      const unsigned bit_size = intrin->def.bit_size;
      unsigned i = io_sem.location - VARYING_SLOT_TESS_LEVEL_OUTER;

      nir_def *var = nir_load_var(b, st->tcs_tess_level[i]);
      return nir_extract_bits(b, &var, 1, component * bit_size, num_components, bit_size);
   }

   if (io_sem.location >= VARYING_SLOT_PATCH0 && io_sem.location <= VARYING_SLOT_PATCH31 &&
       !tcs_output_needs_lds(intrin, b->shader, st)) {
      /* Return the per-patch output from local variables. */
      assert(io_sem.num_slots == 1);
      unsigned index = io_sem.location - VARYING_SLOT_PATCH0;
      nir_def *comp[4];

      for (unsigned i = 0; i < intrin->def.num_components; i++) {
         nir_variable **var = &st->tcs_per_patch_outputs[index][component + io_sem.high_16bits * 4];

         /* If the first use of the variable is a load, which means the variable hasn't been created yet,
          * it's not always undef because we can be inside a loop that initializes the variable later
          * in the loop but in an earlier iteration.
          */
         comp[i] = nir_load_var(b, get_or_create_output_variable(b, var, intrin->def.bit_size));
      }

      return nir_vec(b, comp, intrin->def.num_components);
   }

   /* If an output is not stored by the shader, replace the output load by undef. */
   if (!tcs_output_needs_lds(intrin, b->shader, st))
      return nir_undef(b, intrin->def.num_components, intrin->def.bit_size);

   nir_def *vertex_index = intrin->intrinsic == nir_intrinsic_load_per_vertex_output ?
                              nir_get_io_arrayed_index_src(intrin)->ssa : NULL;
   nir_def *off = hs_output_lds_offset(b, st, io_sem.location, component,
                                       vertex_index, nir_get_io_offset_src(intrin)->ssa);
   nir_def *load = NULL;

   AC_NIR_LOAD_IO(load, b, intrin->def.num_components, intrin->def.bit_size, io_sem.high_16bits,
                  nir_load_shared, off);

   return load;
}

static void
update_hs_barrier(nir_intrinsic_instr *intrin, lower_tess_io_state *st)
{
   /* Output loads and stores are lowered to shared memory access,
    * so we have to update the barriers to also reflect this.
    */
   unsigned mem_modes = nir_intrinsic_memory_modes(intrin);
   if (mem_modes & nir_var_shader_out) {
      mem_modes |= nir_var_mem_shared;
      mem_modes &= ~nir_var_shader_out;
   }
   nir_intrinsic_set_memory_modes(intrin, mem_modes);

   mesa_scope exec_scope = nir_intrinsic_execution_scope(intrin);
   if (exec_scope == SCOPE_WORKGROUP && st->tcs_out_patch_fits_subgroup)
      nir_intrinsic_set_execution_scope(intrin, SCOPE_SUBGROUP);

   mesa_scope mem_scope = nir_intrinsic_memory_scope(intrin);
   if (mem_scope == SCOPE_WORKGROUP && st->tcs_out_patch_fits_subgroup)
      nir_intrinsic_set_memory_scope(intrin, SCOPE_SUBGROUP);
}

static nir_def *
lower_hs_output_access(nir_builder *b,
                       nir_instr *instr,
                       void *state)
{
   lower_tess_io_state *st = (lower_tess_io_state *) state;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic == nir_intrinsic_store_output ||
       intrin->intrinsic == nir_intrinsic_store_per_vertex_output) {
      return lower_hs_output_store(b, intrin, st);
   } else if (intrin->intrinsic == nir_intrinsic_load_output ||
              intrin->intrinsic == nir_intrinsic_load_per_vertex_output) {
      return lower_hs_output_load(b, intrin, st);
   } else if (intrin->intrinsic == nir_intrinsic_barrier) {
      update_hs_barrier(intrin, st);
      return NIR_LOWER_INSTR_PROGRESS;
   } else {
      UNREACHABLE("intrinsic not supported by lower_hs_output_access");
   }
}

static tess_levels
hs_load_tess_levels(nir_builder *b,
                    lower_tess_io_state *st)
{
   unsigned output_comps[2];
   mesa_count_tess_level_components(b->shader->info.tess._primitive_mode,
                                    &output_comps[0], &output_comps[1]);

   nir_def *outputs[2] = {0};
   nir_def *lds_base = NULL;

   for (unsigned i = 0; i < 2; i++) {
      if (!output_comps[i] || !st->tcs_tess_level_chan_mask[i]) {
         /* Set tess levels to zero if the shader doesn't write them. */
         if (output_comps[i])
            outputs[i] = nir_imm_zero(b, output_comps[i], 32);
         continue;
      }

      if (st->io_info.vgpr_output_mask & BITFIELD64_BIT(VARYING_SLOT_TESS_LEVEL_OUTER + i)) {
         outputs[i] = nir_load_var(b, st->tcs_tess_level[i]);
         outputs[i] = nir_trim_vector(b, outputs[i], output_comps[i]);
         continue;
      }

      /* Base LDS address of per-patch outputs in the current patch. */
      if (!lds_base)
         lds_base = hs_output_lds_offset(b, st, 0, 0, NULL, NULL);

      /* Load tessellation levels from LDS. */
      const unsigned mapped = hs_output_lds_map_io_location(b->shader, false,
                                                            VARYING_SLOT_TESS_LEVEL_OUTER + i, st);
      outputs[i] = nir_load_shared(b, output_comps[i], 32, lds_base, .base = mapped * 16);
   }

   return (tess_levels){
      .outer = outputs[0],
      .inner = outputs[1],
   };
}

static void
hs_store_dynamic_control_word_gfx6(nir_builder *b)
{
   nir_def *rel_patch_id = nir_load_tess_rel_patch_id_amd(b);
   nir_def *tessfactor_ring = nir_load_ring_tess_factors_amd(b);
   nir_def *tess_factors_base = nir_load_ring_tess_factors_offset_amd(b);

   /* Store the dynamic HS control word. */
   nir_if *rel_patch_id_zero = nir_push_if(b, nir_ieq_imm(b, rel_patch_id, 0));
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *ctrlw = nir_imm_int(b, 0x80000000u);
   nir_store_buffer_amd(b, ctrlw, tessfactor_ring, zero, tess_factors_base, zero,
                        .access = ACCESS_COHERENT);
   nir_pop_if(b, rel_patch_id_zero);
}

static nir_def *
hs_resize_tess_factor(nir_builder *b, nir_def *tf, unsigned comps)
{
   if (!comps)
      return NULL;
   else if (!tf)
      return nir_imm_zero(b, comps, 32);
   else if (comps > tf->num_components)
      return nir_pad_vector_imm_int(b, tf, 0, comps);
   else if (comps < tf->num_components)
      return nir_trim_vector(b, tf, comps);
   else
      return tf;
}

static nir_if *
hs_if_invocation_id_zero(nir_builder *b)
{
   nir_def *invocation_id = nir_load_invocation_id(b);

   /* Only the 1st invocation of each patch needs to do this. */
   nir_if *invocation_id_zero = nir_push_if(b, nir_ieq_imm(b, invocation_id, 0));

   /* When the output patch size is <= 32 then we can flatten the branch here
    * because we know for sure that at least 1 invocation in all waves will
    * take the branch.
    */
   if (b->shader->info.tess.tcs_vertices_out <= 32)
      invocation_id_zero->control = nir_selection_control_divergent_always_taken;

   return invocation_id_zero;
}

#define VOTE_RESULT_NORMAL       0  /* execute output stores and tess factor stores */
#define VOTE_RESULT_ALL_TF_ZERO  1  /* skip output stores, skip tess factor stores on GFX11+ */
#define VOTE_RESULT_ALL_TF_ONE   2  /* execute output stores, skip tess factor stores on GFX11+ */

/* Return VOTE_RESULT_*. This also sends the HS_TESSFACTOR shader message on GFX11+. */
static nir_def *
hs_tess_level_group_vote(nir_builder *b, lower_tess_io_state *st,
                         tess_levels *tessfactors, nir_def *prim_mode)
{
   /* Don't do the group vote and send the message directly if tess level values were determined
    * by nir_gather_tcs_info at compile time.
    *
    * Disable the shader cache if you set the environment variable.
    */
   if (debug_get_bool_option("AMD_FAST_HS_MSG", true) &&
       (st->tcs_info.all_tess_levels_are_effectively_zero ||
        st->tcs_info.all_tess_levels_are_effectively_one)) {
      if (st->gfx_level >= GFX11) {
         nir_if *if_subgroup0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
         {
            /* m0[0] == 0 means all TF are 0 in the workgroup.
             * m0[0] == 1 means all TF are 1 in the workgroup.
             */
            nir_def *m0 = nir_imm_int(b, st->tcs_info.all_tess_levels_are_effectively_zero ? 0 : 1);
            nir_sendmsg_amd(b, m0, .base = AC_SENDMSG_HS_TESSFACTOR);
         }
         nir_pop_if(b, if_subgroup0);
      }

      return nir_imm_int(b, st->tcs_info.all_tess_levels_are_effectively_zero ?
                              VOTE_RESULT_ALL_TF_ZERO : VOTE_RESULT_ALL_TF_ONE);
   }

   /* If TCS never discards patches, GFX6-10 don't need the group vote because the vote is only
    * used to skip output stores there.
    */
   if (st->gfx_level < GFX11 && !st->tcs_info.can_discard_patches)
      return nir_imm_int(b, VOTE_RESULT_NORMAL);

   /* Initialize the first LDS dword for the tf0/1 group vote at the beginning of TCS. */
   nir_block *start_block = nir_start_block(nir_shader_get_entrypoint(b->shader));
   nir_builder top_b = nir_builder_at(nir_before_block(start_block));

   nir_if *thread0 = nir_push_if(&top_b,
                                 nir_iand(&top_b, nir_ieq_imm(&top_b, nir_load_subgroup_id(&top_b), 0),
                                          nir_inverse_ballot_imm(&top_b, 0x1, st->wave_size)));
   {
      /* 0x3 is the initial bitmask (tf0 | tf1). Each subgroup will do atomic iand on it for the vote. */
      nir_store_shared(&top_b, nir_imm_int(&top_b, 0x3), nir_imm_int(&top_b, 0),
                       .write_mask = 0x1, .align_mul = 4);
   }
   nir_pop_if(&top_b, thread0);

   /* Insert a barrier to wait for initialization above if there hasn't been any other barrier
    * in the shader. If tcs_out_patch_fits_subgroup=true, then TCS barriers don't have a scope
    * larger than a subgroup.
    */
   if (!st->tcs_info.always_executes_barrier || st->tcs_out_patch_fits_subgroup) {
      nir_barrier(b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
                  .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
   }

   /* Use s_sendmsg to tell the hw whether the whole workgroup has either of these cases:
    *
    * tf0: All patches in the workgroup have at least one outer tess level component either
    *      in the [-inf, 0] range or equal to NaN, causing them to be discarded. Inner tess levels
    *      have no effect.
    *
    * tf1: All patches in the workgroup have the values of tess levels set to 1 or equivalent numbers,
    *      which doesn't discard any patches. Each spacing interprets different tess level ranges as 1:
    *
    *      1) equal_spacing, fractional_odd_spacing, and unknown spacing
    *      For undiscarded patches, the tessellator clamps all tess levels to 1. If all tess levels
    *      are in the (0, 1] range, which is effectively 1, untessellated patches are
    *      drawn.
    *
    *      2) fractional_even_spacing
    *      For undiscarded patches, the tessellator clamps all tess levels to 2 (both outer and inner)
    *      except isolines, which clamp the first outer tess level component to 1. If all outer tess
    *      levels are in the (0, 2] or (0, 1] range (for outer[0] of isolines) and all inner tess levels
    *      are in the [-inf, 2] range, the tf1 message can be used. The tessellator will receive 1 via
    *      the message, but will clamp them to 2 or keep 1 (for outer[0] of isolines).
    *
    *      If we make this mutually exclusive with tf0, we only have to compare against the upper bound.
    */

   /* Determine tf0/tf1 for the subgroup at the end of TCS. */
   nir_if *if_invocation_id_zero = hs_if_invocation_id_zero(b);
   {
      *tessfactors = hs_load_tess_levels(b, st);

      nir_if *if0 = NULL, *if1 = NULL;
      nir_def *lane_tf_effectively_0[3] = {0};
      nir_def *lane_tf_effectively_1[3] = {0};

      static_assert(TESS_PRIMITIVE_TRIANGLES == 1, "");
      static_assert(TESS_PRIMITIVE_QUADS == 2, "");
      static_assert(TESS_PRIMITIVE_ISOLINES == 3, "");

      for (unsigned prim = TESS_PRIMITIVE_TRIANGLES; prim <= TESS_PRIMITIVE_ISOLINES; prim++) {
         /* Generate:
          *    if (triangles) ...
          *    else if (quads) ...
          *    else // isolines
          */
         if (prim == TESS_PRIMITIVE_TRIANGLES) {
            if0 = nir_push_if(b, nir_ieq_imm(b, prim_mode, prim));
         } else if (prim == TESS_PRIMITIVE_QUADS) {
            nir_push_else(b, if0);
            if1 = nir_push_if(b, nir_ieq_imm(b, prim_mode, prim));
         } else {
            nir_push_else(b, if1);
         }

         unsigned outer_comps, inner_comps;
         mesa_count_tess_level_components(prim, &outer_comps, &inner_comps);
         outer_comps = MIN2(outer_comps, tessfactors->outer->num_components);
         inner_comps = tessfactors->inner ? MIN2(inner_comps, tessfactors->inner->num_components) : 0;

         lane_tf_effectively_0[prim - 1] = nir_imm_false(b);
         for (unsigned i = 0; i < outer_comps; i++) {
            /* fgeu returns true for NaN */
            nir_def *le0 = nir_fgeu(b, nir_imm_float(b, 0), nir_channel(b, tessfactors->outer, i));
            lane_tf_effectively_0[prim - 1] = nir_ior(b, lane_tf_effectively_0[prim - 1], le0);
         }

         /* Use case 1: unknown spacing */
         lane_tf_effectively_1[prim - 1] = nir_imm_true(b);
         for (unsigned i = 0; i < outer_comps; i++) {
            nir_def *le1 = nir_fle_imm(b, nir_channel(b, tessfactors->outer, i), 1);
            lane_tf_effectively_1[prim - 1] = nir_iand(b, lane_tf_effectively_1[prim - 1], le1);
         }

         for (unsigned i = 0; i < inner_comps; i++) {
            nir_def *le1 = nir_fle_imm(b, nir_channel(b, tessfactors->inner, i), 1);
            lane_tf_effectively_1[prim - 1] = nir_iand(b, lane_tf_effectively_1[prim - 1], le1);
         }
      }

      nir_pop_if(b, if1);
      lane_tf_effectively_0[1] = nir_if_phi(b, lane_tf_effectively_0[1], lane_tf_effectively_0[2]);
      lane_tf_effectively_1[1] = nir_if_phi(b, lane_tf_effectively_1[1], lane_tf_effectively_1[2]);
      nir_pop_if(b, if0);
      lane_tf_effectively_0[0] = nir_if_phi(b, lane_tf_effectively_0[0], lane_tf_effectively_0[1]);
      lane_tf_effectively_1[0] = nir_if_phi(b, lane_tf_effectively_1[0], lane_tf_effectively_1[1]);

      /* Make them mutually exclusive. */
      lane_tf_effectively_1[0] = nir_iand(b, lane_tf_effectively_1[0], nir_inot(b, lane_tf_effectively_0[0]));

      nir_def *subgroup_uses_tf0 = nir_b2i32(b, nir_vote_all(b, 1, lane_tf_effectively_0[0]));
      nir_def *subgroup_uses_tf1 = nir_b2i32(b, nir_vote_all(b, 1, lane_tf_effectively_1[0]));

      /* Pack the value for LDS. Encoding:
       *    0 = none of the below
       *    1 = all tess factors are effectively 0
       *    2 = all tess factors are effectively 1
       *    3 = invalid
       *
       * Since we will do bitwise AND reduction across all waves, 3 can never occur.
       */
      nir_def *packed_tf01_mask = nir_ior(b, subgroup_uses_tf0,
                                          nir_ishl_imm(b, subgroup_uses_tf1, 1));

      /* This function is only called within a block that only executes for patch invocation 0, so we
       * only need to mask out invocation 0 of other patches in the subgroup to execute on only 1 lane.
       *
       * Since patch invocations are placed sequentially in the subgroup, we know that invocation 0
       * of the lowest patch must be somewhere in BITFIELD_MASK(tcs_vertices_out) lanes.
       */
      const unsigned tcs_vertices_out = b->shader->info.tess.tcs_vertices_out;
      assert(tcs_vertices_out <= 32);
      nir_def *is_first_active_lane =
         nir_inverse_ballot_imm(b, BITFIELD_MASK(tcs_vertices_out), st->wave_size);

      /* Only the first active invocation in each subgroup performs the AND reduction through LDS. */
      nir_if *if_first_active_lane = nir_push_if(b, is_first_active_lane);
      if_first_active_lane->control = nir_selection_control_divergent_always_taken;
      {
         /* Use atomic iand to combine results from all subgroups. */
         nir_shared_atomic(b, 32, nir_imm_int(b, 0), packed_tf01_mask,
                           .atomic_op = nir_atomic_op_iand);
      }
      nir_pop_if(b, if_first_active_lane);
   }
   nir_pop_if(b, if_invocation_id_zero);
   /* The caller will reuse these. */
   tessfactors->outer = nir_if_phi(b, tessfactors->outer, nir_undef(b, tessfactors->outer->num_components, 32));
   if (tessfactors->inner) /* Isolines don't have inner tess levels. */
      tessfactors->inner = nir_if_phi(b, tessfactors->inner, nir_undef(b, tessfactors->inner->num_components, 32));

   /* Wait for all waves to execute the LDS atomic. */
   nir_barrier(b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);

   /* Read the result from LDS. Only 1 lane should load it to prevent LDS bank conflicts. */
   nir_def *lds_result;
   nir_if *if_lane0 = nir_push_if(b, nir_inverse_ballot_imm(b, 0x1, st->wave_size));
   if_lane0->control = nir_selection_control_divergent_always_taken;
   {
      lds_result = nir_load_shared(b, 1, 32, nir_imm_int(b, 0), .align_mul = 4);
   }
   nir_pop_if(b, if_lane0);
   lds_result = nir_if_phi(b, lds_result, nir_undef(b, 1, 32));
   lds_result = nir_read_invocation(b, lds_result, nir_imm_int(b, 0));

   /* Send the message. */
   if (st->gfx_level >= GFX11) {
      nir_def *use_memory = nir_ieq_imm(b, lds_result, 0);

      nir_if *if_subgroup0_sendmsg = nir_push_if(b, nir_iand(b, nir_inot(b, use_memory),
                                                             nir_ieq_imm(b, nir_load_subgroup_id(b), 0)));
      {
         /* m0[0] == 0 means all TF are 0 in the workgroup.
          * m0[0] == 1 means all TF are 1 in the workgroup.
          */
         nir_def *m0 = nir_iadd_imm(b, lds_result, -1);
         nir_sendmsg_amd(b, m0, .base = AC_SENDMSG_HS_TESSFACTOR);
      }
      nir_pop_if(b, if_subgroup0_sendmsg);
   }

   return lds_result;
}

static void
hs_store_tess_factors_for_tessellator(nir_builder *b, enum amd_gfx_level gfx_level,
                                      enum tess_primitive_mode prim_mode,
                                      tess_levels tessfactors)
{
   nir_def *rel_patch_id = nir_load_tess_rel_patch_id_amd(b);
   nir_def *tessfactor_ring = nir_load_ring_tess_factors_amd(b);
   nir_def *tess_factors_base = nir_load_ring_tess_factors_offset_amd(b);
   nir_def *zero = nir_imm_int(b, 0);

   const unsigned tess_factors_const_offset = gfx_level <= GFX8 ? 4 : 0;
   unsigned outer_comps, inner_comps;

   mesa_count_tess_level_components(prim_mode, &outer_comps, &inner_comps);

   nir_def *tess_factors_offset =
      nir_imul_imm(b, rel_patch_id, (inner_comps + outer_comps) * 4u);

   nir_def *tf_outer = hs_resize_tess_factor(b, tessfactors.outer, outer_comps);
   nir_def *tf_inner = hs_resize_tess_factor(b, tessfactors.inner, inner_comps);

   /* Store tess factors for the tessellator */
   if (prim_mode == TESS_PRIMITIVE_ISOLINES) {
      /* LINES reversal */
      nir_def *t = nir_vec2(b, nir_channel(b, tf_outer, 1), nir_channel(b, tf_outer, 0));
      nir_store_buffer_amd(b, t, tessfactor_ring, tess_factors_offset, tess_factors_base, zero,
                           .base = tess_factors_const_offset, .access = ACCESS_COHERENT | ACCESS_CP_GE_COHERENT_AMD);
   } else if (prim_mode == TESS_PRIMITIVE_TRIANGLES) {
      nir_def *t = nir_vec4(b, nir_channel(b, tf_outer, 0), nir_channel(b, tf_outer, 1),
                               nir_channel(b, tf_outer, 2), nir_channel(b, tf_inner, 0));
      nir_store_buffer_amd(b, t, tessfactor_ring, tess_factors_offset, tess_factors_base, zero,
                           .base = tess_factors_const_offset, .access = ACCESS_COHERENT | ACCESS_CP_GE_COHERENT_AMD);
   } else {
      nir_store_buffer_amd(b, tf_outer, tessfactor_ring, tess_factors_offset, tess_factors_base, zero,
                           .base = tess_factors_const_offset, .access = ACCESS_COHERENT | ACCESS_CP_GE_COHERENT_AMD);
      nir_store_buffer_amd(b, tf_inner, tessfactor_ring, tess_factors_offset, tess_factors_base, zero,
                           .base = tess_factors_const_offset + 4u * outer_comps,
                           .access = ACCESS_COHERENT | ACCESS_CP_GE_COHERENT_AMD);
   }
}

static void
hs_store_tess_factors_for_tes(nir_builder *b, tess_levels tessfactors, lower_tess_io_state *st)
{
   nir_def *hs_ring_tess_offchip = nir_load_ring_tess_offchip_amd(b);
   nir_def *offchip_offset = nir_load_ring_tess_offchip_offset_amd(b);
   nir_def *zero = nir_imm_int(b, 0);

   /* For linked shaders, we must only write the tess factors that the TES actually reads,
    * otherwise we would write to a memory location reserved for another per-patch output.
    */
   for (unsigned i = 0; i < 2; i++) {
      nir_def *output_value = i ? tessfactors.inner : tessfactors.outer;

      if (!output_value || !(st->io_info.vram_output_mask & (VARYING_BIT_TESS_LEVEL_OUTER << i)))
         continue;

      nir_def *vmem_off = hs_per_patch_output_vmem_offset(b, st, VARYING_SLOT_TESS_LEVEL_OUTER + i, 0, zero, NULL);

      /* Always store whole vec4s to get cached bandwidth. Non-vec4 stores cause implicit memory loads
       * to fill the rest of cache lines with this layout.
       */
      nir_store_buffer_amd(b, nir_pad_vec4(b, output_value), hs_ring_tess_offchip, vmem_off,
                           offchip_offset, zero,
                           .memory_modes = nir_var_shader_out,
                           .access = ACCESS_COHERENT);
   }
}

static nir_def *
make_vec4(nir_builder *b, nir_def *comp[4])
{
   for (unsigned i = 0; i < 4; i++) {
      if (!comp[i])
         comp[i] = nir_undef(b, 1, 32);
   }

   return nir_vec(b, comp, 4);
}

static nir_def *
load_output_channel_from_var(nir_builder *b, nir_variable *vec[8], unsigned chan)
{
   nir_def *lo = NULL, *hi = NULL;

   /* It can be one 32-bit value or two 16-bit values. */
   if (vec[chan])
      lo = nir_load_var(b, vec[chan]);
   if (vec[4 + chan])
      hi = nir_load_var(b, vec[4 + chan]);

   if (lo && hi)
      return nir_pack_32_2x16_split(b, lo, hi);
   else if (hi)
      return nir_ishl_imm(b, nir_u2u32(b, hi), 16);
   else
      return nir_u2u32(b, lo);
}

static void
hs_finale(nir_shader *shader, lower_tess_io_state *st)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);
   nir_block *last_block = nir_impl_last_block(impl);
   assert(last_block);

   nir_builder builder = nir_builder_at(nir_after_block(last_block));
   nir_builder *b = &builder; /* This is to avoid the & */

   /* Insert a barrier to wait for output stores to LDS. */
   if (shader->info.outputs_written & ~st->io_info.vgpr_output_mask ||
       shader->info.patch_outputs_written & ~st->io_info.vgpr_patch_output_mask) {
      mesa_scope scope = st->tcs_out_patch_fits_subgroup ? SCOPE_SUBGROUP : SCOPE_WORKGROUP;
      nir_barrier(b, .execution_scope = scope, .memory_scope = scope,
                     .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
      st->tcs_info.always_executes_barrier = true;
   }

   nir_def *prim_mode = nir_load_tcs_primitive_mode_amd(b);
   tess_levels tessfactors = {0};
   nir_def *vote_result = hs_tess_level_group_vote(b, st, &tessfactors, prim_mode);

   /* Only the 1st invocation of each patch needs to access VRAM and/or LDS. */
   nir_if *if_invocation_id_zero = hs_if_invocation_id_zero(b);
   {
      if (!tessfactors.outer)
         tessfactors = hs_load_tess_levels(b, st);

      nir_if *if_use_memory = NULL;
      if (st->gfx_level >= GFX11)
         if_use_memory = nir_push_if(b, nir_ieq_imm(b, vote_result, VOTE_RESULT_NORMAL));

      if (st->gfx_level <= GFX8)
         hs_store_dynamic_control_word_gfx6(b);

      nir_if *if_triangles = nir_push_if(b, nir_ieq_imm(b, prim_mode, TESS_PRIMITIVE_TRIANGLES));
      {
         hs_store_tess_factors_for_tessellator(b, st->gfx_level, TESS_PRIMITIVE_TRIANGLES, tessfactors);
      }
      nir_push_else(b, if_triangles);
      {
         nir_if *if_isolines = nir_push_if(b, nir_ieq_imm(b, prim_mode, TESS_PRIMITIVE_ISOLINES));
         {
            hs_store_tess_factors_for_tessellator(b, st->gfx_level, TESS_PRIMITIVE_ISOLINES, tessfactors);
         }
         nir_push_else(b, if_isolines);
         {
            hs_store_tess_factors_for_tessellator(b, st->gfx_level, TESS_PRIMITIVE_QUADS, tessfactors);
         }
         nir_pop_if(b, if_isolines);
      }
      nir_pop_if(b, if_triangles);

      if (if_use_memory != NULL)
         nir_pop_if(b, if_use_memory);

      nir_if *if_tes_reads_tf = nir_push_if(b, nir_load_tcs_tess_levels_to_tes_amd(b));
      {
         hs_store_tess_factors_for_tes(b, tessfactors, st);
      }
      nir_pop_if(b, if_tes_reads_tf);
   }
   nir_pop_if(b, if_invocation_id_zero);

   /* Gather per-vertex output values from local variables and LDS. */
   nir_def *outputs[VARYING_SLOT_MAX] = {0};
   nir_def *patch_outputs[MAX_VARYING] = {0};
   nir_def *invocation_id = nir_load_invocation_id(b);
   nir_def *zero = nir_imm_int(b, 0);

   /* Don't load per-vertex and per-patch outputs from LDS if all tess factors are 0. */
   nir_if *if_not_discarded = nir_push_if(b, nir_ine_imm(b, vote_result, VOTE_RESULT_ALL_TF_ZERO));
   {
      /* Load per-vertex outputs from LDS or local variables. */
      u_foreach_bit64(slot, st->io_info.vram_output_mask & ~TESS_LVL_MASK) {
         if (!st->tcs_per_vertex_output_vmem_chan_mask[slot])
            continue;

         nir_def *comp[4] = {0};

         /* Gather stored components either from LDS or from local variables.  */
         if ((shader->info.outputs_written & ~st->io_info.vgpr_output_mask) & BITFIELD64_BIT(slot)) {
            u_foreach_bit(i, st->tcs_per_vertex_output_vmem_chan_mask[slot]) {
               nir_def *lds_off = hs_output_lds_offset(b, st, slot, i, invocation_id, zero);
               comp[i] = nir_load_shared(b, 1, 32, lds_off);
            }
         } else {
            u_foreach_bit(i, st->tcs_per_vertex_output_vmem_chan_mask[slot]) {
               comp[i] = load_output_channel_from_var(b, st->tcs_per_vertex_outputs[slot], i);
            }
         }

         outputs[slot] = make_vec4(b, comp);
      }

      /* Load per-patch outputs from LDS or local variables. */
      u_foreach_bit(slot, st->io_info.vram_patch_output_mask) {
         if (!st->tcs_per_patch_output_vmem_chan_mask[slot])
            continue;

         nir_def *comp[4] = {0};

         /* Gather stored components either from LDS or from local variables.  */
         if ((shader->info.patch_outputs_written & ~st->io_info.vgpr_patch_output_mask) & BITFIELD_BIT(slot)) {
            u_foreach_bit(i, st->tcs_per_patch_output_vmem_chan_mask[slot]) {
               nir_def *lds_off = hs_output_lds_offset(b, st, VARYING_SLOT_PATCH0 + slot, i,
                                                       NULL, zero);
               comp[i] = nir_load_shared(b, 1, 32, lds_off);
            }
         } else {
            u_foreach_bit(i, st->tcs_per_patch_output_vmem_chan_mask[slot]) {
               comp[i] = load_output_channel_from_var(b, st->tcs_per_patch_outputs[slot], i);
            }
         }

         patch_outputs[slot] = make_vec4(b, comp);
      }
   }
   nir_pop_if(b, if_not_discarded);
   u_foreach_bit64(slot, st->io_info.vram_output_mask & ~TESS_LVL_MASK) {
      if (outputs[slot])
         outputs[slot] = nir_if_phi(b, outputs[slot], nir_undef(b, 4, 32));
   }
   u_foreach_bit(slot, st->io_info.vram_patch_output_mask) {
      if (patch_outputs[slot])
         patch_outputs[slot] = nir_if_phi(b, patch_outputs[slot], nir_undef(b, 4, 32));
   }

   if (st->gfx_level >= GFX9) {
      /* Wrap the whole shader in a conditional block, allowing only TCS (HS) invocations to execute
       * in the LS-HS workgroup.
       */
      nir_cf_list *extracted = rzalloc(shader, nir_cf_list);
      nir_cf_extract(extracted, nir_before_impl(impl), nir_after_impl(impl));

      builder = nir_builder_at(nir_before_impl(impl));
      nir_if *if_tcs =
         nir_push_if(b, nir_is_subgroup_invocation_lt_amd(b, nir_load_merged_wave_info_amd(b),
                                                          .base = 8));
      {
         nir_cf_reinsert(extracted, b->cursor);
      }
      nir_pop_if(b, if_tcs);
      vote_result = nir_if_phi(b, vote_result, nir_undef(b, 1, 32)); /* no-op, it should be an SGPR */

      u_foreach_bit64(slot, st->io_info.vram_output_mask & ~TESS_LVL_MASK) {
         if (outputs[slot])
            outputs[slot] = nir_if_phi(b, outputs[slot], nir_undef(b, 4, 32));
      }

      u_foreach_bit(slot, st->io_info.vram_patch_output_mask) {
         if (patch_outputs[slot])
            patch_outputs[slot] = nir_if_phi(b, patch_outputs[slot], nir_undef(b, 4, 32));
      }
   }

   /* Store per-vertex outputs to memory. */
   nir_def *is_tcs_thread = nir_imm_true(b);
   nir_def *is_pervertex_store_thread = nir_imm_true(b);

   /* Align the EXEC mask to 8 lanes to overwrite whole 128B blocks on GFX10+, or 4 lanes to
    * overwrite whole 64B blocks on GFX9.
    *
    * Per-patch outputs get the same treatment if tcs_vertices_out == 1, using the same
    * aligned EXEC.
    *
    * GFX6-8 can't align the EXEC mask because it's not ~0.
    */
   if (st->gfx_level >= GFX9) {
      unsigned align = st->gfx_level >= GFX10 ? 8 : 4;
      nir_def *num_tcs_threads = nir_ubfe_imm(b, nir_load_merged_wave_info_amd(b), 8, 8);
      nir_def *aligned_tcs_threads = nir_align_imm(b, num_tcs_threads, align);
      is_tcs_thread = nir_is_subgroup_invocation_lt_amd(b, num_tcs_threads);
      is_pervertex_store_thread = nir_is_subgroup_invocation_lt_amd(b, aligned_tcs_threads);
   }

   nir_def *local_invocation_index = nir_load_local_invocation_index(b);
   nir_def *hs_ring_tess_offchip = nir_load_ring_tess_offchip_amd(b);
   nir_def *offchip_offset = nir_load_ring_tess_offchip_offset_amd(b);
   bool patch_outputs_use_vertex_threads = shader->info.tess.tcs_vertices_out == 1;
   nir_if *if_perpatch_stores = NULL;

   zero = nir_imm_int(b, 0);

   nir_if *if_pervertex_stores =
      nir_push_if(b, nir_iand(b, is_pervertex_store_thread,
                              nir_ine_imm(b, vote_result, VOTE_RESULT_ALL_TF_ZERO)));
   {
      u_foreach_bit64(slot, st->io_info.vram_output_mask & ~TESS_LVL_MASK) {
         if (!outputs[slot])
            continue;

         nir_def *vmem_off = hs_per_vertex_output_vmem_offset(b, st, slot, 0, local_invocation_index,
                                                              zero, zero);

         /* Always store whole vec4s to get cached bandwidth. Non-vec4 stores cause implicit memory loads
          * to fill the rest of cache lines with this layout.
          */
         nir_store_buffer_amd(b, outputs[slot], hs_ring_tess_offchip, vmem_off, offchip_offset, zero,
                              .memory_modes = nir_var_shader_out, .access = ACCESS_COHERENT);
      }
   }

   /* If we don't use vertex threads to store per-patch outputs, i.e. tcs_vertices_out != 1,
    * store per-patch outputs in the first invocation of each patch.
    */
   if (!patch_outputs_use_vertex_threads) {
      nir_pop_if(b, if_pervertex_stores);

      if_perpatch_stores =
         nir_push_if(b, nir_iand(b, is_tcs_thread,
                                 nir_iand(b, nir_ieq_imm(b, nir_load_invocation_id(b), 0),
                                          nir_ine_imm(b, vote_result, VOTE_RESULT_ALL_TF_ZERO))));
   }
   {
      u_foreach_bit(slot, st->io_info.vram_patch_output_mask) {
         if (!patch_outputs[slot])
            continue;

         nir_def *vmem_off = hs_per_patch_output_vmem_offset(b, st, VARYING_SLOT_PATCH0 + slot, 0, zero,
                                                             patch_outputs_use_vertex_threads ?
                                                                nir_imul_imm(b, local_invocation_index, 16u) :
                                                                NULL);

         /* Always store whole vec4s to get cached bandwidth. Non-vec4 stores cause implicit memory loads
          * to fill the rest of cache lines with this layout, as well as when a wave doesn't write whole
          * 64B (GFX6-9) or 128B (GFX10+) blocks.
          *
          * A wave gets cached bandwidth for per-patch output stores only in these cases:
          * - tcs_vertices_out == 1 and lanes are aligned to 4 (GFX6-9) or 8 (GFX10+) lanes (always done)
          * - tcs_vertices_out == 2 or 4 except the last 4 (GFX6-9) or 8 (GFX10+) invocation_id==0 lanes
          *   if not all lanes are enabled in the last group of 4 or 8 in the last wave
          * - tcs_vertices_out == 8 only with wave64 on GFX10+ except the last 8 invocation_id==0 lanes
          *   if not all lanes are enabled in the last group of 8 in the last wave
          * - all full groups of 4 (GFX6-9) or 8 (GFX10+) lanes in the first wave because lane 0 outputs
          *   of the first wave are always aligned to 256B
          *
          * Note that the sparsity of invocation_id==0 lanes doesn't matter as long as the whole wave
          * covers one or more whole 64B (GFX6-9) or 128B (GFX10+) blocks.
          */
         nir_store_buffer_amd(b, patch_outputs[slot], hs_ring_tess_offchip, vmem_off, offchip_offset, zero,
                              .memory_modes = nir_var_shader_out, .access = ACCESS_COHERENT);
      }
   }
   if (patch_outputs_use_vertex_threads)
      nir_pop_if(b, if_pervertex_stores);
   else
      nir_pop_if(b, if_perpatch_stores);

   nir_progress(true, impl, nir_metadata_none);
}

static nir_def *
lower_tes_input_load(nir_builder *b,
                     nir_instr *instr,
                     void *state)
{
   lower_tess_io_state *st = (lower_tess_io_state *) state;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   nir_def *offchip_ring = nir_load_ring_tess_offchip_amd(b);
   nir_def *offchip_offset = nir_load_ring_tess_offchip_offset_amd(b);
   nir_def *off = intrin->intrinsic == nir_intrinsic_load_per_vertex_input
                    ? hs_per_vertex_output_vmem_offset(b, st, io_sem.location,
                                                       nir_intrinsic_component(intrin),
                                                       nir_get_io_arrayed_index_src(intrin)->ssa,
                                                       nir_get_io_offset_src(intrin)->ssa, NULL)
                    : hs_per_patch_output_vmem_offset(b, st, io_sem.location,
                                                      nir_intrinsic_component(intrin),
                                                      nir_get_io_offset_src(intrin)->ssa, NULL);

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *load = NULL;

   AC_NIR_LOAD_IO(load, b, intrin->def.num_components, intrin->def.bit_size, io_sem.high_16bits,
                  nir_load_buffer_amd, offchip_ring, off, offchip_offset, zero,
                  .access = ACCESS_COHERENT | ACCESS_CAN_REORDER | ACCESS_CAN_SPECULATE,
                  .memory_modes = nir_var_shader_in);

   return load;
}

static bool
filter_hs_output_access(const nir_instr *instr,
                         UNUSED const void *st)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return intrin->intrinsic == nir_intrinsic_store_output ||
          intrin->intrinsic == nir_intrinsic_store_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_load_output ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_barrier;
}

static bool
filter_any_input_access(const nir_instr *instr,
                        UNUSED const void *st)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return intrin->intrinsic == nir_intrinsic_load_input ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_input;
}

bool
ac_nir_lower_ls_outputs_to_mem(nir_shader *shader,
                               ac_nir_map_io_driver_location map,
                               enum amd_gfx_level gfx_level,
                               bool tcs_in_out_eq,
                               uint64_t tcs_inputs_via_temp,
                               uint64_t tcs_inputs_via_lds)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);
   assert(gfx_level >= GFX9 || !tcs_in_out_eq);

   lower_tess_io_state state = {
      .gfx_level = gfx_level,
      .map_io = map,
   };

   if (tcs_in_out_eq) {
      state.tcs_inputs_via_temp = tcs_inputs_via_temp;
      state.tcs_inputs_via_lds = tcs_inputs_via_lds;
   } else {
      state.tcs_inputs_via_lds = tcs_inputs_via_lds | tcs_inputs_via_temp;
   }

   return nir_shader_intrinsics_pass(shader, lower_ls_output_store,
                                     nir_metadata_control_flow,
                                     &state);
}

bool
ac_nir_lower_hs_inputs_to_mem(nir_shader *shader,
                              ac_nir_map_io_driver_location map,
                              enum amd_gfx_level gfx_level,
                              bool tcs_in_out_eq,
                              uint64_t tcs_inputs_via_temp,
                              uint64_t tcs_inputs_via_lds)
{
   assert(shader->info.stage == MESA_SHADER_TESS_CTRL);
   assert(gfx_level >= GFX9 || !tcs_in_out_eq);

   lower_tess_io_state state = {
      .gfx_level = gfx_level,
      .map_io = map,
   };

   if (tcs_in_out_eq) {
      state.tcs_inputs_via_temp = tcs_inputs_via_temp;
      state.tcs_inputs_via_lds = tcs_inputs_via_lds;
   } else {
      state.tcs_inputs_via_lds = shader->info.inputs_read;
   }

   return nir_shader_lower_instructions(shader,
                                        filter_load_tcs_per_vertex_input,
                                        lower_hs_per_vertex_input_load,
                                        &state);
}

bool
ac_nir_lower_hs_outputs_to_mem(nir_shader *shader, const nir_tcs_info *info,
                               const ac_nir_tess_io_info *io_info,
                               ac_nir_map_io_driver_location map,
                               enum amd_gfx_level gfx_level,
                               unsigned wave_size)
{
   assert(shader->info.stage == MESA_SHADER_TESS_CTRL);

   /* Fold constant offset srcs for IO. */
   NIR_PASS(_, shader, nir_opt_constant_folding);

   lower_tess_io_state state = {
      .gfx_level = gfx_level,
      .wave_size = wave_size,
      .tcs_info = *info,
      .io_info = *io_info,
      .tcs_out_patch_fits_subgroup = wave_size % shader->info.tess.tcs_vertices_out == 0,
      .map_io = map,
   };

   for (unsigned i = 0; i < 2; i++) {
      state.tcs_tess_level[i] =
         nir_local_variable_create(nir_shader_get_entrypoint(shader), glsl_vec4_type(), "tess outer");
   }

   nir_shader_lower_instructions(shader,
                                 filter_hs_output_access,
                                 lower_hs_output_access,
                                 &state);

   hs_finale(shader, &state);

   NIR_PASS(_, shader, nir_lower_vars_to_ssa);
   NIR_PASS(_, shader, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS(_, shader, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS(_, shader, nir_lower_phis_to_scalar, ac_nir_lower_phis_to_scalar_cb, NULL);

   return true;
}

bool
ac_nir_lower_tes_inputs_to_mem(nir_shader *shader,
                               ac_nir_map_io_driver_location map)
{
   assert(shader->info.stage == MESA_SHADER_TESS_EVAL);

   lower_tess_io_state state = {
      .io_info.vram_output_mask = shader->info.inputs_read,
      .io_info.vram_patch_output_mask = shader->info.patch_inputs_read,
      .map_io = map,
   };

   return nir_shader_lower_instructions(shader,
                                        filter_any_input_access,
                                        lower_tes_input_load,
                                        &state);
}

void
ac_nir_compute_tess_wg_info(const struct radeon_info *info, const ac_nir_tess_io_info *io_info,
                            unsigned tcs_vertices_out, unsigned wave_size, bool tess_uses_primid,
                            unsigned num_tcs_input_cp, unsigned lds_input_vertex_size,
                            unsigned num_remapped_tess_level_outputs, unsigned *num_patches_per_wg,
                            unsigned *lds_size)
{
   unsigned lds_per_patch = num_tcs_input_cp * lds_input_vertex_size +
                            get_lds_output_patch_stride(io_info, tcs_vertices_out);
   unsigned num_patches = ac_compute_num_tess_patches(info, num_tcs_input_cp, tcs_vertices_out,
                                                      io_info->highest_remapped_vram_output,
                                                      MAX2(io_info->highest_remapped_vram_patch_output,
                                                           num_remapped_tess_level_outputs),
                                                      lds_per_patch, wave_size, tess_uses_primid);
   *lds_size = lds_per_patch * num_patches + AC_TESS_LEVEL_VOTE_LDS_BYTES;

   /* SPI_SHADER_PGM_RSRC2_HS.LDS_SIZE specifies the allocation size only for LDS. The HS offchip
    * ring buffer always uses a fixed allocation size per workgroup determined by
    * info->hs_offchip_workgroup_dw_size.
    *
    * LDS is only used for TCS inputs (with cross-invocation or indirect access only or if TCS in/out
    * vertex counts are different) and for TCS outputs that are read (including tess level outputs
    * if they need to be re-read in invocation 0), while the HS ring buffer is only used for TCS
    * outputs consumed by TES.
    */
   assert(*lds_size <= (info->gfx_level >= GFX9 ? 65536 : 32768));

   *num_patches_per_wg = num_patches;
}
