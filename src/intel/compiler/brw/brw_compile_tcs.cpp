/*
 * Copyright © 2013 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_eu.h"
#include "intel_nir.h"
#include "brw_nir.h"
#include "brw_shader.h"
#include "brw_builder.h"
#include "brw_private.h"
#include "dev/intel_debug.h"

/**
 * Return the number of patches to accumulate before a MULTI_PATCH mode thread is
 * launched.  In cases with a large number of input control points and a large
 * amount of VS outputs, the VS URB space needed to store an entire 8 patches
 * worth of data can be prohibitive, so it can be beneficial to launch threads
 * early.
 *
 * See the 3DSTATE_HS::Patch Count Threshold documentation for the recommended
 * values.  Note that 0 means to "disable" early dispatch, meaning to wait for
 * a full 8 patches as normal.
 */
static int
get_patch_count_threshold(int input_control_points)
{
   if (input_control_points <= 4)
      return 0;
   else if (input_control_points <= 6)
      return 5;
   else if (input_control_points <= 8)
      return 4;
   else if (input_control_points <= 10)
      return 3;
   else if (input_control_points <= 14)
      return 2;

   /* Return patch count 1 for PATCHLIST_15 - PATCHLIST_32 */
   return 1;
}

static void
brw_emit_tcs_thread_end(brw_shader &s)
{
   /* Try and tag the last URB write with EOT instead of emitting a whole
    * separate write just to finish the thread.  There isn't guaranteed to
    * be one, so this may not succeed.
    */
   if (s.mark_last_urb_write_with_eot())
      return;

   const brw_builder bld = brw_builder(&s);

   /* Otherwise, we emit a URB write which writes zero to a reserved/MBZ
    * patch header DWord to end the thread.  We use DWord 0 for legacy
    * layouts, and DWord 6 for reversed layouts.
    */
   const bool reversed = s.devinfo->ver >= 12;
   unsigned components = 1;
   unsigned offset = 0;

   brw_reg srcs[URB_LOGICAL_NUM_SRCS];
   srcs[URB_LOGICAL_SRC_HANDLE] = s.tcs_payload().patch_urb_output;
   srcs[URB_LOGICAL_SRC_DATA] = brw_imm_ud(0);

   if (s.devinfo->ver >= 20) {
      offset = 28; /* .z of slot 1 */
   } else if (reversed) {
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(WRITEMASK_Z);

      brw_reg zeroes[3] = { brw_imm_ud(0), brw_imm_ud(0), brw_imm_ud(0) };
      srcs[URB_LOGICAL_SRC_DATA] = bld.vgrf(BRW_TYPE_UD, 3);
      bld.VEC(srcs[URB_LOGICAL_SRC_DATA], zeroes, 3);

      components = 3;
      offset = 1;
   } else {
      srcs[URB_LOGICAL_SRC_CHANNEL_MASK] = brw_imm_ud(WRITEMASK_X);
   }

   brw_urb_inst *urb = bld.URB_WRITE(srcs, ARRAY_SIZE(srcs));
   urb->eot = true;
   urb->offset = offset;
   urb->components = components;
}

static bool
run_tcs(brw_shader &s)
{
   assert(s.stage == MESA_SHADER_TESS_CTRL);

   struct brw_vue_prog_data *vue_prog_data = brw_vue_prog_data(s.prog_data);

   assert(vue_prog_data->dispatch_mode == INTEL_DISPATCH_MODE_TCS_SINGLE_PATCH ||
          vue_prog_data->dispatch_mode == INTEL_DISPATCH_MODE_TCS_MULTI_PATCH);

   s.payload_ = new brw_tcs_thread_payload(s);

   brw_from_nir(&s);

   if (s.failed)
      return false;

   brw_calculate_cfg(s);

   brw_emit_tcs_thread_end(s);

   brw_optimize(s);

   s.assign_curb_setup();
   brw_assign_urb_setup(s);

   brw_lower_3src_null_dest(s);
   brw_workaround_emit_dummy_mov_instruction(s);

   brw_allocate_registers(s, true /* allow_spilling */);

   brw_workaround_source_arf_before_eot(s);

   return !s.failed;
}

static bool
fix_single_patch_thread_dispatch(nir_shader *nir)
{
   if ((nir->info.tess.tcs_vertices_out % 8) == 0)
      return false;

   /* Wrap shader inside if (InvocationID < VerticesOut) */
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   if (nir_cf_list_is_empty_block(&impl->body))
      return false;

   nir_cf_list body;
   nir_cf_list_extract(&body, &impl->body);

   nir_builder b = nir_builder_at(nir_after_impl(impl));
   nir_push_if(&b, nir_ilt_imm(&b, nir_load_invocation_id(&b),
                               nir->info.tess.tcs_vertices_out));
   nir_cf_reinsert(&body, b.cursor);
   nir_pop_if(&b, NULL);

   return true;
}

static bool
lower_single_patch_invoc_id(nir_builder *b, nir_intrinsic_instr *intr, void *)
{
   /* brw_from_nir returns Instance ID (thread #) for load_invocation_id,
    * which is correct for multi-patch.  For single patch, it should be
    * 8 * InstanceID + <76543210>.
    */
   if (intr->intrinsic == nir_intrinsic_load_invocation_id) {
      b->cursor = nir_after_instr(&intr->instr);
      nir_def *invoc = nir_iadd(b, nir_imul_imm(b, &intr->def, 8),
                                nir_load_subgroup_invocation(b));
      nir_def_rewrite_uses_after(&intr->def, invoc);
      return true;
   }
   return false;
}

static bool
lower_single_patch_invocation_id(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_single_patch_invoc_id,
                                     nir_metadata_control_flow, NULL);
}

extern "C" const unsigned *
brw_compile_tcs(const struct brw_compiler *compiler,
                struct brw_compile_tcs_params *params)
{
   const struct intel_device_info *devinfo = compiler->devinfo;
   nir_shader *nir = params->base.nir;
   const struct brw_tcs_prog_key *key =
      (const struct brw_tcs_prog_key *)params->base.key;
   struct brw_tcs_prog_data *prog_data =
      (struct brw_tcs_prog_data *)params->base.prog_data;
   struct brw_vue_prog_data *vue_prog_data = &prog_data->base;
   const unsigned dispatch_width = brw_geometry_stage_dispatch_width(compiler->devinfo);

   const bool debug_enabled = brw_should_print_shader(nir, DEBUG_TCS, params->base.source_hash);

   brw_pass_tracker pt_ = {
      .nir = nir,
      .dispatch_width = dispatch_width,
      .compiler = compiler,
      .key = &key->base,
      .archiver = params->base.archiver,
   }, *pt = &pt_;

   BRW_NIR_SNAPSHOT("first");

   brw_prog_data_init(&prog_data->base.base, &params->base);

   brw_fill_tess_info_from_shader_info(&prog_data->tess_info,
                                       &nir->info);

   nir->info.outputs_written = key->outputs_written;
   nir->info.patch_outputs_written = key->patch_outputs_written;

   struct intel_vue_map input_vue_map;
   brw_compute_vue_map(devinfo, &input_vue_map, nir->info.inputs_read,
                       key->base.vue_layout, 1);
   brw_compute_tess_vue_map(&vue_prog_data->vue_map,
                            nir->info.outputs_written,
                            nir->info.patch_outputs_written,
                            key->separate_tess_vue_layout);

   brw_nir_apply_key(pt, &key->base, dispatch_width);
   brw_nir_lower_tcs_inputs(nir, devinfo, &input_vue_map);
   brw_nir_lower_tcs_outputs(nir, devinfo, &vue_prog_data->vue_map,
                             key->_tes_primitive_mode);
   BRW_NIR_SNAPSHOT("after_lower_io");

   brw_nir_opt_vectorize_urb(pt);
   BRW_NIR_PASS(intel_nir_lower_patch_vertices_in, key->input_vertices);


   if (!intel_use_tcs_multi_patch(devinfo)) {
      BRW_NIR_PASS(fix_single_patch_thread_dispatch);
      BRW_NIR_PASS(lower_single_patch_invocation_id);
   }

   brw_postprocess_nir(pt, debug_enabled);

   bool has_primitive_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

   prog_data->input_vertices = key->input_vertices;
   prog_data->output_vertices = nir->info.tess.tcs_vertices_out;
   prog_data->patch_count_threshold = get_patch_count_threshold(key->input_vertices);

   if (intel_use_tcs_multi_patch(devinfo)) {
      vue_prog_data->dispatch_mode = INTEL_DISPATCH_MODE_TCS_MULTI_PATCH;
      prog_data->instances = nir->info.tess.tcs_vertices_out;
      prog_data->include_primitive_id = has_primitive_id;
   } else {
      unsigned verts_per_thread = 8;
      vue_prog_data->dispatch_mode = INTEL_DISPATCH_MODE_TCS_SINGLE_PATCH;
      prog_data->instances =
         DIV_ROUND_UP(nir->info.tess.tcs_vertices_out, verts_per_thread);
   }

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
   const int num_per_patch_slots = vue_prog_data->vue_map.num_per_patch_slots;
   const int num_per_vertex_slots = vue_prog_data->vue_map.num_per_vertex_slots;
   unsigned output_size_bytes = 0;
   /* Note that the patch header is counted in num_per_patch_slots. */
   output_size_bytes += num_per_patch_slots * 16;
   output_size_bytes += nir->info.tess.tcs_vertices_out *
                        num_per_vertex_slots * 16;

   assert(output_size_bytes >= 1);
   if (output_size_bytes > GFX7_MAX_HS_URB_ENTRY_SIZE_BYTES)
      return NULL;

   /* URB entry sizes are stored as a multiple of 64 bytes. */
   vue_prog_data->urb_entry_size = align(output_size_bytes, 64) / 64;

   /* HS does not use the usual payload pushing from URB to GRFs,
    * because we don't have enough registers for a full-size payload, and
    * the hardware is broken on Haswell anyway.
    */
   vue_prog_data->urb_read_length = 0;

   if (unlikely(debug_enabled)) {
      fprintf(stderr, "TCS Input ");
      brw_print_vue_map(stderr, &input_vue_map, MESA_SHADER_TESS_CTRL);
      fprintf(stderr, "TCS Output ");
      brw_print_vue_map(stderr, &vue_prog_data->vue_map, MESA_SHADER_TESS_CTRL);
   }

   const brw_shader_params shader_params = {
      .compiler                = compiler,
      .mem_ctx                 = params->base.mem_ctx,
      .nir                     = nir,
      .key                     = &key->base,
      .prog_data               = &prog_data->base.base,
      .dispatch_width          = dispatch_width,
      .needs_register_pressure = params->base.stats != NULL,
      .log_data                = params->base.log_data,
      .debug_enabled           = debug_enabled,
      .archiver                = params->base.archiver,
   };
   brw_shader v(&shader_params);
   if (!run_tcs(v)) {
      params->base.error_str =
         ralloc_strdup(params->base.mem_ctx, v.fail_msg);
      return NULL;
   }

   assert(v.payload().num_regs % reg_unit(devinfo) == 0);
   prog_data->base.base.dispatch_grf_start_reg = v.payload().num_regs / reg_unit(devinfo);
   prog_data->base.base.grf_used = v.grf_used;

   const brw_to_binary_params to_binary_params = {
      .compiler = compiler,
      .params = &params->base,
      .prog_data = &prog_data->base.base,
      .shaders = { &v },
   };
   return brw_to_binary(&to_binary_params);
}
