/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "msl_private.h"
#include "nir_to_msl.h"

#include "nir.h"
#include "nir_builder.h"

#include "util/format/u_format.h"

bool
msl_nir_vs_remove_point_size_write(nir_builder *b, nir_intrinsic_instr *intrin,
                                   void *data)
{
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
   if (io.location == VARYING_SLOT_PSIZ) {
      return nir_remove_sysval_output(intrin, MESA_SHADER_FRAGMENT);
   }

   return false;
}

bool
msl_nir_fs_remove_depth_write(nir_builder *b, nir_intrinsic_instr *intrin,
                              void *data)
{
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
   if (io.location == FRAG_RESULT_DEPTH) {
      return nir_remove_sysval_output(intrin, MESA_SHADER_FRAGMENT);
   }

   return false;
}

bool
msl_nir_fs_force_output_signedness(
   nir_shader *nir, enum pipe_format render_target_formats[MAX_DRAW_BUFFERS])
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   bool update_derefs = false;
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_out) {
      if (FRAG_RESULT_DATA0 <= var->data.location &&
          var->data.location <= FRAG_RESULT_DATA7 &&
          glsl_type_is_integer(var->type)) {
         unsigned int slot = var->data.location - FRAG_RESULT_DATA0;

         if (glsl_type_is_uint_16_32_64(var->type) &&
             util_format_is_pure_sint(render_target_formats[slot])) {
            var->type = glsl_ivec_type(var->type->vector_elements);
            update_derefs = true;
         } else if (glsl_type_is_int_16_32_64(var->type) &&
                    util_format_is_pure_uint(render_target_formats[slot])) {
            var->type = glsl_uvec_type(var->type->vector_elements);
            update_derefs = true;
         }
      }
   }

   if (update_derefs) {
      nir_foreach_function_impl(impl, nir) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
               switch (instr->type) {
               case nir_instr_type_deref: {
                  nir_deref_instr *deref = nir_instr_as_deref(instr);
                  if (deref->deref_type == nir_deref_type_var) {
                     deref->type = deref->var->type;
                  }
                  break;
               }
               default:
                  break;
               }
            }
         }
         nir_progress(update_derefs, impl, nir_metadata_control_flow);
      }
   }

   return update_derefs;
}

bool
msl_lower_textures(nir_shader *nir)
{
   bool progress = false;
   nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0u,
      .lower_sampler_lod_bias = true,

      /* We don't use 1D textures because they are really limited in Metal */
      .lower_1d = true,

      /* Metal does not support tg4 with individual offsets for each sample */
      .lower_tg4_offsets = true,

      /* Metal does not natively support offsets for texture.read operations */
      .lower_txf_offset = true,
      .lower_txd_cube_map = true,
   };

   NIR_PASS(progress, nir, nir_lower_tex, &lower_tex_options);
   return progress;
}

static bool
replace_sample_id_for_sample_mask(nir_builder *b, nir_intrinsic_instr *intrin,
                                  void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_sample_mask_in)
      return false;

   nir_def_replace(nir_instr_def(&intrin->instr), (nir_def *)data);
   return true;
}

static bool
msl_replace_load_sample_mask_in_for_static_sample_mask(
   nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_sample_mask_in)
      return false;

   nir_def *sample_mask = (nir_def *)data;
   nir_def_rewrite_uses(&intr->def, sample_mask);
   return true;
}

bool
msl_lower_static_sample_mask(nir_shader *nir, uint32_t sample_mask)
{
   /* Only support vertex for now */
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   /* Embed sample mask */
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_at(nir_before_impl(entrypoint));

   struct nir_io_semantics io_semantics = {
      .location = FRAG_RESULT_SAMPLE_MASK,
      .num_slots = 1u,
   };
   nir_def *sample_mask_def = nir_imm_int(&b, sample_mask);
   nir_store_output(&b, sample_mask_def, nir_imm_int(&b, 0u), .base = 0u,
                    .range = 1u, .write_mask = 0x1, .component = 0u,
                    .src_type = nir_type_uint32, .io_semantics = io_semantics);

   return nir_shader_intrinsics_pass(
      nir, msl_replace_load_sample_mask_in_for_static_sample_mask,
      nir_metadata_control_flow, sample_mask_def);

   return true;
}

bool
msl_ensure_depth_write(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   bool has_depth_write =
      nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH);
   if (!has_depth_write) {
      nir_variable *depth_var = nir_create_variable_with_location(
         nir, nir_var_shader_out, FRAG_RESULT_DEPTH, glsl_float_type());

      /* Write to depth at the very beginning */
      nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
      nir_builder b = nir_builder_at(nir_before_impl(entrypoint));

      nir_deref_instr *depth_deref = nir_build_deref_var(&b, depth_var);
      nir_def *position = nir_load_frag_coord(&b);
      nir_store_deref(&b, depth_deref, nir_channel(&b, position, 2u),
                      0xFFFFFFFF);

      nir->info.outputs_written |= BITFIELD64_BIT(FRAG_RESULT_DEPTH);
      nir->info.fs.depth_layout = FRAG_DEPTH_LAYOUT_ANY;
      return nir_progress(true, entrypoint, nir_metadata_control_flow);
   }
   return false;
}

bool
msl_ensure_vertex_position_output(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX);

   bool has_position_write =
      nir->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_POS);
   if (!has_position_write) {
      nir_variable *position_var = nir_create_variable_with_location(
         nir, nir_var_shader_out, VARYING_SLOT_POS, glsl_vec4_type());

      /* Write to depth at the very beginning */
      nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
      nir_builder b = nir_builder_at(nir_before_impl(entrypoint));

      nir_deref_instr *position_deref = nir_build_deref_var(&b, position_var);
      nir_def *zero = nir_imm_float(&b, 0.0f);
      nir_store_deref(&b, position_deref, nir_vec4(&b, zero, zero, zero, zero),
                      0xFFFFFFFF);

      nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_POS);
      return nir_progress(true, entrypoint, nir_metadata_control_flow);
   }
   return false;
}

static bool
msl_fs_io_types(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic == nir_intrinsic_load_input) {
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intr);
      if (io.location == VARYING_SLOT_VIEWPORT) {
         nir_intrinsic_set_dest_type(intr, nir_type_uint32);
         return true;
      }
   }

   if (intr->intrinsic == nir_intrinsic_store_output) {
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intr);
      if (io.location == FRAG_RESULT_SAMPLE_MASK) {
         nir_intrinsic_set_src_type(intr, nir_type_uint32);
         return true;
      } else if (io.location == FRAG_RESULT_STENCIL) {
         nir_alu_type type = nir_intrinsic_src_type(intr);
         nir_intrinsic_set_src_type(
            intr, nir_type_uint | nir_alu_type_get_type_size(type));
         return true;
      }
   }

   if (intr->intrinsic == nir_intrinsic_load_output) {
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intr);
      if (io.location == FRAG_RESULT_STENCIL) {
         nir_alu_type type = nir_intrinsic_dest_type(intr);
         nir_intrinsic_set_dest_type(
            intr, nir_type_uint | nir_alu_type_get_type_size(type));
         return true;
      }
   }

   return false;
}

bool
msl_nir_fs_io_types(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);
   return nir_shader_intrinsics_pass(nir, msl_fs_io_types, nir_metadata_all,
                                     NULL);
}

static bool
msl_vs_io_types(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic == nir_intrinsic_store_output) {
      struct nir_io_semantics io = nir_intrinsic_io_semantics(intr);
      if (io.location == VARYING_SLOT_LAYER ||
          io.location == VARYING_SLOT_VIEWPORT) {
         nir_intrinsic_set_src_type(intr, nir_type_uint32);
         return true;
      }
   }

   return false;
}

bool
msl_nir_vs_io_types(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX);
   return nir_shader_intrinsics_pass(nir, msl_vs_io_types, nir_metadata_all,
                                     NULL);
}

static bool
lower_clip_distance(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io = nir_intrinsic_io_semantics(intr);
   unsigned component = nir_intrinsic_component(intr);
   if (io.location != VARYING_SLOT_CLIP_DIST0 &&
       io.location != VARYING_SLOT_CLIP_DIST1)
      return false;

   unsigned base = (io.location - VARYING_SLOT_CLIP_DIST0) * 4 + component;
   if (intr->intrinsic == nir_intrinsic_store_output) {
      b->cursor = nir_after_instr(&intr->instr);
      nir_store_clip_distance_kk(b, intr->src[0].ssa, .base = base);
   }
   return true;
}

bool
msl_nir_lower_clip_distance(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_clip_distance, nir_metadata_all,
                                     NULL);
}
