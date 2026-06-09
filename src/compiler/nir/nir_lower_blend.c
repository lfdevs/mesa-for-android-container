/*
 * Copyright (C) 2025 Google LLC
 * Copyright (C) 2019-2021 Collabora, Ltd.
 * Copyright (C) 2019 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * @file
 *
 * Implements the fragment pipeline (blending and writeout) in software, to be
 * run as a dedicated "blend shader" stage on Midgard/Bifrost, or as a fragment
 * shader variant on typical GPUs. This pass is useful if hardware lacks
 * fixed-function blending in part or in full.
 */

#include "nir_lower_blend.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_blend_equation_advanced_helper.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "util/blend.h"
#include "nir_builder_opcodes.h"

struct ctx {
   const nir_lower_blend_options *options;
   nir_def *src1[8];
};

/* Given processed factors, combine them per a blend function */

#define nir_blend_func(...) MESA_CALL_WITH_LTR_ARGS(_nir_blend_func, __VA_ARGS__)
static nir_def *
_nir_blend_func(
   nir_builder *b,
   enum pipe_blend_func func,
   nir_def *src, nir_def *dst)
{
   switch (func) {
   case PIPE_BLEND_ADD:
      return nir_fadd(b, src, dst);
   case PIPE_BLEND_SUBTRACT:
      return nir_fsub(b, src, dst);
   case PIPE_BLEND_REVERSE_SUBTRACT:
      return nir_fsub(b, dst, src);
   case PIPE_BLEND_MIN:
      return nir_fmin(b, src, dst);
   case PIPE_BLEND_MAX:
      return nir_fmax(b, src, dst);
   }

   UNREACHABLE("Invalid blend function");
}

/* Does this blend function multiply by a blend factor? */

static bool
nir_blend_factored(enum pipe_blend_func func)
{
   switch (func) {
   case PIPE_BLEND_ADD:
   case PIPE_BLEND_SUBTRACT:
   case PIPE_BLEND_REVERSE_SUBTRACT:
      return true;
   default:
      return false;
   }
}

/* Compute a src_alpha_saturate factor */

#define nir_alpha_saturate(...) MESA_CALL_WITH_LTR_ARGS(_nir_alpha_saturate, __VA_ARGS__)
static nir_def *
_nir_alpha_saturate(
   nir_builder *b,
   nir_def *src, nir_def *dst,
   unsigned chan)
{
   nir_def *Asrc = nir_channel(b, src, 3);
   nir_def *Adst = nir_channel(b, dst, 3);
   nir_def *one = nir_imm_floatN_t(b, 1.0, src->bit_size);
   nir_def *Adsti = nir_fsub(b, one, Adst);

   return (chan < 3) ? nir_fmin(b, Asrc, Adsti) : one;
}

/* Returns a scalar single factor, unmultiplied */

#define nir_blend_factor_value(...) MESA_CALL_WITH_LTR_ARGS(_nir_blend_factor_value, __VA_ARGS__)
static nir_def *
_nir_blend_factor_value(
   nir_builder *b,
   nir_def *src, nir_def *src1, nir_def *dst, nir_def *bconst,
   unsigned chan,
   enum pipe_blendfactor factor_without_invert)
{
   switch (factor_without_invert) {
   case PIPE_BLENDFACTOR_ONE:
      return nir_imm_floatN_t(b, 1.0, src->bit_size);
   case PIPE_BLENDFACTOR_SRC_COLOR:
      return nir_channel(b, src, chan);
   case PIPE_BLENDFACTOR_SRC1_COLOR:
      return nir_channel(b, src1, chan);
   case PIPE_BLENDFACTOR_DST_COLOR:
      return nir_channel(b, dst, chan);
   case PIPE_BLENDFACTOR_SRC_ALPHA:
      return nir_channel(b, src, 3);
   case PIPE_BLENDFACTOR_SRC1_ALPHA:
      return nir_channel(b, src1, 3);
   case PIPE_BLENDFACTOR_DST_ALPHA:
      return nir_channel(b, dst, 3);
   case PIPE_BLENDFACTOR_CONST_COLOR:
      return nir_channel(b, bconst, chan);
   case PIPE_BLENDFACTOR_CONST_ALPHA:
      return nir_channel(b, bconst, 3);
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return nir_alpha_saturate(b, src, dst, chan);
   default:
      assert(util_blendfactor_is_inverted(factor_without_invert));
      UNREACHABLE("Unexpected inverted factor");
   }
}

static nir_def *
nir_build_fsat_signed(nir_builder *b, nir_def *x)
{
   return nir_fclamp(b, x, nir_imm_floatN_t(b, -1.0, x->bit_size),
                     nir_imm_floatN_t(b, +1.0, x->bit_size));
}

static nir_def *
nir_fsat_to_format(nir_builder *b, nir_def *x, enum pipe_format format)
{
   if (util_format_is_unorm(format))
      return nir_fsat(b, x);
   else if (util_format_is_snorm(format))
      return nir_build_fsat_signed(b, x);
   else
      return x;
}

static bool
channel_uses_dest(nir_lower_blend_channel chan)
{
   /* If blend factors are ignored, dest is used (min/max) */
   if (!nir_blend_factored(chan.func))
      return true;

   /* If dest has a nonzero factor, it is used */
   if (chan.dst_factor != PIPE_BLENDFACTOR_ZERO)
      return true;

   /* Else, check the source factor */
   switch (util_blendfactor_without_invert(chan.src_factor)) {
   case PIPE_BLENDFACTOR_DST_COLOR:
   case PIPE_BLENDFACTOR_DST_ALPHA:
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return true;
   default:
      return false;
   }
}

#define nir_blend_factor(...) MESA_CALL_WITH_LTR_ARGS(_nir_blend_factor, __VA_ARGS__)
static nir_def *
_nir_blend_factor(
   nir_builder *b,
   nir_def *raw_scalar,
   nir_def *src, nir_def *src1, nir_def *dst, nir_def *bconst,
   unsigned chan,
   enum pipe_blendfactor factor,
   enum pipe_format format)
{
   nir_def *f =
      nir_blend_factor_value(b, src, src1, dst, bconst, chan,
                             util_blendfactor_without_invert(factor));

   if (util_blendfactor_is_inverted(factor))
      f = nir_fadd_imm(b, nir_fneg(b, f), 1.0);

   return nir_fmul(b, raw_scalar, f);
}

/* Given a colormask, "blend" with the destination */

nir_def *
_nir_color_mask(nir_builder *b, nir_def *src, nir_def *dst, unsigned mask)
{
   mask &= 0xf;
   if (mask == 0)
      return dst;
   else if (mask == 0xf)
      return src;

   return nir_vec4(b,
                   nir_channel(b, (mask & (1 << 0)) ? src : dst, 0),
                   nir_channel(b, (mask & (1 << 1)) ? src : dst, 1),
                   nir_channel(b, (mask & (1 << 2)) ? src : dst, 2),
                   nir_channel(b, (mask & (1 << 3)) ? src : dst, 3));
}

#define nir_logicop_func(...) MESA_CALL_WITH_LTR_ARGS(_nir_logicop_func, __VA_ARGS__)
static nir_def *
_nir_logicop_func(
   nir_builder *b,
   enum pipe_logicop func,
   nir_def *src, nir_def *dst, nir_def *bitmask)
{
   switch (func) {
   case PIPE_LOGICOP_CLEAR:
      return nir_imm_ivec4(b, 0, 0, 0, 0);
   case PIPE_LOGICOP_NOR:
      return nir_ixor(b, nir_ior(b, src, dst), bitmask);
   case PIPE_LOGICOP_AND_INVERTED:
      return nir_iand(b, nir_ixor(b, src, bitmask), dst);
   case PIPE_LOGICOP_COPY_INVERTED:
      return nir_ixor(b, src, bitmask);
   case PIPE_LOGICOP_AND_REVERSE:
      return nir_iand(b, src, nir_ixor(b, dst, bitmask));
   case PIPE_LOGICOP_INVERT:
      return nir_ixor(b, dst, bitmask);
   case PIPE_LOGICOP_XOR:
      return nir_ixor(b, src, dst);
   case PIPE_LOGICOP_NAND:
      return nir_ixor(b, nir_iand(b, src, dst), bitmask);
   case PIPE_LOGICOP_AND:
      return nir_iand(b, src, dst);
   case PIPE_LOGICOP_EQUIV:
      return nir_ixor(b, nir_ixor(b, src, dst), bitmask);
   case PIPE_LOGICOP_NOOP:
      UNREACHABLE("optimized out");
   case PIPE_LOGICOP_OR_INVERTED:
      return nir_ior(b, nir_ixor(b, src, bitmask), dst);
   case PIPE_LOGICOP_COPY:
      return src;
   case PIPE_LOGICOP_OR_REVERSE:
      return nir_ior(b, src, nir_ixor(b, dst, bitmask));
   case PIPE_LOGICOP_OR:
      return nir_ior(b, src, dst);
   case PIPE_LOGICOP_SET:
      return nir_imm_ivec4(b, ~0, ~0, ~0, ~0);
   }

   UNREACHABLE("Invalid logciop function");
}

nir_def *
_nir_color_logicop(nir_builder *b, nir_def *src, nir_def *dst,
                  enum pipe_logicop func, enum pipe_format format)
{
   unsigned bit_size = src->bit_size;
   const struct util_format_description *format_desc =
      util_format_description(format);

   /* From section 17.3.9 ("Logical Operation") of the OpenGL 4.6 core spec:
    *
    *    Logical operation has no effect on a floating-point destination color
    *    buffer, or when FRAMEBUFFER_SRGB is enabled and the value of
    *    FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING for the framebuffer attachment
    *    corresponding to the destination buffer is SRGB (see section 9.2.3).
    *    However, if logical operation is enabled, blending is still disabled.
    */
   if (util_format_is_float(format) || util_format_is_srgb(format))
      return src;
   else if (func == PIPE_LOGICOP_COPY)
      return src;
   else if (func == PIPE_LOGICOP_NOOP)
      return dst;

   nir_alu_type type =
      util_format_is_pure_integer(format) ? nir_type_uint : nir_type_float;

   if (bit_size != 32) {
      src = nir_convert_to_bit_size(b, src, type, 32);
      dst = nir_convert_to_bit_size(b, dst, type, 32);
   }

   assert(src->num_components <= 4);
   assert(dst->num_components <= 4);

   unsigned bits[4];
   for (int i = 0; i < 4; ++i)
      bits[i] = format_desc->channel[i].size;

   if (util_format_is_unorm(format)) {
      src = nir_format_float_to_unorm(b, src, bits);
      dst = nir_format_float_to_unorm(b, dst, bits);
   } else if (util_format_is_snorm(format)) {
      src = nir_format_float_to_snorm(b, src, bits);
      dst = nir_format_float_to_snorm(b, dst, bits);
   } else {
      assert(util_format_is_pure_integer(format));
   }

   nir_const_value mask[4];
   for (int i = 0; i < 4; ++i)
      mask[i] = nir_const_value_for_uint(BITFIELD_MASK(bits[i]), 32);

   nir_def *out = nir_logicop_func(b, func, src, dst,
                                   nir_build_imm(b, 4, 32, mask));

   if (util_format_is_unorm(format)) {
      out = nir_format_unorm_to_float(b, out, bits);
   } else if (util_format_is_snorm(format)) {
      /* Sign extend before converting so the i2f in snorm_to_float works */
      out = nir_format_sign_extend_ivec(b, out, bits);
      out = nir_format_snorm_to_float(b, out, bits);
   } else {
      assert(util_format_is_pure_integer(format));
   }

   if (bit_size != 32)
      out = nir_convert_to_bit_size(b, out, type, bit_size);

   return out;
}

static bool
channel_exists(const struct util_format_description *desc, unsigned i)
{
   return (i < desc->nr_channels) &&
          desc->channel[i].type != UTIL_FORMAT_TYPE_VOID;
}

/*
 * Test if the blending options for a given channel encode the "replace" blend
 * mode: dest = source. In this case, blending may be specially optimized.
 */
static bool
nir_blend_replace_channel(const nir_lower_blend_channel *c)
{
   return (c->func == PIPE_BLEND_ADD) &&
          (c->src_factor == PIPE_BLENDFACTOR_ONE) &&
          (c->dst_factor == PIPE_BLENDFACTOR_ZERO);
}

static bool
nir_blend_replace_rt(const nir_lower_blend_rt *rt)
{
   return nir_blend_replace_channel(&rt->rgb) &&
          nir_blend_replace_channel(&rt->alpha);
}


static nir_def *
minv3(nir_builder *b, nir_def *v)
{
   return nir_fmin(b, nir_fmin(b, nir_channel(b, v, 0), nir_channel(b, v, 1)),
                   nir_channel(b, v, 2));
}

static nir_def *
maxv3(nir_builder *b, nir_def *v)
{
   return nir_fmax(b, nir_fmax(b, nir_channel(b, v, 0), nir_channel(b, v, 1)),
                   nir_channel(b, v, 2));
}

static nir_def *
lumv3(nir_builder *b, nir_def *c)
{
   return nir_fdot(b, c, nir_imm_vec3(b, 0.30, 0.59, 0.11));
}

static nir_def *
satv3(nir_builder *b, nir_def *c)
{
   return nir_fsub(b, maxv3(b, c), minv3(b, c));
}

/* Clip color to [0,1] while preserving luminosity */
static nir_def *
clip_color(nir_builder *b, nir_def *c)
{
   nir_def *lum = lumv3(b, c);
   nir_def *mincol = minv3(b, c);
   nir_def *maxcol = maxv3(b, c);

   /* If min < 0: c = lum + (c - lum) * lum / (lum - min) */
   nir_def *t1 = nir_fdiv(b,
                          nir_fmul(b, nir_fsub(b, c, lum), lum),
                          nir_fsub(b, lum, mincol));
   nir_def *c1 = nir_fadd(b, lum, t1);

   /* If max > 1: c = lum + (c - lum) * (1 - lum) / (max - lum) */
   nir_def *t2 = nir_fdiv(b,
                          nir_fmul(b, nir_fsub(b, c, lum), nir_fsub_imm(b, 1.0, lum)),
                          nir_fsub(b, maxcol, lum));
   nir_def *c2 = nir_fadd(b, lum, t2);

   nir_def *min_neg = nir_flt_imm(b, mincol, 0.0);
   nir_def *max_gt1 = nir_fgt_imm(b, maxcol, 1.0);

   return nir_bcsel(b, min_neg, c1,
                    nir_bcsel(b, max_gt1, c2, c));
}

/* Set luminosity of cbase to match clum */

#define set_lum(...) MESA_CALL_WITH_LTR_ARGS(_set_lum, __VA_ARGS__)
static nir_def *
_set_lum(nir_builder *b, nir_def *cbase, nir_def *clum)
{
   nir_def *lbase = lumv3(b, cbase);
   nir_def *llum = lumv3(b, clum);
   nir_def *diff = nir_fsub(b, llum, lbase);
   nir_def *c = nir_fadd(b, cbase, diff);

   return clip_color(b, c);
}

/* Set saturation of cbase to match csat, then luminosity to match clum */

#define set_lum_sat(...) MESA_CALL_WITH_LTR_ARGS(_set_lum_sat, __VA_ARGS__)
static nir_def *
_set_lum_sat(nir_builder *b, nir_def *cbase, nir_def *csat, nir_def *clum)
{
   nir_def *sbase = satv3(b, cbase);
   nir_def *ssat = satv3(b, csat);
   nir_def *minbase = minv3(b, cbase);

   /* Scale saturation: (cbase - min) * ssat / sbase */
   nir_def *scaled = nir_bcsel(b,
                               nir_fgt_imm(b, sbase, 0.0),
                               nir_fdiv(b, nir_fmul(b, nir_fsub(b, cbase, minbase), ssat), sbase),
                               imm3(b, 0.0));

   return set_lum(b, scaled, clum);
}

#define blend_hsl_hue(...) MESA_CALL_WITH_LTR_ARGS(_blend_hsl_hue, __VA_ARGS__)
static nir_def *
_blend_hsl_hue(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* Hue from src, saturation and luminosity from dst */
   return set_lum_sat(b, src, dst, dst);
}

#define blend_hsl_saturation(...) MESA_CALL_WITH_LTR_ARGS(_blend_hsl_saturation, __VA_ARGS__)
static nir_def *
_blend_hsl_saturation(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* Saturation from src, hue and luminosity from dst */
   return set_lum_sat(b, dst, src, dst);
}

#define blend_hsl_color(...) MESA_CALL_WITH_LTR_ARGS(_blend_hsl_color, __VA_ARGS__)
static nir_def *
_blend_hsl_color(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* Hue and saturation from src, luminosity from dst */
   return set_lum(b, src, dst);
}

#define blend_hsl_luminosity(...) MESA_CALL_WITH_LTR_ARGS(_blend_hsl_luminosity, __VA_ARGS__)
static nir_def *
_blend_hsl_luminosity(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* Luminosity from src, hue and saturation from dst */
   return set_lum(b, dst, src);
}

#define blend_invert(...) MESA_CALL_WITH_LTR_ARGS(_blend_invert, __VA_ARGS__)
static nir_def *
_blend_invert(nir_builder *b, nir_def *src, nir_def *dst)
{
   return nir_fsub_imm(b, 1.0, dst);
}

#define blend_invert_rgb(...) MESA_CALL_WITH_LTR_ARGS(_blend_invert_rgb, __VA_ARGS__)
static nir_def *
_blend_invert_rgb(nir_builder *b, nir_def *src, nir_def *dst)
{
   return nir_fmul(b, src, nir_fsub_imm(b, 1.0, dst));
}

#define blend_lineardodge(...) MESA_CALL_WITH_LTR_ARGS(_blend_lineardodge, __VA_ARGS__)
static nir_def *
_blend_lineardodge(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* min(1, src + dst) */
   return nir_fmin(b, imm3(b, 1.0), nir_fadd(b, src, dst));
}

#define blend_linearburn(...) MESA_CALL_WITH_LTR_ARGS(_blend_linearburn, __VA_ARGS__)
static nir_def *
_blend_linearburn(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* max(0, src + dst - 1) */
   return nir_fmax(b, nir_imm_float(b, 0.0),
                   nir_fadd(b, src, nir_fadd_imm(b, dst, -1.0)));
}

#define blend_vividlight(...) MESA_CALL_WITH_LTR_ARGS(_blend_vividlight, __VA_ARGS__)
static nir_def *
_blend_vividlight(nir_builder *b, nir_def *src, nir_def *dst)
{
   /*
    * if src <= 0: 0
    * if src < 0.5: 1 - min(1, (1-dst) / (2*src))
    * if src < 1: min(1, dst / (2*(1-src)))
    * else: 1
    */
   nir_def *two_src = nir_fmul_imm(b, src, 2.0);
   nir_def *one_minus_dst = nir_fsub_imm(b, 1.0, dst);
   nir_def *one_minus_src = nir_fsub_imm(b, 1.0, src);

   nir_def *case_lt_half = nir_fsub_imm(b, 1.0,
                                        nir_fmin(b, imm3(b, 1.0), nir_fdiv(b, one_minus_dst, two_src)));
   nir_def *case_lt_one = nir_fmin(b, imm3(b, 1.0),
                                   nir_fdiv(b, dst, nir_fmul_imm(b, one_minus_src, 2.0)));

   return nir_bcsel(b, nir_fle_imm(b, src, 0.0), imm3(b, 0.0),
                    nir_bcsel(b, nir_flt_imm(b, src, 0.5), case_lt_half,
                              nir_bcsel(b, nir_flt_imm(b, src, 1.0), case_lt_one,
                                        imm3(b, 1.0))));
}

#define blend_linearlight(...) MESA_CALL_WITH_LTR_ARGS(_blend_linearlight, __VA_ARGS__)
static nir_def *
_blend_linearlight(nir_builder *b, nir_def *src, nir_def *dst)
{
   /*
    * if 2*src + dst > 2: 1
    * if 2*src + dst <= 1: 0
    * else: 2*src + dst - 1
    */
   nir_def *two_src = nir_fmul_imm(b, src, 2.0);
   nir_def *sum = nir_fadd(b, two_src, dst);
   nir_def *result = nir_fsub(b, sum, imm3(b, 1.0));

   return nir_bcsel(b, nir_fgt_imm(b, sum, 2.0), imm3(b, 1.0),
                    nir_bcsel(b, nir_fge(b, imm3(b, 1.0), sum), imm3(b, 0.0),
                              result));
}

#define blend_pinlight(...) MESA_CALL_WITH_LTR_ARGS(_blend_pinlight, __VA_ARGS__)
static nir_def *
_blend_pinlight(nir_builder *b, nir_def *src, nir_def *dst)
{
   /*
    * if (2*src - 1 > dst) && src < 0.5: 0
    * if (2*src - 1 > dst) && src >= 0.5: 2*src - 1
    * if (2*src - 1 <= dst) && src < 0.5*dst: 2*src
    * if (2*src - 1 <= dst) && src >= 0.5*dst: dst
    */
   nir_def *two_src = nir_fmul_imm(b, src, 2.0);
   nir_def *two_src_minus_1 = nir_fsub(b, two_src, imm3(b, 1.0));
   nir_def *half_dst = nir_fmul_imm(b, dst, 0.5);

   nir_def *cond1 = nir_flt(b, dst, two_src_minus_1);
   nir_def *cond2 = nir_flt_imm(b, src, 0.5);
   nir_def *cond3 = nir_flt(b, src, half_dst);

   return nir_bcsel(b, cond1,
                    nir_bcsel(b, cond2, imm3(b, 0.0), two_src_minus_1),
                    nir_bcsel(b, cond3, two_src, dst));
}

#define blend_hardmix(...) MESA_CALL_WITH_LTR_ARGS(_blend_hardmix, __VA_ARGS__)
static nir_def *
_blend_hardmix(nir_builder *b, nir_def *src, nir_def *dst)
{
   /* if src + dst >= 1: 1, else 0.
    * Use small epsilon to handle 8-bit quantization.
    */
   nir_def *sum = nir_fadd(b, src, dst);
   nir_def *threshold = nir_imm_float(b, 1.0 - 0.5 / 255.0); /* ~0.998039 */
   return nir_bcsel(b, nir_fge(b, sum, threshold),
                    imm3(b, 1.0), imm3(b, 0.0));
}

/*
 * Calculate the blend factor f(Cs', Cd').
 * Returns NULL for blend modes where X=0, meaning f() is not used.
 */

#define calc_blend_factor(...) MESA_CALL_WITH_LTR_ARGS(_calc_blend_factor, __VA_ARGS__)
static nir_def *
_calc_blend_factor(nir_builder *b, enum pipe_advanced_blend_mode blend_op, nir_def *src, nir_def *dst)
{
   switch (blend_op) {
   /* f() result unused (X=0) */
   case PIPE_ADVANCED_BLEND_NONE:
   case PIPE_ADVANCED_BLEND_SRC_OUT:
   case PIPE_ADVANCED_BLEND_DST_OUT:
   case PIPE_ADVANCED_BLEND_XOR:
      return NULL;

   /* Standard blend modes */
   case PIPE_ADVANCED_BLEND_MULTIPLY:
      return blend_multiply(b, src, dst);
   case PIPE_ADVANCED_BLEND_SCREEN:
      return blend_screen(b, src, dst);
   case PIPE_ADVANCED_BLEND_OVERLAY:
      return blend_overlay(b, src, dst);
   case PIPE_ADVANCED_BLEND_DARKEN:
      return blend_darken(b, src, dst);
   case PIPE_ADVANCED_BLEND_LIGHTEN:
      return blend_lighten(b, src, dst);
   case PIPE_ADVANCED_BLEND_COLORDODGE:
      return blend_colordodge(b, src, dst);
   case PIPE_ADVANCED_BLEND_COLORBURN:
      return blend_colorburn(b, src, dst);
   case PIPE_ADVANCED_BLEND_HARDLIGHT:
      return blend_hardlight(b, src, dst);
   case PIPE_ADVANCED_BLEND_SOFTLIGHT:
      return blend_softlight(b, src, dst);
   case PIPE_ADVANCED_BLEND_DIFFERENCE:
      return blend_difference(b, src, dst);
   case PIPE_ADVANCED_BLEND_EXCLUSION:
      return blend_exclusion(b, src, dst);

   /* HSL blend modes */
   case PIPE_ADVANCED_BLEND_HSL_HUE:
      return blend_hsl_hue(b, src, dst);
   case PIPE_ADVANCED_BLEND_HSL_SATURATION:
      return blend_hsl_saturation(b, src, dst);
   case PIPE_ADVANCED_BLEND_HSL_COLOR:
      return blend_hsl_color(b, src, dst);
   case PIPE_ADVANCED_BLEND_HSL_LUMINOSITY:
      return blend_hsl_luminosity(b, src, dst);

   /* Porter-Duff modes where f(Cs,Cd) = Cs or Cd */
   case PIPE_ADVANCED_BLEND_SRC:
   case PIPE_ADVANCED_BLEND_SRC_OVER:
   case PIPE_ADVANCED_BLEND_SRC_IN:
   case PIPE_ADVANCED_BLEND_SRC_ATOP:
      return src;
   case PIPE_ADVANCED_BLEND_DST:
   case PIPE_ADVANCED_BLEND_DST_OVER:
   case PIPE_ADVANCED_BLEND_DST_IN:
   case PIPE_ADVANCED_BLEND_DST_ATOP:
      return dst;

   /* Extended blend modes */
   case PIPE_ADVANCED_BLEND_INVERT:
      return blend_invert(b, src, dst);
   case PIPE_ADVANCED_BLEND_INVERT_RGB:
      return blend_invert_rgb(b, src, dst);
   case PIPE_ADVANCED_BLEND_LINEARDODGE:
      return blend_lineardodge(b, src, dst);
   case PIPE_ADVANCED_BLEND_LINEARBURN:
      return blend_linearburn(b, src, dst);
   case PIPE_ADVANCED_BLEND_VIVIDLIGHT:
      return blend_vividlight(b, src, dst);
   case PIPE_ADVANCED_BLEND_LINEARLIGHT:
      return blend_linearlight(b, src, dst);
   case PIPE_ADVANCED_BLEND_PINLIGHT:
      return blend_pinlight(b, src, dst);
   case PIPE_ADVANCED_BLEND_HARDMIX:
      return blend_hardmix(b, src, dst);
   default:
      UNREACHABLE("Invalid advanced blend op");
   }
}

#define calc_additional_rgb_blend(...) MESA_CALL_WITH_LTR_ARGS(_calc_additional_rgb_blend, __VA_ARGS__)
static nir_def *
_calc_additional_rgb_blend(nir_builder *b, const nir_lower_blend_options *options,
                          unsigned rt,
                          nir_def *src, nir_def *dst)
{
   nir_def *src_rgb = nir_trim_vector(b, src, 3);
   nir_def *dst_rgb = nir_trim_vector(b, dst, 3);
   nir_def *src_a = nir_channel(b, src, 3);
   nir_def *dst_a = nir_channel(b, dst, 3);

   /* Premultiply if non-premultiplied */
   if (!options->rt[rt].src_premultiplied)
      src_rgb = nir_fmul(b, src_rgb, src_a);
   if (!options->rt[rt].dst_premultiplied)
      dst_rgb = nir_fmul(b, dst_rgb, dst_a);

   nir_def *rgb, *a;

   switch (options->rt[rt].blend_mode) {
   case PIPE_ADVANCED_BLEND_PLUS:
      rgb = nir_fadd(b, src_rgb, dst_rgb);
      a = nir_fadd(b, src_a, dst_a);
      break;
   case PIPE_ADVANCED_BLEND_PLUS_CLAMPED:
      rgb = nir_fmin(b, imm3(b, 1.0), nir_fadd(b, src_rgb, dst_rgb));
      a = nir_fmin(b, nir_imm_float(b, 1.0), nir_fadd(b, src_a, dst_a));
      break;
   case PIPE_ADVANCED_BLEND_PLUS_CLAMPED_ALPHA: {
      nir_def *max_a = nir_fmin(b, nir_imm_float(b, 1.0), nir_fadd(b, src_a, dst_a));
      rgb = nir_fmin(b, max_a, nir_fadd(b, src_rgb, dst_rgb));
      a = max_a;
      break;
   }
   case PIPE_ADVANCED_BLEND_PLUS_DARKER: {
      nir_def *max_a = nir_fmin(b, nir_imm_float(b, 1.0), nir_fadd(b, src_a, dst_a));
      /* max(0, max_a - ((src_a - src_rgb) + (dst_a - dst_rgb))) */
      nir_def *src_diff = nir_fsub(b, src_a, src_rgb);
      nir_def *dst_diff = nir_fsub(b, dst_a, dst_rgb);
      rgb = nir_fmax(b, imm3(b, 0.0), nir_fsub(b, max_a, nir_fadd(b, src_diff, dst_diff)));
      a = max_a;
      break;
   }
   case PIPE_ADVANCED_BLEND_MINUS:
      rgb = nir_fsub(b, dst_rgb, src_rgb);
      a = nir_fsub(b, dst_a, src_a);
      break;
   case PIPE_ADVANCED_BLEND_MINUS_CLAMPED:
      rgb = nir_fmax(b, imm3(b, 0.0), nir_fsub(b, dst_rgb, src_rgb));
      a = nir_fmax(b, nir_imm_float(b, 0.0), nir_fsub(b, dst_a, src_a));
      break;
   case PIPE_ADVANCED_BLEND_CONTRAST: {
      /* res.rgb = (dst_a / 2) + 2 * (dst_rgb - dst_a/2) * (src_rgb - src_a/2) */
      nir_def *half_dst_a = nir_fmul_imm(b, dst_a, 0.5);
      nir_def *half_src_a = nir_fmul_imm(b, src_a, 0.5);
      nir_def *dst_centered = nir_fsub(b, dst_rgb, half_dst_a);
      nir_def *src_centered = nir_fsub(b, src_rgb, half_src_a);
      rgb = nir_fadd(b, half_dst_a,
                     nir_fmul_imm(b, nir_fmul(b, dst_centered, src_centered), 2.0));
      a = dst_a;
      break;
   }
   case PIPE_ADVANCED_BLEND_INVERT_OVG: {
      /* res.rgb = src_a * (1 - dst_rgb) + (1 - src_a) * dst_rgb */
      nir_def *one_minus_dst = nir_fsub_imm(b, 1.0, dst_rgb);
      nir_def *one_minus_src_a = nir_fsub_imm(b, 1.0, src_a);
      rgb = nir_fadd(b, nir_fmul(b, src_a, one_minus_dst),
                     nir_fmul(b, one_minus_src_a, dst_rgb));
      a = nir_fsub(b, nir_fadd(b, src_a, dst_a), nir_fmul(b, src_a, dst_a));
      break;
   }
   case PIPE_ADVANCED_BLEND_RED:
      rgb = nir_vec3(b, nir_channel(b, src_rgb, 0), nir_channel(b, dst_rgb, 1), nir_channel(b, dst_rgb, 2));
      a = dst_a;
      break;
   case PIPE_ADVANCED_BLEND_GREEN:
      rgb = nir_vec3(b, nir_channel(b, dst_rgb, 0), nir_channel(b, src_rgb, 1), nir_channel(b, dst_rgb, 2));
      a = dst_a;
      break;
   case PIPE_ADVANCED_BLEND_BLUE:
      rgb = nir_vec3(b, nir_channel(b, dst_rgb, 0), nir_channel(b, dst_rgb, 1), nir_channel(b, src_rgb, 2));
      a = dst_a;
      break;
   default:
      UNREACHABLE("Invalid additional RGB blend op");
   }

   /* If dst is non-premultiplied, the output should also be non-premultiplied */
   if (!options->rt[rt].dst_premultiplied) {
      rgb = nir_bcsel(b,
                      nir_fgt_imm(b, a, 0.0),
                      nir_fdiv(b, rgb, a),
                      imm3(b, 0.0));
   }

   return nir_vec4(b, nir_channel(b, rgb, 0), nir_channel(b, rgb, 1),
                   nir_channel(b, rgb, 2), a);
}

/*
 * X, Y, Z blend factors for the advanced blend equation:
 *   RGB = f(Cs',Cd') * X * p0 + Cs' * Y * p1 + Cd' * Z * p2
 *   A   = X * p0 + Y * p1 + Z * p2
 *
 * Index by enum pipe_advanced_blend_mode.
 * Modes >= PIPE_ADVANCED_BLEND_PLUS use separate calc_additional_rgb_blend().
 */
static const float blend_xyz[][3] = {
   [PIPE_ADVANCED_BLEND_NONE] = { 0, 0, 0 },
   [PIPE_ADVANCED_BLEND_MULTIPLY] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_SCREEN] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_OVERLAY] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_DARKEN] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_LIGHTEN] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_COLORDODGE] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_COLORBURN] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_HARDLIGHT] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_SOFTLIGHT] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_DIFFERENCE] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_EXCLUSION] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_HSL_HUE] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_HSL_SATURATION] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_HSL_COLOR] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_HSL_LUMINOSITY] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_SRC] = { 1, 1, 0 },
   [PIPE_ADVANCED_BLEND_DST] = { 1, 0, 1 },
   [PIPE_ADVANCED_BLEND_SRC_OVER] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_DST_OVER] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_SRC_IN] = { 1, 0, 0 },
   [PIPE_ADVANCED_BLEND_DST_IN] = { 1, 0, 0 },
   [PIPE_ADVANCED_BLEND_SRC_OUT] = { 0, 1, 0 },
   [PIPE_ADVANCED_BLEND_DST_OUT] = { 0, 0, 1 },
   [PIPE_ADVANCED_BLEND_SRC_ATOP] = { 1, 0, 1 },
   [PIPE_ADVANCED_BLEND_DST_ATOP] = { 1, 1, 0 },
   [PIPE_ADVANCED_BLEND_XOR] = { 0, 1, 1 },
   [PIPE_ADVANCED_BLEND_INVERT] = { 1, 0, 1 },
   [PIPE_ADVANCED_BLEND_INVERT_RGB] = { 1, 0, 1 },
   [PIPE_ADVANCED_BLEND_LINEARDODGE] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_LINEARBURN] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_VIVIDLIGHT] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_LINEARLIGHT] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_PINLIGHT] = { 1, 1, 1 },
   [PIPE_ADVANCED_BLEND_HARDMIX] = { 1, 1, 1 },
};

#define nir_blend_advanced(...) MESA_CALL_WITH_LTR_ARGS(_nir_blend_advanced, __VA_ARGS__)
static nir_def *
_nir_blend_advanced(
   nir_builder *b,
   const nir_lower_blend_options *options,
   unsigned rt,
   nir_def *src, nir_def *dst)
{
   /* Advanced blend uses hardcoded 32-bit constants. Convert inputs to f32
    * and convert back at the end.
    */
   const unsigned bit_size = src->bit_size;
   src = nir_f2f32(b, src);
   dst = nir_f2f32(b, dst);

   /* Check if this is an additional RGB blend op */
   if (options->rt[rt].blend_mode >= PIPE_ADVANCED_BLEND_PLUS &&
       options->rt[rt].blend_mode <= PIPE_ADVANCED_BLEND_BLUE) {
      nir_def *result = calc_additional_rgb_blend(b, options, rt, src, dst);
      return nir_f2fN(b, result, bit_size);
   }

   nir_def *src_rgb = nir_trim_vector(b, src, 3);
   nir_def *dst_rgb = nir_trim_vector(b, dst, 3);
   nir_def *src_a = nir_channel(b, src, 3);
   nir_def *dst_a = nir_channel(b, dst, 3);

   /* Unpremultiply */
   nir_def *src_rgb_unpre;
   if (options->rt[rt].src_premultiplied) {
      src_rgb_unpre = nir_bcsel(b,
                                nir_feq_imm(b, src_a, 0.0),
                                imm3(b, 0.0),
                                nir_fdiv(b, src_rgb, src_a));
   } else {
      src_rgb_unpre = src_rgb;
   }

   nir_def *dst_rgb_unpre;
   if (options->rt[rt].dst_premultiplied) {
      dst_rgb_unpre = nir_bcsel(b,
                                nir_feq_imm(b, dst_a, 0.0),
                                imm3(b, 0.0),
                                nir_fdiv(b, dst_rgb, dst_a));
   } else {
      dst_rgb_unpre = dst_rgb;
   }

   /* f(Cs', Cd') - may be NULL if X=0 (result unused) */
   nir_def *factor = calc_blend_factor(b, options->rt[rt].blend_mode, src_rgb_unpre, dst_rgb_unpre);

   nir_def *p0, *p1, *p2;

   switch (options->rt[rt].overlap) {
   case PIPE_BLEND_OVERLAP_UNCORRELATED:
      /* p0 = As * Ad, p1 = As * (1 - Ad), p2 = Ad * (1 - As) */
      p0 = nir_fmul(b, src_a, dst_a);
      p1 = nir_fmul(b, src_a, nir_fsub_imm(b, 1.0, dst_a));
      p2 = nir_fmul(b, dst_a, nir_fsub_imm(b, 1.0, src_a));
      break;
   case PIPE_BLEND_OVERLAP_CONJOINT:
      /* p0 = min(As, Ad), p1 = max(As - Ad, 0), p2 = max(Ad - As, 0) */
      p0 = nir_fmin(b, src_a, dst_a);
      p1 = nir_fmax(b, nir_fsub(b, src_a, dst_a), nir_imm_float(b, 0.0));
      p2 = nir_fmax(b, nir_fsub(b, dst_a, src_a), nir_imm_float(b, 0.0));
      break;
   case PIPE_BLEND_OVERLAP_DISJOINT:
      /* p0 = max(As + Ad - 1, 0), p1 = min(As, 1 - Ad), p2 = min(Ad, 1 - As) */
      p0 = nir_fmax(b, nir_fadd_imm(b, nir_fadd(b, src_a, dst_a), -1.0), nir_imm_float(b, 0.0));
      p1 = nir_fmin(b, src_a, nir_fsub_imm(b, 1.0, dst_a));
      p2 = nir_fmin(b, dst_a, nir_fsub_imm(b, 1.0, src_a));
      break;
   default:
      UNREACHABLE("invalid overlap");
   }

   const float x = blend_xyz[options->rt[rt].blend_mode][0];
   const float y = blend_xyz[options->rt[rt].blend_mode][1];
   const float z = blend_xyz[options->rt[rt].blend_mode][2];

   /* RGB = f * X * p0 + Cs' * Y * p1 + Cd' * Z * p2 */
   nir_def *rgb = imm3(b, 0.0);
   if (factor)
      rgb = nir_fmul(b, factor, nir_fmul_imm(b, p0, x));
   if (y != 0.0)
      rgb = nir_fadd(b, rgb, nir_fmul(b, src_rgb_unpre, nir_fmul_imm(b, p1, y)));
   if (z != 0.0)
      rgb = nir_fadd(b, rgb, nir_fmul(b, dst_rgb_unpre, nir_fmul_imm(b, p2, z)));

   /* A = X * p0 + Y * p1 + Z * p2 */
   nir_def *a = nir_imm_float(b, 0.0);
   if (x != 0.0)
      a = nir_fmul_imm(b, p0, x);
   if (y != 0.0)
      a = nir_fadd(b, a, nir_fmul_imm(b, p1, y));
   if (z != 0.0)
      a = nir_fadd(b, a, nir_fmul_imm(b, p2, z));

   /* If dst is non-premultiplied, the output should also be non-premultiplied */
   if (!options->rt[rt].dst_premultiplied) {
      rgb = nir_bcsel(b,
                      nir_fgt_imm(b, a, 0.0),
                      nir_fdiv(b, rgb, a),
                      imm3(b, 0.0));
   }

   nir_def *result = nir_vec4(b, nir_channel(b, rgb, 0), nir_channel(b, rgb, 1),
                              nir_channel(b, rgb, 2), a);
   return nir_f2fN(b, result, bit_size);
}

/* Given a blend state, the source color, and the destination color,
 * return the blended color
 */

nir_def *
_nir_color_blend(nir_builder *b, nir_def *src, nir_def *src1, nir_def *dst,
                const nir_lower_blend_rt *rt, bool scalar_blend_const)
{
   if (util_format_is_pure_integer(rt->format) || nir_blend_replace_rt(rt))
      return src;

   /* Don't crash if src1 isn't written. It doesn't matter what dual colour we
    * blend with in that case, as long as we don't dereference NULL.
    */
   if (!src1)
      src1 = nir_imm_zero(b, 4, src->bit_size);

   /* Grab the blend constant ahead of time */
   nir_def *bconst;
   if (scalar_blend_const) {
      bconst = nir_vec4(b,
                        nir_load_blend_const_color_r_float(b),
                        nir_load_blend_const_color_g_float(b),
                        nir_load_blend_const_color_b_float(b),
                        nir_load_blend_const_color_a_float(b));
   } else {
      bconst = nir_load_blend_const_color_rgba(b);
   }

   if (src->bit_size == 16) {
      bconst = nir_f2f16(b, bconst);
      src1 = nir_f2f16(b, src1);
   }

   /* The input colours need to be clamped to the format. Contrary to the
    * OpenGL/Vulkan specs, it really is the inputs that get clamped and not the
    * intermediate blend factors. This matches the CTS and hardware behaviour.
    */
   src = nir_fsat_to_format(b, src, rt->format);
   bconst = nir_fsat_to_format(b, bconst, rt->format);

   if (src1)
      src1 = nir_fsat_to_format(b, src1, rt->format);

   /* DST_ALPHA reads back 1.0 if there is no alpha channel */
   const struct util_format_description *desc =
      util_format_description(rt->format);

   nir_def *zero = nir_imm_floatN_t(b, 0.0, dst->bit_size);
   nir_def *one = nir_imm_floatN_t(b, 1.0, dst->bit_size);

   dst = nir_vec4(b,
                  channel_exists(desc, 0) ? nir_channel(b, dst, 0) : zero,
                  channel_exists(desc, 1) ? nir_channel(b, dst, 1) : zero,
                  channel_exists(desc, 2) ? nir_channel(b, dst, 2) : zero,
                  channel_exists(desc, 3) ? nir_channel(b, dst, 3) : one);

   /* We blend per channel and recombine later */
   nir_def *channels[4];

   for (unsigned c = 0; c < 4; ++c) {
      /* Decide properties based on channel */
      nir_lower_blend_channel chan = (c < 3) ? rt->rgb : rt->alpha;

      nir_def *psrc = nir_channel(b, src, c);
      nir_def *pdst = nir_channel(b, dst, c);

      if (nir_blend_factored(chan.func)) {
         psrc = nir_blend_factor(
            b, psrc,
            src, src1, dst, bconst, c,
            chan.src_factor, rt->format);

         pdst = nir_blend_factor(
            b, pdst,
            src, src1, dst, bconst, c,
            chan.dst_factor, rt->format);
      }

      channels[c] = nir_blend_func(b, chan.func, psrc, pdst);
   }

   return nir_vec(b, channels, 4);
}

static int
color_index_for_location(unsigned location)
{
   assert(location != FRAG_RESULT_COLOR &&
          "gl_FragColor must be lowered before nir_lower_blend");

   if (location < FRAG_RESULT_DATA0)
      return -1;
   else
      return location - FRAG_RESULT_DATA0;
}

static bool
nir_lower_blend_instr(nir_builder *b, nir_intrinsic_instr *store, void *data)
{
   struct ctx *ctx = data;
   const nir_lower_blend_options *options = ctx->options;
   if (store->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(store);
   int rt = color_index_for_location(sem.location);

   /* No blend lowering requested on this RT */
   if (rt < 0 || options->rt[rt].format == PIPE_FORMAT_NONE)
      return false;

   /* Only process stores once. Pass flags are cleared by consume_dual_stores */
   if (store->instr.pass_flags)
      return false;

   store->instr.pass_flags = 1;

   /* Store are sunk to the bottom of the block to ensure that the dual
    * source colour is already written.
    */
   b->cursor = nir_after_block(store->instr.block);

   const enum pipe_format format = options->rt[rt].format;
   enum pipe_logicop logicop_func = options->logicop_func;

   /* From the Vulkan spec ("Logical operations"):
    *
    *    Logical operations are not applied to floating-point or sRGB format
    *    color attachments...
    *
    *    If logicOpEnable is VK_TRUE... blending of all attachments is treated
    *    as if it were disabled. Any attachments using color formats for which
    *    logical operations are not supported simply pass through the color
    *    values unmodified.
    *
    * The semantic for unsupported formats is equivalent to a logicop of COPY.
    * It is /not/ equivalent to disabled logicops (which would incorrectly apply
    * blending). To implement this spec text with minimal special casing, we
    * override the logicop func to COPY for unsupported formats.
    */
   if (util_format_is_float(format) || util_format_is_srgb(format)) {
      logicop_func = PIPE_LOGICOP_COPY;
   }

   /* Don't bother copying the destination to the source for disabled RTs */
   if (options->rt[rt].colormask == 0 ||
       (options->logicop_enable && logicop_func == PIPE_LOGICOP_NOOP)) {

      nir_instr_remove(&store->instr);
      return true;
   }

   /* Grab the input color.  We always want 4 channels during blend.  Dead
    * code will clean up any channels we don't need.
    */
   nir_def *src = nir_pad_vector(b, store->src[0].ssa, 4);

   assert(nir_src_as_uint(store->src[1]) == 0 && "store_output invariant");

   /* Grab the previous fragment color if we need it */
   nir_def *dst;

   if (options->rt[rt].advanced_blend ||
       channel_uses_dest(options->rt[rt].rgb) ||
       channel_uses_dest(options->rt[rt].alpha) ||
       options->logicop_enable ||
       options->rt[rt].colormask != BITFIELD_MASK(4)) {

      b->shader->info.outputs_read |= BITFIELD64_BIT(sem.location);
      b->shader->info.fs.uses_fbfetch_output = true;
      b->shader->info.fs.uses_sample_shading = true;
      sem.fb_fetch_output = true;

      dst = nir_load_output(b, 4, nir_src_bit_size(store->src[0]),
                            nir_imm_int(b, 0),
                            .dest_type = nir_intrinsic_src_type(store),
                            .io_semantics = sem);
   } else {
      dst = nir_undef(b, 4, nir_src_bit_size(store->src[0]));
   }

   /* Blend the two colors per the passed options. Blending is prioritized as:
    * 1. Logic operations (if logicop_enable is true) - mutually exclusive with blending
    * 2. Advanced blending (if advanced_blend is set) - uses complex blend equations
    * 3. Standard blending (if configured) - uses traditional blend factors
    *
    * We only call nir_blend if blending is enabled with a blend mode other than replace
    * (independent of the color mask). That avoids unnecessary fsat instructions in the
    * common case where blending is disabled at an API level, but the driver calls
    * nir_blend (possibly for color masking).
    */
   nir_def *blended = src;

   if (options->logicop_enable) {
      blended = nir_color_logicop(b, src, dst, options->logicop_func, format);
   } else if (options->rt[rt].advanced_blend) {
      blended = nir_blend_advanced(b, options, rt, src, dst);
   } else if (!util_format_is_pure_integer(format) &&
              !nir_blend_replace_rt(&options->rt[rt])) {
      assert(!util_format_is_scaled(format));
      blended = nir_color_blend(b, src, ctx->src1[rt], dst, &options->rt[rt],
                                options->scalar_blend_const);
   }

   /* Apply a colormask if necessary */
   blended = nir_color_mask(b, blended, dst,
                            (unsigned) options->rt[rt].colormask);

   /* Shave off any components we don't want to store */
   const unsigned num_components = util_format_get_nr_components(format);
   blended = nir_trim_vector(b, blended, num_components);

   /* Grow or shrink the store destination as needed */
   store->num_components = num_components;
   nir_intrinsic_set_write_mask(store, nir_intrinsic_write_mask(store) &
                                          nir_component_mask(num_components));

   /* Write out the final color instead of the input */
   nir_src_rewrite(&store->src[0], blended);

   /* Sink to bottom */
   nir_instr_remove(&store->instr);
   nir_builder_instr_insert(b, &store->instr);
   return true;
}

/*
 * Dual-source colours are only for blending, so when nir_lower_blend is used,
 * the dual source store_output is for us (only). Remove dual stores so the
 * backend doesn't have to deal with them, collecting the sources for blending.
 */
static bool
consume_dual_stores(nir_builder *b, nir_intrinsic_instr *store, void *data)
{
   nir_def **outputs = data;
   if (store->intrinsic != nir_intrinsic_store_output)
      return false;

   /* While we're here, clear the pass flags for store_outputs, since we'll set
    * them later.
    */
   store->instr.pass_flags = 0;

   nir_io_semantics sem = nir_intrinsic_io_semantics(store);
   int rt = 0;
   if (sem.dual_source_blend_index)
      rt = color_index_for_location(sem.location);
   else if (sem.location != FRAG_RESULT_DUAL_SRC_BLEND)
      return false;

   assert(rt >= 0 && rt < 8 && "bounds for dual-source blending");

   outputs[rt] = store->src[0].ssa;
   nir_instr_remove(&store->instr);
   return true;
}

/** Lower blending to framebuffer fetch and some math
 *
 * This pass requires that shader I/O is lowered to explicit load/store
 * instructions using nir_lower_io.
 */
bool
nir_lower_blend(nir_shader *shader, const nir_lower_blend_options *options)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   struct ctx ctx = { .options = options };
   bool progress = nir_shader_intrinsics_pass(shader, consume_dual_stores,
                                              nir_metadata_control_flow,
                                              ctx.src1);

   progress |= nir_shader_intrinsics_pass(shader, nir_lower_blend_instr,
                                          nir_metadata_control_flow,
                                          &ctx);
   return progress;
}
