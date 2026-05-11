/*
 * Copyright (C) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_NIR_H__
#define __PAN_NIR_H__

#include "nir.h"
#include "nir_builder.h"
#include "pan_compiler.h"

struct util_format_description;

static inline nir_def *
pan_nir_tile_rt_sample(nir_builder *b, nir_def *rt, nir_def *sample)
{
   /* y = 255 means "current pixel" */
   return nir_pack_32_4x8_split(b, nir_u2u8(b, sample),
                                   nir_u2u8(b, rt),
                                   nir_imm_intN_t(b, 0, 8),
                                   nir_imm_intN_t(b, 255, 8));
}

static inline nir_def *
pan_nir_tile_location_sample(nir_builder *b, gl_frag_result location,
                             nir_def *sample)
{
   uint8_t rt;
   if (location == FRAG_RESULT_DEPTH) {
      rt = 255;
   } else if (location == FRAG_RESULT_STENCIL) {
      rt = 254;
   } else {
      assert(location >= FRAG_RESULT_DATA0);
      rt = location - FRAG_RESULT_DATA0;
   }

   return pan_nir_tile_rt_sample(b, nir_imm_int(b, rt), sample);
}

static inline nir_def *
pan_nir_tile_default_coverage(nir_builder *b)
{
   return nir_iand_imm(b, nir_load_cumulative_coverage_pan(b), 0x1f);
}

static inline nir_def *
pan_nir_res_handle(nir_builder *b, uint32_t table,
                   uint32_t index, nir_def *offset)
{
   if (offset) {
      return nir_ior_imm(b, nir_iadd_imm(b, offset, index),
                            pan_res_handle(table, 0));
   } else {
      return nir_imm_int(b, pan_res_handle(table, index));
   }
}

static nir_def *
pan_nir_load_va_desc(nir_builder *b, unsigned num_components, unsigned bit_size,
                     nir_def *handle, uint32_t offset)
{
   nir_def *table = nir_ushr_imm(b, handle, 24);
   nir_def *index = nir_iand_imm(b, handle, 0x00ffffff);

   nir_def *table_handle = nir_ior_imm(b, table, pan_res_handle(62, 0));
   nir_def *table_offset = nir_iadd_imm(b, nir_imul_imm(b, index, 32), offset);

   assert(offset < 32);
   return nir_load_ssbo(b, num_components, bit_size,
                        table_handle, table_offset,
                        .access = ACCESS_CAN_REORDER,
                        .align_mul = 32,
                        .align_offset = offset);
}

static nir_def *
pan_nir_load_va_buf_cvt(nir_builder *b, nir_def *handle)
{
   /* Dword 7 of the buffer descriptor type is unused by hardware and is
    * reserved for software to do whatever it wants with it.  By convention,
    * we place the conversion in dw7 so that we can fetch it from the shader.
    */
   nir_def *cvt = pan_nir_load_va_desc(b, 1, 32, handle, 7 * 4);

   /* CONSTANT 0000 L */
   nir_def *zero_cvt = nir_imm_int(b, 95 << 12 | 231);
   cvt = nir_bcsel(b, nir_ieq_imm(b, cvt, 0), zero_cvt, cvt);

   return cvt;
}

static inline nir_def *
pan_nir_load_va_buf_size_el(nir_builder *b, nir_def *handle)
{
   nir_def *size = pan_nir_load_va_desc(b, 1, 32, handle, 1 * 4);
   nir_def *stride = pan_nir_load_va_desc(b, 1, 32, handle, 4 * 4);
   return nir_udiv(b, size, stride);
}

static inline nir_def *
pan_nir_load_va_tex_size(nir_builder *b, nir_def *handle,
                         enum glsl_sampler_dim dim, bool is_array)
{
   nir_def *dw01 = pan_nir_load_va_desc(b, 4, 16, handle, 0);
   nir_def *is_null = nir_ieq_imm(b, nir_channel(b, dw01, 0), 0);

   nir_def *size, *zero;
   nir_if *nif = nir_push_if(b, nir_inot(b, is_null));
   {
      nir_def *comps[4] = {};
      unsigned nr_comps = 0;

      comps[nr_comps++] = nir_channel(b, dw01, 2);
      if (dim != GLSL_SAMPLER_DIM_1D)
         comps[nr_comps++] = nir_channel(b, dw01, 3);

      if (dim == GLSL_SAMPLER_DIM_3D)
         comps[nr_comps++] = pan_nir_load_va_desc(b, 1, 16, handle, 7 * 4);

      if (is_array)
         comps[nr_comps++] = pan_nir_load_va_desc(b, 1, 16, handle, 6 * 4);

      size = nir_vec(b, comps, nr_comps);

      /* All size fields are stored minus(1) */
      size = nir_iadd_imm(b, nir_u2u32(b, size), 1);
   }
   nir_push_else(b, nif);
   {
      zero = nir_imm_zero(b, size->num_components, 32);
   }
   nir_pop_if(b, nif);

   return nir_if_phi(b, size, zero);
}

static inline nir_def *
pan_nir_load_va_tex_levels(nir_builder *b, nir_def *handle)
{
   nir_def *hw0 = pan_nir_load_va_desc(b, 1, 16, handle, 0);
   nir_def *is_null = nir_ieq_imm(b, hw0, 0);
   nir_def *zero = nir_imm_int(b, 0);

   nir_def *levels;
   nir_if *nif = nir_push_if(b, nir_inot(b, is_null));
   {
      /* LOD count is stored in word2[16:20] and has a minus(1) modifier. */
      nir_def *w = pan_nir_load_va_desc(b, 1, 16, handle, 2 * 4 + 2);
      levels = nir_iand_imm(b, nir_u2u32(b, w), 0x1f);
      levels = nir_iadd_imm(b, levels, 1);
   }
   nir_pop_if(b, nif);

   return nir_if_phi(b, levels, zero);
}

static inline nir_def *
pan_nir_load_va_tex_samples(nir_builder *b, nir_def *handle)
{
   nir_def *hw0 = pan_nir_load_va_desc(b, 1, 16, handle, 0);
   nir_def *is_null = nir_ieq_imm(b, hw0, 0);
   nir_def *zero = nir_imm_int(b, 0);

   nir_def *samples;
   nir_if *nif = nir_push_if(b, nir_inot(b, is_null));
   {
      /* Sample count is stored in word3[13:15], and has a log2 modifier. */
      nir_def *w = pan_nir_load_va_desc(b, 1, 16, handle, 3 * 4);
      /* No need to mask because it's at the top of the half-word */
      samples = nir_ushr_imm(b, w, 13);
      samples = nir_ishl(b, nir_imm_int(b, 1), nir_u2u32(b, samples));
   }
   nir_pop_if(b, nif);

   return nir_if_phi(b, samples, zero);
}

bool pan_nir_lower_bool_to_bitsize(nir_shader *shader);

bool pan_nir_lower_vertex_id(nir_shader *shader);

bool pan_nir_lower_image_ms(nir_shader *shader);

bool pan_nir_lower_var_special_pan(nir_shader *shader);
bool pan_nir_lower_noperspective_vs(nir_shader *shader);
bool pan_nir_lower_noperspective_fs(nir_shader *shader,
                                    uint32_t *noperspective_varyings);

bool pan_nir_lower_vs_outputs(nir_shader *shader, uint64_t gpu_id,
                              const struct pan_varying_layout *varying_layout,
                              bool has_idvs, bool *needs_extended_fifo);

bool pan_nir_lower_fs_inputs(nir_shader *shader, uint64_t gpu_id,
                             const struct pan_varying_layout *varying_layout,
                             struct pan_shader_info *info);

bool pan_nir_lower_helper_invocation(nir_shader *shader);
bool pan_nir_lower_sample_pos(nir_shader *shader);
bool pan_nir_lower_xfb(nir_shader *nir);

bool pan_nir_lower_image_index(nir_shader *shader,
                               unsigned vs_img_attrib_offset);
bool pan_nir_lower_texel_buffer_fetch_index(nir_shader *shader,
                                            unsigned attrib_offset);

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct pan_bi_tex_flags {
   bool skip : 1;
   bool explicit_lod : 1;
   unsigned _pad : 14;
   unsigned sampler_idx : 8;
   unsigned texture_idx : 8;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct pan_bi_tex_flags) == 4, "Must fit in uint32_t");

static inline struct pan_bi_tex_flags
nir_intrinsic_pan_bi_tex_flags(const nir_intrinsic_instr *instr)
{
   uint32_t flags_u32 = nir_intrinsic_flags(instr);
   struct pan_bi_tex_flags flags;
   memcpy(&flags, &flags_u32, sizeof(flags));
   return flags;
}

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct pan_va_tex_flags {
   bool wide_indices : 1;
   bool array_enable : 1;
   bool texel_offset : 1;
   bool compare_enable : 1;
   unsigned lod_mode : 3;
   bool derivative_enable : 1;
   bool force_delta_enable : 1;
   bool lod_bias_disable : 1;
   bool lod_clamp_disable : 1;
   unsigned _pad : 21;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct pan_va_tex_flags) == 4, "Must fit in uint32_t");

bool pan_nir_lower_tex(nir_shader *nir, uint64_t gpu_id);
bool pan_nir_lower_image(nir_shader *nir, uint64_t gpu_id);

nir_alu_type
pan_unpacked_type_for_format(const struct util_format_description *desc);

bool pan_nir_lower_framebuffer(nir_shader *shader,
                               const enum pipe_format *rt_fmts,
                               uint8_t raw_fmt_mask,
                               unsigned blend_shader_nr_samples,
                               bool broken_ld_special);

bool pan_nir_lower_fs_outputs(nir_shader *shader, bool skip_atest);

uint32_t pan_nir_collect_noperspective_varyings_fs(nir_shader *s);

bool pan_nir_resize_varying_io(nir_shader *nir,
                               const struct pan_varying_layout *varying_layout);

#endif /* __PAN_NIR_H__ */
