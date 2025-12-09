/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_pvfio.c
 *
 * \brief PCO NIR per-vertex/fragment input/output passes.
 */

#include "compiler/glsl_types.h"
#include "compiler/shader_enums.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_format_convert.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"
#include "util/u_dynarray.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/** Per-fragment output pass state. */
struct pfo_state {
   struct util_dynarray loads; /** List of fragment loads. */
   struct util_dynarray stores; /** List of fragment stores. */

   /* Src for depth feedback (NULL if unused). */
   nir_def *depth_feedback_src;

   nir_def *discard_cond_reg;
   bool has_discards;

   nir_intrinsic_instr *last_discard_store;

   bool has_sample_check;

   /* nir_instr *terminate; */

   pco_fs_data *fs; /** Fragment-specific data. */
};

/** Per-vertex input pass state. */
struct pvi_state {
   nir_def *attribs[MAX_VERTEX_GENERIC_ATTRIBS]; /** Loaded vertex attribs. */
   pco_vs_data *vs; /** Vertex-specific data. */
};

/**
 * \brief Returns the GLSL base type equivalent of a pipe format.
 *
 * \param[in] format Pipe format.
 * \return The GLSL base type, or GLSL_TYPE_ERROR if unsupported/invalid.
 */
static inline enum glsl_base_type base_type_from_fmt(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   int chan = util_format_get_first_non_void_channel(format);
   if (chan < 0)
      return GLSL_TYPE_ERROR;

   switch (desc->channel[chan].type) {
   case UTIL_FORMAT_TYPE_UNSIGNED:
      return GLSL_TYPE_UINT;

   case UTIL_FORMAT_TYPE_SIGNED:
      return GLSL_TYPE_INT;

   case UTIL_FORMAT_TYPE_FLOAT:
      return GLSL_TYPE_FLOAT;

   default:
      break;
   }

   return GLSL_TYPE_ERROR;
}

static enum pipe_format
to_pbe_format(nir_builder *b, enum pipe_format format, nir_def **input)
{
   switch (format) {
   case PIPE_FORMAT_B5G6R5_UNORM:
      return PIPE_FORMAT_R8G8B8_UNORM;

   case PIPE_FORMAT_A4R4G4B4_UNORM:
   case PIPE_FORMAT_B5G5R5A1_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
      return PIPE_FORMAT_R8G8B8A8_UNORM;

   case PIPE_FORMAT_R8G8B8A8_SRGB:
   case PIPE_FORMAT_B8G8R8A8_SRGB:
      if (input)
         *input = nir_fsat(b, *input);
      FALLTHROUGH;

   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return PIPE_FORMAT_R16G16B16A16_FLOAT;

   case PIPE_FORMAT_R11G11B10_FLOAT:
      return PIPE_FORMAT_R16G16B16_FLOAT;

   /* For loadops. */
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
   case PIPE_FORMAT_Z16_UNORM:
   case PIPE_FORMAT_Z24X8_UNORM:
      assert(b->shader->info.internal);
      return PIPE_FORMAT_R32_FLOAT;

   default:
      break;
   }

   return format;
}

static unsigned format_chans_per_dword(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   unsigned accum_bits = 0;

   for (unsigned u = 0; u < desc->nr_channels; ++u) {
      /* Exactly one dword, great! */
      if (accum_bits == 32)
         return u;

      /* Went over, back off by one. */
      if (accum_bits > 32) {
         /* We don't support formats with channels > 1 dword. */
         assert(u > 1);
         return u - 1;
      }

      accum_bits += desc->channel[u].size;
   }

   /* Loop finished, all channels can fit. */
   return desc->nr_channels;
}

static nir_def *pack_to_format(nir_builder *b,
                               nir_def *input,
                               nir_alu_type src_type,
                               enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *one = nir_alu_type_get_base_type(src_type) == nir_type_float
                     ? nir_imm_float(b, 1.0f)
                     : nir_imm_int(b, 1);

   nir_def *input_comps[4];
   /* Populate any missing components .*/
   for (unsigned u = 0; u < ARRAY_SIZE(input_comps); ++u) {
      enum pipe_swizzle s = desc->swizzle[u];

      if (s <= PIPE_SWIZZLE_W)
         input_comps[u] = nir_channel(b, input, s);
      else if (s == PIPE_SWIZZLE_0)
         input_comps[u] = zero;
      else if (s == PIPE_SWIZZLE_1)
         input_comps[u] = one;
      else
         UNREACHABLE("");
   }

   unsigned format_bits = util_format_get_blocksizebits(format);
   unsigned format_dwords = DIV_ROUND_UP(format_bits, 32);
   nir_def *packed_comps[] = { zero, zero, zero, zero };

   /* Special case: no packing required. */
   if (util_format_get_max_channel_size(format) == 32)
      return nir_vec(b, input_comps, format_dwords);

   /* Special case: can't be packed with op, need bit-packing instead. */
   if (util_format_is_pure_integer(format)) {
      for (unsigned u = 0; u < desc->nr_channels; ++u) {
         unsigned dword = desc->channel[u].shift / 32;
         unsigned offset = desc->channel[u].shift % 32;
         unsigned size = desc->channel[u].size;

         packed_comps[dword] = nir_bitfield_insert_imm(b,
                                                       packed_comps[dword],
                                                       input_comps[u],
                                                       offset,
                                                       size);
      }

      return nir_vec(b, packed_comps, format_dwords);
   }

   unsigned chans_per_dword = format_chans_per_dword(format);
   unsigned chans_remaining = desc->nr_channels;
   input = nir_vec(b, input_comps, desc->nr_channels);
   for (unsigned u = 0; u < format_dwords; ++u) {
      unsigned chans_to_pack =
         chans_remaining > chans_per_dword ? chans_per_dword : chans_remaining;
      unsigned chans_packed = desc->nr_channels - chans_remaining;

      nir_def *input_chans =
         nir_channels(b, input, BITFIELD_RANGE(chans_packed, chans_to_pack));
      packed_comps[u] = nir_pack_pco(b, input_chans, .format = format);

      chans_remaining -= chans_to_pack;
   }

   assert(!chans_remaining);
   return nir_vec(b, packed_comps, format_dwords);
}

static nir_def *unpack_from_format(nir_builder *b,
                                   nir_def *packed_comps[static 4],
                                   nir_alu_type dest_type,
                                   enum pipe_format format,
                                   unsigned components_needed)
{
   const struct util_format_description *desc = util_format_description(format);

   nir_def *unpacked_comps[4];

   unsigned format_bits = util_format_get_blocksizebits(format);
   unsigned format_dwords = DIV_ROUND_UP(format_bits, 32);

   /* Special case: no unpacking required. */
   if (util_format_get_max_channel_size(format) == 32) {
      for (unsigned u = 0; u < desc->nr_channels; ++u)
         unpacked_comps[u] = packed_comps[u];
   }

   /* Special case: can't be unpacked with op, need bit-unpacking instead. */
   else if (util_format_is_pure_integer(format)) {
      nir_def *(*nir_bitfield_extract_imm)(nir_builder *,
                                           nir_def *,
                                           uint32_t,
                                           uint32_t) =
         util_format_is_pure_uint(format) ? nir_ubitfield_extract_imm
                                          : nir_ibitfield_extract_imm;

      for (unsigned u = 0; u < desc->nr_channels; ++u) {
         unsigned dword = desc->channel[u].shift / 32;
         unsigned offset = desc->channel[u].shift % 32;
         unsigned size = desc->channel[u].size;

         unpacked_comps[u] =
            nir_bitfield_extract_imm(b, packed_comps[dword], offset, size);
      }
   }

   else {
      unsigned chans_per_dword = format_chans_per_dword(format);
      unsigned chans_remaining = desc->nr_channels;

      for (unsigned u = 0; u < format_dwords; ++u) {
         unsigned chans_to_unpack = chans_remaining > chans_per_dword
                                       ? chans_per_dword
                                       : chans_remaining;

         nir_def *unpacked = nir_unpack_pco(b,
                                            chans_to_unpack,
                                            packed_comps[u],
                                            .format = format);

         unsigned chans_unpacked = desc->nr_channels - chans_remaining;
         for (unsigned v = 0; v < chans_to_unpack; ++v)
            unpacked_comps[chans_unpacked + v] = nir_channel(b, unpacked, v);

         chans_remaining -= chans_to_unpack;
      }

      assert(!chans_remaining);
   }

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *one = nir_alu_type_get_base_type(dest_type) == nir_type_float
                     ? nir_imm_float(b, 1.0f)
                     : nir_imm_int(b, 1);

   nir_def *output_comps[4];
   /* Populate any missing components .*/
   for (unsigned u = 0; u < ARRAY_SIZE(output_comps); ++u) {
      enum pipe_swizzle s = desc->swizzle[u];

      if (s <= PIPE_SWIZZLE_W)
         output_comps[u] = unpacked_comps[s];
      else if (s == PIPE_SWIZZLE_0)
         output_comps[u] = zero;
      else if (s == PIPE_SWIZZLE_1)
         output_comps[u] = one;
      else
         UNREACHABLE("");
   }

   return nir_vec(b, output_comps, components_needed);
}

static inline bool is_processed(nir_intrinsic_instr *intr)
{
   nir_alu_type type;

   if (nir_intrinsic_has_src_type(intr))
      type = nir_intrinsic_src_type(intr);
   else if (nir_intrinsic_has_dest_type(intr))
      type = nir_intrinsic_dest_type(intr);
   else
      return true;

   return nir_alu_type_get_base_type(type) == nir_type_invalid;
}

static nir_def *lower_pfo_store(nir_builder *b,
                                nir_intrinsic_instr *intr,
                                struct pfo_state *state)
{
   /* Skip stores we've already processed. */
   if (is_processed(intr)) {
      util_dynarray_append(&state->stores, intr);
      return NULL;
   }

   nir_def *input = intr->src[0].ssa;
   nir_src *offset = &intr->src[1];
   assert(nir_src_as_uint(*offset) == 0);

   ASSERTED unsigned bit_size = input->bit_size;
   assert(bit_size == 32);

   unsigned component = nir_intrinsic_component(intr);
   assert(!component);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   gl_frag_result location = io_semantics.location;

   b->cursor = nir_before_instr(&intr->instr);

   enum pipe_format format = state->fs->output_formats[location];
   if (format == PIPE_FORMAT_NONE)
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   format = to_pbe_format(b, format, &input);

   nir_alu_type src_type = nir_intrinsic_src_type(intr);
   nir_def *output = pack_to_format(b, input, src_type, format);

   /* Emit and track the new store. */
   nir_intrinsic_instr *store =
      nir_store_output(b,
                       output,
                       offset->ssa,
                       .base = nir_intrinsic_base(intr),
                       .write_mask = BITFIELD_MASK(output->num_components),
                       .src_type = nir_type_invalid | 32,
                       .component = component,
                       .io_semantics = io_semantics,
                       .io_xfb = nir_intrinsic_io_xfb(intr),
                       .io_xfb2 = nir_intrinsic_io_xfb2(intr));

   util_dynarray_append(&state->stores, store);

   /* Update the type of the stored variable. */
   nir_variable *var =
      nir_find_variable_with_location(b->shader, nir_var_shader_out, location);
   assert(var);
   var->type = glsl_uvec_type(output->num_components);

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

static nir_def *lower_pfo_load(nir_builder *b,
                               nir_intrinsic_instr *intr,
                               struct pfo_state *state)
{
   /* Skip loads we've already processed. */
   if (is_processed(intr)) {
      util_dynarray_append(&state->loads, intr);
      return NULL;
   }

   unsigned base = nir_intrinsic_base(intr);

   nir_src *offset = &intr->src[0];
   assert(nir_src_as_uint(*offset) == 0);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   gl_frag_result location = io_semantics.location;

   enum pipe_format format;
   /* Special case for input attachments. */
   if (location == FRAG_RESULT_COLOR) {
      format = state->fs->ia_formats[base];
   } else {
      assert(location >= FRAG_RESULT_DATA0);
      assert(!base);
      format = state->fs->output_formats[location];
   }

   if (format == PIPE_FORMAT_NONE)
      return nir_undef(b, intr->def.num_components, intr->def.bit_size);

   format = to_pbe_format(b, format, NULL);

   nir_def *packed_comps[4];
   for (unsigned c = 0; c < ARRAY_SIZE(packed_comps); ++c) {
      packed_comps[c] = nir_load_output(b,
                                        1,
                                        32,
                                        offset->ssa,
                                        .base = base,
                                        .component = c,
                                        .dest_type = nir_type_invalid | 32,
                                        .io_semantics = io_semantics);

      nir_intrinsic_instr *load =
         nir_def_as_intrinsic(packed_comps[c]);

      util_dynarray_append(&state->loads, load);
   }

   nir_alu_type dest_type = nir_intrinsic_dest_type(intr);
   return unpack_from_format(b,
                             packed_comps,
                             dest_type,
                             format,
                             intr->def.num_components);
}

/**
 * \brief Filters PFO-related instructions.
 *
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return True if the instruction matches the filter.
 */
static bool is_pfo(const nir_instr *instr, UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_load_output:
   case nir_intrinsic_demote:
   case nir_intrinsic_demote_if:
      return true;

   default:
      break;
   }

   return false;
}

/**
 * \brief Lowers a PFO-related instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return The replacement/lowered def.
 */
static nir_def *lower_pfo(nir_builder *b, nir_instr *instr, void *cb_data)
{
   struct pfo_state *state = cb_data;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_store_output: {
      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      if (sem.location >= FRAG_RESULT_DATA0)
         return lower_pfo_store(b, intr, state);

      if (sem.location == FRAG_RESULT_DEPTH) {
         assert(!state->depth_feedback_src);
         state->depth_feedback_src = nir_fsat(b, intr->src[0].ssa);

         return NIR_LOWER_INSTR_PROGRESS_REPLACE;
      }

      if (sem.location == FRAG_RESULT_SAMPLE_MASK) {
         nir_def *smp_msk =
            nir_ishl(b, nir_imm_int(b, 1), nir_load_sample_id(b));

         smp_msk = nir_iand(b, smp_msk, nir_load_sample_mask_in(b));
         smp_msk = nir_iand(b, smp_msk, intr->src[0].ssa);
         nir_def *cond = nir_ieq_imm(b, smp_msk, 0);

         state->has_discards = true;
         state->has_sample_check = true;
         nir_def *val = nir_load_reg(b, state->discard_cond_reg);
         val = nir_ior(b, val, cond);
         state->last_discard_store =
            nir_build_store_reg(b, val, state->discard_cond_reg);
         return NIR_LOWER_INSTR_PROGRESS_REPLACE;
      }

      UNREACHABLE("");
   }

   case nir_intrinsic_load_output:
      return lower_pfo_load(b, intr, state);

   case nir_intrinsic_demote:
      state->has_discards = true;
      state->last_discard_store =
         nir_build_store_reg(b, nir_imm_true(b), state->discard_cond_reg);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   case nir_intrinsic_demote_if: {
      state->has_discards = true;
      nir_def *val = nir_load_reg(b, state->discard_cond_reg);
      val = nir_ior(b, val, intr->src[0].ssa);
      state->last_discard_store =
         nir_build_store_reg(b, val, state->discard_cond_reg);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }

   default:
      break;
   }

   return false;
}

static bool lower_isp_fb(nir_builder *b, struct pfo_state *state)
{
   if (b->shader->info.internal)
      return false;

   bool has_depth_feedback = !!state->depth_feedback_src;
   if (b->shader->info.writes_memory && !has_depth_feedback) {
      nir_variable *var_pos = nir_get_variable_with_location(b->shader,
                                                             nir_var_shader_in,
                                                             VARYING_SLOT_POS,
                                                             glsl_vec4_type());
      var_pos->data.interpolation = INTERP_MODE_NOPERSPECTIVE;

      b->cursor = nir_before_block(
         nir_start_block(nir_shader_get_entrypoint(b->shader)));

      state->depth_feedback_src =
         nir_load_input(b,
                        1,
                        32,
                        nir_imm_int(b, 0),
                        .component = 2,
                        .dest_type = nir_type_float32,
                        .io_semantics = (nir_io_semantics){
                           .location = VARYING_SLOT_POS,
                           .num_slots = 1,
                        });

      has_depth_feedback = true;
   }

   if (!state->has_discards) {
      b->cursor = nir_after_instr(&state->last_discard_store->instr);

      nir_def *smp_msk = nir_ishl(b, nir_imm_int(b, 1), nir_load_sample_id(b));
      smp_msk = nir_iand(b, smp_msk, nir_load_sample_mask_in(b));
      nir_def *cond = nir_ieq_imm(b, smp_msk, 0);

      nir_def *val = nir_load_reg(b, state->discard_cond_reg);
      val = nir_ior(b, val, cond);
      state->last_discard_store =
         nir_build_store_reg(b, val, state->discard_cond_reg);

      state->has_discards = true;
   }

   /* Insert isp feedback instruction before the first store,
    * or if there are no stores, at the end.
    */
   if (state->stores.size > 0)
      b->cursor = nir_before_instr(
         &(*(nir_intrinsic_instr **)util_dynarray_begin(&state->stores))->instr);
   else
      b->cursor = nir_after_block(
         nir_impl_last_block(nir_shader_get_entrypoint(b->shader)));

   nir_def *undef = nir_undef(b, 1, 32);

   nir_isp_feedback_pco(
      b,
      state->has_discards ? nir_i2b(b, nir_load_reg(b, state->discard_cond_reg))
                          : undef,
      has_depth_feedback ? state->depth_feedback_src : undef);

   state->fs->uses.discard = state->has_discards;
   state->fs->uses.depth_feedback = has_depth_feedback;

   return true;
}

static bool sink_outputs(nir_shader *shader, struct pfo_state *state)
{
   bool progress = false;

   nir_instr *after_instr = nir_block_last_instr(
      nir_impl_last_block(nir_shader_get_entrypoint(shader)));

   util_dynarray_foreach (&state->stores, nir_intrinsic_instr *, store) {
      nir_instr *instr = &(*store)->instr;

      progress |= nir_instr_move(nir_after_instr(after_instr), instr);
      after_instr = instr;
   }

   return progress;
}

static bool z_replicate(nir_shader *shader, struct pfo_state *state)
{
   if (shader->info.internal || state->fs->z_replicate == ~0u)
      return false;

   assert(!nir_find_variable_with_location(shader,
                                           nir_var_shader_out,
                                           state->fs->z_replicate));

   nir_create_variable_with_location(shader,
                                     nir_var_shader_out,
                                     state->fs->z_replicate,
                                     glsl_float_type());

   if (!state->depth_feedback_src) {
      nir_variable *var_pos = nir_get_variable_with_location(shader,
                                                             nir_var_shader_in,
                                                             VARYING_SLOT_POS,
                                                             glsl_vec4_type());
      var_pos->data.interpolation = INTERP_MODE_NOPERSPECTIVE;

      nir_builder b = nir_builder_at(
         nir_before_block(nir_start_block(nir_shader_get_entrypoint(shader))));

      state->depth_feedback_src =
         nir_load_input(&b,
                        1,
                        32,
                        nir_imm_int(&b, 0),
                        .component = 2,
                        .dest_type = nir_type_float32,
                        .io_semantics = (nir_io_semantics){
                           .location = VARYING_SLOT_POS,
                           .num_slots = 1,
                        });
   }

   nir_builder b = nir_builder_at(
      nir_after_block(nir_impl_last_block(nir_shader_get_entrypoint(shader))));
   nir_store_output(&b,
                    state->depth_feedback_src,
                    nir_imm_int(&b, 0),
                    .write_mask = 1,
                    .src_type = nir_type_invalid | 32,
                    .io_semantics = (nir_io_semantics){
                       .location = state->fs->z_replicate,
                       .num_slots = 1,
                    });

   return true;
}

static bool is_frag_color_out(const nir_instr *instr,
                              UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   gl_frag_result location = nir_intrinsic_io_semantics(intr).location;
   return location >= FRAG_RESULT_DATA0 && location < FRAG_RESULT_MAX;
}

static bool lower_demote_samples(nir_builder *b,
                                 nir_intrinsic_instr *intr,
                                 UNUSED void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_demote_samples)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *to_keep = nir_u2u32(b, nir_inot(b, intr->src[0].ssa));
   nir_def *sample_mask = nir_load_savmsk_vm_pco(b);
   nir_def *current_mask =
      nir_ishl(b, nir_imm_int(b, 1), nir_load_sample_id(b));
   nir_def *cond = nir_iand(b, to_keep, nir_iand(b, sample_mask, current_mask));
   nir_demote_if(b, nir_ieq_imm(b, cond, 0));

   nir_instr_remove(&intr->instr);

   return true;
}

bool pco_nir_lower_alpha_to_coverage(nir_shader *shader)
{
   if (shader->info.internal)
      return false;

   nir_builder b = nir_builder_create(nir_shader_get_entrypoint(shader));
   b.cursor =
      nir_before_block(nir_start_block(nir_shader_get_entrypoint(shader)));
   nir_def *a2c_enabled = nir_ine_imm(
      &b,
      nir_ubitfield_extract_imm(&b, nir_load_fs_meta_pco(&b), 25, 1),
      0);

   nir_lower_alpha_to_coverage(shader, 0, true, a2c_enabled);

   nir_shader_intrinsics_pass(shader,
                              lower_demote_samples,
                              nir_metadata_control_flow,
                              NULL);

   return true;
}

static nir_def *
lower_alpha_to_one(nir_builder *b, nir_instr *instr, UNUSED void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   nir_src *input_src = &intr->src[0];
   nir_def *input = input_src->ssa;
   nir_src *offset = &intr->src[1];
   assert(nir_src_as_uint(*offset) == 0);

   /* Skip color write that don't include alpha. */
   if (input->num_components != 4)
      return NULL;

   b->cursor = nir_before_instr(&intr->instr);

   /* TODO: define or other way of representing bit 0 of metadata... */
   nir_def *alpha_to_one_enabled =
      nir_ine_imm(b,
                  nir_ubitfield_extract_imm(b, nir_load_fs_meta_pco(b), 0, 1),
                  0);

   nir_def *alpha = nir_bcsel(b,
                              alpha_to_one_enabled,
                              nir_imm_float(b, 1.0f),
                              nir_channel(b, input, 3));

   nir_src_rewrite(input_src, nir_vector_insert_imm(b, input, alpha, 3));

   return NIR_LOWER_INSTR_PROGRESS;
}

static bool is_load_sample_mask(const nir_instr *instr,
                                UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   return intr->intrinsic == nir_intrinsic_load_sample_mask_in;
}

static nir_def *
lower_load_sample_mask(nir_builder *b, nir_instr *instr, UNUSED void *cb_data)
{
   b->cursor = nir_before_instr(instr);

   nir_def *smp_msk =
      nir_ubitfield_extract_imm(b, nir_load_fs_meta_pco(b), 9, 16);

   smp_msk = nir_iand(b, smp_msk, nir_load_savmsk_vm_pco(b));

   return smp_msk;
}

static nir_def *
lower_color_write_enable(nir_builder *b, nir_instr *instr, UNUSED void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   nir_src *input_src = &intr->src[0];
   nir_def *input = input_src->ssa;
   nir_def *offset = intr->src[1].ssa;

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   unsigned color_write_index = io_semantics.location - FRAG_RESULT_DATA0;
   io_semantics.fb_fetch_output = true;

   b->cursor = nir_before_instr(&intr->instr);

   /* TODO: nir op that returns bool based on whether a bit is set. */
   /* TODO: define for 1 */
   nir_def *color_write_enabled =
      nir_ine_imm(b,
                  nir_ubitfield_extract_imm(b,
                                            nir_load_fs_meta_pco(b),
                                            1 + color_write_index,
                                            1),
                  0);

   nir_def *prev_input =
      nir_load_output(b,
                      input->num_components,
                      input->bit_size,
                      offset,
                      .base = nir_intrinsic_base(intr),
                      .range = nir_intrinsic_range(intr),
                      .component = nir_intrinsic_component(intr),
                      .dest_type = nir_intrinsic_src_type(intr),
                      .io_semantics = io_semantics);

   nir_src_rewrite(input_src,
                   nir_bcsel(b, color_write_enabled, input, prev_input));

   return NIR_LOWER_INSTR_PROGRESS;
}

/**
 * \brief Per-fragment output pass.
 *
 * \param[in,out] shader NIR shader.
 * \param[in,out] fs Fragment shader-specific data.
 * \return True if the pass made progress.
 */
bool pco_nir_pfo(nir_shader *shader, pco_fs_data *fs)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   nir_builder b = nir_builder_create(nir_shader_get_entrypoint(shader));
   b.cursor =
      nir_before_block(nir_start_block(nir_shader_get_entrypoint(shader)));

   struct pfo_state state = {
      .fs = fs,
      .discard_cond_reg = nir_decl_reg(&b, 1, 1, 0),
   };
   state.last_discard_store =
      nir_build_store_reg(&b, nir_imm_false(&b), state.discard_cond_reg);

   state.loads = UTIL_DYNARRAY_INIT;
   state.stores = UTIL_DYNARRAY_INIT;

   bool progress = false;

   /* TODO: instead of doing multiple passes, probably better to just cache all
    * the stores
    */
   if (!shader->info.internal) {
      progress |= nir_shader_lower_instructions(shader,
                                                is_frag_color_out,
                                                lower_alpha_to_one,
                                                &state);
   }

   if (fs->meta_present.color_write_enable)
      progress |= nir_shader_lower_instructions(shader,
                                                is_frag_color_out,
                                                lower_color_write_enable,
                                                NULL);

   progress |= nir_shader_lower_instructions(shader, is_pfo, lower_pfo, &state);
   progress |= lower_isp_fb(&b, &state);

   progress |= sink_outputs(shader, &state);
   progress |= z_replicate(shader, &state);

   progress |= nir_shader_lower_instructions(shader,
                                             is_load_sample_mask,
                                             lower_load_sample_mask,
                                             NULL);

   util_dynarray_fini(&state.stores);
   util_dynarray_fini(&state.loads);

   return progress;
}

static nir_def *lower_pvi(nir_builder *b, nir_instr *instr, void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   struct pvi_state *state = cb_data;

   unsigned start_comp = nir_intrinsic_component(intr);
   unsigned num_comps = intr->def.num_components;

   ASSERTED nir_src *offset = &intr->src[0];
   assert(nir_src_as_uint(*offset) == 0);

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   gl_vert_attrib location = io_semantics.location;
   nir_def *attrib = state->attribs[location - VERT_ATTRIB_GENERIC0];
   assert(attrib);

   b->cursor = nir_before_instr(&intr->instr);
   return nir_channels(b, attrib, BITFIELD_RANGE(start_comp, num_comps));
}

static bool is_pvi(const nir_instr *instr, const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_input)
      return false;

   if (is_processed(intr))
      return false;

   ASSERTED gl_vert_attrib location = nir_intrinsic_io_semantics(intr).location;
   assert(location >= VERT_ATTRIB_GENERIC0 &&
          location <= VERT_ATTRIB_GENERIC15);

   return true;
}

/**
 * \brief Per-vertex input pass.
 *
 * \param[in,out] shader NIR shader.
 * \param[in,out] vs Vertex shader-specific data.
 * \return True if the pass made progress.
 */
bool pco_nir_pvi(nir_shader *shader, pco_vs_data *vs)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);

   struct pvi_state state = { .vs = vs };

   nir_builder b = nir_builder_at(
      nir_before_block(nir_start_block(nir_shader_get_entrypoint(shader))));
   for (unsigned u = 0; u < ARRAY_SIZE(state.attribs); ++u) {
      gl_vert_attrib location = u + VERT_ATTRIB_GENERIC0;
      enum pipe_format format = vs->attrib_formats[location];
      if (format == PIPE_FORMAT_NONE)
         continue;

      /* Update the type of the stored variable, remove any fractional vars. */
      nir_variable *var = NULL;
      nir_alu_type base_type = 0;
      nir_foreach_variable_with_modes_safe (iter_var,
                                            shader,
                                            nir_var_shader_in) {
         if (iter_var->data.location != location)
            continue;

         if (!base_type)
            base_type = nir_get_nir_type_for_glsl_type(iter_var->type);
#ifndef NDEBUG
         else
            assert(base_type == nir_get_nir_type_for_glsl_type(iter_var->type));
#endif /* NDEBUG */

         if (!iter_var->data.location_frac) {
            assert(!var);
            var = iter_var;
            continue;
         }

         exec_node_remove(&iter_var->node);
      }

      if (!var) {
         if (!base_type)
            continue;

         /* An attrib var was found but was fractional so we dropped it. */
         var = nir_variable_create(shader, nir_var_shader_in, NULL, NULL);
         var->data.location = location;
      }

      unsigned format_dwords =
         DIV_ROUND_UP(util_format_get_blocksize(format), sizeof(uint32_t));
      var->type = glsl_uvec_type(format_dwords);

      nir_def *packed_comps[4];
      for (unsigned c = 0; c < ARRAY_SIZE(packed_comps); ++c) {
         packed_comps[c] = nir_load_input(&b,
                                          1,
                                          32,
                                          nir_imm_int(&b, 0),
                                          .range = 1,
                                          .component = c,
                                          .dest_type = nir_type_invalid | 32,
                                          .io_semantics = (nir_io_semantics){
                                             .location = location,
                                             .num_slots = 1,
                                          });
      }

      state.attribs[u] =
         unpack_from_format(&b, packed_comps, base_type, format, 4);
   }

   nir_shader_lower_instructions(shader, is_pvi, lower_pvi, &state);

   return true;
}

/**
 * \brief Checks if the point size is written.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in] cb_data User callback data.
 * \return True if the instruction was lowered.
 */
static bool
check_psiz_write(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   bool *writes_psiz = cb_data;

   struct nir_io_semantics io_semantics = nir_intrinsic_io_semantics(intr);
   *writes_psiz |= (io_semantics.location == VARYING_SLOT_PSIZ);

   return false;
}

/**
 * \brief Vertex shader point size pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_point_size(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);
   if (shader->info.internal)
      return false;

   bool writes_psiz = false;
   nir_shader_intrinsics_pass(shader,
                              check_psiz_write,
                              nir_metadata_all,
                              &writes_psiz);

   /* Nothing to do if the shader already writes the point size. */
   if (writes_psiz)
      return false;

   /* Create a point size variable if there isn't one. */
   nir_get_variable_with_location(shader,
                                  nir_var_shader_out,
                                  VARYING_SLOT_PSIZ,
                                  glsl_float_type());

   /* Add a point size write. */
   nir_builder b = nir_builder_at(
      nir_after_block(nir_impl_last_block(nir_shader_get_entrypoint(shader))));

   nir_store_output(&b,
                    nir_imm_float(&b, PVR_POINT_SIZE_RANGE_MIN),
                    nir_imm_int(&b, 0),
                    .base = 0,
                    .range = 1,
                    .write_mask = 1,
                    .component = 0,
                    .src_type = nir_type_float32,
                    .io_semantics = (nir_io_semantics){
                       .location = VARYING_SLOT_PSIZ,
                       .num_slots = 1,
                    });

   return true;
}

static bool is_fs_intr(const nir_instr *instr, UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_front_face:
      return true;

   default:
      break;
   }

   return false;
}

static nir_def *lower_front_face(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *face_ccw = nir_load_face_ccw_pco(b);
   nir_def *front_face = nir_ieq_imm(b, face_ccw, 0);

   nir_def *ff_op = nir_load_front_face_op_pco(b);
   nir_def *ff_elems[] = {
      [PCO_FRONT_FACE_OP_NOP] = front_face,
      [PCO_FRONT_FACE_OP_SWAP] = nir_inot(b, front_face),
      [PCO_FRONT_FACE_OP_TRUE] = nir_imm_true(b),
   };

   return nir_select_from_ssa_def_array(b,
                                        ff_elems,
                                        ARRAY_SIZE(ff_elems),
                                        ff_op);
}

static nir_def *
lower_fs_intr(nir_builder *b, nir_instr *instr, UNUSED void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_front_face:
      return lower_front_face(b, intr);

   default:
      break;
   }

   UNREACHABLE("");
}

bool pco_nir_lower_fs_intrinsics(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   return nir_shader_lower_instructions(shader, is_fs_intr, lower_fs_intr, NULL);
}

static bool
lower_vs_intr(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *cb_data)
{
   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   /* First vs base vertex is handled in the PDS, so they're equivalent. */
   case nir_intrinsic_load_first_vertex:
      nir_def_replace(&intr->def, nir_load_base_vertex(b));
      nir_instr_free(&intr->instr);
      return true;

   default:
      break;
   }

   return false;
}

bool pco_nir_lower_vs_intrinsics(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX);

   return nir_shader_intrinsics_pass(shader,
                                     lower_vs_intr,
                                     nir_metadata_control_flow,
                                     NULL);
}

bool pco_nir_lower_clip_cull_vars(nir_shader *shader)
{
   if (shader->info.internal)
      return false;

   unsigned clip_cull_comps = shader->info.clip_distance_array_size +
                              shader->info.cull_distance_array_size;
   if (!clip_cull_comps)
      return false;

   /* Remove the old variables. */
   const gl_varying_slot clip_cull_locations[] = {
      VARYING_SLOT_CLIP_DIST0,
      VARYING_SLOT_CLIP_DIST1,
   };

   nir_variable *var;
   for (unsigned u = 0; u < ARRAY_SIZE(clip_cull_locations); ++u) {
      gl_varying_slot location = clip_cull_locations[u];
      while ((var = nir_find_variable_with_location(shader,
                                                    nir_var_shader_out,
                                                    location))) {
         exec_node_remove(&var->node);
      }
   }

   /* Create new variables. */
   nir_create_variable_with_location(shader,
                                     nir_var_shader_out,
                                     VARYING_SLOT_CLIP_DIST0,
                                     glsl_vec_type(MIN2(clip_cull_comps, 4)));

   if (clip_cull_comps > 4) {
      nir_create_variable_with_location(shader,
                                        nir_var_shader_out,
                                        VARYING_SLOT_CLIP_DIST1,
                                        glsl_vec_type(clip_cull_comps - 4));
   }

   nir_metadata_invalidate(shader);

   return true;
}

static bool
clone_clip_cull_stores(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   if (deref->deref_type != nir_deref_type_array)
      return false;

   nir_variable *var = nir_deref_instr_get_variable(deref);
   if (var->data.location != VARYING_SLOT_CLIP_DIST0 &&
       var->data.location != VARYING_SLOT_CLIP_DIST1)
      return false;

   b->cursor = nir_after_instr(&intr->instr);

   unsigned var_index = var->data.location - VARYING_SLOT_CLIP_DIST0;
   nir_def *index =
      nir_iadd_imm(b, deref->arr.index.ssa, var->data.location_frac);
   index = nir_iadd_imm(b, index, var_index * 4);

   nir_variable *clone_var = data;
   nir_store_array_var(b, clone_var, index, intr->src[1].ssa, 1);

   return true;
}

static bool is_clip_cull_load(const nir_instr *instr,
                              UNUSED const void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   if (deref->deref_type != nir_deref_type_array)
      return false;

   nir_variable *var = nir_deref_instr_get_variable(deref);

   return var->data.location == VARYING_SLOT_CLIP_DIST0 ||
          var->data.location == VARYING_SLOT_CLIP_DIST1;
}

static nir_def *
swap_clip_cull_load(nir_builder *b, nir_instr *instr, void *cb_data)
{
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   unsigned var_index = var->data.location - VARYING_SLOT_CLIP_DIST0;
   nir_def *index =
      nir_iadd_imm(b, deref->arr.index.ssa, var->data.location_frac);
   index = nir_iadd_imm(b, index, var_index * 4);

   nir_variable *clone_var = cb_data;
   return nir_load_array_var(b, clone_var, index);
}

bool pco_nir_link_clip_cull_vars(nir_shader *producer, nir_shader *consumer)
{
   if (producer->info.stage != MESA_SHADER_VERTEX ||
       consumer->info.stage != MESA_SHADER_FRAGMENT) {
      return false;
   }

   unsigned clip_cull_comps = consumer->info.clip_distance_array_size +
                              consumer->info.cull_distance_array_size;
   /* Skip if clip/cull comps aren't actually consumed. */
   if (!clip_cull_comps)
      return false;

   const glsl_type *clone_var_type =
      glsl_array_type(glsl_float_type(), clip_cull_comps, 0);

   /* Find unused varying slot to use and create the variables. */
   gl_varying_slot clone_slot = VARYING_SLOT_VAR0;
   nir_foreach_shader_out_variable (var, producer) {
      clone_slot = MAX2(clone_slot, var->data.location + 1);
   }
   assert(clone_slot < VARYING_SLOT_MAX);

   nir_variable *clone_var =
      nir_variable_create(producer, nir_var_shader_out, clone_var_type, NULL);
   clone_var->data.location = clone_slot;

   nir_shader_intrinsics_pass(producer,
                              clone_clip_cull_stores,
                              nir_metadata_block_index | nir_metadata_dominance,
                              clone_var);

   clone_var =
      nir_variable_create(consumer, nir_var_shader_in, clone_var_type, NULL);
   clone_var->data.location = clone_slot;

   nir_shader_lower_instructions(consumer,
                                 is_clip_cull_load,
                                 swap_clip_cull_load,
                                 clone_var);

   return true;
}

static nir_def *alu_iter(nir_builder *b,
                         nir_def *coords,
                         unsigned component,
                         struct nir_io_semantics io_semantics)
{
   nir_def *coeffs = nir_load_fs_coeffs_pco(b,
                                            .component = component,
                                            .io_semantics = io_semantics);

   nir_def *result = nir_ffma(b,
                              nir_channel(b, coeffs, 1),
                              nir_channel(b, coords, 1),
                              nir_channel(b, coeffs, 2));
   result =
      nir_ffma(b, nir_channel(b, coeffs, 0), nir_channel(b, coords, 0), result);

   return result;
}

static bool
lower_sample_pos(nir_builder *b, nir_intrinsic_instr *intr, pco_fs_data *fs)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_def *msaa_samples = nir_bit_count(
      b,
      nir_u2u32(b, nir_alpha_to_coverage(b, nir_imm_float(b, 1.0f))));

   nir_def *sample_id = intr->intrinsic == nir_intrinsic_load_sample_pos
                           ? nir_load_sample_id(b)
                           : intr->src[0].ssa;

   nir_def *dword_index =
      nir_ishr_imm(b, nir_iadd(b, msaa_samples, sample_id), 2);

   nir_def *packed_sample_location =
      nir_load_packed_sample_location_pco(b, dword_index);
   fs->uses.sample_locations = true;

   nir_def *byte_index = nir_iand_imm(b, sample_id, 0b11);

   packed_sample_location =
      nir_extract_u8(b, packed_sample_location, byte_index);

   nir_def *sample_location =
      nir_vec2(b,
               nir_ubitfield_extract_imm(b, packed_sample_location, 0, 4),
               nir_ubitfield_extract_imm(b, packed_sample_location, 4, 4));

   sample_location = nir_u2f32(b, sample_location);
   sample_location = nir_fdiv_imm(b, sample_location, 16.0f);
   sample_location = nir_bcsel(b,
                               nir_ieq_imm(b, msaa_samples, 1),
                               nir_imm_vec2(b, 0.5f, 0.5f),
                               sample_location);

   nir_def_replace(&intr->def, sample_location);
   nir_instr_free(&intr->instr);

   return true;
}

static bool
lower_interp(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   pco_fs_data *fs = cb_data;
   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_pos_from_id:
      return lower_sample_pos(b, intr, fs);

   case nir_intrinsic_load_interpolated_input:
      break;

   default:
      return false;
   }

   nir_intrinsic_instr *bary = nir_src_as_intrinsic(intr->src[0]);
   assert(bary);

   /* Skip cases that don't need handling. */
   if (bary->intrinsic != nir_intrinsic_load_barycentric_at_offset)
      return false;

   assert(nir_src_as_uint(intr->src[1]) == 0);

   nir_def *coords = nir_load_tile_coord_pco(b, 2);
   coords = nir_fadd(b, coords, bary->src[0].ssa);

   enum glsl_interp_mode interp_mode = nir_intrinsic_interp_mode(bary);
   nir_def *rhw = alu_iter(b,
                           coords,
                           3,
                           (struct nir_io_semantics){
                              .location = VARYING_SLOT_POS,
                              .num_slots = 1,
                           });

   nir_def *comps[4];
   for (unsigned u = 0; u < intr->def.num_components; ++u) {
      comps[u] = alu_iter(b, coords, u, nir_intrinsic_io_semantics(intr));
      if (interp_mode != INTERP_MODE_NOPERSPECTIVE)
         comps[u] = nir_fdiv(b, comps[u], rhw);
   }

   nir_def *repl = nir_vec(b, comps, intr->def.num_components);
   nir_def_replace(&intr->def, repl);
   nir_instr_free(&intr->instr);

   return true;
}

bool pco_nir_lower_interpolation(nir_shader *shader, pco_fs_data *fs)
{
   bool progress = false;

   progress |= nir_opt_barycentric(shader, true);
   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_interp,
                                          nir_metadata_control_flow,
                                          fs);

   return progress;
}

static bool lower_load_view_index_fs(struct nir_builder *b,
                                     nir_intrinsic_instr *intr,
                                     void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_load_view_index)
      return false;

   nir_variable *view_index_var = cb_data;
   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_load_var(b, view_index_var));
   nir_instr_free(&intr->instr);

   return true;
}

bool pco_nir_link_multiview(nir_shader *producer,
                            nir_shader *consumer,
                            pco_data *consumer_data)
{
   if (producer->info.stage != MESA_SHADER_VERTEX ||
       consumer->info.stage != MESA_SHADER_FRAGMENT ||
       !consumer_data->common.multiview) {
      return false;
   }

   /* Find unused varying slot for the view index. */
   gl_varying_slot view_index_slot = VARYING_SLOT_VAR0;
   nir_foreach_shader_out_variable (var, producer) {
      view_index_slot = MAX2(view_index_slot, var->data.location + 1);
   }
   assert(view_index_slot < VARYING_SLOT_MAX);
   consumer_data->fs.view_index_slot = view_index_slot;

   /* Create output variable in the producer. */
   nir_variable *view_index_var = nir_variable_create(producer,
                                                      nir_var_shader_out,
                                                      glsl_uint_type(),
                                                      "view_index");
   view_index_var->data.location = view_index_slot;
   view_index_var->data.interpolation = INTERP_MODE_FLAT;
   view_index_var->data.always_active_io = true;

   /* Store view index in the producer. */
   nir_builder b = nir_builder_at(nir_after_block(
      nir_impl_last_block(nir_shader_get_entrypoint(producer))));
   nir_store_var(&b, view_index_var, nir_load_view_index(&b), 1);

   /* Create input variable in the consumer. */
   view_index_var = nir_variable_create(consumer,
                                        nir_var_shader_in,
                                        glsl_uint_type(),
                                        "view_index");
   view_index_var->data.location = view_index_slot;
   view_index_var->data.interpolation = INTERP_MODE_FLAT;
   view_index_var->data.always_active_io = true;

   /* Lower view index loads in the consumer. */
   nir_shader_intrinsics_pass(consumer,
                              lower_load_view_index_fs,
                              nir_metadata_all,
                              view_index_var);

   return true;
}
