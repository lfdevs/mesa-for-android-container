/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "compiler/brw/brw_eu.h"
#include "compiler/brw/brw_eu_defines.h"
#include "compiler/brw/brw_nir.h"
#include "compiler/brw/brw_private.h"
#include "compiler/intel_nir.h"
#include "jay_private.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_intrinsics.h"
#include "shader_enums.h"

#define JAY_NIR_SNAPSHOT(name)  BRW_NIR_SNAPSHOT(name)
#define JAY_NIR_PASS(pass, ...) BRW_NIR_PASS(pass, ##__VA_ARGS__)

/*
 * Jay-to-NIR relies on a careful indexing of defs: every 32-bit word has
 * its own index. Vectors/64-bit use contiguous indices. We therefore run a
 * modified version of nir_index_ssa_defs right before translating NIR->Jay.
 */
static bool
index_ssa_def_cb(nir_def *def, void *state)
{
   unsigned *index = (unsigned *) state;
   def->index = *index;
   *index += DIV_ROUND_UP(def->num_components * MAX2(def->bit_size, 32), 32);
   return true;
}

static void
nj_index_ssa_defs(nir_shader *nir)
{
   nir_foreach_function_impl(impl, nir) {
      /* The zero index means null in Jay, so start SSA indices at 1 */
      unsigned index = 1;

      nir_foreach_block_unstructured(block, impl) {
         nir_foreach_instr(instr, block)
            nir_foreach_def(instr, index_ssa_def_cb, &index);
      }

      impl->ssa_alloc = index;
   }
}

static bool
lower_frag_coord(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_load_pixel_coord) {
      nir_def_replace(&intr->def,
                      nir_unpack_32_2x16(b, nir_load_pixel_coord_intel(b)));
      return true;
   } else if (intr->intrinsic == nir_intrinsic_load_frag_coord_w) {
      nir_def_replace(&intr->def, nir_frcp(b, nir_load_frag_coord_w_rcp(b)));
      return true;
   }
   return false;
}

static bool
jay_nir_lower_simd(nir_builder *b, nir_intrinsic_instr *intr, void *simd_)
{
   b->cursor = nir_after_instr(&intr->instr);
   unsigned *simd_width = simd_;

   /* mask & -mask isolates the lowest set bit in the mask. */
   if (intr->intrinsic == nir_intrinsic_elect) {
      nir_def *mask = nir_ballot(b, 1, *simd_width, nir_imm_true(b));
      mask = nir_iand(b, mask, nir_ineg(b, mask));
      nir_def_replace(&intr->def, nir_inverse_ballot(b, mask));
      return true;
   }

   /* Ballots must match the SIMD size */
   if (intr->intrinsic == nir_intrinsic_ballot ||
       intr->intrinsic == nir_intrinsic_ballot_relaxed) {
      unsigned old_bitsize = intr->def.bit_size;
      intr->def.bit_size = *simd_width;
      nir_def *u2uN = nir_u2uN(b, &intr->def, old_bitsize);
      nir_def_rewrite_uses_after(&intr->def, u2uN);
      return true;
   }

   /* Just a constant */
   if (intr->intrinsic == nir_intrinsic_load_simd_width_intel) {
      nir_def_replace(&intr->def, nir_imm_int(b, *simd_width));
      return true;
   }

   /* Note: we don't treat read_invocation specially because there's little
    * benefit but doing so would require expensive uniformizing in some cases.
    */
   if (intr->intrinsic != nir_intrinsic_shuffle &&
       intr->intrinsic != nir_intrinsic_read_invocation)
      return false;

   nir_def *data = intr->src[0].ssa;
   assert(data->num_components == 1 && data->bit_size <= 32 && "scalarized");

   nir_def *offset_B = nir_imul_imm(b, intr->src[1].ssa, 4);
   nir_def_replace(&intr->def, nir_shuffle_intel(b, 1, data, offset_B));
   return true;
}

struct frag_out_ctx {
   nir_scalar colour[FRAG_RESULT_MAX][4];
   nir_def *outputs[FRAG_RESULT_MAX];
   bool dual_blend;
   bool replicate_alpha;
};

static bool
collect_fragment_output(nir_builder *b, nir_intrinsic_instr *intr, void *ctx_)
{
   struct frag_out_ctx *ctx = ctx_;
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   const unsigned wrmask = nir_intrinsic_write_mask(intr);
   const unsigned c = nir_intrinsic_component(intr);

   gl_frag_result loc = nir_intrinsic_io_semantics(intr).location;
   if (loc == FRAG_RESULT_COLOR)
      loc = FRAG_RESULT_DATA0;
   else if (loc == FRAG_RESULT_DUAL_SRC_BLEND)
      ctx->dual_blend = true;

   if (loc < FRAG_RESULT_DATA0) {
      assert(c == 0 && wrmask == 1);
      assert(!ctx->outputs[loc] && "each non-colour output written only once");
      ctx->outputs[loc] = intr->src[0].ssa;

      /* Remove SampleMask writes that don't mask out any samples */
      const unsigned all_samples = BITFIELD_MASK(16);
      if (loc == FRAG_RESULT_SAMPLE_MASK &&
          nir_src_is_const(intr->src[0]) &&
          (nir_src_as_uint(intr->src[0]) & all_samples) == all_samples)
         ctx->outputs[loc] = NULL;
   } else {
      u_foreach_bit(i, wrmask) {
         assert(!ctx->colour[loc][c + i].def &&
                "each colour component written only once");
         ctx->colour[loc][c + i] = nir_get_scalar(intr->src[0].ssa, i);
      }
   }

   nir_instr_remove(&intr->instr);
   return true;
}

/* nir_vec_scalar colour components, filling any unwritten with undef */
static bool
gather_colour_components(nir_builder *b,
                         struct frag_out_ctx *ctx,
                         gl_frag_result loc,
                         nir_def *undef)
{
   bool written = false;

   for (unsigned c = 0; c < 4; c++) {
      if (!ctx->colour[loc][c].def)
         ctx->colour[loc][c] = nir_get_scalar(undef, 0);
      else
         written = true;
   }

   if (written)
      ctx->outputs[loc] = nir_vec_scalars(b, ctx->colour[loc], 4);

   return written;
}

static void
insert_rt_store(nir_builder *b, struct frag_out_ctx *ctx, signed target)
{
   const unsigned src0_alpha_loc =
      FRAG_RESULT_DATA0 + (ctx->replicate_alpha ? 0 : MAX2(target, 0));

   nir_def *colour = ctx->outputs[FRAG_RESULT_DATA0 + MAX2(target, 0)];
   nir_def *dual_colour = ctx->outputs[FRAG_RESULT_DUAL_SRC_BLEND] ?: colour;
   nir_def *src0_alpha = nir_mov_scalar(b, ctx->colour[src0_alpha_loc][3]);

   nir_store_render_target_intel(b, colour, dual_colour, src0_alpha,
                                 ctx->outputs[FRAG_RESULT_SAMPLE_MASK],
                                 ctx->outputs[FRAG_RESULT_DEPTH],
                                 ctx->outputs[FRAG_RESULT_STENCIL],
                                 .target = target);
}

static void
lower_fragment_outputs(nir_function_impl *impl,
                       const struct intel_device_info *devinfo,
                       unsigned nr_colour_regions,
                       bool replicate_alpha)
{
   nir_builder b_ = nir_builder_at(nir_after_impl(impl));
   nir_builder *b = &b_;

   struct frag_out_ctx ctx = { .replicate_alpha = replicate_alpha };
   nir_function_intrinsics_pass(impl, collect_fragment_output,
                                nir_metadata_control_flow, &ctx);

   nir_def *undef = nir_undef(b, 1, 32);
   if (!ctx.outputs[FRAG_RESULT_DEPTH])
      ctx.outputs[FRAG_RESULT_DEPTH] = undef;
   if (!ctx.outputs[FRAG_RESULT_STENCIL])
      ctx.outputs[FRAG_RESULT_STENCIL] = undef;
   if (!ctx.outputs[FRAG_RESULT_SAMPLE_MASK])
      ctx.outputs[FRAG_RESULT_SAMPLE_MASK] = undef;

   if (ctx.dual_blend) {
      gather_colour_components(b, &ctx, FRAG_RESULT_DATA0, undef);
      gather_colour_components(b, &ctx, FRAG_RESULT_DUAL_SRC_BLEND, undef);
      insert_rt_store(b, &ctx, 0);
      return;
   }
   ctx.outputs[FRAG_RESULT_DUAL_SRC_BLEND] = nir_undef(b, 4, 32);

   bool written = false;
   for (unsigned i = 0; i < nr_colour_regions; i++) {
      if (gather_colour_components(b, &ctx, FRAG_RESULT_DATA0 + i, undef)) {
         insert_rt_store(b, &ctx, i);
         written = true;
      }
   }

   if (!written) {
      /* Even if we don't write a RT, we still need to write alpha for
       * alpha-to-coverage and alpha testing. Optimize the other channels out.
       */
      for (unsigned c = 0; c < 3; c++)
         ctx.colour[FRAG_RESULT_DATA0][c] = nir_get_scalar(undef, 0);
      gather_colour_components(b, &ctx, FRAG_RESULT_DATA0, undef);

      insert_rt_store(b, &ctx, -1);
   }
}

unsigned
jay_process_nir(const struct intel_device_info *devinfo,
                nir_shader *nir,
                union brw_any_prog_data *prog_data,
                union brw_any_prog_key *key,
                debug_archiver *archiver,
                bool *track_helpers)
{
   enum mesa_shader_stage stage = nir->info.stage;
   struct brw_compiler compiler = { .devinfo = devinfo };
   unsigned nr_packed_regs = 0;

   prog_data->base.ray_queries = nir->info.ray_queries;
   prog_data->base.stage = stage;
   // TODO: Make the driver do this?
   // prog_data->base.source_hash = params->source_hash;
   prog_data->base.total_shared = nir->info.shared_size;

   /* TODO: Real heuristic */
   bool do_simd32 = INTEL_SIMD(FS, 32);
   do_simd32 &= stage == MESA_SHADER_COMPUTE || stage == MESA_SHADER_FRAGMENT;

   /* The 'Render Target Write message' section of the docs says:
    *
    *    "Output Stencil is not supported with SIMD16 Render Target
    *     Write Messages."
    *
    * Likewise for Xe2 at SIMD32.
    */
   if (stage == MESA_SHADER_FRAGMENT &&
       (nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL)))
      do_simd32 = false;

   if (stage == MESA_SHADER_FRAGMENT && nir->info.fs.color_is_dual_source)
      do_simd32 = false;

   /* SIMD splitting of ray queries is inefficient, avoid it when possible */
   if (prog_data->base.ray_queries && nir->info.min_subgroup_size < 32)
      do_simd32 = false;

   unsigned simd_width = do_simd32 ? (nir->info.api_subgroup_size ?: 32) : 16;

   brw_pass_tracker pt_ = {
      .nir = nir,
      .key = &key->base,
      .dispatch_width = simd_width,
      .compiler = &compiler,
      .archiver = archiver,
   }, *pt = &pt_;

   JAY_NIR_SNAPSHOT("first");

   brw_nir_apply_key(pt, &key->base, simd_width);

   if (stage == MESA_SHADER_VERTEX) {
      /* We only expect slot compaction to be disabled when using device
       * generated commands, to provide an independent 3DSTATE_VERTEX_ELEMENTS
       * programming. This should always be enabled together with VF component
       * packing to minimize the size of the payload.
       */
      assert(!key->vs.no_vf_slot_compaction || key->vs.vf_component_packing);

      /* When using Primitive Replication for multiview, each view gets its own
       * position slot.
       */
      const uint32_t pos_slots =
         (nir->info.per_view_outputs & VARYING_BIT_POS) ?
            MAX2(1, util_bitcount(key->base.view_mask)) :
            1;

      /* Only position is allowed to be per-view */
      assert(!(nir->info.per_view_outputs & ~VARYING_BIT_POS));

      brw_compute_vue_map(devinfo, &prog_data->vue.vue_map,
                          nir->info.outputs_written, key->base.vue_layout,
                          pos_slots);

      prog_data->vs.inputs_read = nir->info.inputs_read;
      prog_data->vs.double_inputs_read = nir->info.vs.double_inputs;
      prog_data->vs.no_vf_slot_compaction = key->vs.no_vf_slot_compaction;

      brw_nir_lower_vs_inputs(nir);
      brw_nir_lower_vue_outputs(nir);
      JAY_NIR_SNAPSHOT("after_lower_io");

      memset(prog_data->vs.vf_component_packing, 0,
             sizeof(prog_data->vs.vf_component_packing));
      if (key->vs.vf_component_packing) {
         nr_packed_regs = brw_nir_pack_vs_input(nir, &prog_data->vs);
      }

      /* Get constant offsets out of the way for proper clip/cull handling */
      JAY_NIR_PASS(nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);
      /* Unroll multiview loops */
      JAY_NIR_PASS(nir_opt_loop_unroll);
      JAY_NIR_PASS(nir_opt_constant_folding);
      JAY_NIR_PASS(intel_nir_lower_shading_rate_output);
      JAY_NIR_PASS(brw_nir_lower_deferred_urb_writes, devinfo,
                   &prog_data->vue.vue_map, 0, 0);
   } else if (stage == MESA_SHADER_TESS_CTRL) {
      nir->info.outputs_written = key->tcs.outputs_written;
      nir->info.patch_outputs_written = key->tcs.patch_outputs_written;

      struct intel_vue_map input_vue_map;
      brw_compute_vue_map(devinfo, &input_vue_map, nir->info.inputs_read,
                          key->base.vue_layout, 1);
      brw_compute_tess_vue_map(&prog_data->vue.vue_map,
                               nir->info.outputs_written,
                               nir->info.patch_outputs_written,
                               key->tcs.separate_tess_vue_layout);

      brw_nir_lower_tcs_inputs(nir, devinfo, &input_vue_map);
      brw_nir_lower_tcs_outputs(nir, devinfo, &prog_data->vue.vue_map,
                                key->tcs._tes_primitive_mode);
      JAY_NIR_SNAPSHOT("after_lower_io");

      brw_nir_opt_vectorize_urb(pt);
      JAY_NIR_PASS(intel_nir_lower_patch_vertices_in, key->tcs.input_vertices);

      /* Compute URB entry size.  The maximum allowed URB entry size is 32k.
       * That divides up as follows:
       *
       *     32 bytes for the patch header (tessellation factors)
       *    480 bytes for per-patch varyings (a varying component is 4 bytes and
       *              gl_MaxTessPatchComponents = 120)
       *  16384 bytes for per-vertex varyings (a varying component is 4 bytes,
       *              gl_MaxPatchVertices = 32 and
       *              gl_MaxTessControlOutputComponents = 128)
       *
       *  15808 bytes left for varying packing overhead
       */
      const int num_per_patch_slots =
         prog_data->vue.vue_map.num_per_patch_slots;
      const int num_per_vertex_slots =
         prog_data->vue.vue_map.num_per_vertex_slots;
      unsigned output_size_bytes = 0;
      /* Note that the patch header is counted in num_per_patch_slots. */
      output_size_bytes += num_per_patch_slots * 16;
      output_size_bytes +=
         nir->info.tess.tcs_vertices_out * num_per_vertex_slots * 16;

      assert(output_size_bytes >= 1);
      assert(output_size_bytes < GFX7_MAX_HS_URB_ENTRY_SIZE_BYTES);

      /* URB entry sizes are stored as a multiple of 64 bytes. */
      prog_data->vue.urb_entry_size = align(output_size_bytes, 64) / 64;
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      const uint32_t pos_slots =
         (nir->info.per_view_outputs & VARYING_BIT_POS) ?
            MAX2(1, util_bitcount(key->base.view_mask)) :
            1;

      brw_compute_vue_map(devinfo, &prog_data->vue.vue_map,
                          nir->info.outputs_written, key->base.vue_layout,
                          pos_slots);

      struct intel_vue_map input_vue_map;

      brw_compute_tess_vue_map(&input_vue_map, nir->info.inputs_read,
                               nir->info.patch_inputs_read,
                               key->tes.separate_tess_vue_layout);

      brw_nir_lower_tes_inputs(nir, devinfo, &input_vue_map,
                               &prog_data->vue.urb_read_length);
      brw_nir_lower_vue_outputs(nir);
      BRW_NIR_SNAPSHOT("after_lower_io");

      brw_nir_opt_vectorize_urb(pt);
      BRW_NIR_PASS(intel_nir_lower_patch_vertices_tes);

      BRW_NIR_PASS(brw_nir_lower_deferred_urb_writes, devinfo,
                   &prog_data->vue.vue_map, 0, 0);

      unsigned output_size_bytes = prog_data->vue.vue_map.num_slots * 4 * 4;

      assert(output_size_bytes >= 1);
      assert(output_size_bytes <= GFX7_MAX_DS_URB_ENTRY_SIZE_BYTES);

      prog_data->tes.include_primitive_id =
         BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

      /* URB entry sizes are stored as a multiple of 64 bytes. */
      prog_data->vue.urb_entry_size = align(output_size_bytes, 64) / 64;

      brw_fill_tess_info_from_shader_info(&prog_data->tes.tess_info,
                                          &nir->info);
   } else if (stage == MESA_SHADER_FRAGMENT) {
      assert(key->fs.mesh_input == INTEL_NEVER && "todo");
      brw_nir_lower_fs_inputs(nir, devinfo, &key->fs);
      brw_nir_lower_fs_outputs(nir);
      JAY_NIR_SNAPSHOT("after_lower_io");
      JAY_NIR_PASS(nir_lower_io_to_scalar, nir_var_shader_in, NULL, NULL);

      if (!brw_can_coherent_fb_fetch(devinfo))
         JAY_NIR_PASS(brw_nir_lower_fs_load_output, &key->fs);

      /* Do this lowering before jay_populate_prog_data(). */
      JAY_NIR_PASS(nir_opt_frag_coord_to_pixel_coord);
      JAY_NIR_PASS(nir_lower_frag_coord_to_pixel_coord);
      JAY_NIR_PASS(nir_opt_barycentric, true);
      JAY_NIR_PASS(nir_opt_constant_folding);

      if (key->fs.alpha_to_coverage != INTEL_NEVER)
         JAY_NIR_PASS(brw_nir_lower_alpha_to_coverage);

      lower_fragment_outputs(nir_shader_get_entrypoint(nir), devinfo,
                             key->fs.nr_color_regions,
                             key->fs.alpha_test_replicate_alpha);

      /* nir_lower_terminate_to_demote will hamper our ability to schedule
       * terminates (since it turns them into real control flow), so run
       * nir_opt_move_discards_to_top first as a prepass. That should help
       * scheduling demotes too (which is more important).
       */
      JAY_NIR_PASS(nir_opt_move_discards_to_top);
      JAY_NIR_PASS(nir_lower_terminate_to_demote);

      brw_nir_cleanup_pre_fs_prog_data(pt);

      // TODO
      // JAY_NIR_PASS(brw_nir_move_interpolation_to_top);

      /* Do this before lower_fs_config_intel so that the pass has the right
       * information.
       */
      jay_populate_prog_data(devinfo, nir, prog_data, key, 0);

      if (prog_data->fs.coarse_pixel_dispatch)
         JAY_NIR_PASS(brw_nir_lower_frag_coord_z, devinfo);

      JAY_NIR_PASS(nir_shader_intrinsics_pass, lower_frag_coord,
                   nir_metadata_control_flow, NULL);

      JAY_NIR_PASS(brw_nir_lower_fs_config_intel, &key->fs, &prog_data->fs);
   }

   brw_postprocess_nir_opts(pt);

   JAY_NIR_PASS(nir_shader_intrinsics_pass, jay_nir_lower_simd,
                nir_metadata_control_flow, &simd_width);
   JAY_NIR_PASS(nir_opt_algebraic_late);
   JAY_NIR_PASS(intel_nir_opt_peephole_imul32x16);

   /* Late postprocess while remaining in SSA */
   /* Run fsign lowering again after the last time brw_nir_optimize is called.
    * As is the case with conversion lowering (below), brw_nir_optimize can
    * create additional fsign instructions.
    */
   JAY_NIR_PASS(jay_nir_lower_bfloat_math);
   JAY_NIR_PASS(jay_nir_lower_fsign);
   JAY_NIR_PASS(jay_nir_lower_bool);
   JAY_NIR_PASS(nir_opt_cse);
   JAY_NIR_PASS(nir_opt_dce);
   JAY_NIR_PASS(jay_nir_opt_sel_zero);

   /* Run nir_split_conversions only after the last tiem
    * brw_nir_optimize is called. Various optimizations invoked there can
    * rematerialize the conversions that the lowering pass eliminates.
    */
   const nir_split_conversions_options split_conv_opts = {
      .callback = intel_nir_split_conversions_cb,
   };
   JAY_NIR_PASS(nir_split_conversions, &split_conv_opts);

   /* Do this only after the last opt_gcm. GCM will undo this lowering. */
   if (stage == MESA_SHADER_FRAGMENT) {
      JAY_NIR_PASS(intel_nir_lower_non_uniform_barycentric_at_sample);
   }

   JAY_NIR_PASS(nir_opt_constant_folding);
   JAY_NIR_PASS(nir_lower_load_const_to_scalar);
   JAY_NIR_PASS(nir_lower_all_phis_to_scalar);
   JAY_NIR_PASS(nir_opt_copy_prop);
   JAY_NIR_PASS(nir_opt_dce);

   /* Jay requires LCSSA for correctness reading convergent loop-dependent
    * values outside of a divergent loop. Converting to LCSSA inserts the
    * required divergent 1-source phi after the loop.
    */
   JAY_NIR_PASS(nir_convert_to_lcssa, true, true);

   /* Run divergence analysis at the end */
   nir_sweep(nir);
   nir_divergence_analysis(nir);

   if (stage == MESA_SHADER_FRAGMENT) {
      /* Certain features require tracking helpers for correctness */
      nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
      *track_helpers |= nir->info.fs.uses_discard || nir->info.writes_memory;
      *track_helpers |= BITSET_TEST(nir->info.system_values_read,
                                    SYSTEM_VALUE_HELPER_INVOCATION);

      /* ...but this is more subtle. nir_opt_load_skip_helpers flags texturing
       * operations that we can skip for bandwidth savings.  We need divergence
       * info for this, so we run late.
       *
       * We may or may not want to force track_helpers on if this makes
       * progress. Possibly driconf'ing on furmark makes sense.
       */
      struct nir_opt_load_skip_helpers_options skip_helpers = {
         .no_add_divergence = true
      };
      JAY_NIR_PASS(nir_opt_load_skip_helpers, &skip_helpers);
   } else {
      jay_populate_prog_data(devinfo, nir, prog_data, key, nr_packed_regs);
   }

   /* This must be the very last pass since nir_print itself will reindex! */
   nj_index_ssa_defs(nir);
   return simd_width;
}
