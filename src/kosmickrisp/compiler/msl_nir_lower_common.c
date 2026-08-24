/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "msl_private.h"
#include "nir_to_msl.h"

#include "nir.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"

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

static bool
fs_remove_depth_write(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
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
msl_nir_fs_remove_depth_write(nir_shader *s)
{
   bool progress = nir_shader_intrinsics_pass(s, fs_remove_depth_write,
                                              nir_metadata_control_flow, NULL);
   s->info.outputs_written &= ~BITFIELD64_BIT(FRAG_RESULT_DEPTH);
   return progress;
}

static bool
fs_force_output_type(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io = nir_intrinsic_io_semantics(intrin);
   if (io.location < FRAG_RESULT_DATA0 || FRAG_RESULT_DATA7 < io.location)
      return false;

   enum pipe_format *render_target_formats = (enum pipe_format *)data;
   enum pipe_format format =
      render_target_formats[io.location - FRAG_RESULT_DATA0];
   nir_alu_type type = nir_intrinsic_src_type(intrin);
   if (util_format_is_float(format) || util_format_is_unorm(format)) {
      if (type & nir_type_uint) {
         b->cursor = nir_before_instr(&intrin->instr);
         nir_def *value =
            nir_u2fN(b, intrin->src[0].ssa, nir_src_bit_size(intrin->src[0]));
         nir_src_rewrite(&intrin->src[0], value);
         type ^= (nir_type_float | nir_type_uint);
         nir_intrinsic_set_src_type(intrin, type);
         return true;
      } else if (type & nir_type_int) {
         b->cursor = nir_before_instr(&intrin->instr);
         nir_def *value =
            nir_u2fN(b, intrin->src[0].ssa, nir_src_bit_size(intrin->src[0]));
         nir_src_rewrite(&intrin->src[0], value);
         type ^= (nir_type_float | nir_type_int);
         nir_intrinsic_set_src_type(intrin, type);
         return true;
      }
   } else if (util_format_is_pure_sint(format)) {
      if (type & nir_type_uint) {
         type ^= (nir_type_uint | nir_type_int);
         nir_intrinsic_set_src_type(intrin, type);
         return true;
      } else if (type & nir_type_float) {
         b->cursor = nir_before_instr(&intrin->instr);
         nir_def *value =
            nir_f2uN(b, intrin->src[0].ssa, nir_src_bit_size(intrin->src[0]));
         nir_src_rewrite(&intrin->src[0], value);
         type ^= (nir_type_float | nir_type_int);
         nir_intrinsic_set_src_type(intrin, type);
         return true;
      }
   } else if (util_format_is_pure_uint(format)) {
      if (type & nir_type_int) {
         type ^= (nir_type_int | nir_type_uint);
         nir_intrinsic_set_src_type(intrin, type);
         return true;
      } else if (type & nir_type_float) {
         b->cursor = nir_before_instr(&intrin->instr);
         nir_def *value =
            nir_f2uN(b, intrin->src[0].ssa, nir_src_bit_size(intrin->src[0]));
         nir_src_rewrite(&intrin->src[0], value);
         type ^= (nir_type_float | nir_type_uint);
         nir_intrinsic_set_src_type(intrin, type);
         return true;
      }
   }
   return false;
}

bool
msl_nir_fs_force_output_signedness(
   nir_shader *nir, enum pipe_format render_target_formats[MAX_DRAW_BUFFERS])
{
   return nir_shader_intrinsics_pass(nir, fs_force_output_type,
                                     nir_metadata_control_flow,
                                     render_target_formats);
}

/* Used to weed out instructions where the lod is known to be 0.
 * This is an optimization and avoids trying to instrument
 * texel buffers, which won't work with the generated Metal code
 * for the OOB lod workaround */
static bool
kk_src_is_const_zero(nir_src *src)
{
   return nir_src_is_const(*src) && (nir_src_as_uint(*src) == 0);
}

static void
kk_lod_oob_fixup(nir_builder *b, nir_src *coord, nir_src *lod, nir_def *levels)
{
   /* For chips (M5) that don't handle OOB LOD correctly, transform it into
    * a coordinate OOB, which is handled correctly */
   nir_def *oob = nir_uge(b, lod->ssa, levels);
   nir_def *def = nir_bcsel(b, oob, nir_imm_int(b, -1), coord->ssa);

   nir_src_rewrite(coord, def);
}

static bool
kk_lower_robustness2_textures(nir_builder *b, nir_instr *instr,
                              UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   if (tex->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return false;
   if ((tex->op != nir_texop_txf) && (tex->op != nir_texop_txf_ms))
      return false;

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   if (coord_index < 0)
      return false;

   int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_index < 0)
      return false;

   nir_src *lod = &tex->src[lod_index].src;
   if (!lod || kk_src_is_const_zero(lod))
      return false;

   b->cursor = nir_before_instr(instr);
   nir_def *levels = nir_build_texture_query(b, tex, nir_texop_query_levels, 1,
                                             nir_type_uint32, false, false);

   kk_lod_oob_fixup(b, &tex->src[coord_index].src, lod, levels);

   return true;
}

static bool
kk_lower_robustness2_intrinsics(nir_builder *b, nir_intrinsic_instr *intr,
                                UNUSED void *data)
{
   /* So far it doesn't seem like we need to fix up
    * nir_intrinsic_bindless_image_store, even though it could have an OOB lod. */
   if (intr->intrinsic != nir_intrinsic_bindless_image_load &&
       intr->intrinsic != nir_intrinsic_bindless_image_sparse_load)
      return false;

   nir_src *lod = &intr->src[3];
   if (!lod || kk_src_is_const_zero(lod))
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_intrinsic_instr *levels_instr = nir_intrinsic_instr_create(
      b->shader, nir_intrinsic_bindless_image_levels);
   levels_instr->src[0] = intr->src[0];
   nir_def_init(&levels_instr->instr, &levels_instr->def, 1, 32);
   nir_builder_instr_insert(b, &levels_instr->instr);

   kk_lod_oob_fixup(b, &intr->src[1], lod, &levels_instr->def);

   return true;
}

bool
msl_lower_robustness2_images(nir_shader *nir)
{
   bool progress = false;

   progress |= nir_shader_intrinsics_pass(nir, kk_lower_robustness2_intrinsics,
                                          nir_metadata_control_flow, NULL);

   progress |= nir_shader_instructions_pass(nir, kk_lower_robustness2_textures,
                                            nir_metadata_control_flow, NULL);

   return progress;
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

   b->cursor = nir_after_instr(&intr->instr);
   nir_def *static_sample_mask = (nir_def *)data;
   nir_def *sample_mask = nir_iand(b, &intr->def, static_sample_mask);
   nir_def_rewrite_uses_after(&intr->def, sample_mask);
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
   nir_def *static_sample_mask = nir_imm_int(&b, sample_mask);
   nir_store_output(&b, nir_load_sample_mask_in(&b), nir_imm_int(&b, 0u),
                    .base = 0u, .range = 1u, .write_mask = 0x1, .component = 0u,
                    .src_type = nir_type_uint32, .io_semantics = io_semantics);
   BITSET_SET(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN);

   nir_shader_intrinsics_pass(
      nir, msl_replace_load_sample_mask_in_for_static_sample_mask,
      nir_metadata_control_flow, static_sample_mask);

   return true;
}

static bool
is_color_output(const nir_intrinsic_instr *intr)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   const unsigned location = nir_intrinsic_io_semantics(intr).location;
   return location == FRAG_RESULT_COLOR ||
          (location >= FRAG_RESULT_DATA0 && location <= FRAG_RESULT_DATA7);
}

/* See issue: https://github.com/KhronosGroup/Vulkan-Portability/issues/54
 * Writing sample mask is more expensive than using barycentric coordinates.
 * Find a color store and "rewrite" store value to be a bcsel based on a false
 * value using barycentric coordinates so the MSL compiler does not optimise it.
 * Use sample mask write as a fallback when no color is present.
 */
bool
msl_disable_triangle_merge(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);

   nir_intrinsic_instr *store = NULL;
   nir_foreach_block(block, entrypoint) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (is_color_output(intr)) {
            store = intr;
            break;
         }
      }
      if (store)
         break;
   }

   if (store == NULL)
      return msl_lower_static_sample_mask(nir, 0xFFFFFFFF);

   nir_builder b = nir_builder_at(nir_before_impl(entrypoint));

   nir_def *barycentric = nir_load_barycentric_coord_pixel(
      &b, 32u, .interp_mode = INTERP_MODE_SMOOTH);
   BITSET_SET(nir->info.system_values_read,
              SYSTEM_VALUE_BARYCENTRIC_PERSP_COORD);

   nir_def *all_negative =
      nir_flt_imm(&b, nir_channel(&b, barycentric, 0u), 0.0);
   for (unsigned i = 1u; i < 3u; ++i) {
      nir_def *weight = nir_channel(&b, barycentric, i);
      all_negative = nir_iand(&b, all_negative, nir_flt_imm(&b, weight, 0.0));
   }

   b.cursor = nir_before_instr(&store->instr);
   nir_def *value = store->src[0u].ssa;
   nir_def *unchanged = nir_bcsel(
      &b, all_negative,
      nir_imm_zero(&b, value->num_components, value->bit_size), value);
   nir_src_rewrite(&store->src[0u], unchanged);

   return nir_progress(true, entrypoint, nir_metadata_control_flow);
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
      nir_def *position = nir_build_frag_coord(&b, 3);
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
   assert(nir->info.stage == MESA_SHADER_VERTEX ||
          nir->info.stage == MESA_SHADER_TESS_EVAL);

   bool has_position_write =
      nir->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_POS);
   if (!has_position_write) {
      /* Write to position at the very end, consistent with sunk stores */
      nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
      nir_builder b = nir_builder_at(nir_after_impl(entrypoint));

      struct nir_io_semantics io_semantics = {
         .location = VARYING_SLOT_POS,
         .num_slots = 4u,
      };
      nir_def *zero = nir_imm_float(&b, 0.0f);
      nir_store_output(
         &b, nir_vec4(&b, zero, zero, zero, zero), nir_imm_int(&b, 0u),
         .base = 0u, .range = 4u, .write_mask = 0xf, .component = 0u,
         .src_type = nir_type_float32, .io_semantics = io_semantics);

      nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_POS);
      return nir_progress(true, entrypoint, nir_metadata_control_flow);
   }
   return false;
}

bool
msl_ensure_vertex_point_size_output(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_VERTEX ||
          nir->info.stage == MESA_SHADER_TESS_EVAL);

   bool has_point_size_write =
      nir->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_PSIZ);
   if (!has_point_size_write) {
      /* Write to point size at the very end, consistent with sunk stores */
      nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);
      nir_builder b = nir_builder_at(nir_after_impl(entrypoint));

      struct nir_io_semantics io_semantics = {
         .location = VARYING_SLOT_PSIZ,
         .num_slots = 1u,
      };
      nir_store_output(&b, nir_imm_float(&b, 1.0f), nir_imm_int(&b, 0u),
                       .base = 0u, .range = 1u, .write_mask = 0x1,
                       .component = 0u, .src_type = nir_type_float32,
                       .io_semantics = io_semantics);
      nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_PSIZ);
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
   assert(nir->info.stage == MESA_SHADER_VERTEX ||
          nir->info.stage == MESA_SHADER_TESS_EVAL);
   return nir_shader_intrinsics_pass(nir, msl_vs_io_types, nir_metadata_all,
                                     NULL);
}

static bool
fake_guard_for_discards(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_demote)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *helper = nir_is_helper_invocation(b, 1);
   nir_demote_if(b, nir_inot(b, helper));
   nir_instr_remove(&intrin->instr);
   return true;
}

bool
msl_nir_fake_guard_for_discards(struct nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   /* No side effects, no lowering needed */
   if (!nir->info.writes_memory)
      return false;

   return nir_shader_intrinsics_pass(nir, fake_guard_for_discards,
                                     nir_metadata_control_flow, NULL);
}

/* Returns true if gl_SampleID is required. */
static bool
gather_fs_input_interpolant_usage(nir_builder *b, nir_intrinsic_instr *intr,
                                  void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   bool *uses_interpolant = (bool *)data;
   struct nir_io_semantics io = nir_intrinsic_io_semantics(intr);
   nir_intrinsic_instr *interpolation = nir_src_as_intrinsic(intr->src[0]);
   bool at_sample =
      interpolation->intrinsic == nir_intrinsic_load_barycentric_at_sample;
   uses_interpolant[io.location] |=
      at_sample ||
      interpolation->intrinsic == nir_intrinsic_load_barycentric_at_offset;
   return at_sample;
}

static bool
lower_sample_shading(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic == nir_intrinsic_load_frag_coord) {
      b->cursor = nir_after_instr(&intr->instr);
      nir_def *offset =
         nir_fadd(b, nir_load_sample_pos_from_id(b, 32u, nir_load_sample_id(b)),
                  nir_imm_vec2(b, -0.5f, -0.5f));
      nir_def *sample_position =
         nir_fadd(b, &intr->def, nir_pad_vector_imm_int(b, offset, 0u, 4u));
      nir_def_rewrite_uses_after(&intr->def, sample_position);
      return true;
   }

   if (intr->intrinsic == nir_intrinsic_load_sample_mask_in) {
      b->cursor = nir_after_instr(&intr->instr);
      nir_def *sample_id = nir_load_sample_id(b);
      nir_def *sample_bit = nir_ishl(b, nir_imm_int(b, 1), sample_id);
      nir_def *sample_mask_bit = nir_iand(b, &intr->def, sample_bit);
      nir_def_rewrite_uses_after(&intr->def, sample_mask_bit);
      return true;
   }

   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   bool *uses_interpolant = (bool *)data;
   struct nir_io_semantics io = nir_intrinsic_io_semantics(intr);
   nir_intrinsic_instr *interpolation = nir_src_as_intrinsic(intr->src[0]);
   if (!uses_interpolant[io.location] ||
       interpolation->intrinsic != nir_intrinsic_load_barycentric_sample)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *def = nir_load_barycentric_at_sample(
      b, intr->def.bit_size, nir_load_sample_id(b),
      .interp_mode = nir_intrinsic_interp_mode(interpolation));
   nir_def_rewrite_uses_after(&interpolation->def, def);
   nir_instr_remove(&interpolation->instr);
   return false;
}

bool
msl_nir_lower_sample_shading(nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);

   bool uses_interpolant[NUM_TOTAL_VARYING_SLOTS] = {};

   if (nir_shader_intrinsics_pass(nir, gather_fs_input_interpolant_usage,
                                  nir_metadata_all, uses_interpolant))
      BITSET_SET(nir->info.system_values_read, SYSTEM_VALUE_SAMPLE_ID);

   return nir_shader_intrinsics_pass(
      nir, lower_sample_shading, nir_metadata_control_flow, uses_interpolant);
}

static bool
lower_clip_cull_distance_write(nir_builder *b, nir_intrinsic_instr *intr,
                               UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location != VARYING_SLOT_CLIP_DIST0 &&
       sem.location != VARYING_SLOT_CLIP_DIST1 &&
       sem.location != VARYING_SLOT_CULL_DIST0 &&
       sem.location != VARYING_SLOT_CULL_DIST1)
      return false;

   assert(nir_src_num_components(intr->src[0]) == 1 && "must be scalarized");

   signed location = sem.location + nir_src_as_uint(intr->src[1]);

   if (sem.location == VARYING_SLOT_CLIP_DIST0 ||
       sem.location == VARYING_SLOT_CLIP_DIST1) {
      /* Clip distance, add write to MSL clip_distance output */
      unsigned component = (location - VARYING_SLOT_CLIP_DIST0) * 4 +
                           nir_intrinsic_component(intr);

      b->cursor = nir_after_instr(&intr->instr);
      nir_store_clip_distance_kk(b, intr->src[0].ssa, .base = component);
      return true;
   }

   if (sem.location == VARYING_SLOT_CULL_DIST0 ||
       sem.location == VARYING_SLOT_CULL_DIST1) {
      /* Cull distance, add write to cull primitive output */
      unsigned component = (location - VARYING_SLOT_CULL_DIST0) * 4 +
                           nir_intrinsic_component(intr);

      b->cursor = nir_before_instr(&intr->instr);
      nir_def *offs = nir_imm_int(b, component / 4);
      nir_def *v = nir_b2f32(b, nir_fge_imm(b, intr->src[0].ssa, 0.0));

      nir_store_output(b, v, offs, .component = component % 4,
                       .src_type = nir_type_float32,
                       .io_semantics.location = VARYING_SLOT_CULL_PRIMITIVE,
                       .io_semantics.num_slots = 2);
      return true;
   }

   return false;
}

static bool
msl_nir_lower_clip_cull_distance_vs(nir_shader *s)
{
   if (s->info.clip_distance_array_size == 0 &&
       s->info.cull_distance_array_size == 0)
      return false;

   nir_shader_intrinsics_pass(s, lower_clip_cull_distance_write,
                              nir_metadata_control_flow, NULL);

   if (s->info.cull_distance_array_size > 0)
      s->info.outputs_written |=
         BITFIELD64_RANGE(VARYING_SLOT_CULL_PRIMITIVE,
                          DIV_ROUND_UP(s->info.cull_distance_array_size, 4));

   return true;
}

static bool
msl_nir_lower_cull_distance_fs(nir_shader *s, unsigned nr_distances)
{
   assert(s->info.stage == MESA_SHADER_FRAGMENT);

   if (nr_distances == 0)
      return false;

   nir_builder b_ =
      nir_builder_at(nir_before_impl(nir_shader_get_entrypoint(s)));
   nir_builder *b = &b_;

   /* Test each half-space */
   nir_def *culled = nir_imm_false(b);

   for (unsigned i = 0; i < nr_distances; ++i) {
      /* Load the cull primitive input for this cull distance */
      nir_def *baryc = nir_load_barycentric_pixel(
         b, 32, .interp_mode = INTERP_MODE_NOPERSPECTIVE);
      nir_def *cull = nir_load_interpolated_input(
         b, 1, 32, baryc, nir_imm_int(b, 0), .component = i & 3,
         .io_semantics.location = VARYING_SLOT_CULL_PRIMITIVE + (i / 4),
         .io_semantics.num_slots = nr_distances / 4);

      /* When the cull distance is negative in the vertex shader, the resulting
       * cull primitive output is zero, otherwise it is one. Thus, the
       * interpolated value will be zero only if all of its vertices had
       * negative cull distances, indicating the primitive should be called.
       * Note that, since the value is interpolated at the pixel center, we
       * don't have to worry about corner values. */
      culled = nir_ior(b, culled, nir_ball(b, nir_feq_imm(b, cull, 0)));
   }

   /* Emulate primitive culling by discarding fragments */
   nir_demote_if(b, culled);

   s->info.inputs_read |= BITFIELD64_RANGE(VARYING_SLOT_CULL_PRIMITIVE,
                                           DIV_ROUND_UP(nr_distances, 4));

   s->info.fs.uses_discard = true;
   return nir_progress(true, b->impl, nir_metadata_control_flow);
}

/* Scalarize stores to CLIP_DIST* varyings */
static bool
scalarize_clip_cull_distance_filter(const nir_intrinsic_instr *intrin,
                                    UNUSED const void *_data)
{
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;
   nir_io_semantics semantics = nir_intrinsic_io_semantics(intrin);
   return semantics.location == VARYING_SLOT_CLIP_DIST0 ||
          semantics.location == VARYING_SLOT_CLIP_DIST1 ||
          semantics.location == VARYING_SLOT_CULL_DIST0 ||
          semantics.location == VARYING_SLOT_CULL_DIST1;
}

void
msl_nir_lower_clip_cull_distance(nir_shader *nir, unsigned num_cull_distances)
{
   NIR_PASS(_, nir, nir_lower_io_to_scalar, nir_var_shader_out,
            scalarize_clip_cull_distance_filter, NULL);
   NIR_PASS(_, nir, nir_separate_merged_clip_cull_io);
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, msl_nir_lower_cull_distance_fs, num_cull_distances);
   else
      NIR_PASS(_, nir, msl_nir_lower_clip_cull_distance_vs);
}

static bool
lower_instance_id(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_instance_id)
      return false;

   b->cursor = nir_after_instr(&intr->instr);
   nir_def *base_instance = nir_load_base_instance(b);
   nir_def *instance_id = nir_isub(b, &intr->def, base_instance);
   nir_def_rewrite_uses_after(&intr->def, instance_id);
   BITSET_SET(b->shader->info.system_values_read, SYSTEM_VALUE_BASE_INSTANCE);

   return true;
}

bool
msl_nir_lower_instance_id(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_instance_id,
                                     nir_metadata_control_flow, NULL);
}

static void
collect_viewport_z_transform_data(nir_shader *s,
                                  nir_intrinsic_instr **pos_store,
                                  nir_intrinsic_instr **viewport_idx_store)
{
   /* Fetch necessary stores from the last block */
   nir_foreach_instr(instr, nir_impl_last_block(nir_shader_get_entrypoint(s))) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      if (intr->intrinsic != nir_intrinsic_store_output)
         continue;

      switch (nir_intrinsic_io_semantics(intr).location) {
      case VARYING_SLOT_POS:
         *pos_store = intr;
         break;
      case VARYING_SLOT_VIEWPORT:
         *viewport_idx_store = intr;
         break;
      default:
         break;
      }
   }
}

/* Inserts manual viewport Z transform into the vertex stage to aid in
 * supporting disabling both depth clamp and depth clip simultaneously,
 * by configuring the hardware viewport Z range to [0, 1].
 *
 * Expects I/O to be sunk to the last block and phis to be lowered after.
 */
bool
msl_nir_lower_vs_disabled_depth_clamp_clip(nir_shader *s)
{
   /* Hardware vertex stage */
   assert(s->info.stage == MESA_SHADER_VERTEX ||
          s->info.stage == MESA_SHADER_GEOMETRY ||
          s->info.stage == MESA_SHADER_TESS_EVAL);

   /* Collect outputs from shader to use in transform */
   nir_intrinsic_instr *pos_store = NULL;
   nir_intrinsic_instr *viewport_idx_store = NULL;
   collect_viewport_z_transform_data(s, &pos_store, &viewport_idx_store);
   assert(pos_store && "missing vs pos store");

   /* End of entrypoint to be after both position and viewport index stores */
   nir_builder b_ =
      nir_builder_at(nir_after_impl(nir_shader_get_entrypoint(s)));
   nir_builder *b = &b_;

   nir_def *pos = pos_store->src[0].ssa;
   nir_def *pos_emulated = NULL;

   nir_def *emulated = nir_load_is_viewport_z_transform_emulated_kk(b);
   nir_if *transform_if = nir_push_if(b, emulated);
   {
      /* Load the viewport index if applicable */
      nir_def *viewport_idx = viewport_idx_store
                                 ? viewport_idx_store->src[0].ssa
                                 : nir_imm_int(b, 0u);

      /* Load the correct depth clamp range */
      nir_def *zrange = nir_load_viewport_z_range_kk(b, 2, 32, viewport_idx);
      nir_def *zmin = nir_channel(b, zrange, 0);
      nir_def *zmax = nir_channel(b, zrange, 1);
      nir_def *zscale = nir_fsub(b, zmax, zmin);

      /* Manually perform viewport Z transform. This is safe to do since this
       * emulation assumes clip is disabled.
       *
       * `z * zscale + w * zmin`, when translated to NDC coordinates after the
       * vertex stage, will result in the intended `(z / w) * zscale + zmin`.
       */
      nir_def *z = nir_channel(b, pos, 2);
      nir_def *w = nir_channel(b, pos, 3);
      nir_def *transformed =
         nir_fadd(b, nir_fmul(b, z, zscale), nir_fmul(b, w, zmin));

      pos_emulated = nir_vector_insert_imm(b, pos, transformed, 2);
   }
   nir_pop_if(b, transform_if);

   /* Construct a phi with the emulated result and replace the store. We can't
    * use nir_def_rewrite_uses_after on phis */
   nir_def *phi = nir_if_phi(b, pos_emulated, pos);
   nir_store_output(b, phi, nir_imm_int(b, 0),
                    .io_semantics.location = VARYING_SLOT_POS,
                    .src_type = nir_type_float32);
   nir_instr_remove(&pos_store->instr);

   return nir_progress(true, b->impl, nir_metadata_none);
}

static void
collect_depth_clamp_data(nir_shader *s, nir_intrinsic_instr **depth_store)
{
   /* Fetch necessary stores. May not be in the last block, for example if a
    * missing depth write is inserted at the start */
   nir_foreach_block(block, nir_shader_get_entrypoint(s)) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_store_output ||
             nir_intrinsic_io_semantics(intr).location != FRAG_RESULT_DEPTH)
            continue;

         *depth_store = intr;
         break;
      }
   }
}

/* Inserts manual depth clamping into the fragment stage to aid in supporting
 * enabling both depth clamp and depth clip simultaneously, using hardware
 * to perform clipping.
 *
 * Expects only a single depth write and phis to be lowered after.
 */
bool
msl_nir_lower_fs_combined_depth_clamp_clip(nir_shader *s)
{
   assert(s->info.stage == MESA_SHADER_FRAGMENT);

   /* If the shader does not write depth, there is nothing to clamp, since we
    * only apply it on top of native depth clipping. Anything outside the range
    * [0, w] would be clipped, and anything inside would come out of the
    * viewport transform in the range [z_min, z_max], making clamping a no-op.
    * Thus, clamping only matters for fragment depth writes.
    *
    * This also ensures the emulation does not conflict with forced early
    * fragment tests, which disallows depth writes. */
   if (!(s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH)))
      return false;

   /* Collect outputs from shader to use in transform */
   nir_intrinsic_instr *depth_store = NULL;
   collect_depth_clamp_data(s, &depth_store);
   assert(depth_store && "missing fs depth store");

   nir_builder b_ = nir_builder_at(nir_before_instr(&depth_store->instr));
   nir_builder *b = &b_;

   nir_def *depth = depth_store->src[0].ssa;
   nir_def *depth_emulated = NULL;

   nir_def *emulated = nir_load_is_depth_clamp_emulated_kk(b);
   nir_if *clamp_if = nir_push_if(b, emulated);
   {
      /* Load the depth clamp range for the correct viewport */
      nir_def *viewport_idx = nir_load_input(
         b, 1, 32, nir_imm_int(b, 0), .dest_type = nir_type_uint32,
         .io_semantics.location = VARYING_SLOT_VIEWPORT);
      nir_def *zrange = nir_load_viewport_z_range_kk(b, 2, 32, viewport_idx);
      nir_def *zmin = nir_channel(b, zrange, 0);
      nir_def *zmax = nir_channel(b, zrange, 1);

      /* Manually clamp the output depth to the provided range */
      depth_emulated = nir_fclamp(b, depth, zmin, zmax);
   }
   nir_pop_if(b, clamp_if);

   /* Construct a phi with the emulated result and replace the store. We can't
    * use nir_def_rewrite_uses_after on phis */
   nir_def *phi = nir_if_phi(b, depth_emulated, depth);
   nir_store_output(b, phi, nir_imm_int(b, 0),
                    .io_semantics.location = FRAG_RESULT_DEPTH,
                    .src_type = nir_type_float32);
   nir_instr_remove(&depth_store->instr);

   return nir_progress(true, b->impl, nir_metadata_none);
}
