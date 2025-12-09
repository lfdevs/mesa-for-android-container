/*
 * Copyright © 2024 Collabora, Ltd.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nir_test.h"
#include "nir_format_convert.h"

#include "util/macros.h"
#include "util/format/u_formats.h"
#include "util/format_srgb.h"
#include "util/half_float.h"
#include "util/u_endian.h"
#include "util/u_math.h"

/* These tests and the NIR code currently assume little endian */
#if UTIL_ARCH_LITTLE_ENDIAN

#define NUM_COLORS 32

class nir_format_convert_test
   : public nir_test
   , public testing::WithParamInterface<pipe_format>
{
protected:
   nir_format_convert_test()
      : nir_test::nir_test("nir_format_convert_test")
   { }
};

/* Repeatable random number generator */
static uint16_t
rand_u16(void)
{
   static const uint16_t data[] = {
   /* Important numbers we want to hit, twice for good measure */
      0, 0, 128, 128, 255, 255, 256, 256, 32768, 32768, 65535, 65535,

   /* 100 random 16-bit numbers from random.org */
      31401 , 17066 , 65230 , 954   , 12680 ,
      3631  , 45135 , 1477  , 40861 , 62785 ,
      35775 , 3171  , 24120 , 6774  , 24488 ,
      12377 , 29039 , 22146 , 27893 , 62104 ,
      15854 , 36623 , 23545 , 47719 , 31666 ,
      23657 , 43795 , 64813 , 18846 , 32886 ,
      48464 , 28934 , 48575 , 9247  , 32525 ,
      82    , 15366 , 24299 , 13694 , 48017 ,
      48449 , 13739 , 47257 , 52684 , 52094 ,
      61776 , 42874 , 1031  , 11529 , 15276 ,
      26020 , 494   , 47804 , 50461 , 55816 ,
      44489 , 22592 , 44167 , 64247 , 60796 ,
      49925 , 23000 , 12849 , 20699 , 62204 ,
      5888  , 3813  , 27129 , 29019 , 7790  ,
      65437 , 64541 , 41808 , 28539 , 29315 ,
      50120 , 24765 , 7507  , 9653  , 62136 ,
      32915 , 15143 , 39313 , 50201 , 29143 ,
      14190 , 10041 , 31015 , 20616 , 42076 ,
      23404 , 61917 , 10701 , 40230 , 38904 ,
      30589 , 47499 , 63444 , 49431 , 40875 ,
   };

   static int count = 0;

   int i = count;
   count = (i + 1) % ARRAY_SIZE(data);

   return data[i];
}

static uint32_t
rand_uint(unsigned bits)
{
   uint32_t data = rand_u16();
   if (bits > 16)
      data |= rand_u16() << 16;
   assert(bits <= 32);

   return data & BITFIELD_MASK(bits);
}

static int32_t
rand_int(unsigned bits)
{
   uint32_t data = rand_u16();
   if (bits > 16)
      data |= rand_u16() << 16;
   assert(bits <= 32);

   return util_mask_sign_extend(data, bits);
}

static uint32_t
rand_color(util_format_colorspace colorspace,
           util_format_type type,
           bool normalized,
           bool pure_integer,
           unsigned bits)
{
   switch (type) {
   case UTIL_FORMAT_TYPE_VOID:
      return 0;

   case UTIL_FORMAT_TYPE_UNSIGNED:
      if (pure_integer) {
         assert(colorspace == UTIL_FORMAT_COLORSPACE_RGB);
         return rand_uint(bits);
      } else if (normalized) {
         switch (colorspace) {
         case UTIL_FORMAT_COLORSPACE_RGB:
            /* By starting with an integer, we ensure we don't get funny
             * rounding.
             */
            return fui(rand_uint(bits) / (float)u_uintN_max(bits));

         case UTIL_FORMAT_COLORSPACE_SRGB:
            /* By starting with an integer, we ensure we don't get funny
             * rounding.
             */
            assert(bits == 8);
            return util_format_srgb_8unorm_to_linear_float(rand_uint(8));

         default:
            UNREACHABLE("Unknown colorspace");
         }
      } else {
         assert(colorspace == UTIL_FORMAT_COLORSPACE_RGB);
         return fui((float)rand_uint(bits));
      }

   case UTIL_FORMAT_TYPE_SIGNED:
      assert(colorspace == UTIL_FORMAT_COLORSPACE_RGB);
      if (pure_integer) {
         return rand_int(bits);
      } else if (normalized) {
         /* By starting with an integer, we ensure we don't get funny
          * rounding.
          */
         return fui(rand_int(bits) / (float)u_intN_max(bits));
      } else {
         return fui((float)rand_int(bits));
      }

   case UTIL_FORMAT_TYPE_FIXED:
      assert(colorspace == UTIL_FORMAT_COLORSPACE_RGB);
      return rand_uint(bits);

   case UTIL_FORMAT_TYPE_FLOAT:
      assert(colorspace == UTIL_FORMAT_COLORSPACE_RGB);
      if (bits <= 16) {
         uint16_t val = rand_u16();
         /* Let's keep it to numbers, shall we? */
         if ((val & 0x7c00) == 0x7c00)
            val &= 0xfc00;

         /* Make sure the mantissa fits so we don't round funny */
         if (bits < 16)
            val &= ~BITFIELD_MASK(16 - bits);

         return fui(_mesa_half_to_float(val));
      } else if (bits == 32) {
         uint32_t val = rand_uint(32);
         /* Let's keep it to numbers, shall we? */
         if ((val & 0x7f800000) == 0x7f800000)
            val &= 0xff800000;
         return val;
      }

   default:
      UNREACHABLE("Invalid format type");
   }
}

class rgba : public nir_format_convert_test { };

TEST_P(rgba, pack)
{
   pipe_format format = GetParam();
   auto desc = util_format_description(format);

   struct {
      uint32_t u32[4];
   } colors[NUM_COLORS];
   for (unsigned i = 0; i < NUM_COLORS; i++) {
      for (unsigned c = 0; c < 4; c++) {
         pipe_swizzle s = (pipe_swizzle)desc->swizzle[c];
         if (s < PIPE_SWIZZLE_X || s > PIPE_SWIZZLE_W) {
            colors[i].u32[c] = 0;
         } else {
            auto chan = &desc->channel[s - PIPE_SWIZZLE_X];
            assert(chan->type != (unsigned)UTIL_FORMAT_TYPE_VOID);
            colors[i].u32[c] = rand_color(desc->colorspace,
                                          (util_format_type)chan->type,
                                          chan->normalized,
                                          chan->pure_integer,
                                          chan->size);
         }
      }
   }

   nir_intrinsic_instr *uses[NUM_COLORS];
   for (unsigned i = 0; i < NUM_COLORS; i++) {
      nir_def *rgba = nir_imm_ivec4(b, colors[i].u32[0],
                                       colors[i].u32[1],
                                       colors[i].u32[2],
                                       colors[i].u32[3]);
      nir_def *packed = nir_format_pack_rgba(b, format, rgba);
      uses[i] = nir_use(b, packed);
   }

   nir_lower_undef_to_zero(b->shader);
   ASSERT_TRUE(nir_opt_constant_folding(b->shader));

   for (unsigned i = 0; i < NUM_COLORS; i++) {
      char expected[16] = { 0, };
      util_format_pack_rgba(format, expected, colors[i].u32, 1);

      nir_def *packed_ssa = uses[i]->src[0].ssa;
      const nir_const_value *packed =
         nir_def_as_load_const(packed_ssa)->value;
      if (packed_ssa->num_components == 1) {
         switch (packed_ssa->bit_size) {
         case 8:
            EXPECT_EQ(*(uint8_t *)expected, packed[0].u8);
            break;
         case 16:
            EXPECT_EQ(*(uint16_t *)expected, packed[0].u16);
            break;
         case 32:
            EXPECT_EQ(*(uint32_t *)expected, packed[0].u32);
            break;
         default:
            UNREACHABLE("Unsupported packed data bit size");
         }
      } else {
         assert(packed_ssa->bit_size == 32);
         uint32_t *exp_u32 = (uint32_t *)expected;
         for (unsigned c = 0; c < packed_ssa->num_components; c++)
            EXPECT_EQ(exp_u32[c], packed[c].u32);
      }
   }
}

TEST_P(rgba, unpack)
{
   pipe_format format = GetParam();
   auto desc = util_format_description(format);
   const unsigned dwords = DIV_ROUND_UP(desc->block.bits, 32);

   struct {
      uint32_t u32[4];
   } colors[NUM_COLORS];
   memset(colors, 0, sizeof(colors));

   for (unsigned i = 0; i < NUM_COLORS; i++) {
      for (unsigned dw = 0; dw < dwords; dw++) {
         unsigned bits = MIN2(32, desc->block.bits - dw * 32);
         colors[i].u32[dw] = rand_uint(bits);
      }
   }

   nir_intrinsic_instr *uses[NUM_COLORS];
   for (unsigned i = 0; i < NUM_COLORS; i++) {
      nir_def *packed_comps[4];
      for (unsigned dw = 0; dw < dwords; dw++)
         packed_comps[dw] = nir_imm_int(b, colors[i].u32[dw]);
      nir_def *packed = nir_vec(b, packed_comps, dwords);
      nir_def *rgba = nir_format_unpack_rgba(b, packed, format);
      uses[i] = nir_use(b, rgba);
   }

   nir_lower_undef_to_zero(b->shader);
   ASSERT_TRUE(nir_opt_constant_folding(b->shader));

   for (unsigned i = 0; i < NUM_COLORS; i++) {
      char expected[16] = { 0, };
      util_format_unpack_rgba(format, expected, colors[i].u32, 1);

      nir_def *rgba_ssa = uses[i]->src[0].ssa;
      assert(rgba_ssa->bit_size == 32);
      assert(rgba_ssa->num_components == 4);

      const nir_const_value *rgba = nir_def_as_load_const(rgba_ssa)->value;

      if (util_format_is_pure_integer(format)) {
         uint32_t *exp_u32 = (uint32_t *)expected;
         for (uint32_t c = 0; c < 4; c++)
            EXPECT_EQ(exp_u32[c], rgba[c].u32);
      } else {
         float *exp_f32 = (float *)expected;
         for (uint32_t c = 0; c < 4; c++) {
            EXPECT_EQ(isnan(exp_f32[c]), isnan(uif(rgba[c].u32)));
            if (!isnan(exp_f32[c]) && !isnan(uif(rgba[c].u32))) {
               EXPECT_FLOAT_EQ(exp_f32[c], uif(rgba[c].u32));
            }
         }
      }
   }
}

INSTANTIATE_TEST_SUITE_P(nir_format_convert_test, rgba, testing::Values(
// There's no way to get bit-for-bit identical with the CPU for these
//
// PIPE_FORMAT_R32_UNORM,
// PIPE_FORMAT_R32G32_UNORM,
// PIPE_FORMAT_R32G32B32_UNORM,
// PIPE_FORMAT_R32G32B32A32_UNORM,
// PIPE_FORMAT_R32_USCALED,
// PIPE_FORMAT_R32G32_USCALED,
// PIPE_FORMAT_R32G32B32_USCALED,
// PIPE_FORMAT_R32G32B32A32_USCALED,
// PIPE_FORMAT_R32_SNORM,
// PIPE_FORMAT_R32G32_SNORM,
// PIPE_FORMAT_R32G32B32_SNORM,
// PIPE_FORMAT_R32G32B32A32_SNORM,
// PIPE_FORMAT_R32_SSCALED,
// PIPE_FORMAT_R32G32_SSCALED,
// PIPE_FORMAT_R32G32B32_SSCALED,
// PIPE_FORMAT_R32G32B32A32_SSCALED,

   PIPE_FORMAT_R16_UNORM,
   PIPE_FORMAT_R16G16_UNORM,
   PIPE_FORMAT_R16G16B16_UNORM,
   PIPE_FORMAT_R16G16B16A16_UNORM,
   PIPE_FORMAT_R16_USCALED,
   PIPE_FORMAT_R16G16_USCALED,
   PIPE_FORMAT_R16G16B16_USCALED,
   PIPE_FORMAT_R16G16B16A16_USCALED,
   PIPE_FORMAT_R16_SNORM,
   PIPE_FORMAT_R16G16_SNORM,
   PIPE_FORMAT_R16G16B16_SNORM,
   PIPE_FORMAT_R16G16B16A16_SNORM,
   PIPE_FORMAT_R16_SSCALED,
   PIPE_FORMAT_R16G16_SSCALED,
   PIPE_FORMAT_R16G16B16_SSCALED,
   PIPE_FORMAT_R16G16B16A16_SSCALED,
   PIPE_FORMAT_R8_UNORM,
   PIPE_FORMAT_R8G8_UNORM,
   PIPE_FORMAT_R8G8B8_UNORM,
   PIPE_FORMAT_B8G8R8_UNORM,
   PIPE_FORMAT_R8G8B8A8_UNORM,
   PIPE_FORMAT_B8G8R8A8_UNORM,
   PIPE_FORMAT_R8_USCALED,
   PIPE_FORMAT_R8G8_USCALED,
   PIPE_FORMAT_R8G8B8_USCALED,
   PIPE_FORMAT_B8G8R8_USCALED,
   PIPE_FORMAT_R8G8B8A8_USCALED,
   PIPE_FORMAT_B8G8R8A8_USCALED,
   PIPE_FORMAT_A8B8G8R8_USCALED,
   PIPE_FORMAT_R8_SNORM,
   PIPE_FORMAT_R8G8_SNORM,
   PIPE_FORMAT_R8G8B8_SNORM,
   PIPE_FORMAT_B8G8R8_SNORM,
   PIPE_FORMAT_R8G8B8A8_SNORM,
   PIPE_FORMAT_B8G8R8A8_SNORM,
   PIPE_FORMAT_R8_SSCALED,
   PIPE_FORMAT_R8G8_SSCALED,
   PIPE_FORMAT_R8G8B8_SSCALED,
   PIPE_FORMAT_B8G8R8_SSCALED,
   PIPE_FORMAT_R8G8B8A8_SSCALED,
   PIPE_FORMAT_B8G8R8A8_SSCALED,
   PIPE_FORMAT_A8B8G8R8_SSCALED,
   PIPE_FORMAT_A8R8G8B8_UNORM,

// nir_format_[un]pack() don't handle the legacy GL fixed formats
//
// PIPE_FORMAT_R32_FIXED,
// PIPE_FORMAT_R32G32_FIXED,
// PIPE_FORMAT_R32G32B32_FIXED,
// PIPE_FORMAT_R32G32B32A32_FIXED,

   PIPE_FORMAT_R16_FLOAT,
   PIPE_FORMAT_R16G16_FLOAT,
   PIPE_FORMAT_R16G16B16_FLOAT,
   PIPE_FORMAT_R16G16B16A16_FLOAT,
   PIPE_FORMAT_R8_UINT,
   PIPE_FORMAT_R8G8_UINT,
   PIPE_FORMAT_R8G8B8_UINT,
   PIPE_FORMAT_B8G8R8_UINT,
   PIPE_FORMAT_R8G8B8A8_UINT,
   PIPE_FORMAT_B8G8R8A8_UINT,
   PIPE_FORMAT_R8_SINT,
   PIPE_FORMAT_R8G8_SINT,
   PIPE_FORMAT_R8G8B8_SINT,
   PIPE_FORMAT_B8G8R8_SINT,
   PIPE_FORMAT_R8G8B8A8_SINT,
   PIPE_FORMAT_B8G8R8A8_SINT,
   PIPE_FORMAT_R16_UINT,
   PIPE_FORMAT_R16G16_UINT,
   PIPE_FORMAT_R16G16B16_UINT,
   PIPE_FORMAT_R16G16B16A16_UINT,
   PIPE_FORMAT_R16_SINT,
   PIPE_FORMAT_R16G16_SINT,
   PIPE_FORMAT_R16G16B16_SINT,
   PIPE_FORMAT_R16G16B16A16_SINT,
   PIPE_FORMAT_R32_UINT,
   PIPE_FORMAT_R32G32_UINT,
   PIPE_FORMAT_R32G32B32_UINT,
   PIPE_FORMAT_R32G32B32A32_UINT,
   PIPE_FORMAT_R32_SINT,
   PIPE_FORMAT_R32G32_SINT,
   PIPE_FORMAT_R32G32B32_SINT,
   PIPE_FORMAT_R32G32B32A32_SINT,

   PIPE_FORMAT_R10G10B10A2_UNORM,
   PIPE_FORMAT_R10G10B10A2_SNORM,
   PIPE_FORMAT_R10G10B10A2_USCALED,
   PIPE_FORMAT_R10G10B10A2_SSCALED,

   PIPE_FORMAT_B10G10R10A2_UNORM,
   PIPE_FORMAT_B10G10R10A2_SNORM,
   PIPE_FORMAT_B10G10R10A2_USCALED,
   PIPE_FORMAT_B10G10R10A2_SSCALED,

   PIPE_FORMAT_R11G11B10_FLOAT,

   PIPE_FORMAT_R10G10B10A2_UINT,
   PIPE_FORMAT_R10G10B10A2_SINT,

   PIPE_FORMAT_B10G10R10A2_UINT,
   PIPE_FORMAT_B10G10R10A2_SINT,

   PIPE_FORMAT_B8G8R8X8_UNORM,
   PIPE_FORMAT_X8B8G8R8_UNORM,
   PIPE_FORMAT_X8R8G8B8_UNORM,
   PIPE_FORMAT_B5G5R5A1_UNORM,
   PIPE_FORMAT_R4G4B4A4_UNORM,
   PIPE_FORMAT_B4G4R4A4_UNORM,
   PIPE_FORMAT_R5G6B5_UNORM,
   PIPE_FORMAT_B5G6R5_UNORM,

   PIPE_FORMAT_R8G8_SRGB,
   PIPE_FORMAT_R8G8B8_SRGB,
   PIPE_FORMAT_B8G8R8_SRGB,
   PIPE_FORMAT_A8B8G8R8_SRGB,
   PIPE_FORMAT_X8B8G8R8_SRGB,
   PIPE_FORMAT_B8G8R8A8_SRGB,
   PIPE_FORMAT_B8G8R8X8_SRGB,
   PIPE_FORMAT_A8R8G8B8_SRGB,
   PIPE_FORMAT_X8R8G8B8_SRGB,
   PIPE_FORMAT_R8G8B8A8_SRGB
));

#endif /* UTIL_ARCH_LITTLE_ENDIAN */
