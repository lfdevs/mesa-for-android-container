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
lower_helper_invocation(nir_builder *b, nir_intrinsic_instr *intr, void *_)
{
   if (intr->intrinsic != nir_intrinsic_load_helper_invocation)
      return false;

   /* TODO: Is this right for multisampling? */
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *active =
      nir_inot(b, nir_inverse_ballot(b, nir_load_sample_mask_in(b)));

   nir_def_replace(&intr->def, active);
   return true;
}

static bool
lower_frag_coord(nir_builder *b, nir_intrinsic_instr *intr, void *simd_)
{
   if (intr->intrinsic != nir_intrinsic_load_frag_coord &&
       intr->intrinsic != nir_intrinsic_load_pixel_coord)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *c = nir_unpack_32_2x16(b, nir_load_pixel_coord_intel(b));

   if (intr->intrinsic == nir_intrinsic_load_frag_coord) {
      c = nir_vec4(b, nir_u2f32(b, nir_channel(b, c, 0)),
                   nir_u2f32(b, nir_channel(b, c, 1)), nir_load_frag_coord_z(b),
                   nir_frcp(b, nir_load_frag_coord_w_rcp(b)));
   }

   nir_def_replace(&intr->def, c);
   return true;
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
   nir_def *colour[8], *depth, *stencil, *sample_mask;
};

static bool
collect_fragment_output(nir_builder *b, nir_intrinsic_instr *intr, void *ctx_)
{
   struct frag_out_ctx *ctx = ctx_;
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   unsigned wrmask = nir_intrinsic_write_mask(intr);
   assert(nir_intrinsic_component(intr) == 0 && "component should be lowered");
   assert(util_is_power_of_two_nonzero(wrmask + 1) &&
          "complex writemasks should be lowered");

   /* TODO: Optimize with write mask? */

   gl_frag_result loc = nir_intrinsic_io_semantics(intr).location;
   assert(!nir_intrinsic_io_semantics(intr).dual_source_blend_index && "todo");
   nir_def **out;
   if (loc == FRAG_RESULT_COLOR) {
      out = &ctx->colour[0];
   } else if (loc >= FRAG_RESULT_DATA0 && loc <= FRAG_RESULT_DATA7) {
      out = &ctx->colour[loc - FRAG_RESULT_DATA0];
   } else if (loc == FRAG_RESULT_DEPTH) {
      out = &ctx->depth;
   } else if (loc == FRAG_RESULT_STENCIL) {
      UNREACHABLE("todo");
      out = &ctx->stencil;
   } else if (loc == FRAG_RESULT_SAMPLE_MASK) {
      UNREACHABLE("todo");
      out = &ctx->sample_mask;
   } else {
      UNREACHABLE("invalid location");
   }

   assert((*out) == NULL && "each location written exactly once");
   *out = intr->src[0].ssa;

   nir_instr_remove(&intr->instr);
   return true;
}

static void
append_payload(nir_builder *b,
               nir_def **payload,
               unsigned *len,
               unsigned max_len,
               nir_def *value)
{
   if (value != NULL) {
      for (unsigned i = 0; i < value->num_components; ++i) {
         payload[*len] = nir_channel(b, value, i);
         (*len)++;
         assert((*len) <= max_len);
      }
   }
}

static void
insert_rt_store(nir_builder *b,
                const struct intel_device_info *devinfo,
                signed target,
                bool last,
                nir_def *colour,
                nir_def *src0_alpha,
                nir_def *depth,
                nir_def *stencil,
                nir_def *sample_mask,
                unsigned dispatch_width)
{
   bool null_rt = target < 0;
   target = MAX2(target, 0);

   if (!colour) {
      colour = nir_undef(b, 4, 32);
   }

   colour = nir_pad_vec4(b, colour);

   if (null_rt) {
      /* Even if we don't write a RT, we still need to write alpha for
       * alpha-to-coverage and alpha testing. Optimize the other channels out.
       */
      colour = nir_vector_insert_imm(b, nir_undef(b, 4, 32),
                                     nir_channel(b, colour, 3), 3);
   }

   /* TODO: Not sure I like this. We'll see what 2src looks like. */
   unsigned op = dispatch_width == 32 ?
                    XE2_DATAPORT_RENDER_TARGET_WRITE_SIMD32_SINGLE_SOURCE :
                    BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD16_SINGLE_SOURCE;
   uint64_t desc =
      brw_fb_write_desc(devinfo, target, op, last, false /* coarse write */);

   uint64_t ex_desc = 0;
   if (devinfo->ver >= 20) {
      ex_desc = target << 21 |
                null_rt << 20 |
                (src0_alpha ? (1 << 15) : 0) |
                (stencil ? (1 << 14) : 0) |
                (depth ? (1 << 13) : 0) |
                (sample_mask ? (1 << 12) : 0);
   } else if (devinfo->ver >= 11) {
      /* Set the "Render Target Index" and "Src0 Alpha Present" fields
       * in the extended message descriptor, in lieu of using a header.
       */
      ex_desc = target << 12 | null_rt << 20 | (src0_alpha ? (1 << 15) : 0);
   }

   /* Build the payload */
   nir_def *payload[8] = { NULL };
   unsigned len = 0;
   append_payload(b, payload, &len, ARRAY_SIZE(payload), colour);
   append_payload(b, payload, &len, ARRAY_SIZE(payload), depth);
   /* TODO */

   nir_def *disable = b->shader->info.fs.uses_discard ?
                         nir_is_helper_invocation(b, 1) :
                         nir_imm_false(b);

   nir_store_render_target_intel(b, nir_vec(b, payload, len),
                                 nir_imm_ivec2(b, desc, ex_desc), disable,
                                 .eot = last);
}

static void
lower_fragment_outputs(nir_function_impl *impl,
                       const struct intel_device_info *devinfo,
                       unsigned nr_color_regions,
                       unsigned dispatch_width)
{
   struct frag_out_ctx ctx = { { NULL } };
   nir_function_intrinsics_pass(impl, collect_fragment_output,
                                nir_metadata_control_flow, &ctx);
   nir_builder b_ = nir_builder_at(nir_after_impl(impl));
   nir_builder *b = &b_;
   assert(nr_color_regions <= ARRAY_SIZE(ctx.colour));

   signed first = -1;
   for (unsigned i = 0; i < ARRAY_SIZE(ctx.colour); ++i) {
      if (ctx.colour[i]) {
         first = i;
         break;
      }
   }

   /* Do the later render targets first */
   for (unsigned i = first + 1; i < nr_color_regions; ++i) {
      if (ctx.colour[i]) {
         insert_rt_store(b, devinfo, i, false, ctx.colour[i], NULL, NULL, NULL,
                         NULL, dispatch_width);
      }
   }

   /* Finally do render target zero attaching all the sideband things and
    * setting the LastRT bit. This needs to exist even if nothing is written
    * since it also signals end-of-thread.
    */
   insert_rt_store(b, devinfo, first < nr_color_regions ? first : -1, true,
                   first >= 0 ? ctx.colour[first] : NULL, NULL, ctx.depth,
                   ctx.stencil, ctx.sample_mask, dispatch_width);
}

unsigned
jay_process_nir(const struct intel_device_info *devinfo,
                nir_shader *nir,
                union brw_any_prog_data *prog_data,
                union brw_any_prog_key *key)
{
   enum mesa_shader_stage stage = nir->info.stage;
   struct brw_compiler compiler = { .devinfo = devinfo };
   unsigned nr_packed_regs = 0;

   brw_pass_tracker pt_ = {
      .nir = nir,
      .key = &key->base,
      .dispatch_width = 0,
      .compiler = &compiler,
      .archiver = NULL, //params->base.archiver,
   }, *pt = &pt_;

   BRW_NIR_SNAPSHOT("first");

   prog_data->base.ray_queries = nir->info.ray_queries;
   prog_data->base.stage = stage;
   // TODO: Make the driver do this?
   // prog_data->base.source_hash = params->source_hash;
   prog_data->base.total_shared = nir->info.shared_size;

   /* TODO: Real heuristic */
   bool do_simd32 = INTEL_SIMD(FS, 32);
   do_simd32 &= stage == MESA_SHADER_COMPUTE || stage == MESA_SHADER_FRAGMENT;
   unsigned simd_width = do_simd32 ? (nir->info.api_subgroup_size ?: 32) : 16;

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

      brw_nir_apply_key(pt, &key->base, simd_width);

      prog_data->vs.inputs_read = nir->info.inputs_read;
      prog_data->vs.double_inputs_read = nir->info.vs.double_inputs;
      prog_data->vs.no_vf_slot_compaction = key->vs.no_vf_slot_compaction;

      brw_nir_lower_vs_inputs(nir);
      brw_nir_lower_vue_outputs(nir);
      BRW_NIR_SNAPSHOT("after_lower_io");

      memset(prog_data->vs.vf_component_packing, 0,
             sizeof(prog_data->vs.vf_component_packing));
      if (key->vs.vf_component_packing) {
         nr_packed_regs = brw_nir_pack_vs_input(nir, &prog_data->vs);
      }

      /* Get constant offsets out of the way for proper clip/cull handling */
      BRW_NIR_PASS(nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);
      BRW_NIR_PASS(nir_opt_constant_folding);
      BRW_NIR_PASS(brw_nir_lower_deferred_urb_writes, devinfo,
                   &prog_data->vue.vue_map, 0, 0);
   } else if (stage == MESA_SHADER_FRAGMENT) {
      assert(key->fs.mesh_input == INTEL_NEVER && "todo");
      assert(!key->fs.force_dual_color_blend && "todo");
      brw_nir_apply_key(pt, &key->base, 32);
      brw_nir_lower_fs_inputs(nir, devinfo, &key->fs);
      brw_nir_lower_fs_outputs(nir);
      NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_in, NULL, NULL);

      if (!brw_can_coherent_fb_fetch(devinfo))
         NIR_PASS(_, nir, brw_nir_lower_fs_load_output, &key->fs);

      NIR_PASS(_, nir, nir_opt_frag_coord_to_pixel_coord);
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_frag_coord,
               nir_metadata_control_flow, NULL);
      NIR_PASS(_, nir, nir_opt_barycentric, true);

      lower_fragment_outputs(nir_shader_get_entrypoint(nir), devinfo,
                             key->fs.nr_color_regions, simd_width);
      NIR_PASS(_, nir, nir_lower_helper_writes, true);
      NIR_PASS(_, nir, nir_lower_is_helper_invocation);
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_helper_invocation,
               nir_metadata_control_flow, NULL);

      if (key->fs.alpha_to_coverage != INTEL_NEVER) {
         /* Run constant fold optimization in order to get the correct source
          * offset to determine render target 0 store instruction in
          * emit_alpha_to_coverage pass.
          */
         NIR_PASS(_, nir, nir_opt_constant_folding);
         NIR_PASS(_, nir, brw_nir_lower_alpha_to_coverage);
      }

      // TODO
      // NIR_PASS(_, nir, brw_nir_move_interpolation_to_top);

      if (!brw_fs_prog_key_is_dynamic(&key->fs)) {
         uint32_t f = 0;

         if (key->fs.multisample_fbo == INTEL_ALWAYS)
            f |= INTEL_FS_CONFIG_MULTISAMPLE_FBO;

         if (key->fs.alpha_to_coverage == INTEL_ALWAYS)
            f |= INTEL_FS_CONFIG_ALPHA_TO_COVERAGE;

         if (key->fs.provoking_vertex_last == INTEL_ALWAYS)
            f |= INTEL_FS_CONFIG_PROVOKING_VERTEX_LAST;

         if (key->fs.persample_interp == INTEL_ALWAYS) {
            f |= INTEL_FS_CONFIG_PERSAMPLE_DISPATCH |
                 INTEL_FS_CONFIG_PERSAMPLE_INTERP;
         }

         NIR_PASS(_, nir, nir_inline_sysval, nir_intrinsic_load_fs_config_intel,
                  f);
      }
   } else {
      brw_nir_apply_key(pt, &key->base, simd_width);
   }

   brw_postprocess_nir_opts(pt);

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, jay_nir_lower_simd,
            nir_metadata_control_flow, &simd_width);
   NIR_PASS(_, nir, nir_opt_algebraic_late);
   NIR_PASS(_, nir, intel_nir_opt_peephole_imul32x16);

   /* Late postprocess while remaining in SSA */
   /* Run fsign lowering again after the last time brw_nir_optimize is called.
    * As is the case with conversion lowering (below), brw_nir_optimize can
    * create additional fsign instructions.
    */
   NIR_PASS(_, nir, jay_nir_lower_fsign);
   NIR_PASS(_, nir, jay_nir_lower_bool);
   NIR_PASS(_, nir, nir_opt_cse);
   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, jay_nir_opt_sel_zero);

   /* Run nir_split_conversions only after the last tiem
    * brw_nir_optimize is called. Various optimizations invoked there can
    * rematerialize the conversions that the lowering pass eliminates.
    */
   const nir_split_conversions_options split_conv_opts = {
      .callback = intel_nir_split_conversions_cb,
   };
   NIR_PASS(_, nir, nir_split_conversions, &split_conv_opts);

   /* Do this only after the last opt_gcm. GCM will undo this lowering. */
   if (stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, nir, intel_nir_lower_non_uniform_barycentric_at_sample);
   }

   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_lower_load_const_to_scalar);
   NIR_PASS(_, nir, nir_lower_all_phis_to_scalar);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_dce);

   /* Run divergence analysis at the end */
   nir_sweep(nir);
   nj_index_ssa_defs(nir);
   nir_divergence_analysis(nir);

   jay_populate_prog_data(devinfo, nir, prog_data, key, nr_packed_regs);
   return simd_width;
}
