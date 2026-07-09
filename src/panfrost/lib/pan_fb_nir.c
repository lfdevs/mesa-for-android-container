/*
 * Copyright (C) 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_blend.h"
#include "pan_desc.h"
#include "pan_fb.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/pan_nir.h"

static enum glsl_sampler_dim
mali_to_glsl_dim(enum mali_texture_dimension dim)
{
   switch (dim) {
   case MALI_TEXTURE_DIMENSION_1D: return GLSL_SAMPLER_DIM_1D;
   case MALI_TEXTURE_DIMENSION_CUBE:
   case MALI_TEXTURE_DIMENSION_2D: return GLSL_SAMPLER_DIM_2D;
   case MALI_TEXTURE_DIMENSION_3D: return GLSL_SAMPLER_DIM_3D;
   default:
      UNREACHABLE("Unknown mali_texture_dimension");
   }
}

static enum pan_fb_shader_data_type
data_type_for_format(enum pipe_format format)
{
   if (util_format_is_pure_uint(format))
      return PAN_FB_SHADER_DATA_TYPE_U32;
   else if (util_format_is_pure_sint(format))
      return PAN_FB_SHADER_DATA_TYPE_I32;
   else
      return PAN_FB_SHADER_DATA_TYPE_F32;
}

static nir_alu_type
nir_alu_type_for_data_type(enum pan_fb_shader_data_type data_type)
{
   switch (data_type) {
   case PAN_FB_SHADER_DATA_TYPE_F32:   return nir_type_float32;
   case PAN_FB_SHADER_DATA_TYPE_I32:   return nir_type_int32;
   case PAN_FB_SHADER_DATA_TYPE_U32:   return nir_type_uint32;
   default:
      UNREACHABLE("Invalid pan_fb_shader_data_type");
   }
}

static enum pan_fb_shader_op
get_shader_op_for_load(enum pan_fb_load_op op)
{
   switch (op) {
   case PAN_FB_LOAD_NONE:     return PAN_FB_SHADER_DONT_CARE;
   case PAN_FB_LOAD_CLEAR:    return PAN_FB_SHADER_LOAD_CLEAR;
   case PAN_FB_LOAD_IMAGE:    return PAN_FB_SHADER_LOAD_IMAGE;
   default:
      UNREACHABLE("Invalid load op");
   }
}

static inline enum pan_fb_shader_op
get_shader_op_for_resolve(enum pan_fb_resolve_op op)
{
   switch (op) {
   case PAN_FB_RESOLVE_NONE:  return PAN_FB_SHADER_PRESERVE;
   case PAN_FB_RESOLVE_IMAGE: return PAN_FB_SHADER_LOAD_IMAGE;
   case PAN_FB_RESOLVE_RT_0:  return PAN_FB_SHADER_COPY_RT_0;
   case PAN_FB_RESOLVE_RT_1:  return PAN_FB_SHADER_COPY_RT_1;
   case PAN_FB_RESOLVE_RT_2:  return PAN_FB_SHADER_COPY_RT_2;
   case PAN_FB_RESOLVE_RT_3:  return PAN_FB_SHADER_COPY_RT_3;
   case PAN_FB_RESOLVE_RT_4:  return PAN_FB_SHADER_COPY_RT_4;
   case PAN_FB_RESOLVE_RT_5:  return PAN_FB_SHADER_COPY_RT_5;
   case PAN_FB_RESOLVE_RT_6:  return PAN_FB_SHADER_COPY_RT_6;
   case PAN_FB_RESOLVE_RT_7:  return PAN_FB_SHADER_COPY_RT_7;
   case PAN_FB_RESOLVE_Z:     return PAN_FB_SHADER_COPY_Z;
   case PAN_FB_RESOLVE_S:     return PAN_FB_SHADER_COPY_S;
   default:
      UNREACHABLE("Invalid resolve op");
   }
}

static enum pan_fb_msaa_copy_op
reduce_msaa_op(enum pan_fb_msaa_copy_op msaa, enum pan_fb_shader_op op,
               uint8_t fb_sample_count, uint8_t image_sample_count)
{
   if (pan_fb_shader_op_can_discard(op))
      return PAN_FB_MSAA_COPY_ALL;

   const uint8_t src_sample_count = op == PAN_FB_SHADER_LOAD_IMAGE
                                    ? image_sample_count : fb_sample_count;
   if (msaa == PAN_FB_MSAA_COPY_ALL)
      assert(src_sample_count == fb_sample_count);

   if (src_sample_count <= 1)
      return PAN_FB_MSAA_COPY_SINGLE;

   return msaa;
}

static bool
op_needs_sample_count(enum pan_fb_shader_op op,
                      enum pan_fb_msaa_copy_op msaa)
{
   return msaa == PAN_FB_MSAA_COPY_AVERAGE ||
          msaa == PAN_FB_MSAA_COPY_MIN ||
          msaa == PAN_FB_MSAA_COPY_MAX;
}

static const struct pan_fb_shader_key_target key_target_dont_care = {
   .in_bounds_op = PAN_FB_SHADER_DONT_CARE,
   .border_op = PAN_FB_SHADER_DONT_CARE,
};

static const struct pan_fb_shader_key_target key_target_preserve = {
   .in_bounds_op = PAN_FB_SHADER_PRESERVE,
   .border_op = PAN_FB_SHADER_PRESERVE,
};

static struct pan_fb_shader_key_target
get_key_target(enum pipe_format format,
               uint8_t fb_sample_count,
               bool has_border,
               enum pan_fb_shader_op in_bounds_op,
               enum pan_fb_shader_op border_op,
               enum pan_fb_msaa_copy_op in_bounds_msaa,
               enum pan_fb_msaa_copy_op border_msaa,
               bool sample0_only,
               const struct pan_image_view *iview)
{
   if (format == PIPE_FORMAT_NONE)
      return key_target_dont_care;

   /* If we have a full framebuffer, there is no border.  Set the boarder load
    * equal to the in-bounds load so things in the shader fold nicely.
    */
   if (!has_border) {
      border_op = in_bounds_op;
      border_msaa = in_bounds_msaa;
   }

   if (in_bounds_op == PAN_FB_SHADER_DONT_CARE &&
       border_op == PAN_FB_SHADER_DONT_CARE)
      return key_target_dont_care;

   /* If both load ops can be implemented as a discard, set the target to
    * always PRESERVE and we just won't emit any code to do a load.
    *
    * Importantly, this is true for all load ops except PAN_FB_LOAD_OP_IMAGE.
    */
   if (pan_fb_shader_op_can_discard(in_bounds_op) &&
       pan_fb_shader_op_can_discard(border_op))
      return key_target_preserve;

   /* We don't have a bound on this enum so we need a live assert */
   enum mali_texture_dimension dim = MALI_TEXTURE_DIMENSION_2D;
   bool is_array = false;
   uint8_t image_sample_count = 0;
   if (in_bounds_op == PAN_FB_SHADER_LOAD_IMAGE ||
       border_op == PAN_FB_SHADER_LOAD_IMAGE) {
      if (util_format_has_depth(util_format_description(format)))
         assert(util_format_get_depth_bits(format) ==
                util_format_get_depth_bits(iview->format));
      else if (util_format_has_stencil(util_format_description(format)))
         assert(util_format_has_stencil(util_format_description(iview->format)));
      else
         assert(format == iview->format);

      dim = iview->dim;
      is_array = pan_image_view_layer_count(iview) > 1;
      image_sample_count = pan_image_view_get_nr_samples(iview);
   }

   in_bounds_msaa = reduce_msaa_op(in_bounds_msaa, in_bounds_op,
                                   fb_sample_count, image_sample_count);
   border_msaa = reduce_msaa_op(border_msaa, border_op,
                                fb_sample_count, image_sample_count);

   const bool needs_sample_count =
      op_needs_sample_count(in_bounds_op, in_bounds_msaa) ||
      op_needs_sample_count(border_op, border_msaa);

   assert(((unsigned)dim) < (1 << 2));

   return (struct pan_fb_shader_key_target) {
      .in_bounds_op = in_bounds_op,
      .border_op = border_op,
      .in_bounds_msaa = in_bounds_msaa,
      .border_msaa = border_msaa,
      .sample0_only = sample0_only,
      .image_dim = dim,
      .image_is_array = is_array,
      .image_samples_log2 = needs_sample_count
                            ? util_logbase2(image_sample_count) : 0,
      .data_type = data_type_for_format(format),
   };
}

static struct pan_fb_shader_key_target
get_load_key_target(enum pipe_format format,
                    uint8_t fb_sample_count,
                    bool has_border,
                    const struct pan_fb_load_target *load)
{
   return get_key_target(format, fb_sample_count, has_border,
                         get_shader_op_for_load(load->in_bounds_load),
                         get_shader_op_for_load(load->border_load),
                         load->msaa, load->msaa, false, load->iview);
}

bool
GENX(pan_fb_load_shader_key_fill)(struct pan_fb_shader_key *key,
                                  const struct pan_fb_layout *fb,
                                  const struct pan_fb_load *load,
                                  bool zs_prepass)
{
   *key = (struct pan_fb_shader_key) { };

   const bool has_border = pan_fb_has_partial_tiles(fb);

   /* Z/S can only be written from the prepass and color can only be written
    * from the non-prepass.  Setting everything to zero will cause the shader
    * to just not write anything in that case.
    */
   if (zs_prepass) {
      key->z = get_load_key_target(fb->z_format, fb->sample_count,
                                   has_border, &load->z);
      key->s = get_load_key_target(fb->s_format, fb->sample_count,
                                   has_border, &load->s);
      return pan_fb_shader_key_target_written(&key->z) ||
             pan_fb_shader_key_target_written(&key->s);
   } else {
      bool needs_shader = false;
      for (unsigned rt = 0; rt < fb->rt_count; rt++) {
         key->rts[rt] = get_load_key_target(fb->rt_formats[rt],
                                            fb->sample_count,
                                            has_border, &load->rts[rt]);
         if (pan_fb_shader_key_target_written(&key->rts[rt]))
            needs_shader = true;
      }
      return needs_shader;
   }
}

#if PAN_ARCH >= 6
static struct pan_fb_shader_key_target
get_resolve_key_target(enum pipe_format format,
                       uint8_t fb_sample_count,
                       bool has_border,
                       const struct pan_fb_resolve_target *load)
{
   const enum pan_fb_shader_op in_bounds_op =
      get_shader_op_for_resolve(load->in_bounds.resolve);
   const enum pan_fb_shader_op border_op =
      get_shader_op_for_resolve(load->border.resolve);
   return get_key_target(format, fb_sample_count, has_border,
                         in_bounds_op, border_op,
                         load->in_bounds.msaa, load->border.msaa,
                         true, load->iview);
}

bool
GENX(pan_fb_resolve_shader_key_fill)(struct pan_fb_shader_key *key,
                                     const struct pan_fb_layout *fb,
                                     const struct pan_fb_resolve *resolve)
{
   const bool has_border = pan_fb_has_partial_tiles(fb);

   *key = (struct pan_fb_shader_key) {
      .z = get_resolve_key_target(fb->z_format, fb->sample_count,
                                  has_border, &resolve->z),
      .s = get_resolve_key_target(fb->s_format, fb->sample_count,
                                  has_border, &resolve->s),
      .z_format = fb->z_format,
      .fb_sample_count = fb->sample_count,
   };
   bool needs_shader = pan_fb_shader_key_target_written(&key->z) ||
                       pan_fb_shader_key_target_written(&key->s);

   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      key->rts[rt] = get_resolve_key_target(fb->rt_formats[rt],
                                            fb->sample_count,
                                            has_border,
                                            &resolve->rts[rt]);
      if (pan_fb_shader_key_target_written(&key->rts[rt]))
         needs_shader = true;
   }

   return needs_shader;
}
#endif

static nir_def *
combine_samples_no_div(nir_builder *b, nir_def **samples, uint8_t sample_count,
                       const nir_alu_type nir_type,
                       enum pan_fb_msaa_copy_op msaa)
{
   assert(util_is_power_of_two_nonzero(sample_count));
   if (sample_count == 1)
      return samples[0];

   nir_def *lo = combine_samples_no_div(b, samples,
                                        sample_count / 2, nir_type, msaa);
   nir_def *hi = combine_samples_no_div(b, samples + (sample_count / 2),
                                        sample_count / 2, nir_type, msaa);

   /* We assume that first half always comes before the second so setting the
    * cursor after the second half combine instruction will give us the least
    * common ancestor.
    */
   b->cursor = nir_after_instr(nir_def_instr(hi));

   switch (msaa) {
   case PAN_FB_MSAA_COPY_AVERAGE:
      assert(nir_alu_type_get_base_type(nir_type) == nir_type_float);
      return nir_fadd(b, lo, hi);

   case PAN_FB_MSAA_COPY_MIN:
      switch (nir_alu_type_get_base_type(nir_type)) {
      case nir_type_float:
         return nir_fmin(b, lo, hi);
      case nir_type_uint:
         return nir_umin(b, lo, hi);
      case nir_type_int:
         return nir_imin(b, lo, hi);
      default:
         UNREACHABLE("Unsupported NIR type");
      }

   case PAN_FB_MSAA_COPY_MAX:
      switch (nir_alu_type_get_base_type(nir_type)) {
      case nir_type_float:
         return nir_fmax(b, lo, hi);
      case nir_type_uint:
         return nir_umax(b, lo, hi);
      case nir_type_int:
         return nir_imax(b, lo, hi);
      default:
         UNREACHABLE("Unsupported NIR type");
      }

   default:
      UNREACHABLE("Invalid MSAA op");
   }
}

static nir_def *
combine_samples(nir_builder *b, nir_def **samples, uint8_t sample_count,
                const nir_alu_type nir_type, enum pan_fb_msaa_copy_op msaa)
{
   if (msaa == PAN_FB_MSAA_COPY_SAMPLE_0)
      return samples[0];

   nir_def *val = combine_samples_no_div(b, samples, sample_count,
                                         nir_type, msaa);
   if (msaa == PAN_FB_MSAA_COPY_AVERAGE)
      val = nir_fdiv_imm(b, val, sample_count);

   return val;
}

static nir_def *
build_sample_id(nir_builder *b)
{
   b->shader->info.fs.uses_sample_shading = true;
   return nir_load_sample_id(b);
}

static nir_def *
build_image_load(nir_builder *b, const nir_alu_type nir_type,
                 nir_def *pos, gl_frag_result location,
                 enum pan_fb_msaa_copy_op msaa, uint8_t sample_count,
                 enum mali_texture_dimension dim, bool is_array)
{
   assert(pos->num_components == 3);
   switch (dim) {
   case MALI_TEXTURE_DIMENSION_1D:
      if (is_array)
         pos = nir_channels(b, pos, 0b101);
      else
         pos = nir_channel(b, pos, 0);
      break;

   case MALI_TEXTURE_DIMENSION_CUBE:
      assert(is_array);
      break;

   case MALI_TEXTURE_DIMENSION_2D:
      if (!is_array)
         pos = nir_channels(b, pos, 0b011);
      break;

   case MALI_TEXTURE_DIMENSION_3D:
      break;

   default:
      UNREACHABLE("Unsupported dim");
   }

   nir_def *val;
   switch (msaa) {
   case PAN_FB_MSAA_COPY_SINGLE:
      val = nir_txf(b, pos,
                    .texture_index = location,
                    .dim = mali_to_glsl_dim(dim),
                    .dest_type = nir_type,
                    .is_array = is_array);
      break;

   case PAN_FB_MSAA_COPY_ALL:
   case PAN_FB_MSAA_COPY_IDENTICAL:
   case PAN_FB_MSAA_COPY_SAMPLE_0: {
      assert(dim == MALI_TEXTURE_DIMENSION_2D);

      nir_def *sample_id = msaa == PAN_FB_MSAA_COPY_ALL
                           ? build_sample_id(b) : nir_imm_int(b, 0);

      val = nir_txf_ms(b, pos, sample_id,
                       .texture_index = location,
                       .dim = GLSL_SAMPLER_DIM_MS,
                       .dest_type = nir_type,
                       .is_array = is_array);
      break;
   }

   case PAN_FB_MSAA_COPY_AVERAGE:
   case PAN_FB_MSAA_COPY_MIN:
   case PAN_FB_MSAA_COPY_MAX: {
      assert(dim == MALI_TEXTURE_DIMENSION_2D);
      assert(sample_count > 0);

      nir_def *samples[16];
      assert(sample_count <= ARRAY_SIZE(samples));
      for (uint32_t s = 0; s < sample_count; s++) {
         samples[s] = nir_txf_ms(b, pos, nir_imm_int(b, s),
                                 .texture_index = location,
                                 .dim = GLSL_SAMPLER_DIM_MS,
                                 .dest_type = nir_type,
                                 .is_array = is_array);
      }
      val = combine_samples(b, samples, sample_count, nir_type, msaa);
      break;
   }

   default:
      UNREACHABLE("Invalid copy op");
   }

   if (location == FRAG_RESULT_DEPTH || location == FRAG_RESULT_STENCIL)
      val = nir_channel(b, val, 0);

   return val;
}

#if PAN_ARCH >= 6
static uint32_t
get_fb_conversion(nir_alu_type nir_type, enum pipe_format format)
{
#if PAN_ARCH >= 9
   return 0;
#else
   struct mali_internal_conversion_packed conv;
   pan_pack(&conv, INTERNAL_CONVERSION, cfg) {
      cfg.register_format = pan_blend_type_from_nir(nir_type);
      cfg.memory_format = GENX(pan_dithered_format_from_pipe_format)(
         format, false);
   }
   return conv.opaque[0];
#endif
}

static nir_def *
build_single_fb_load(nir_builder *b, const nir_alu_type nir_type,
                     gl_frag_result location, nir_def *sample,
                     enum pipe_format z_format)
{
   const bool is_zs = location == FRAG_RESULT_DEPTH ||
                      location == FRAG_RESULT_STENCIL;
   unsigned num_components = is_zs ? 1 : 4;

   nir_def *conversion;
   if (location == FRAG_RESULT_DEPTH) {
      conversion = nir_imm_int(b, get_fb_conversion(nir_type, z_format));
   } else if (location == FRAG_RESULT_STENCIL) {
      conversion = nir_imm_int(b,
         get_fb_conversion(nir_type, PIPE_FORMAT_S8_UINT));
   } else {
      assert(location >= FRAG_RESULT_DATA0);
      uint8_t rt = location - FRAG_RESULT_DATA0;
      assert(rt < PAN_MAX_RTS);

      conversion = nir_unpack_64_2x32_split_y(b,
         nir_load_blend_descriptor_pan(b, .base = rt));
   }

   nir_def *val = nir_load_tile_pan(b,
      num_components, 32,
      pan_nir_tile_location_sample(b, location, sample),
      pan_nir_tile_default_coverage(b),
      conversion,
      .dest_type = nir_type,
      .io_semantics.location = location,
      .io_semantics.num_slots = 1);

   /* If we loaded the stencil value, the upper 24 bits might contain garbage,
    * so we need to mask that off before we do any math on the result.
    */
   if (location == FRAG_RESULT_STENCIL)
      val = nir_iand_imm(b, val, 0xff);

   return val;
}

static nir_def *
build_fb_load(nir_builder *b, const nir_alu_type nir_type,
              gl_frag_result location, enum pipe_format z_format,
              enum pan_fb_msaa_copy_op msaa, uint8_t fb_sample_count)
{
   switch (msaa) {
   case PAN_FB_MSAA_COPY_SINGLE:
   case PAN_FB_MSAA_COPY_ALL:
   case PAN_FB_MSAA_COPY_IDENTICAL:
   case PAN_FB_MSAA_COPY_SAMPLE_0: {
      nir_def *sample_id = msaa == PAN_FB_MSAA_COPY_ALL
                           ? build_sample_id(b) : nir_imm_int(b, 0);

      return build_single_fb_load(b, nir_type, location, sample_id, z_format);
   }

   case PAN_FB_MSAA_COPY_AVERAGE:
   case PAN_FB_MSAA_COPY_MIN:
   case PAN_FB_MSAA_COPY_MAX: {
      nir_def *samples[16];
      assert(fb_sample_count > 0 && fb_sample_count <= ARRAY_SIZE(samples));
      for (uint32_t s = 0; s < fb_sample_count; s++) {
         samples[s] = build_single_fb_load(b, nir_type, location,
                                           nir_imm_int(b, s), z_format);
      }
      return combine_samples(b, samples, fb_sample_count, nir_type, msaa);
   }

   default:
      UNREACHABLE("Invalid copy op");
   }
}
#endif

static nir_def *
build_load(nir_builder *b, nir_def *pos,
           enum pan_fb_shader_op op, enum pan_fb_msaa_copy_op msaa,
           gl_frag_result location,
           const struct pan_fb_shader_key_target *target,
           const struct pan_fb_shader_key *key)
{
   const nir_alu_type nir_type = nir_alu_type_for_data_type(target->data_type);
   const bool is_zs = location == FRAG_RESULT_DEPTH ||
                      location == FRAG_RESULT_STENCIL;

   switch (op) {
#if PAN_ARCH >= 6
   case PAN_FB_SHADER_PRESERVE:
      return build_fb_load(b, nir_type, location, key->z_format,
                           PAN_FB_MSAA_COPY_ALL, key->fb_sample_count);
#endif

   case PAN_FB_SHADER_DONT_CARE:
      return nir_imm_zero(b, is_zs ? 1 : 4, 32);

   case PAN_FB_SHADER_LOAD_CLEAR:
      return nir_load_clear_value_pan(b, is_zs ? 1 : 4, 32,
                                      .io_semantics.location = location,
                                      .dest_type = nir_type);

   case PAN_FB_SHADER_LOAD_IMAGE:
      return build_image_load(b, nir_type, pos, location, msaa,
                              1 << target->image_samples_log2,
                              target->image_dim, target->image_is_array);

#if PAN_ARCH >= 6
   default: { /* PAN_FB_SHADER_COPY_RT_N */
      uint8_t rt = op - PAN_FB_SHADER_COPY_RT_0;
      assert(rt < PAN_MAX_RTS);
      return build_fb_load(b, nir_type, FRAG_RESULT_DATA0 + rt,
                           PIPE_FORMAT_NONE, msaa, key->fb_sample_count);
   }

   case PAN_FB_SHADER_COPY_Z:
      return build_fb_load(b, nir_type, FRAG_RESULT_DEPTH,
                           key->z_format, msaa, key->fb_sample_count);

   case PAN_FB_SHADER_COPY_S:
      return build_fb_load(b, nir_type, FRAG_RESULT_STENCIL,
                           PIPE_FORMAT_S8_UINT, msaa, key->fb_sample_count);
#else
   default:
      UNREACHABLE("Unsupported shader op");
#endif
   }
}

static void
build_op(nir_builder *b, nir_def *pos, nir_def *in_bounds,
         gl_frag_result location,
         const struct pan_fb_shader_key_target *target,
         const struct pan_fb_shader_key *key)
{
   nir_if *nif_s0 = NULL;
   if (target->sample0_only)
      nif_s0 = nir_push_if(b, nir_ieq_imm(b, build_sample_id(b), 0));

   nir_def *val;
   if (target->in_bounds_op == target->border_op &&
       target->in_bounds_msaa == target->border_msaa) {
      val = build_load(b, pos, target->in_bounds_op, target->in_bounds_msaa,
                       location, target, key);
   } else {
      nir_def *in_bounds_val, *border_val;
      nir_if *nif = nir_push_if(b, in_bounds);
      {
         in_bounds_val = build_load(b, pos, target->in_bounds_op,
                                    target->in_bounds_msaa,
                                    location, target, key);
      }
      nir_push_else(b, nif);
      {
         border_val = build_load(b, pos, target->border_op,
                                 target->border_msaa,
                                 location, target, key);
      }
      nir_pop_if(b, nif);
      val = nir_if_phi(b, in_bounds_val, border_val);
   }

   nir_store_output(b, val, nir_imm_int(b, 0),
                    .base = location, .range = 1,
                    .write_mask = nir_component_mask(val->num_components),
                    .src_type = nir_alu_type_for_data_type(target->data_type),
                    .io_semantics.location = location,
                    .io_semantics.num_slots = 1);

   if (nif_s0)
      nir_pop_if(b, nif_s0);
}

struct pan_fb_shader_info {
   bool discard_in_bounds;
   bool discard_border;
   bool sample0_only;
};

static void
gather_target_info(struct pan_fb_shader_info *info,
                   const struct pan_fb_shader_key_target *target)
{
   /* Ignore any targets we don't write */
   if (!pan_fb_shader_key_target_written(target))
      return;

   if (!pan_fb_shader_op_can_discard(target->in_bounds_op))
      info->discard_in_bounds = false;

   if (!pan_fb_shader_op_can_discard(target->border_op))
      info->discard_border = false;

   if (!target->sample0_only)
      info->sample0_only = false;
}

static struct pan_fb_shader_info
gather_shader_info(const struct pan_fb_shader_key *key)
{
   struct pan_fb_shader_info info = {
      .discard_in_bounds = true,
      .discard_border = true,
      .sample0_only = true,
   };

   for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++)
      gather_target_info(&info, &key->rts[rt]);

   gather_target_info(&info, &key->z);
   gather_target_info(&info, &key->s);

   return info;
}

static bool
opt_sample0_only_intr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_sample_id:
      b->cursor = nir_before_instr(&intr->instr);
      nir_def_replace(&intr->def, nir_imm_int(b, 0));
      return true;

   case nir_intrinsic_load_cumulative_coverage_pan:
      b->cursor = nir_before_instr(&intr->instr);
      nir_def_replace(&intr->def, nir_imm_int(b, 1));
      return true;

   default:
      return false;
   }
}

nir_shader *
GENX(pan_get_fb_shader)(const struct pan_fb_shader_key *key,
                        const struct nir_shader_compiler_options *nir_options)
{
   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, nir_options, "pan-fb-load");
   nir_builder *b = &builder;

   const struct pan_fb_shader_info info = gather_shader_info(key);

   /* We shouldn't even be building a shader at all in this case but, on the
    * off chance that we do, build one that just discards.
    */
   if (info.discard_in_bounds && info.discard_border) {
      nir_terminate(b);
      return builder.shader;
   }

   nir_def *pos_xy = nir_load_pixel_coord(b);
   nir_def *pos_x = nir_u2u32(b, nir_channel(b, pos_xy, 0));
   nir_def *pos_y = nir_u2u32(b, nir_channel(b, pos_xy, 1));
   nir_def *pos = nir_vec3(b, pos_x, pos_y, nir_load_layer_id(b));

   nir_def *ra = nir_load_fb_render_area_pan(b);
   nir_def *ra_min_xy = nir_channels(b, ra, 0b0011);
   nir_def *ra_max_xy = nir_channels(b, ra, 0b1100);

   nir_def *in_bounds =
      nir_ball(b, nir_iand(b, nir_uge(b, pos_xy, ra_min_xy),
                              nir_uge(b, ra_max_xy, pos_xy)));

   if (PAN_ARCH >= 6 && !pan_fb_shader_key_target_written(&key->z) &&
                        !pan_fb_shader_key_target_written(&key->s)) {
      /* We assume that the framebuffer will clear if either load op is set to
       * clear.  For color-only on bifrost, we can do a bit better by emitting
       * BLEND instructions directly only writing the tile buffer if we're
       * actually loading the image.
       */
      for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
         const struct pan_fb_shader_key_target *target = &key->rts[rt];
         const gl_frag_result location = FRAG_RESULT_DATA0 + rt;

         if (!pan_fb_shader_key_target_written(target))
            continue;

         nir_def *coverage = nir_load_cumulative_coverage_pan(b);
         nir_def *z1 = nir_imm_int(b, 0);
         nir_def *u4 = nir_undef(b, 4, 32);

         nir_if *nif_s0 = NULL;
         if (target->sample0_only)
            nif_s0 = nir_push_if(b, nir_ieq_imm(b, build_sample_id(b), 0));

         nir_def *color;
         if (target->in_bounds_op == target->border_op &&
             target->in_bounds_msaa == target->border_msaa) {
            color = build_load(b, pos, target->in_bounds_op,
                               target->in_bounds_msaa,
                               location, target, key);
         } else {
            nir_def *ib_color, *bd_color, *ib_cov, *bd_cov;
            nir_if *nif = nir_push_if(b, in_bounds);
            {
               if (pan_fb_shader_op_can_discard(target->in_bounds_op)) {
                  ib_color = u4;
                  ib_cov = z1;
               } else {
                  ib_color = build_load(b, pos, target->in_bounds_op,
                                        target->in_bounds_msaa,
                                        location, target, key);
                  ib_cov = coverage;
               }
            }
            nir_push_else(b, nif);
            {
               if (pan_fb_shader_op_can_discard(target->border_op)) {
                  bd_color = u4;
                  bd_cov = z1;
               } else {
                  bd_color = build_load(b, pos, target->border_op,
                                        target->border_msaa,
                                        location, target, key);
                  bd_cov = coverage;
               }
            }
            nir_pop_if(b, nif);
            color = nir_if_phi(b, ib_color, bd_color);
            coverage = nir_if_phi(b, ib_cov, bd_cov);
         }

         if (nif_s0) {
            nir_pop_if(b, nif_s0);
            color = nir_if_phi(b, color, u4);
            coverage = nir_if_phi(b, coverage, z1);
         }

         const nir_alu_type nir_type =
            nir_alu_type_for_data_type(target->data_type);
         nir_def *blend = nir_load_blend_descriptor_pan(b, .base = rt);

         nir_blend_pan(b, coverage, blend, color,
                       .src_type = nir_type,
                       .io_semantics.location = location,
                       .io_semantics.num_slots = 1);
      }
   } else {
      if (info.discard_in_bounds) {
         nir_terminate_if(b, in_bounds);
         in_bounds = nir_imm_false(b);
      } else if (info.discard_border) {
         nir_terminate_if(b, nir_inot(b, in_bounds));
         in_bounds = nir_imm_true(b);
      }

      if (info.sample0_only) {
         /* The little optimization we do at the end will take care of most
          * of what we need for sample0-only but we also need to write
          * SAMPLE_MASK so pan_nir_lower_fs_outputs will do the right thing.
          */
         nir_store_output(b, nir_imm_int(b, 1), nir_imm_int(b, 0),
                          .base = FRAG_RESULT_SAMPLE_MASK, .range = 1,
                          .write_mask = 1,
                          .src_type = nir_type_uint32,
                          .io_semantics.location = FRAG_RESULT_SAMPLE_MASK,
                          .io_semantics.num_slots = 1);
      }

      if (pan_fb_shader_key_target_written(&key->z)) {
         assert(key->z.sample0_only == info.sample0_only);
         build_op(b, pos, in_bounds, FRAG_RESULT_DEPTH, &key->z, key);
      }

      if (pan_fb_shader_key_target_written(&key->s)) {
         assert(key->s.sample0_only == info.sample0_only);
         build_op(b, pos, in_bounds, FRAG_RESULT_STENCIL, &key->s, key);
      }

      for (unsigned rt = 0; rt < PAN_MAX_RTS; rt++) {
         if (pan_fb_shader_key_target_written(&key->rts[rt])) {
            assert(key->rts[rt].sample0_only == info.sample0_only);
            build_op(b, pos, in_bounds, FRAG_RESULT_DATA0 + rt,
                     &key->rts[rt], key);
         }
      }
   }

   if (info.sample0_only) {
      NIR_PASS(_, b->shader, nir_shader_intrinsics_pass,
               opt_sample0_only_intr, nir_metadata_control_flow, NULL);
      b->shader->info.fs.uses_sample_shading = false;
   }

   return builder.shader;
}
