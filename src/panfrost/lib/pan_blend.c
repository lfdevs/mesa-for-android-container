/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_blend.h"
#include "util/blend.h"

#ifdef PAN_ARCH
#include "pan_texture.h"
#endif

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_conversion_builder.h"
#include "compiler/nir/nir_lower_blend.h"
#include "compiler/pan_compiler.h"
#include "compiler/pan_nir.h"
#include "util/format/u_format.h"

#ifndef PAN_ARCH

/* Fixed function blending */

static bool
factor_is_supported(enum pipe_blendfactor factor)
{
   factor = util_blendfactor_without_invert(factor);

   return factor != PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE &&
          factor != PIPE_BLENDFACTOR_SRC1_COLOR &&
          factor != PIPE_BLENDFACTOR_SRC1_ALPHA;
}

/* The set of factors supported by the hardware for floats is significantly
 * reduced.
 */
static bool
factor_is_supported_for_float(enum pipe_blendfactor factor)
{
   return factor == PIPE_BLENDFACTOR_ZERO ||
          factor == PIPE_BLENDFACTOR_ONE ||
          factor == PIPE_BLENDFACTOR_SRC_ALPHA ||
          factor == PIPE_BLENDFACTOR_INV_SRC_ALPHA;
}

/* OpenGL allows encoding (src*dest + dest*src) which is incompatiblle with
 * Midgard style blending since there are two multiplies. However, it may be
 * factored as 2*src*dest = dest*(2*src), which can be encoded on Bifrost as 0
 * + dest * (2*src) wih the new source_2 value of C. Detect this case. */

static bool
is_2srcdest(enum pipe_blend_func blend_func, enum pipe_blendfactor src_factor,
            enum pipe_blendfactor dest_factor, bool is_alpha)
{
   return (blend_func == PIPE_BLEND_ADD) &&
          ((src_factor == PIPE_BLENDFACTOR_DST_COLOR) ||
           ((src_factor == PIPE_BLENDFACTOR_DST_ALPHA) && is_alpha)) &&
          ((dest_factor == PIPE_BLENDFACTOR_SRC_COLOR) ||
           ((dest_factor == PIPE_BLENDFACTOR_SRC_ALPHA) && is_alpha));
}

static bool
can_fixed_function_equation(unsigned arch,
                            enum pipe_blend_func blend_func,
                            enum pipe_blendfactor src_factor,
                            enum pipe_blendfactor dest_factor,
                            bool is_alpha, bool is_float,
                            bool supports_2src)
{
   /* We can only do add/subtract in hardware.  No min/max. */
   if (blend_func != PIPE_BLEND_ADD && blend_func != PIPE_BLEND_SUBTRACT &&
       blend_func != PIPE_BLEND_REVERSE_SUBTRACT)
      return false;

   if (is_float) {
      /* There are a couple special cases for add with zero */
      if (blend_func == PIPE_BLEND_ADD &&
          src_factor == PIPE_BLENDFACTOR_ZERO &&
          (dest_factor == PIPE_BLENDFACTOR_INV_SRC_COLOR ||
           dest_factor == PIPE_BLENDFACTOR_DST_ALPHA))
         return true;

      return factor_is_supported_for_float(src_factor) &&
             factor_is_supported_for_float(dest_factor);
   } else {
      if (is_2srcdest(blend_func, src_factor, dest_factor, is_alpha))
         return supports_2src;

      if (!factor_is_supported(src_factor) || !factor_is_supported(dest_factor))
         return false;

      /* Fixed function requires src/dest factors to match (up to invert) or be
       * zero/one.
       */
      enum pipe_blendfactor src = util_blendfactor_without_invert(src_factor);
      enum pipe_blendfactor dest = util_blendfactor_without_invert(dest_factor);

      if (arch < 6 &&
          (src == PIPE_BLENDFACTOR_DST_ALPHA))
         return false;
      return (src == dest) || (src == PIPE_BLENDFACTOR_ONE) ||
             (dest == PIPE_BLENDFACTOR_ONE);
   }
}

static unsigned
blend_factor_constant_mask(enum pipe_blendfactor factor)
{
   factor = util_blendfactor_without_invert(factor);

   if (factor == PIPE_BLENDFACTOR_CONST_COLOR)
      return 0b0111; /* RGB */
   else if (factor == PIPE_BLENDFACTOR_CONST_ALPHA)
      return 0b1000; /* A */
   else
      return 0b0000; /* - */
}

unsigned
pan_blend_constant_mask(const struct pan_blend_equation eq)
{
   return blend_factor_constant_mask(eq.rgb_src_factor) |
          blend_factor_constant_mask(eq.rgb_dst_factor) |
          blend_factor_constant_mask(eq.alpha_src_factor) |
          blend_factor_constant_mask(eq.alpha_dst_factor);
}

static inline bool
is_min_max(enum pipe_blend_func func)
{
   return func == PIPE_BLEND_MIN || func == PIPE_BLEND_MAX;
}

void
pan_blend_optimize_equation(struct pan_blend_equation *eq,
                            enum pipe_format format,
                            const float *constants)
{
   unsigned comp_mask = 0xf;

   if (!eq->blend_enable)
      return;

   /* Sanitize alpha blend factors because later optimizations rely on COLOR
    * actually meaning color.
    */
   eq->alpha_src_factor = util_blendfactor_to_alpha(eq->alpha_src_factor);
   eq->alpha_dst_factor = util_blendfactor_to_alpha(eq->alpha_dst_factor);

   if (is_min_max(eq->rgb_func)) {
      eq->rgb_src_factor = PIPE_BLENDFACTOR_ONE;
      eq->rgb_dst_factor = PIPE_BLENDFACTOR_ONE;
   }

   if (is_min_max(eq->alpha_func)) {
      eq->alpha_src_factor = PIPE_BLENDFACTOR_ONE;
      eq->alpha_dst_factor = PIPE_BLENDFACTOR_ONE;
   }

   /* If we know the format, we can optimize a few things */
   if (format != PIPE_FORMAT_NONE) {
      const struct util_format_description *fmt =
         util_format_description(format);
      assert(eq->is_float == (fmt->channel[0].type == UTIL_FORMAT_TYPE_FLOAT));

      comp_mask = util_format_colormask(fmt);

      /* Check to see if any of the disabled channels actually matter.  If
       * not, smash the blend mask to 0xf.  Otherwise, pan_blend_is_opaque()
       * may return false unnecessarily.
       */
      if (!((~eq->color_mask) & comp_mask))
         eq->color_mask = 0xf;

      if (!(comp_mask & 0b1000)) {
         eq->rgb_src_factor =
            util_blend_dst_alpha_to_one(eq->rgb_src_factor);
         eq->rgb_dst_factor =
            util_blend_dst_alpha_to_one(eq->rgb_dst_factor);

         eq->alpha_src_factor =
            util_blend_dst_alpha_to_one(eq->alpha_src_factor);
         eq->alpha_dst_factor =
            util_blend_dst_alpha_to_one(eq->alpha_dst_factor);
      }
   }

   /* If we know the blend constants, we can optimize more */
   unsigned constant_mask = pan_blend_constant_mask(*eq);
   if (constant_mask && constants != NULL) {
      enum pipe_blendfactor const_alpha = PIPE_BLENDFACTOR_CONST_ALPHA;
      if (constant_mask & 0b1000) {
         if (constants[3] == 0.0f)
            const_alpha = PIPE_BLENDFACTOR_ZERO;
         else if (constants[3] == 1.0f)
            const_alpha = PIPE_BLENDFACTOR_ONE;
      }

      /* Each color blend constant can only affect it's corresponding channel
       * so we only care about components actually in our format.
       */
      const unsigned color_constant_mask = comp_mask & constant_mask & 0b0111;

      enum pipe_blendfactor const_color = PIPE_BLENDFACTOR_CONST_COLOR;
      if (color_constant_mask) {
         bool all_zero = true, all_one = true;
         u_foreach_bit(i, color_constant_mask) {
            if (constants[i] != 0.0f)
               all_zero = false;
            if (constants[i] != 1.0f)
               all_one = false;
         }
         assert(!all_zero || !all_one);
         if (all_zero)
            const_color = PIPE_BLENDFACTOR_ZERO;
         if (all_one)
            const_color = PIPE_BLENDFACTOR_ONE;
      }

      if (const_color != PIPE_BLENDFACTOR_CONST_COLOR ||
          const_alpha != PIPE_BLENDFACTOR_CONST_ALPHA) {
#define REPLACE_CONST_FACTOR(factor) do {             \
   if (factor == PIPE_BLENDFACTOR_CONST_COLOR)        \
      factor = const_color;                           \
   else if (factor == PIPE_BLENDFACTOR_CONST_ALPHA)   \
      factor = const_alpha;                           \
} while (false)

         REPLACE_CONST_FACTOR(eq->rgb_src_factor);
         REPLACE_CONST_FACTOR(eq->rgb_dst_factor);
         REPLACE_CONST_FACTOR(eq->alpha_src_factor);
         REPLACE_CONST_FACTOR(eq->alpha_dst_factor);

#undef REPLACE_CONST_FACTOR
      }
   }

   if (pan_blend_is_opaque(*eq))
      eq->blend_enable = false;
}

/* Only "homogenous" (scalar or vector with all components equal) constants are
 * valid for fixed-function, so check for this condition */

bool
pan_blend_is_homogenous_constant(unsigned mask, const float *constants)
{
   float constant = pan_blend_get_constant(mask, constants);

   u_foreach_bit(i, mask) {
      if (constants[i] != constant)
         return false;
   }

   return true;
}

uint16_t
pan_pack_blend_constant(enum pipe_format format, float cons)
{
   const struct util_format_description *format_desc =
      util_format_description(format);

   /* Mali doesn't support float blend constants */
   assert(format_desc->channel[0].type != UTIL_FORMAT_TYPE_FLOAT);

   /* On Bifrost, the blend constant is expressed with a UNORM of the
    * size of the target format. The value is then shifted such that
    * used bits are in the MSB.
    */
   unsigned chan_size = 0;
   for (unsigned i = 0; i < format_desc->nr_channels; i++)
      chan_size = MAX2(format_desc->channel[0].size, chan_size);

   float factor = ((1 << chan_size) - 1) << (16 - chan_size);

   return cons * factor;
}

/* Determines if an equation can run in fixed function */

bool
pan_blend_can_fixed_function(unsigned arch,
                             const struct pan_blend_equation equation,
                             bool supports_2src)
{
   return !equation.blend_enable ||
          (can_fixed_function_equation(arch,
              equation.rgb_func, equation.rgb_src_factor,
              equation.rgb_dst_factor, false /* is_alpha */,
              equation.is_float, supports_2src) &&
           can_fixed_function_equation(arch,
              equation.alpha_func, equation.alpha_src_factor,
              equation.alpha_dst_factor, true /* is_alpha */,
              equation.is_float, supports_2src));
}

static enum mali_blend_operand_c
to_c_factor(enum pipe_blendfactor factor)
{
   switch (util_blendfactor_without_invert(factor)) {
   case PIPE_BLENDFACTOR_ONE:
      /* Extra invert to flip back in caller */
      return MALI_BLEND_OPERAND_C_ZERO;

   case PIPE_BLENDFACTOR_SRC_ALPHA:
      return MALI_BLEND_OPERAND_C_SRC_ALPHA;

   case PIPE_BLENDFACTOR_DST_ALPHA:
      return MALI_BLEND_OPERAND_C_DEST_ALPHA;

   case PIPE_BLENDFACTOR_SRC_COLOR:
      return MALI_BLEND_OPERAND_C_SRC;

   case PIPE_BLENDFACTOR_DST_COLOR:
      return MALI_BLEND_OPERAND_C_DEST;

   case PIPE_BLENDFACTOR_CONST_COLOR:
   case PIPE_BLENDFACTOR_CONST_ALPHA:
      return MALI_BLEND_OPERAND_C_CONSTANT;

   default:
      UNREACHABLE("Unsupported blend factor");
   }
}

static void
to_mali_function(enum pipe_blend_func blend_func,
                 enum pipe_blendfactor src_factor,
                 enum pipe_blendfactor dest_factor,
                 bool is_alpha, bool is_float,
                 struct MALI_BLEND_FUNCTION *function)
{
   /* We handle ZERO/ONE specially since it's the hardware has 0 and can invert
    * to 1 but Gallium has 0 as the uninverted version.
    */
   bool src_inverted =
      util_blendfactor_is_inverted(src_factor) ^
      (util_blendfactor_without_invert(src_factor) == PIPE_BLENDFACTOR_ONE);

   bool dest_inverted =
      util_blendfactor_is_inverted(dest_factor) ^
      (util_blendfactor_without_invert(dest_factor) == PIPE_BLENDFACTOR_ONE);

   if (src_factor == PIPE_BLENDFACTOR_ZERO) {
      function->a = MALI_BLEND_OPERAND_A_ZERO;
      function->b = MALI_BLEND_OPERAND_B_DEST;
      if (blend_func == PIPE_BLEND_SUBTRACT)
         function->negate_b = true;
      function->invert_c = dest_inverted;
      function->c = to_c_factor(dest_factor);
   } else if (src_factor == PIPE_BLENDFACTOR_ONE) {
      function->a = MALI_BLEND_OPERAND_A_SRC;
      function->b = MALI_BLEND_OPERAND_B_DEST;
      if (blend_func == PIPE_BLEND_SUBTRACT)
         function->negate_b = true;
      else if (blend_func == PIPE_BLEND_REVERSE_SUBTRACT)
         function->negate_a = true;
      function->invert_c = dest_inverted;
      function->c = to_c_factor(dest_factor);
   } else if (dest_factor == PIPE_BLENDFACTOR_ZERO) {
      function->a = MALI_BLEND_OPERAND_A_ZERO;
      function->b = MALI_BLEND_OPERAND_B_SRC;
      if (blend_func == PIPE_BLEND_REVERSE_SUBTRACT)
         function->negate_b = true;
      function->invert_c = src_inverted;
      function->c = to_c_factor(src_factor);
   } else if (dest_factor == PIPE_BLENDFACTOR_ONE) {
      function->a = MALI_BLEND_OPERAND_A_DEST;
      function->b = MALI_BLEND_OPERAND_B_SRC;
      if (blend_func == PIPE_BLEND_SUBTRACT)
         function->negate_a = true;
      else if (blend_func == PIPE_BLEND_REVERSE_SUBTRACT)
         function->negate_b = true;
      function->invert_c = src_inverted;
      function->c = to_c_factor(src_factor);
   } else if (src_factor == dest_factor) {
      function->a = MALI_BLEND_OPERAND_A_ZERO;
      function->invert_c = src_inverted;
      function->c = to_c_factor(src_factor);

      switch (blend_func) {
      case PIPE_BLEND_ADD:
         function->b = MALI_BLEND_OPERAND_B_SRC_PLUS_DEST;
         break;
      case PIPE_BLEND_REVERSE_SUBTRACT:
         function->negate_b = true;
         FALLTHROUGH;
      case PIPE_BLEND_SUBTRACT:
         function->b = MALI_BLEND_OPERAND_B_SRC_MINUS_DEST;
         break;
      default:
         UNREACHABLE("Invalid blend function");
      }
   } else if (is_2srcdest(blend_func, src_factor, dest_factor, is_alpha)) {
      /* src*dest + dest*src = 2*src*dest = 0 + dest*(2*src) */
      function->a = MALI_BLEND_OPERAND_A_ZERO;
      function->b = MALI_BLEND_OPERAND_B_DEST;
      function->c = MALI_BLEND_OPERAND_C_SRC_X_2;
   } else {
      assert(util_blendfactor_without_invert(src_factor) ==
                util_blendfactor_without_invert(dest_factor) &&
             src_inverted != dest_inverted);

      function->a = MALI_BLEND_OPERAND_A_DEST;
      function->invert_c = src_inverted;
      function->c = to_c_factor(src_factor);

      switch (blend_func) {
      case PIPE_BLEND_ADD:
         function->b = MALI_BLEND_OPERAND_B_SRC_MINUS_DEST;
         break;
      case PIPE_BLEND_REVERSE_SUBTRACT:
         function->b = MALI_BLEND_OPERAND_B_SRC_PLUS_DEST;
         function->negate_b = true;
         break;
      case PIPE_BLEND_SUBTRACT:
         function->b = MALI_BLEND_OPERAND_B_SRC_PLUS_DEST;
         function->negate_a = true;
         break;
      default:
         UNREACHABLE("Invalid blend function\n");
      }
   }
}

bool
pan_blend_is_opaque(const struct pan_blend_equation equation)
{
   /* If a channel is masked out, we can't use opaque mode even if
    * blending is disabled, since we need a tilebuffer read in there */
   if (equation.color_mask != 0xF)
      return false;

   /* With nothing masked out, disabled bledning is opaque */
   if (!equation.blend_enable)
      return true;

   /* NOTE (NaN/inf):
    *
    * Technically, we should reject this optimization for float blending
    * because 0.0 * NaN/inf = NaN.  However, Vulkan and OpenGL both allow us
    * to drop inf/NaN pretty much at-will and the NIR blending we would fall
    * back to will also drop 0.0 blend factors.  One day we might want to do
    * all this behind a driconf flag but today is not that day.
    */

   /* Also detect open-coded opaque blending */
   return equation.rgb_src_factor == PIPE_BLENDFACTOR_ONE &&
          equation.rgb_dst_factor == PIPE_BLENDFACTOR_ZERO &&
          (equation.rgb_func == PIPE_BLEND_ADD ||
           equation.rgb_func == PIPE_BLEND_SUBTRACT) &&
          equation.alpha_src_factor == PIPE_BLENDFACTOR_ONE &&
          equation.alpha_dst_factor == PIPE_BLENDFACTOR_ZERO &&
          (equation.alpha_func == PIPE_BLEND_ADD ||
           equation.alpha_func == PIPE_BLEND_SUBTRACT);
}

/* Check if a factor represents a constant value of val, assuming src_alpha is
 * the given constant.
 */

static inline bool
is_factor_01(enum pipe_blendfactor factor, unsigned val, unsigned srca)
{
   assert(val == 0 || val == 1);
   assert(srca == 0 || srca == 1);

   switch (factor) {
   case PIPE_BLENDFACTOR_ZERO:
      return (val == 0);

   case PIPE_BLENDFACTOR_ONE:
      return (val == 1);

   case PIPE_BLENDFACTOR_SRC_ALPHA:
      return (val == srca);

   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
      return (val == (1 - srca));

   default:
      return false;
   }
}

/* Returns if src alpha = 0 implies the blended colour equals the destination
 * colour. Suppose source alpha = 0 and consider cases.
 *
 * Additive blending: Equivalent to D = S * f_s + D * f_d for all D and all S
 * with S_a = 0, for each component. For the alpha component (if it unmasked),
 * we have S_a = 0 so this reduces to D = D * f_d <===> f_d = 1. For RGB
 * components (if unmasked), we need f_s = 0 and f_d = 1.
 *
 * Subtractive blending: Fails in general (D = S * f_S - D * f_D). We
 * would need f_S = 0 and f_D = -1, which is not valid in the APIs.
 *
 * Reverse subtractive blending (D = D * f_D - S * f_S), we need f_D = 1
 * and f_S = 0 up to masking. This is the same as additive blending.
 *
 * Min/max: Fails in general on the RGB components.
 */

bool
pan_blend_alpha_zero_nop(const struct pan_blend_equation eq)
{
   if (eq.rgb_func != PIPE_BLEND_ADD &&
       eq.rgb_func != PIPE_BLEND_REVERSE_SUBTRACT)
      return false;

   if (eq.color_mask & 0x8) {
      if (!is_factor_01(eq.alpha_dst_factor, 1, 0))
         return false;
   }

   if (eq.color_mask & 0x7) {
      if (!is_factor_01(eq.rgb_dst_factor, 1, 0))
         return false;

      if (!is_factor_01(eq.rgb_src_factor, 0, 0))
         return false;
   }

   return true;
}

/* Returns if src alpha = 1 implies the blended colour equals the source
 * colour. Suppose source alpha = 1 and consider cases.
 *
 * Additive blending: S = S * f_s + D * f_d. We need f_s = 1 and f_d = 0.
 *
 * Subtractive blending: S = S * f_s - D * f_d. Same as additive blending.
 *
 * Reverse subtractive blending: S = D * f_d - S * f_s. Fails in general since
 * it would require f_s = -1, which is not valid in the APIs.
 *
 * Min/max: Fails in general on the RGB components.
 *
 * Note if any component is masked, we can't use a store.
 */

bool
pan_blend_alpha_one_store(const struct pan_blend_equation eq)
{
   if (eq.rgb_func != PIPE_BLEND_ADD && eq.rgb_func != PIPE_BLEND_SUBTRACT)
      return false;

   if (eq.color_mask != 0xf)
      return false;

   return is_factor_01(eq.rgb_src_factor, 1, 1) &&
          is_factor_01(eq.alpha_src_factor, 1, 1) &&
          is_factor_01(eq.rgb_dst_factor, 0, 1) &&
          is_factor_01(eq.alpha_dst_factor, 0, 1);
}

static bool
is_dest_factor(enum pipe_blendfactor factor, bool alpha)
{
   factor = util_blendfactor_without_invert(factor);

   return factor == PIPE_BLENDFACTOR_DST_ALPHA ||
          factor == PIPE_BLENDFACTOR_DST_COLOR ||
          (factor == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE && !alpha);
}

/* Determines if a blend equation reads back the destination. This can occur by
 * explicitly referencing the destination in the blend equation, or by using a
 * partial writemask. */

bool
pan_blend_reads_dest(const struct pan_blend_equation equation)
{
   if (equation.color_mask && equation.color_mask != 0xF)
      return true;

   if (!equation.blend_enable)
      return false;

   /* NOTE (NaN/inf):
    *
    * Technically, we should reject this optimization for float blending
    * because 0.0 * NaN/inf = NaN.  However, Vulkan and OpenGL both allow us
    * to drop inf/NaN pretty much at-will and the NIR blending we would fall
    * back to will also drop 0.0 blend factors.  One day we might want to do
    * all this behind a driconf flag but today is not that day.
    */

   /* Min/max blending ignores the factors so the destination always gets
    * read verbatim.
    */
   if (is_min_max(equation.rgb_func) || is_min_max(equation.alpha_func))
      return true;

   return is_dest_factor(equation.rgb_src_factor, false) ||
          is_dest_factor(equation.alpha_src_factor, true) ||
          equation.rgb_dst_factor != PIPE_BLENDFACTOR_ZERO ||
          equation.alpha_dst_factor != PIPE_BLENDFACTOR_ZERO;
}

/* Create the descriptor for a fixed blend mode given the corresponding API
 * state. Assumes the equation can be represented as fixed-function. */

void
pan_blend_to_fixed_function_equation(const struct pan_blend_equation equation,
                                     struct MALI_BLEND_EQUATION *out)
{
   /* If no blending is enabled, default back on `replace` mode */
   if (!equation.blend_enable) {
      out->color_mask = equation.color_mask;
      out->rgb.a = MALI_BLEND_OPERAND_A_SRC;
      out->rgb.b = MALI_BLEND_OPERAND_B_SRC;
      out->rgb.c = MALI_BLEND_OPERAND_C_ZERO;
      out->alpha.a = MALI_BLEND_OPERAND_A_SRC;
      out->alpha.b = MALI_BLEND_OPERAND_B_SRC;
      out->alpha.c = MALI_BLEND_OPERAND_C_ZERO;
      return;
   }

   /* Compile the fixed-function blend */
   to_mali_function(equation.rgb_func, equation.rgb_src_factor,
                    equation.rgb_dst_factor, false /* is_alpha */,
                    equation.is_float, &out->rgb);
   to_mali_function(equation.alpha_func, equation.alpha_src_factor,
                    equation.alpha_dst_factor, true /* is_alpha */,
                    equation.is_float, &out->alpha);

   out->color_mask = equation.color_mask;
}

uint32_t
pan_pack_blend(const struct pan_blend_equation equation)
{
   struct mali_blend_equation_packed out;

   pan_pack(&out, BLEND_EQUATION, cfg) {
      pan_blend_to_fixed_function_equation(equation, &cfg);
   }

   return out.opaque[0];
}

enum mali_register_file_format
pan_blend_type_from_nir(nir_alu_type nir_type)
{
   switch (nir_type) {
   case 0: /* Render target not in use */
      return 0;
   case nir_type_float16:
      return MALI_REGISTER_FILE_FORMAT_F16;
   case nir_type_float32:
      return MALI_REGISTER_FILE_FORMAT_F32;
   case nir_type_int32:
      return MALI_REGISTER_FILE_FORMAT_I32;
   case nir_type_uint32:
      return MALI_REGISTER_FILE_FORMAT_U32;
   case nir_type_int16:
      return MALI_REGISTER_FILE_FORMAT_I16;
   case nir_type_uint16:
      return MALI_REGISTER_FILE_FORMAT_U16;
   default:
      UNREACHABLE("Unsupported blend shader type for NIR alu type");
      return 0;
   }
}

#else /* ifndef PAN_ARCH */

static const char *
logicop_str(enum pipe_logicop logicop)
{
   switch (logicop) {
   case PIPE_LOGICOP_CLEAR:
      return "clear";
   case PIPE_LOGICOP_NOR:
      return "nor";
   case PIPE_LOGICOP_AND_INVERTED:
      return "and-inverted";
   case PIPE_LOGICOP_COPY_INVERTED:
      return "copy-inverted";
   case PIPE_LOGICOP_AND_REVERSE:
      return "and-reverse";
   case PIPE_LOGICOP_INVERT:
      return "invert";
   case PIPE_LOGICOP_XOR:
      return "xor";
   case PIPE_LOGICOP_NAND:
      return "nand";
   case PIPE_LOGICOP_AND:
      return "and";
   case PIPE_LOGICOP_EQUIV:
      return "equiv";
   case PIPE_LOGICOP_NOOP:
      return "noop";
   case PIPE_LOGICOP_OR_INVERTED:
      return "or-inverted";
   case PIPE_LOGICOP_COPY:
      return "copy";
   case PIPE_LOGICOP_OR_REVERSE:
      return "or-reverse";
   case PIPE_LOGICOP_OR:
      return "or";
   case PIPE_LOGICOP_SET:
      return "set";
   default:
      UNREACHABLE("Invalid logicop\n");
   }
}

static void
get_equation_str(const struct pan_blend_rt_state *rt_state, char *str,
                 unsigned len)
{
   const char *funcs[] = {
      "add", "sub", "reverse_sub", "min", "max",
   };
   const char *factors[] = {
      "",           "one",           "src_color",   "src_alpha",   "dst_alpha",
      "dst_color",  "src_alpha_sat", "const_color", "const_alpha", "src1_color",
      "src1_alpha",
   };
   int ret;

   if (!rt_state->equation.blend_enable) {
      ret = snprintf(str, len, "replace(%s%s%s%s)",
                     (rt_state->equation.color_mask & 1) ? "R" : "",
                     (rt_state->equation.color_mask & 2) ? "G" : "",
                     (rt_state->equation.color_mask & 4) ? "B" : "",
                     (rt_state->equation.color_mask & 8) ? "A" : "");
      assert(ret > 0);
      return;
   }

   if (rt_state->equation.color_mask & 7) {
      assert(rt_state->equation.rgb_func < ARRAY_SIZE(funcs));
      ret = snprintf(
         str, len, "%s%s%s(func=%s,src_factor=%s%s,dst_factor=%s%s)%s",
         (rt_state->equation.color_mask & 1) ? "R" : "",
         (rt_state->equation.color_mask & 2) ? "G" : "",
         (rt_state->equation.color_mask & 4) ? "B" : "",
         funcs[rt_state->equation.rgb_func],
         util_blendfactor_is_inverted(rt_state->equation.rgb_src_factor) ? "-"
                                                                         : "",
         factors[util_blendfactor_without_invert(
            rt_state->equation.rgb_src_factor)],
         util_blendfactor_is_inverted(rt_state->equation.rgb_dst_factor) ? "-"
                                                                         : "",
         factors[util_blendfactor_without_invert(
            rt_state->equation.rgb_dst_factor)],
         rt_state->equation.color_mask & 8 ? ";" : "");
      assert(ret > 0);
      str += ret;
      len -= ret;
   }

   if (rt_state->equation.color_mask & 8) {
      assert(rt_state->equation.alpha_func < ARRAY_SIZE(funcs));
      ret = snprintf(
         str, len, "A(func=%s,src_factor=%s%s,dst_factor=%s%s)",
         funcs[rt_state->equation.alpha_func],
         util_blendfactor_is_inverted(rt_state->equation.alpha_src_factor) ? "-"
                                                                           : "",
         factors[util_blendfactor_without_invert(
            rt_state->equation.alpha_src_factor)],
         util_blendfactor_is_inverted(rt_state->equation.alpha_dst_factor) ? "-"
                                                                           : "",
         factors[util_blendfactor_without_invert(
            rt_state->equation.alpha_dst_factor)]);
      assert(ret > 0);
      str += ret;
      len -= ret;
   }
}

enum pipe_format
GENX(pan_blend_shader_fmt)(enum pipe_format format)
{
   switch (format) {
#if PAN_ARCH < 6
   case PIPE_FORMAT_A16_FLOAT:
   case PIPE_FORMAT_I16_FLOAT:
      return PIPE_FORMAT_R16G16B16A16_FLOAT;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_R10G10B10X2_UNORM:
      return PIPE_FORMAT_R8G8B8A8_UNORM;
#endif
   case PIPE_FORMAT_A8_UNORM:
   case PIPE_FORMAT_L8A8_UNORM:
      return PIPE_FORMAT_R8G8B8A8_UNORM;
   default:
      return format;
   }
}

nir_shader *
GENX(pan_blend_create_shader)(const struct pan_blend_state *state,
                              nir_alu_type src0_type, nir_alu_type src1_type,
                              unsigned rt)
{
   const struct pan_blend_rt_state *rt_state = &state->rts[rt];
   char equation_str[128] = {0};

   get_equation_str(rt_state, equation_str, sizeof(equation_str));

   const nir_shader_compiler_options *compiler_options =
      pan_get_nir_shader_compiler_options(PAN_ARCH, false);
   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, compiler_options,
      "pan_blend(rt=%d,fmt=%s,nr_samples=%d,%s=%s)", rt,
      util_format_name(rt_state->format), rt_state->nr_samples,
      state->logicop_enable ? "logicop" : "equation",
      state->logicop_enable ? logicop_str(state->logicop_func) : equation_str);
   nir_builder *b = &builder;

   const enum pipe_format format = GENX(pan_blend_shader_fmt)(rt_state->format);
   const struct util_format_description *format_desc =
      util_format_description(format);

   /* Choose a type which is not going to lead to precision loss while
    * blending.  If we're not dual-source blending, src1_type will be
    * nir_type_invalid which has a size of zero.
    */
   nir_alu_type dest_type = pan_unpacked_type_for_format(format_desc);
   if (PAN_ARCH >= 6 && nir_alu_type_get_type_size(dest_type) == 8)
      dest_type = nir_alu_type_get_base_type(dest_type) | 16;

   const unsigned dest_bit_size = nir_alu_type_get_type_size(dest_type);
   const nir_alu_type dest_base_type = nir_alu_type_get_base_type(dest_type);

   /* Midgard doesn't always provide types at all but it's always float32 */
   src0_type = src0_type ?: nir_type_float32;
   src1_type = src1_type ?: nir_type_float32;

   nir_def *src0 = nir_load_blend_input_pan(b,
      4, nir_alu_type_get_type_size(src0_type),
      .io_semantics.location = FRAG_RESULT_DATA0 + rt,
      .io_semantics.dual_source_blend_index = 0,
      .io_semantics.num_slots = 1,
      .dest_type = src0_type);

   nir_def *src1 = nir_load_blend_input_pan(b,
      4, nir_alu_type_get_type_size(src1_type),
      .io_semantics.location = FRAG_RESULT_DATA0 + rt,
      .io_semantics.dual_source_blend_index = 1,
      .io_semantics.num_slots = 1,
      .dest_type = src1_type);

   /* Make sure everyone is the same type.  We assume the destination type
    * here because TGSI sometimes gives us bogus types.  When they're not
    * bogus, shader types are required to match the format anyway.
    *
    * On Midgard, the blend shader is responsible for format conversion.
    * As the OpenGL spec requires integer conversions to saturate, we must
    * saturate ourselves here. On Bifrost and later, the conversion
    * hardware handles this automatically.
    */
   bool should_saturate = PAN_ARCH <= 5 && dest_base_type != nir_type_float;
   src0 = nir_convert_with_rounding(b, src0, dest_base_type, dest_type,
                                    nir_rounding_mode_undef, should_saturate);
   src1 = nir_convert_with_rounding(b, src1, dest_base_type, dest_type,
                                    nir_rounding_mode_undef, should_saturate);

   if (state->alpha_to_one && dest_base_type == nir_type_float) {
      nir_def *one = nir_imm_floatN_t(b, 1.0, dest_bit_size);
      src0 = nir_vector_insert_imm(b, src0, one, 3);
      src1 = nir_vector_insert_imm(b, src1, one, 3);
   }

#if PAN_ARCH >= 6
   const uint64_t opaque_blend_desc =
      GENX(pan_blend_get_internal_desc)(format, rt, dest_bit_size, false);
#else
   const uint64_t opaque_blend_desc = 0;
#endif

   nir_def *dest;
   if (PAN_ARCH >= 6) {
      nir_def *sample_id =
         rt_state->nr_samples > 1 ? nir_load_sample_id(b) : nir_imm_int(b, 0);
      dest = nir_load_tile_pan(b,
         4, dest_bit_size,
         pan_nir_tile_rt_sample(b, nir_imm_int(b, rt), sample_id),
         pan_nir_tile_default_coverage(b),
         nir_imm_int(b, opaque_blend_desc >> 32),
         .dest_type = dest_type,
         .io_semantics.location = FRAG_RESULT_DATA0 + rt,
         .io_semantics.num_slots = 1);
   } else {
      dest = nir_load_output(b,
         4, dest_bit_size,
         nir_imm_int(b, 0),
         .dest_type = dest_type,
         .io_semantics.location = FRAG_RESULT_DATA0 + rt,
         .io_semantics.num_slots = 1);
   }

   nir_def *color = src0;
   if (state->logicop_enable) {
      color = nir_color_logicop(b, src0, dest, state->logicop_func, format);
   } else if (rt_state->equation.blend_enable) {
      const nir_lower_blend_rt nir_rt = {
         .format = format,
         .rgb.func = rt_state->equation.rgb_func,
         .rgb.src_factor = rt_state->equation.rgb_src_factor,
         .rgb.dst_factor = rt_state->equation.rgb_dst_factor,
         .alpha.func = rt_state->equation.alpha_func,
         .alpha.src_factor = rt_state->equation.alpha_src_factor,
         .alpha.dst_factor = rt_state->equation.alpha_dst_factor,
         .colormask = rt_state->equation.color_mask,
      };
      color = nir_color_blend(b, src0, src1, dest, &nir_rt, false);
   }

   color = nir_color_mask(b, color, dest, rt_state->equation.color_mask);

   /* Throw away any channels we don't need */
   color = nir_color_mask(b, color, nir_undef(b, 4, dest_bit_size),
                          util_format_colormask(format_desc));

   /* Only write the destination if it changed */
   if (color != dest) {
      if (PAN_ARCH >= 6) {
         nir_blend_pan(b, nir_load_cumulative_coverage_pan(b),
                       nir_imm_int64(b, opaque_blend_desc),
                       color,
                       .src_type = dest_type,
                       .io_semantics.location = FRAG_RESULT_DATA0 + rt,
                       .io_semantics.num_slots = 1);
      } else {
         nir_store_output(b, color, nir_imm_int(b, 0),
                          .src_type = dest_type,
                          .io_semantics.location = FRAG_RESULT_DATA0 + rt,
                          .io_semantics.num_slots = 1);
      }
   }

   if (PAN_ARCH >= 6)
      nir_blend_return_pan(b);

   b->shader->info.io_lowered = true;

   return builder.shader;
}

#if PAN_ARCH >= 6

#if PAN_ARCH < 9
static enum mali_register_file_format
get_register_format(nir_alu_type T)
{
   switch (T) {
   case nir_type_float16:
      return MALI_REGISTER_FILE_FORMAT_F16;
   case nir_type_float32:
      return MALI_REGISTER_FILE_FORMAT_F32;
   case nir_type_int8:
   case nir_type_int16:
      return MALI_REGISTER_FILE_FORMAT_I16;
   case nir_type_int32:
      return MALI_REGISTER_FILE_FORMAT_I32;
   case nir_type_uint8:
   case nir_type_uint16:
      return MALI_REGISTER_FILE_FORMAT_U16;
   case nir_type_uint32:
      return MALI_REGISTER_FILE_FORMAT_U32;
   default:
      UNREACHABLE("Invalid format");
   }
}
#endif

uint64_t
GENX(pan_blend_get_internal_desc)(enum pipe_format fmt, unsigned rt,
                                  unsigned force_size, bool dithered)
{
   const struct util_format_description *desc = util_format_description(fmt);
   struct mali_internal_blend_packed res;

   pan_pack(&res, INTERNAL_BLEND, cfg) {
      cfg.mode = MALI_BLEND_MODE_OPAQUE;
      cfg.fixed_function.num_comps = desc->nr_channels;
      cfg.fixed_function.rt = rt;

#if PAN_ARCH < 9
      nir_alu_type T = pan_unpacked_type_for_format(desc);

      if (force_size)
         T = nir_alu_type_get_base_type(T) | force_size;

      cfg.fixed_function.conversion.register_format = get_register_format(T);
#endif

      cfg.fixed_function.conversion.memory_format =
         GENX(pan_dithered_format_from_pipe_format)(fmt, dithered);
   }

   return res.opaque[0] | ((uint64_t)res.opaque[1] << 32);
}

#if PAN_ARCH < 9
enum mali_register_file_format
GENX(pan_fixup_blend_type)(nir_alu_type T_size, enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   unsigned size = nir_alu_type_get_type_size(T_size);
   nir_alu_type T_format = pan_unpacked_type_for_format(desc);
   nir_alu_type T = nir_alu_type_get_base_type(T_format) | size;

   return pan_blend_type_from_nir(T);
}
#endif

#endif

#endif /* ifndef PAN_ARCH */
