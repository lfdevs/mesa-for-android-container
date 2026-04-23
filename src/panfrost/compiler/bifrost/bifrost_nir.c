/*
 * Copyright (C) 2020,2026 Collabora Ltd.
 * Copyright (C) 2022 Alyssa Rosenzweig
 * Copyright (C) 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/glsl_types.h"
#include "compiler/nir/nir_builder.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/compiler/pan_nir.h"
#include "util/perf/cpu_trace.h"

#include "panfrost/model/pan_model.h"
#include "valhall/valhall.h"
#include "bi_debug.h"
#include "bifrost_compile.h"
#include "bifrost_nir.h"
#include "compiler.h"

/*
 * Some operations are only available as 32-bit instructions. 64-bit floats are
 * unsupported and ints are lowered with nir_lower_int64.  Certain 8-bit and
 * 16-bit instructions, however, are lowered here.
 */
static unsigned
bi_lower_bit_size(const nir_instr *instr, void *data)
{
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      uint64_t gpu_id = *((uint64_t *)data);

      switch (alu->op) {
      case nir_op_fexp2:
      case nir_op_flog2:
      case nir_op_fpow:
      case nir_op_fsin:
      case nir_op_fcos:
      case nir_op_bit_count:
      case nir_op_bitfield_reverse:
         return (nir_src_bit_size(alu->src[0].src) == 32) ? 0 : 32;
      case nir_op_fround_even:
      case nir_op_fceil:
      case nir_op_ffloor:
      case nir_op_ffract:
      case nir_op_ftrunc:
      case nir_op_frexp_sig:
      case nir_op_frexp_exp:
         /* On v11+, FROUND.v2s16 is gone */
         if (pan_arch(gpu_id) < 11)
            return 0;
         return (nir_src_bit_size(alu->src[0].src) == 32) ? 0 : 32;
      case nir_op_iadd:
      case nir_op_isub:
      case nir_op_iadd_sat:
      case nir_op_uadd_sat:
      case nir_op_isub_sat:
      case nir_op_usub_sat:
      case nir_op_ineg:
      case nir_op_iabs:
         /* On v11+, IABS.v4s8, IADD.v4s8 and ISUB.v4s8 are gone */
         if (pan_arch(gpu_id) < 11)
            return 0;

         return (nir_src_bit_size(alu->src[0].src) == 8) ? 16 : 0;
      default:
         return 0;
      }
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

      /* We only support ballot on 32-bit types. */
      switch (intr->intrinsic) {
      case nir_intrinsic_ballot:
      case nir_intrinsic_ballot_relaxed:
         return (nir_src_bit_size(intr->src[0]) == 32) ? 0 : 32;
      default:
         return 0;
      }
   }

   default:
      return 0;
   }
}

/* Although Bifrost generally supports packed 16-bit vec2 and 8-bit vec4,
 * transcendentals are an exception. Also shifts because of lane size mismatch
 * (8-bit in Bifrost, 32-bit in NIR TODO - workaround!). Some conversions need
 * to be scalarized due to type size. */

static uint8_t
bi_vectorize_filter(const nir_instr *instr, const void *data)
{
   uint64_t gpu_id = *((uint64_t *)data);

   if (instr->type == nir_instr_type_phi) {
      unsigned bit_size = nir_instr_as_phi(instr)->def.bit_size;
      if (bit_size == 8)
         return 4;
      if (bit_size == 16)
         return 2;
      return 1;
   }

   /* Do not vectorize all non-ALU instruction */
   if (instr->type != nir_instr_type_alu)
      return 0;

   const nir_alu_instr *alu = nir_instr_as_alu(instr);

   switch (alu->op) {
   case nir_op_ball_fequal2:
   case nir_op_ball_fequal3:
   case nir_op_ball_fequal4:
   case nir_op_bany_fnequal2:
   case nir_op_bany_fnequal3:
   case nir_op_bany_fnequal4:
   case nir_op_ball_iequal2:
   case nir_op_ball_iequal3:
   case nir_op_ball_iequal4:
   case nir_op_bany_inequal2:
   case nir_op_bany_inequal3:
   case nir_op_bany_inequal4: return 1;
   case nir_op_pack_uvec2_to_uint:
   case nir_op_pack_uvec4_to_uint:
      return 0;
   case nir_op_frcp:
   case nir_op_frsq:
   case nir_op_ishl:
   case nir_op_ishr:
   case nir_op_ushr:
   case nir_op_extract_u16:
   case nir_op_extract_i16:
   case nir_op_insert_u16:
      return 1;
   /* On v11+, we lost all packed F16 conversions */
   case nir_op_f2f16:
   case nir_op_f2f16_rtz:
   case nir_op_f2f16_rtne:
   case nir_op_u2f16:
   case nir_op_i2f16:
   case nir_op_frexp_sig:
   case nir_op_frexp_exp:
      if (pan_arch(gpu_id) >= 11)
         return 1;

      break;
   default:
      break;
   }

   const uint8_t bit_size =
      MAX2(alu->def.bit_size, nir_src_bit_size(alu->src[0].src));

   if (bit_size == 1)
      return 0;
   else
      return MAX2(1, 32 / bit_size);
}

static bool
mem_vectorize_cb(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                 unsigned num_components, int64_t hole_size,
                 nir_intrinsic_instr *low, nir_intrinsic_instr *high,
                 void *data)
{
   if (hole_size > 0)
      return false;

   /* We have a hard limit of at most 4 components */
   if (num_components > 4)
      return false;

   const unsigned bytes = num_components * (bit_size / 8);
   const unsigned max_bytes = 128u / 8u; /* LOAD.i128 */

   const unsigned combined_align = nir_combined_align(align_mul, align_offset);
   return bytes <= combined_align && bytes <= max_bytes;
}

static void
bi_optimize_loop_nir(nir_shader *nir, uint64_t gpu_id, bool allow_copies)
{
   bool progress;

   do {
      progress = false;

      NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_opt_deref);

      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_lower_wrmasks);

      if (allow_copies) {
         /* Only run this pass in the first call to bi_optimize_nir. Later
          * calls assume that we've lowered away any copy_deref instructions
          * and we don't want to introduce any more.
          */
         NIR_PASS(progress, nir, nir_opt_find_array_copies);
      }

      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);
      NIR_PASS(progress, nir, nir_opt_combine_stores, nir_var_all);

      NIR_PASS(progress, nir, nir_lower_alu_width, bi_vectorize_filter, &gpu_id);
      NIR_PASS(progress, nir, nir_opt_vectorize, bi_vectorize_filter, &gpu_id);
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_cse);

      nir_opt_peephole_select_options peephole_select_options = {
         .limit = 64,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select,
               &peephole_select_options);
      NIR_PASS(progress, nir, nir_opt_idiv_const, 8);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_dead_cf);

      bool loop_progress = false;
      NIR_PASS(loop_progress, nir, nir_opt_loop);
      progress |= loop_progress;

      if (loop_progress) {
         /* If nir_opt_loop makes progress, then we need to clean things up
          * if we want any hope of nir_opt_if or nir_opt_loop_unroll to make
          * progress.
          */
         NIR_PASS(progress, nir, nir_opt_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
      }

      /* XXX: On Bifrost (G52), this cause a failure on
       * "dEQP-VK.graphicsfuzz.spv-composite-phi" and is related to an unknown
       * scheduling issue */
      if (pan_arch(gpu_id) >= 9)
         NIR_PASS(
            progress, nir, nir_opt_if,
            nir_opt_if_optimize_phi_true_false | nir_opt_if_avoid_64bit_phis);

      NIR_PASS(progress, nir, nir_opt_phi_to_bool);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_undef);
   } while (progress);

   NIR_PASS(_, nir, nir_lower_undef_to_zero);

   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
}

void
bifrost_optimize_nir(nir_shader *nir, uint64_t gpu_id)
{
   bi_optimize_loop_nir(nir, gpu_id, true);
}

static void
bi_optimize_nir(nir_shader *nir, uint64_t gpu_id,
                nir_variable_mode robust_modes)
{
   NIR_PASS(_, nir, nir_opt_shrink_stores, true);
   bi_optimize_loop_nir(nir, gpu_id, false);

   NIR_PASS(_, nir, nir_opt_shrink_vectors, false);

   nir_load_store_vectorize_options vectorize_opts = {
      .modes = nir_var_mem_global |
               nir_var_mem_shared |
               nir_var_mem_ubo |
               nir_var_shader_temp,
      .callback = mem_vectorize_cb,
      .robust_modes = robust_modes,
   };

   /* Only allow vectorization of SSBOs when no robustness2 is configured */
   if (!(robust_modes & nir_var_mem_ssbo))
      vectorize_opts.modes |= nir_var_mem_ssbo;

   NIR_PASS(_, nir, nir_opt_load_store_vectorize, &vectorize_opts);

   /* nir_lower_pack can generate split operations, execute algebraic again to
    * handle them */
   NIR_PASS(_, nir, nir_opt_algebraic);

   /* TODO: Why is 64-bit getting rematerialized?
    * KHR-GLES31.core.shader_image_load_store.basic-allTargets-atomicFS */
   NIR_PASS(_, nir, nir_lower_int64);

   /* Algebraic can materialize instructions with a bit_size that we need to lower */
   NIR_PASS(_, nir, nir_lower_bit_size, bi_lower_bit_size, &gpu_id);

   /* We need to cleanup after each iteration of late algebraic
    * optimizations, since otherwise NIR can produce weird edge cases
    * (like fneg of a constant) which we don't handle */
   bool late_algebraic = true;
   while (late_algebraic) {
      late_algebraic = false;
      NIR_PASS(late_algebraic, nir, nir_opt_algebraic_late);
      NIR_PASS(_, nir, nir_lower_alu_width, bi_vectorize_filter, &gpu_id);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_opt_copy_prop);
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_opt_cse);
   }

   /* This opt currently helps on Bifrost but not Valhall */
   if (pan_arch(gpu_id) < 9)
      NIR_PASS(_, nir, bifrost_nir_opt_boolean_bitwise);

   NIR_PASS(_, nir, pan_nir_lower_bool_to_bitsize);
   NIR_PASS(_, nir, nir_lower_alu_width, bi_vectorize_filter, &gpu_id);
   NIR_PASS(_, nir, nir_opt_vectorize, bi_vectorize_filter, &gpu_id);

   /* Prepass to simplify instruction selection */
   bool late_algebraic_progress = true;
   while (late_algebraic_progress) {
      late_algebraic_progress = false;
      NIR_PASS(late_algebraic_progress, nir, bifrost_nir_lower_algebraic_late,
               pan_arch(gpu_id));
      late_algebraic |= late_algebraic_progress;
   }

   while (late_algebraic) {
      late_algebraic = false;
      NIR_PASS(late_algebraic, nir, nir_opt_algebraic_late);
      NIR_PASS(_, nir, nir_lower_alu_width, bi_vectorize_filter, &gpu_id);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_opt_copy_prop);
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_opt_cse);
   }

   /* Backend scheduler is purely local, so do some global optimizations
    * to reduce register pressure. */
   nir_move_options move_all = nir_move_const_undef | nir_move_load_ubo |
                               nir_move_load_input | nir_move_load_frag_coord |
                               nir_move_comparisons | nir_move_copies |
                               nir_move_load_ssbo;

   NIR_PASS(_, nir, nir_opt_sink, move_all);
   NIR_PASS(_, nir, nir_opt_move, move_all);

   /* We might lower attribute, varying, and image indirects. Use the
    * gathered info to skip the extra analysis in the happy path. */
   bool any_indirects =
      nir->info.inputs_read_indirectly || nir->info.outputs_read_indirectly ||
      nir->info.outputs_written_indirectly ||
      nir->info.patch_inputs_read_indirectly ||
      nir->info.patch_outputs_read_indirectly ||
      nir->info.patch_outputs_written_indirectly || nir->info.images_used[0];

   if (any_indirects) {
      nir_divergence_analysis(nir);
      NIR_PASS(_, nir, bi_lower_divergent_indirects,
               pan_subgroup_size(pan_arch(gpu_id)));
   }
}

void
bifrost_preprocess_nir(nir_shader *nir, uint64_t gpu_id)
{
   MESA_TRACE_FUNC();

   /* The DISCARD instruction just flags the thread as discarded, but the
    * actual termination only happens when all threads in the quad are
    * discarded, or when an instruction with a .discard flow is
    * encountered (Valhall) or when a clause with a .terminate_discarded_thread
    * is reached (Bifrost).
    * We could do without nir_lower_terminate_to_demote(), but this allows
    * for extra dead-code elimination when code sections are detected as
    * being unused after a termination is crossed.
    */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_lower_terminate_to_demote);

   /* Ensure that halt are translated to returns and get ride of them */
   NIR_PASS(_, nir, nir_lower_halt_to_return);
   NIR_PASS(_, nir, nir_lower_returns);

   /* Lower gl_Position pre-optimisation, but after lowering vars to ssa
    * (so we don't accidentally duplicate the epilogue since mesa/st has
    * messed with our I/O quite a bit already) */

   NIR_PASS(_, nir, nir_lower_vars_to_ssa);

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      if (pan_arch(gpu_id) <= 7)
         NIR_PASS(_, nir, pan_nir_lower_vertex_id);
   }

   /* Get rid of any global vars before we lower to scratch. */
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);

   /* Valhall introduces packed thread local storage, which improves cache
    * locality of TLS access. However, access to packed TLS cannot
    * straddle 16-byte boundaries. As such, when packed TLS is in use
    * (currently unconditional for Valhall), we force vec4 alignment for
    * scratch access.
    */
   glsl_type_size_align_func vars_to_scratch_size_align_func =
      (pan_arch(gpu_id) >= 9) ? glsl_get_vec4_size_align_bytes
                              : glsl_get_natural_size_align_bytes;
   /* Lower large arrays to scratch and small arrays to bcsel */
   NIR_PASS(_, nir, nir_lower_scratch_to_var);
   NIR_PASS(_, nir, nir_lower_vars_to_scratch, 256,
            vars_to_scratch_size_align_func, vars_to_scratch_size_align_func);
   NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_function_temp, ~0);

   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);

   bi_optimize_loop_nir(nir, gpu_id, true);

   /* Lower away all variables for smaller shaders */
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
}

/*
 * Build a bit mask of varyings (by location) that are flatshaded. This
 * information is needed by lower_mediump_io, as we don't yet support 16-bit
 * flat varyings.
 *
 * Also varyings that are used as texture coordinates should be kept at fp32 so
 * the texture instruction may be promoted to VAR_TEX. In general this is a good
 * idea, as fp16 texture coordinates are not supported by the hardware and are
 * usually inappropriate. (There are both relevant CTS bugs here, even.)
 *
 * TODO: If we compacted the varyings with some fixup code in the vertex shader,
 * we could implement 16-bit flat varyings. Consider if this case matters.
 *
 * TODO: The texture coordinate handling could be less heavyhanded.
 */
static bool
bi_gather_texcoords(nir_builder *b, nir_instr *instr, void *data)
{
   uint64_t *mask = data;

   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);

   int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord_idx < 0)
      return false;

   nir_src src = tex->src[coord_idx].src;
   nir_scalar x = nir_scalar_resolved(src.ssa, 0);
   nir_scalar y = nir_scalar_resolved(src.ssa, 1);

   if (x.def != y.def)
      return false;

   nir_instr *parent = nir_def_instr(x.def);

   if (parent->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(parent);

   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   *mask |= BITFIELD64_BIT(sem.location);
   return false;
}

static uint64_t
bi_fp32_varying_mask(nir_shader *nir)
{
   uint64_t mask = 0;

   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   nir_foreach_shader_in_variable(var, nir) {
      if (var->data.interpolation == INTERP_MODE_FLAT) {
         unsigned slots = glsl_count_attribute_slots(var->type, false);
         mask |= BITFIELD64_RANGE(var->data.location, slots);
      }
   }

   nir_shader_instructions_pass(nir, bi_gather_texcoords, nir_metadata_all,
                                &mask);

   return mask;
}

static bool
bi_lower_subgroups(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   uint64_t gpu_id = *(uint64_t *)data;
   unsigned int arch = pan_arch(gpu_id);

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *val = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_vote_any:
      val = nir_ine_imm(b, nir_ballot(b, 1, 32, intr->src[0].ssa), 0);
      break;

   case nir_intrinsic_vote_all:
      val = nir_ieq_imm(b, nir_ballot(b, 1, 32, nir_inot(b, intr->src[0].ssa)), 0);
      break;

   case nir_intrinsic_load_subgroup_id: {
      nir_def *local_id = nir_load_local_invocation_id(b);
      nir_def *local_size = nir_load_workgroup_size(b);
      /* local_id.x + local_size.x * (local_id.y + local_size.y * local_id.z) */
      nir_def *flat_local_id =
         nir_iadd(b,
            nir_channel(b, local_id, 0),
            nir_imul(b,
               nir_channel(b, local_size, 0),
               nir_iadd(b,
                  nir_channel(b, local_id, 1),
                  nir_imul(b,
                     nir_channel(b, local_size, 1),
                     nir_channel(b, local_id, 2)))));
      /*
       * nir_udiv_imm with a power of two divisor, which pan_subgroup_size is,
       * will construct a right shift instead of an udiv.
       */
      val = nir_udiv_imm(b, flat_local_id, pan_subgroup_size(arch));
      break;
   }

   case nir_intrinsic_load_subgroup_size:
      val = nir_imm_int(b, pan_subgroup_size(arch));
      break;

   case nir_intrinsic_load_num_subgroups: {
      uint32_t subgroup_size = pan_subgroup_size(arch);
      assert(!b->shader->info.workgroup_size_variable);
      uint32_t workgroup_size =
         b->shader->info.workgroup_size[0] *
         b->shader->info.workgroup_size[1] *
         b->shader->info.workgroup_size[2];
      uint32_t num_subgroups = DIV_ROUND_UP(workgroup_size, subgroup_size);
      val = nir_imm_int(b, num_subgroups);
      break;
   }

   default:
      return false;
   }

   nir_def_rewrite_uses(&intr->def, val);
   return true;
}

/* Workgroups may be merged if the structure of the workgroup is not software
 * visible. This is true if neither shared memory nor BARRIER instructions nor
 * subgroups are used. The hardware may be able to optimize compute shaders
 * that set this flag.
 *
 * From the vulkan spec version 1.4.317 section 9.25.8:
 *
 *    "For shaders that have defined workgroups, each invocation in a subgroup
 *     must be in the same local workgroup."
 */
bool
valhall_can_merge_workgroups(nir_shader *nir)
{
   if (nir->info.shared_size != 0)
      return false;

   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            /* We only emit BARRIER instructions for workgroup execution
             * barriers. For subgroup execution barriers, the only consequence
             * of merging workgroups is that the scope may be larger, which is
             * allowed. */
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic == nir_intrinsic_barrier &&
                nir_intrinsic_execution_scope(intrin) == SCOPE_WORKGROUP)
               return false;

            /* This is in nir->info.uses_wide_subgroups, but we don't want to
             * force an extra nir_gather_shader_info call. */
            if (nir_intrinsic_has_semantic(intrin, NIR_INTRINSIC_SUBGROUP))
               return false;

            /* load_subgroup_invocation allows observing merged workgroups
             * because the first thread in the workgroup may have a nonzero
             * subgroup invocation and so on. We don't have to care about
             * load_subgroup_id, because we implement it by dividing the local
             * invocation id, so it doesn't care what the actual subgroup
             * layout is in hw.
             *
             * Note that these intrinsics do not have NIR_INTRINSIC_SUBGROUP
             * because they do not perform any communication with other
             * subgroup threads. */
            if (intrin->intrinsic == nir_intrinsic_load_subgroup_invocation)
               return false;
         }
      }
   }

   return true;
}

static bool
bi_lower_load_output(nir_builder *b, nir_intrinsic_instr *intr,
                     UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_output)
      return false;

   unsigned loc = nir_intrinsic_io_semantics(intr).location;
   assert(loc >= FRAG_RESULT_DATA0);
   unsigned rt = loc - FRAG_RESULT_DATA0;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *conversion = nir_load_rt_conversion_pan(
      b, .base = rt, .src_type = nir_intrinsic_dest_type(intr));

   nir_def *lowered = nir_load_tile_pan(
      b, intr->def.num_components, intr->def.bit_size,
      pan_nir_tile_location_sample(b, loc, nir_imm_int(b, 0)),
      pan_nir_tile_default_coverage(b),
      conversion, .dest_type = nir_intrinsic_dest_type(intr),
      .io_semantics = nir_intrinsic_io_semantics(intr));

   nir_def_rewrite_uses(&intr->def, lowered);
   return true;
}

static bool
bifrost_nir_lower_load_output(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   return nir_shader_intrinsics_pass(
      nir, bi_lower_load_output,
      nir_metadata_control_flow, NULL);
}

static nir_mem_access_size_align
mem_access_size_align_cb(nir_intrinsic_op intrin, uint8_t bytes,
                         uint8_t bit_size, uint32_t align_mul,
                         uint32_t align_offset, bool offset_is_const,
                         enum gl_access_qualifier access, const void *cb_data)
{
   uint32_t align = nir_combined_align(align_mul, align_offset);
   assert(util_is_power_of_two_nonzero(align));

   /* No more than 16 bytes at a time. */
   bytes = MIN2(bytes, 16);

   /* If the number of bytes is a multiple of 4, use 32-bit loads. Else if it's
    * a multiple of 2, use 16-bit loads. Else use 8-bit loads.
    *
    * But if we're only aligned to 1 byte, use 8-bit loads. If we're only
    * aligned to 2 bytes, use 16-bit loads, unless we needed 8-bit loads due to
    * the size.
    */
   if ((bytes & 1) || (align == 1))
      bit_size = 8;
   else if ((bytes & 2) || (align == 2))
      bit_size = 16;
   else if (bit_size >= 32)
      bit_size = 32;

   unsigned num_comps = MIN2(bytes / (bit_size / 8), 4);

   /* Push constants require 32-bit loads. */
   if (intrin == nir_intrinsic_load_push_constant) {
      if (align_mul >= 4) {
         /* If align_mul is bigger than 4 we can use align_offset to find
          * the exact number of words we need to read.
          */
         num_comps = DIV_ROUND_UP((align_offset % 4) + bytes, 4);
      } else {
         /* If bytes is aligned on 32-bit, the access might still cross one
          * word at the beginning, and one word at the end. If bytes is not
          * aligned on 32-bit, the extra two words should cover for both the
          * size and offset mis-alignment.
          */
         num_comps = (bytes / 4) + 2;
      }

      bit_size = MAX2(bit_size, 32);
      align = 4;
   } else {
      align = bit_size / 8;
   }

   return (nir_mem_access_size_align){
      .num_components = num_comps,
      .bit_size = bit_size,
      .align = align,
      .shift = nir_mem_access_shift_method_scalar,
   };
}

static void bi_lower_texture_nir(nir_shader *nir, uint64_t gpu_id);
static void bi_lower_texture_late_nir(nir_shader *nir, uint64_t gpu_id);

void
bifrost_postprocess_nir(nir_shader *nir, uint64_t gpu_id)
{
   MESA_TRACE_FUNC();

   /* We assume that UBO and SSBO were lowered, let's move things around. */
   nir_move_options move_all = nir_move_const_undef | nir_move_load_ubo |
                               nir_move_comparisons | nir_move_copies |
                               nir_move_load_ssbo;

   NIR_PASS(_, nir, nir_opt_sink, move_all);
   NIR_PASS(_, nir, nir_opt_move, move_all);

   bi_lower_texture_nir(nir, gpu_id);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, pan_nir_lower_noperspective_fs);

      NIR_PASS(_, nir, nir_lower_mediump_io,
               nir_var_shader_in | nir_var_shader_out,
               ~bi_fp32_varying_mask(nir), false);

      NIR_PASS(_, nir, bifrost_nir_lower_load_output);
   } else if (nir->info.stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_, nir, nir_lower_viewport_transform);
      NIR_PASS(_, nir, nir_lower_point_size, 1.0, 0.0);
      NIR_PASS(_, nir, pan_nir_lower_noperspective_vs);
   }

   nir_lower_mem_access_bit_sizes_options mem_size_options = {
      .modes = nir_var_mem_ubo | nir_var_mem_push_const | nir_var_mem_ssbo |
               nir_var_mem_constant | nir_var_mem_task_payload |
               nir_var_shader_temp | nir_var_function_temp |
               nir_var_mem_global | nir_var_mem_shared,
      .callback = mem_access_size_align_cb,
   };
   NIR_PASS(_, nir, nir_lower_mem_access_bit_sizes, &mem_size_options);

   nir_lower_ssbo_options ssbo_opts = {
      .native_loads = pan_arch(gpu_id) >= 9,
      .native_offset = pan_arch(gpu_id) >= 9,
   };
   NIR_PASS(_, nir, nir_lower_ssbo, &ssbo_opts);

   /*
    * Lower subgroups ops before lowering int64: nir_lower_int64 doesn't know
    * how to lower imul reductions and scans.
    *
    * TODO: we can implement certain operations (notably reductions, scans,
    * certain shuffles, etc) more efficiently than nir_lower_subgroups. Moreover
    * we can implement reductions and scans on f16vec2 values without splitting
    * to scalar first.
    */
   bool lower_subgroups_progress = false;
   NIR_PASS(lower_subgroups_progress, nir, nir_lower_subgroups,
      &(nir_lower_subgroups_options) {
         .subgroup_size = pan_subgroup_size(pan_arch(gpu_id)),
         .ballot_bit_size = 32,
         .ballot_components = 1,
         .lower_to_scalar = true,
         .lower_vote_feq = true,
         .lower_vote_ieq = true,
         .lower_vote_bool_eq = true,
         .lower_first_invocation_to_ballot = true,
         .lower_read_first_invocation = true,
         .lower_subgroup_masks = true,
         .lower_relative_shuffle = true,
         .lower_shuffle = true,
         .lower_quad = true,
         .lower_quad_broadcast_dynamic = true,
         .lower_quad_vote = true,
         .lower_elect = true,
         .lower_rotate_to_shuffle = true,
         .lower_rotate_clustered_to_shuffle = true,
         .lower_inverse_ballot = true,
         .lower_reduce = true,
         .lower_boolean_reduce = true,
         .lower_boolean_shuffle = true,
      });
   /* nir_lower_subgroups creates new vars, clean them up. */
   if (lower_subgroups_progress)
      NIR_PASS(_, nir, nir_lower_vars_to_ssa);

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, bi_lower_subgroups,
      nir_metadata_control_flow, &gpu_id);

   NIR_PASS(_, nir, nir_lower_64bit_phis);
   NIR_PASS(_, nir, nir_lower_int64);
   NIR_PASS(_, nir, nir_lower_bit_size, bi_lower_bit_size, &gpu_id);

   NIR_PASS(_, nir, nir_opt_idiv_const, 8);
   NIR_PASS(_, nir, nir_lower_idiv,
            &(nir_lower_idiv_options){.allow_fp16 = true});

   NIR_PASS(_, nir, nir_lower_alu_width, bi_vectorize_filter, &gpu_id);
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, nir, nir_lower_phis_to_scalar, bi_vectorize_filter, &gpu_id);
   NIR_PASS(_, nir, nir_lower_flrp, 16 | 32 | 64, false /* always_precise */);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_alu);
   NIR_PASS(_, nir, nir_lower_frag_coord_to_pixel_coord);
   NIR_PASS(_, nir, pan_nir_lower_var_special_pan);

   bi_lower_texture_late_nir(nir, gpu_id);
}

static void
bi_lower_texture_nir(nir_shader *nir, uint64_t gpu_id)
{
   NIR_PASS(_, nir, nir_lower_image_atomics_to_global, NULL, NULL);

   /* on Bifrost, lower MSAA load/stores to 3D load/stores */
   if (pan_arch(gpu_id) < 9)
      NIR_PASS(_, nir, pan_nir_lower_image_ms);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, nir_lower_is_helper_invocation);
      NIR_PASS(_, nir, pan_nir_lower_helper_invocation);
      NIR_PASS(_, nir, pan_nir_lower_sample_pos);
   }
}

static bool
lower_texel_buffer_fetch(nir_builder *b, nir_tex_instr *tex, void *data)
{
   if (tex->op != nir_texop_txf || tex->sampler_dim != GLSL_SAMPLER_DIM_BUF)
      return false;

   unsigned *arch = data;
   b->cursor = nir_before_instr(&tex->instr);

   nir_def *res_handle = nir_imm_int(b, tex->texture_index);
   nir_def *buf_index = NULL;
   for (unsigned i = 0; i < tex->num_srcs; ++i) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_coord:
         buf_index = tex->src[i].src.ssa;
         break;
      case nir_tex_src_texture_offset:
         /* This should always be 0 as lower_index_to_offset is expected to be
          * set */
         assert(tex->texture_index == 0);
         res_handle = tex->src[i].src.ssa;
         break;
      default:
         continue;
      }
   }

   nir_def *loaded_texel_addr =
      nir_load_texel_buf_index_address_pan(b, res_handle, buf_index);
   nir_def *texel_addr =
      nir_pack_64_2x32(b, nir_channels(b, loaded_texel_addr, BITFIELD_MASK(2)));

   nir_def *loaded_mem;
   if (*arch >= 9) {
      nir_def *icd = nir_load_texel_buf_conv_pan(b, res_handle);
      loaded_mem = nir_load_global_cvt_pan(b, tex->def.num_components,
                                              tex->def.bit_size, texel_addr,
                                              icd, tex->dest_type);
   } else {
      nir_def *icd = nir_channel(b, loaded_texel_addr, 2);
      loaded_mem = nir_load_global_cvt_pan(b, tex->def.num_components,
                                              tex->def.bit_size, texel_addr,
                                              icd, tex->dest_type);
   }
   nir_def_replace(&tex->def, loaded_mem);
   return true;
}

static bool
pan_nir_lower_texel_buffer_fetch(nir_shader *shader, unsigned arch)
{
   return nir_shader_tex_pass(shader, lower_texel_buffer_fetch,
                              nir_metadata_control_flow, &arch);
}

static bool
lower_buf_image_access(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_image_texel_address:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
      break;
   default:
      return false;
   }
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(intr);
   if (dim != GLSL_SAMPLER_DIM_BUF)
      return false;

   unsigned *arch = data;
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *res_handle = intr->src[0].ssa;
   nir_def *buf_index = nir_channel(b, intr->src[1].ssa, 0);
   nir_def *loaded_texel_addr =
      nir_load_texel_buf_index_address_pan(b, res_handle, buf_index);
   nir_def *texel_addr =
      nir_pack_64_2x32(b, nir_channels(b, loaded_texel_addr, BITFIELD_MASK(2)));

   switch (intr->intrinsic) {
   case nir_intrinsic_image_texel_address:
      nir_def_replace(&intr->def, texel_addr);
      break;
   case nir_intrinsic_image_load: {
      nir_def *icd;
      if (*arch >= 9)
         icd = nir_load_texel_buf_conv_pan(b, res_handle);
      else
         icd = nir_channel(b, loaded_texel_addr, 2);
      nir_def *loaded_mem = nir_load_global_cvt_pan(
         b, intr->def.num_components, intr->def.bit_size, texel_addr, icd,
         .dest_type = nir_intrinsic_dest_type(intr));
      nir_def_replace(&intr->def, loaded_mem);
      break;
   }
   case nir_intrinsic_image_store: {
      /* Due to SPIR-V limitations, the source type is not fully reliable: it
       * reports uint32 even for write_imagei. This causes an incorrect
       * u32->s32->u32 roundtrip which incurs an unwanted clamping. Use auto32
       * instead, which will match per the OpenCL spec. Of course this does
       * not work for 16-bit stores, but those are not available in OpenCL.
       */
      ASSERTED nir_alu_type T = nir_intrinsic_src_type(intr);
      assert(nir_alu_type_get_type_size(T) == 32);

      nir_def *value = intr->src[3].ssa;
      nir_def *icd;
      if (*arch >= 9)
         icd = nir_load_texel_buf_conv_pan(b, res_handle);
      else
         icd = nir_channel(b, loaded_texel_addr, 2);
      nir_store_global_cvt_pan(b, value, texel_addr, icd, .src_type = 32);
      nir_instr_remove(&intr->instr);
      break;
   }
   default:
      UNREACHABLE("Unexpected intrinsic");
   }

   return true;
}

static bool
pan_nir_lower_buf_image_access(nir_shader *shader, unsigned arch)
{
   return nir_shader_intrinsics_pass(shader, lower_buf_image_access,
                                     nir_metadata_control_flow, &arch);
}

/* This must be called after any lowering of resource indices
 * (panfrost_nir_lower_res_indices / panvk_per_arch(nir_lower_descriptors))
 * and lowering of attribute indices (pan_nir_lower_image_index /
 * pan_nir_lower_texel_buffer_fetch_index)
 */
static void
bi_lower_texture_late_nir(nir_shader *nir, uint64_t gpu_id)
{
   NIR_PASS(_, nir, pan_nir_lower_texel_buffer_fetch, pan_arch(gpu_id));
   NIR_PASS(_, nir, pan_nir_lower_buf_image_access, pan_arch(gpu_id));
}

/* Decide if Index-Driven Vertex Shading should be used for a given shader */
static bool
bi_should_idvs(nir_shader *nir, const struct pan_compile_inputs *inputs)
{
   /* Opt-out */
   if (inputs->no_idvs || bifrost_debug & BIFROST_DBG_NOIDVS)
      return false;

   /* IDVS splits up vertex shaders, not defined on other shader stages */
   if (nir->info.stage != MESA_SHADER_VERTEX)
      return false;

   /* Bifrost cannot write gl_PointSize during IDVS */
   if ((pan_arch(inputs->gpu_id) < 9) &&
       nir->info.outputs_written & VARYING_BIT_PSIZ)
      return false;

   /* Otherwise, IDVS is usually better */
   return true;
}

/* Atomics and memory write on the vertex stage have implementation-defined
 * behaviors on how many invocations will happen. However for some reasons,
 * atomic counters on GL/GLES specs are quite ambigous here and even have tests
 * counting how many invocations have been made on VS.... This pass detects
 * atomics that result in a direct store output of one specific IDVS stage
 * and ensure it's only executed for said stage.
 *
 * This allows
 * "dEQP-GLES31.functional.shaders.opaque_type_indexing.atomic_counter.*" to
 * pass under ANGLE.
 */

static bool
bifrost_nir_lower_vs_atomics_impl(nir_builder *b, nir_intrinsic_instr *intr,
                                  UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_global_atomic)
      return false;

   unsigned output_mask = 0;
   nir_foreach_use(use, &intr->def) {
      nir_instr *parent = nir_src_parent_instr(use);
      if (parent->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *parent_intr = nir_instr_as_intrinsic(parent);
      if (parent_intr->intrinsic != nir_intrinsic_store_output &&
          parent_intr->intrinsic != nir_intrinsic_store_per_view_output)
         continue;

      nir_io_semantics sem = nir_intrinsic_io_semantics(parent_intr);
      output_mask |= BITFIELD_BIT(va_shader_output_from_loc(sem.location));
   }

   /* In case they are not written to any outputs, we default to only output in
    * the position stage */
   if (output_mask == 0)
      output_mask |= VA_SHADER_OUTPUT_POSITION_BIT;

   /* In case they are not written to both IDVS stages, we just do not try
    * lowering it */
   if (((output_mask & VA_SHADER_OUTPUT_VARY_BIT) &&
        (output_mask & (VA_SHADER_OUTPUT_POSITION_BIT |
                        VA_SHADER_OUTPUT_ATTRIB_BIT))))
      return false;

   /* In case we know we have only outputs to a certain type, we can make the
    * atomic exclusive to this */
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *res = nir_undef(b, intr->def.num_components, intr->def.bit_size);

   nir_def *shader_output = nir_load_shader_output_pan(b);
   nir_push_if(b, nir_i2b(b, nir_iand_imm(b, shader_output, output_mask)));
   nir_instr *new_instr = nir_instr_clone(b->shader, &intr->instr);
   nir_intrinsic_instr *new_intr = nir_instr_as_intrinsic(new_instr);
   nir_builder_instr_insert(b, new_instr);
   nir_pop_if(b, NULL);

   res = nir_if_phi(b, &new_intr->def, res);
   nir_def_replace(&intr->def, res);

   return true;
}

static bool
bifrost_nir_lower_vs_atomics(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);
   return nir_shader_intrinsics_pass(shader, bifrost_nir_lower_vs_atomics_impl,
                                     nir_metadata_none, NULL);
}

void
bifrost_compile_shader_nir(nir_shader *nir,
                           const struct pan_compile_inputs *inputs,
                           struct util_dynarray *binary,
                           struct pan_shader_info *info)
{
   MESA_TRACE_FUNC();

   bifrost_init_debug_options();

   /* The varying layout (if any) may have different bit sizes for some
    * varyings than we have in the shader.  For descriptors, this isn't a
    * problem as it's handled by the descriptor layout.  However, for direct
    * loads and stores on Valhall+, we need the right bit sizes in the shader.
    * We could do this in the back-end as we emit but it's easier for now to
    * lower in NIR.  This also handles the case where we do a load from the
    * fragment shader of something that isn't written by the vertex shader.
    * In that case, we just return zero.
    */
   if (pan_arch(inputs->gpu_id) >= 9 && inputs->varying_layout)
      NIR_PASS(_, nir, pan_nir_resize_varying_io, inputs->varying_layout);

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      info->vs.idvs = bi_should_idvs(nir, inputs);

      if (info->vs.idvs && nir->info.writes_memory)
         NIR_PASS(_, nir, bifrost_nir_lower_vs_atomics);

      NIR_PASS(_, nir, pan_nir_lower_vs_outputs, inputs->gpu_id,
               inputs->varying_layout, info->vs.idvs,
               &info->vs.needs_extended_fifo);
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* Blit shaders may not need to run ATEST, since ATEST is not needed if
       * early-z is forced, alpha-to-coverage is disabled, and there are no
       * writes to the coverage mask. The latter two are satisfied for all
       * blit shaders, so we just care about early-z, which blit shaders force
       * iff they do not write depth or stencil
       */
      const bool emit_zs =
         nir->info.outputs_written & (BITFIELD_BIT(FRAG_RESULT_DEPTH) |
                                      BITFIELD_BIT(FRAG_RESULT_STENCIL));
      const bool skip_atest = inputs->is_blit && !emit_zs;
      NIR_PASS(_, nir, pan_nir_lower_fs_outputs, skip_atest);
   }

   bi_optimize_nir(nir, inputs->gpu_id, inputs->robust_modes);

   /* Lower constants to scalar but then immediately fold so we get minimum-
    * width vectors instead of scalars
    */
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   uint64_t gpu_id = inputs->gpu_id;
   NIR_PASS(_, nir, nir_lower_phis_to_scalar, bi_vectorize_filter, &gpu_id);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_dce);

   info->tls_size = nir->scratch_size;
   info->stage = nir->info.stage;

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      assert(inputs->varying_layout);
      memcpy(&info->varyings.formats, inputs->varying_layout,
             sizeof(*inputs->varying_layout));
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      pan_varying_collect_formats(&info->varyings.formats,
                                  nir, inputs->gpu_id,
                                  inputs->trust_varying_flat_highp_types, false);
      info->varyings.noperspective =
         pan_nir_collect_noperspective_varyings_fs(nir);

      if (!inputs->is_blend)
         NIR_PASS(_, nir, pan_nir_lower_fs_inputs, inputs->gpu_id,
                  inputs->varying_layout, info);
   }

   if (nir->info.stage == MESA_SHADER_VERTEX && info->vs.idvs) {
      /* On 5th Gen, IDVS is only in one binary */
      if (pan_arch(inputs->gpu_id) >= 12)
         bi_compile_variant(nir, inputs, binary, info, BI_IDVS_ALL);
      else {
         bi_compile_variant(nir, inputs, binary, info, BI_IDVS_POSITION);
         bi_compile_variant(nir, inputs, binary, info, BI_IDVS_VARYING);
      }
   } else {
      bi_compile_variant(nir, inputs, binary, info, BI_IDVS_NONE);
   }

   info->ubo_mask &= (1 << nir->info.num_ubos) - 1;
}
