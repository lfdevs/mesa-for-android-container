/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_border.h"
#include "pvr_csb.h"
#include "pvr_device_info.h"
#include "pvr_private.h"
#include "util/bitset.h"
#include "util/format_r11g11b10f.h"
#include "util/format_rgb9e5.h"
#include "util/format/format_utils.h"
#include "util/format/u_format_pack.h"
#include "util/macros.h"
#include "util/u_endian.h"
#include "vk_sampler.h"
#include "vk_util.h"

#define PVR_BORDER_COLOR_TABLE_NR_FORMATS \
   (PVRX(TEXSTATE_IMAGE_WORD0_TEXFORMAT_MAX_SIZE) + 1)

/* TODO: Eliminate all of these format-wrangling macros & functions by encoding
 * our internal formats in a csv (a la src/mesa/main/formats.csv)
 */

#define intx(i, b) (i & BITFIELD_MASK(b))
#define normx(n, b) _mesa_float_to_unorm(n, b)
#define snormx(s, b) _mesa_float_to_snorm(s, b)

#define int1(i) intx(i, 1)
#define int2(i) intx(i, 2)
#define int3(i) intx(i, 3)
#define int4(i) intx(i, 4)
#define int5(i) intx(i, 5)
#define int6(i) intx(i, 6)
#define int8(i) intx(i, 8)
#define int10(i) intx(i, 10)
#define int16(i) intx(i, 16)
#define int24(i) intx(i, 24)
#define int32(i) intx(i, 32)

#define norm1(n) normx(n, 1)
#define norm2(n) normx(n, 2)
#define norm3(n) normx(n, 3)
#define norm4(n) normx(n, 4)
#define norm5(n) normx(n, 5)
#define norm6(n) normx(n, 6)
#define norm8(n) normx(n, 8)
#define norm10(n) normx(n, 10)
#define norm16(n) normx(n, 16)
#define norm24(n) normx(n, 24)
#define norm32(n) normx(n, 32)

#define snorm5(s) snormx(s, 5)
#define snorm8(s) snormx(s, 8)
#define snorm16(s) snormx(s, 16)
#define snorm32(s) snormx(s, 32)

#define zero8 (0)
#define zero10 (0)
#define zero24 (0)
#define zero32 (0)

#define float10(f) f32_to_uf10(f)
#define float11(f) f32_to_uf11(f)
#define float16(f) ((uint32_t)_mesa_float_to_half(f))
#define float32(f) ((uint32_t)(f))

union pvr_border_color_table_value {
   struct {
      uint32_t w0, w1, w2, w3;
   };
   uint32_t arr[4];
   uint8_t bytes[16];
} PACKED;
static_assert(sizeof(union pvr_border_color_table_value) ==
                 4 * sizeof(uint32_t),
              "pvr_border_color_table_value must be 4 x u32");

struct pvr_border_color_table_entry {
   union pvr_border_color_table_value formats[PVR_BORDER_COLOR_TABLE_NR_FORMATS];
   union pvr_border_color_table_value
      compressed_formats[PVR_BORDER_COLOR_TABLE_NR_FORMATS];
} PACKED;

static inline union pvr_border_color_table_value
pvr_pack_border_color_i8(const uint32_t i0)
{
   return (union pvr_border_color_table_value){
      .w0 = int8(i0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i8i8(const uint32_t i0, const uint32_t i1)
{
   return (union pvr_border_color_table_value){
      .w0 = int8(i0) | int8(i1) << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i8i8i8(const uint32_t i0,
                             const uint32_t i1,
                             const uint32_t i2)
{
   return (union pvr_border_color_table_value){
      .w0 = int8(i0) | int8(i1) << 8 | int8(i2) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i8i8i8i8(const uint32_t i0,
                               const uint32_t i1,
                               const uint32_t i2,
                               const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int8(i0) | int8(i1) << 8 | int8(i2) << 16 | int8(i3) << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i8i8i8x8(const uint32_t i0,
                               const uint32_t i1,
                               const uint32_t i2)
{
   return (union pvr_border_color_table_value){
      .w0 = int8(i0) | int8(i1) << 8 | int8(i2) << 16 | zero8 << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i16(const uint32_t i0)
{
   return (union pvr_border_color_table_value){
      .w0 = int16(i0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i16i16(const uint32_t i0, const uint32_t i1)
{
   return (union pvr_border_color_table_value){
      .w0 = int16(i0) | int16(i1) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i16i16i16(const uint32_t i0,
                                const uint32_t i1,
                                const uint32_t i2)
{
   return (union pvr_border_color_table_value){
      .w0 = int16(i0) | int16(i1) << 16,
      .w1 = int16(i2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i16i16i16i16(const uint32_t i0,
                                   const uint32_t i1,
                                   const uint32_t i2,
                                   const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int16(i0) | int16(i1) << 16,
      .w1 = int16(i2) | int16(i3) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i32(const uint32_t i0)
{
   return (union pvr_border_color_table_value){
      .w0 = int32(i0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i32i32(const uint32_t i0, const uint32_t i1)
{
   return (union pvr_border_color_table_value){
      .w0 = int32(i0),
      .w1 = int32(i1),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i32i32i32(const uint32_t i0,
                                const uint32_t i1,
                                const uint32_t i2)
{
   return (union pvr_border_color_table_value){
      .w0 = int32(i0),
      .w1 = int32(i1),
      .w2 = int32(i2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i32i32i32i32(const uint32_t i0,
                                   const uint32_t i1,
                                   const uint32_t i2,
                                   const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int32(i0),
      .w1 = int32(i1),
      .w2 = int32(i2),
      .w3 = int32(i3),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i4i4i4i4(const uint32_t i0,
                               const uint32_t i1,
                               const uint32_t i2,
                               const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int4(i0) | int4(i1) << 4 | int4(i2) << 8 | int4(i3) << 12,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i2i3i3i8(const uint32_t i0,
                               const uint32_t i1,
                               const uint32_t i2,
                               const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int2(i0) | int3(i1) << 2 | int3(i2) << 5 | int8(i3) << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i5i5i5i1(const uint32_t i0,
                               const uint32_t i1,
                               const uint32_t i2,
                               const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int5(i0) | int5(i1) << 5 | int5(i2) << 10 | int1(i3) << 15,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i1i5i5i5(const uint32_t i0,
                               const uint32_t i1,
                               const uint32_t i2,
                               const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int1(i0) | int5(i1) << 1 | int5(i2) << 6 | int5(i3) << 11,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i5i6i5(const uint32_t i0,
                             const uint32_t i1,
                             const uint32_t i2)
{
   return (union pvr_border_color_table_value){
      .w0 = int5(i0) | int6(i1) << 5 | int5(i2) << 11,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i6i5i5(const uint32_t i0,
                             const uint32_t i1,
                             const uint32_t i2)
{
   return (union pvr_border_color_table_value){
      .w0 = int6(i0) | int5(i1) << 6 | int5(i2) << 11,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i5i5i6(const uint32_t i0,
                             const uint32_t i1,
                             const uint32_t i2)
{
   return (union pvr_border_color_table_value){
      .w0 = int5(i0) | int5(i1) << 5 | int6(i2) << 10,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i10i10i10i2(const uint32_t i0,
                                  const uint32_t i1,
                                  const uint32_t i2,
                                  const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int10(i0) | int10(i1) << 10 | int10(i2) << 20 | int2(i3) << 30,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_x10x10x10i2(const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = zero10 | zero10 << 10 | zero10 << 20 | int2(i3) << 30,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i2i10i10i10(const uint32_t i0,
                                  const uint32_t i1,
                                  const uint32_t i2,
                                  const uint32_t i3)
{
   return (union pvr_border_color_table_value){
      .w0 = int2(i0) | int10(i1) << 2 | int10(i2) << 12 | int10(i3) << 22,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i2x10x10x10(const uint32_t i0)
{
   return (union pvr_border_color_table_value){
      .w0 = int2(i0) | zero10 << 2 | zero10 << 12 | zero10 << 22,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i24i8(const uint32_t i0, const uint32_t i1)
{
   return (union pvr_border_color_table_value){
      .w0 = int24(i0) | int8(i1) << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i24x8(const uint32_t i0)
{
   return (union pvr_border_color_table_value){
      .w0 = int24(i0) | zero8 << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_x24i8(const uint32_t i1)
{
   return (union pvr_border_color_table_value){
      .w0 = zero24 | int8(i1) << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_i8i24(const uint32_t i0, const uint32_t i1)
{
   return (union pvr_border_color_table_value){
      .w0 = int8(i0) | int24(i1) << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_x32i8x24(const uint32_t i1)
{
   return (union pvr_border_color_table_value){
      .w0 = zero32,
      .w1 = int8(i1) | zero24 << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n8(const float n0)
{
   return (union pvr_border_color_table_value){
      .w0 = norm8(n0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n8n8(const float n0, const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = norm8(n0) | norm8(n1) << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n8n8n8(const float n0, const float n1, const float n2)
{
   return (union pvr_border_color_table_value){
      .w0 = norm8(n0) | norm8(n1) << 8 | norm8(n2) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n8n8n8n8(const float n0,
                               const float n1,
                               const float n2,
                               const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm8(n0) | norm8(n1) << 8 | norm8(n2) << 16 | norm8(n3) << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n8n8n8x8(const float n0, const float n1, const float n2)
{
   return (union pvr_border_color_table_value){
      .w0 = norm8(n0) | norm8(n1) << 8 | norm8(n2) << 16 | zero8 << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n8s8s8x8(const float n0, const float s1, const float s2)
{
   return (union pvr_border_color_table_value){
      .w0 = norm8(n0) | snorm8(s1) << 8 | snorm8(s2) << 16 | zero8 << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s8(const float s0)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm8(s0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s8s8(const float s0, const float s1)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm8(s0) | snorm8(s1) << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s8s8s8(const float s0, const float s1, const float s2)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm8(s0) | snorm8(s1) << 8 | snorm8(s2) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s8s8s8s8(const float s0,
                               const float s1,
                               const float s2,
                               const float s3)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm8(s0) | snorm8(s1) << 8 | snorm8(s2) << 16 | snorm8(s3) << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n16(const float n0)
{
   return (union pvr_border_color_table_value){
      .w0 = norm16(n0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n16n16(const float n0, const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = norm16(n0) | norm16(n1) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n16n16n16(const float n0, const float n1, const float n2)
{
   return (union pvr_border_color_table_value){
      .w0 = norm16(n0) | norm16(n1) << 16,
      .w1 = norm16(n2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n16n16n16n16(const float n0,
                                   const float n1,
                                   const float n2,
                                   const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm16(n0) | norm16(n1) << 16,
      .w1 = norm16(n2) | norm16(n3) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s16(const float s0)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm16(s0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s16s16(const float s0, const float s1)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm16(s0) | snorm16(s1) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s16s16s16(const float s0, const float s1, const float s2)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm16(s0) | snorm16(s1) << 16,
      .w1 = snorm16(s2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s16s16s16s16(const float s0,
                                   const float s1,
                                   const float s2,
                                   const float s3)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm16(s0) | snorm16(s1) << 16,
      .w1 = snorm16(s2) | snorm16(s3) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n32(const float n0)
{
   return (union pvr_border_color_table_value){
      .w0 = norm32(n0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n32n32(const float n0, const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = norm32(n0),
      .w1 = norm32(n1),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n32n32n32(const float n0, const float n1, const float n2)
{
   return (union pvr_border_color_table_value){
      .w0 = norm32(n0),
      .w1 = norm32(n1),
      .w2 = norm32(n2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n32n32n32n32(const float n0,
                                   const float n1,
                                   const float n2,
                                   const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm32(n0),
      .w1 = norm32(n1),
      .w2 = norm32(n2),
      .w3 = norm32(n3),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s32(const float s0)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm32(s0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s32s32(const float s0, const float s1)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm32(s0),
      .w1 = snorm32(s1),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s32s32s32(const float s0, const float s1, const float s2)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm32(s0),
      .w1 = snorm32(s1),
      .w2 = snorm32(s2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s32s32s32s32(const float s0,
                                   const float s1,
                                   const float s2,
                                   const float s3)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm32(s0),
      .w1 = snorm32(s1),
      .w2 = snorm32(s2),
      .w3 = snorm32(s3),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n4n4n4n4(const float n0,
                               const float n1,
                               const float n2,
                               const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm4(n0) | norm4(n1) << 4 | norm4(n2) << 8 | norm4(n3) << 12,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n2n3n3n8(const float n0,
                               const float n1,
                               const float n2,
                               const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm2(n0) | norm3(n1) << 2 | norm3(n2) << 5 | norm8(n3) << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n5n5n5n1(const float n0,
                               const float n1,
                               const float n2,
                               const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm5(n0) | norm5(n1) << 5 | norm5(n2) << 10 | norm1(n3) << 15,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n1n5n5n5(const float n0,
                               const float n1,
                               const float n2,
                               const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm1(n0) | norm5(n1) << 1 | norm5(n2) << 6 | norm5(n3) << 11,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n5n6n5(const float n0, const float n1, const float n2)
{
   return (union pvr_border_color_table_value){
      .w0 = norm5(n0) | norm6(n1) << 5 | norm5(n2) << 11,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n6s5s5(const float n0, const float s1, const float s2)
{
   return (union pvr_border_color_table_value){
      .w0 = norm6(n0) | snorm5(s1) << 6 | snorm5(s2) << 11,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_s5s5n6(const float s0, const float s1, const float n2)
{
   return (union pvr_border_color_table_value){
      .w0 = snorm5(s0) | snorm5(s1) << 5 | norm6(n2) << 10,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n10n10n10n2(const float n0,
                                  const float n1,
                                  const float n2,
                                  const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm10(n0) | norm10(n1) << 10 | norm10(n2) << 20 | norm2(n3) << 30,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f10f10f10n2(const float f0,
                                  const float f1,
                                  const float f2,
                                  const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = float10(f0) | float10(f1) << 10 | float10(f2) << 20 |
            norm2(n3) << 30,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n2n10n10n10(const float n0,
                                  const float n1,
                                  const float n2,
                                  const float n3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm2(n0) | norm10(n1) << 2 | norm10(n2) << 12 | norm10(n3) << 22,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n2f10f10f10(const float n0,
                                  const float f1,
                                  const float f2,
                                  const float f3)
{
   return (union pvr_border_color_table_value){
      .w0 = norm2(n0) | float10(f1) << 2 | float10(f2) << 12 |
            float10(f3) << 22,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n24n8(const float n0, const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = norm24(n0) | norm8(n1) << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n24x8(const float n0)
{
   return (union pvr_border_color_table_value){
      .w0 = norm24(n0) | zero8 << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_x24n8(const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = zero24 | norm8(n1) << 24,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_n8n24(const float n0, const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = norm8(n0) | norm24(n1) << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f32n8x24(const float f0, const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = float32(f0),
      .w1 = norm8(n1) | zero24 << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f32x8x24(const float f0)
{
   return (union pvr_border_color_table_value){
      .w0 = float32(f0),
      .w1 = zero8 | zero24 << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_x32n8x24(const float n1)
{
   return (union pvr_border_color_table_value){
      .w0 = zero32,
      .w1 = norm8(n1) | zero24 << 8,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f16(const float f0)
{
   return (union pvr_border_color_table_value){
      .w0 = float16(f0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f16f16(const float f0, const float f1)
{
   return (union pvr_border_color_table_value){
      .w0 = float16(f0) | float16(f1) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f16f16f16(const float f0, const float f1, const float f2)
{
   return (union pvr_border_color_table_value){
      .w0 = float16(f0) | float16(f1) << 16,
      .w1 = float16(f2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f16f16f16f16(const float f0,
                                   const float f1,
                                   const float f2,
                                   const float f3)
{
   return (union pvr_border_color_table_value){
      .w0 = float16(f0) | float16(f1) << 16,
      .w1 = float16(f2) | float16(f3) << 16,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f32(const float f0)
{
   return (union pvr_border_color_table_value){
      .w0 = float32(f0),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_g32(const float g0)
{
   return (union pvr_border_color_table_value){
      .w0 = float32(g0) & 0x7fffffff,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f32f32(const float f0, const float f1)
{
   return (union pvr_border_color_table_value){
      .w0 = float32(f0),
      .w1 = float32(f1),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f32f32f32(const float f0, const float f1, const float f2)
{
   return (union pvr_border_color_table_value){
      .w0 = float32(f0),
      .w1 = float32(f1),
      .w2 = float32(f2),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f32f32f32f32(const float f0,
                                   const float f1,
                                   const float f2,
                                   const float f3)
{
   return (union pvr_border_color_table_value){
      .w0 = float32(f0),
      .w1 = float32(f1),
      .w2 = float32(f2),
      .w3 = float32(f3),
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f10f11f11(const float f0, const float f1, const float f2)
{
   return (union pvr_border_color_table_value){
      .w0 = float10(f0) | float11(f1) << 10 | float11(f2) << 21,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_f11f11f10(const float f0, const float f1, const float f2)
{
   return (union pvr_border_color_table_value){
      .w0 = float11(f0) | float11(f1) << 11 | float10(f2) << 22,
   };
}

static inline union pvr_border_color_table_value
pvr_pack_border_color_e9e9e9x5(const float f0, const float f1, const float f2)
{
   return (union pvr_border_color_table_value){
      .w0 = float3_to_rgb9e5((float[3]){ f0, f1, f2 }),
   };
}

#define PACK(format_, layout_, channels_...)         \
   entry->formats[PVRX(TEXSTATE_FORMAT_##format_)] = \
      pvr_pack_border_color_##layout_(channels_)

/* clang-format off */
#define UDEF(format_)                                \
   entry->formats[PVRX(TEXSTATE_FORMAT_##format_)] = \
      (union pvr_border_color_table_value){ 0 }
/* clang-format on */

static void
pvr_pack_border_color_ints(struct pvr_border_color_table_entry *const entry,
                           const uint32_t color[const static 4])
{
   const uint32_t r = color[0];
   const uint32_t g = color[1];
   const uint32_t b = color[2];
   const uint32_t a = color[3];

   /*   0 */ PACK(U8, i8, r);
   /*   1 */ PACK(S8, i8, r);
   /*   7 */ PACK(U8U8, i8i8, g, r);
   /*   8 */ PACK(S8S8, i8i8, g, r);
   /*   9 */ PACK(U16, i16, r);
   /*  10 */ PACK(S16, i16, r);
   /*  11 */ UDEF(F16);
   /*  12 */ PACK(U8U8U8U8, i8i8i8i8, a, b, g, r);
   /*  13 */ PACK(S8S8S8S8, i8i8i8i8, a, b, g, r);
   /*  14 */ PACK(A2R10B10G10, i10i10i10i2, r, g, b, a);
   /*  15 */ PACK(U16U16, i16i16, g, r);
   /*  16 */ PACK(S16S16, i16i16, g, r);
   /*  17 */ UDEF(F16F16);
   /*  18 */ UDEF(F32);
   /*  22 */ PACK(ST8U24, i24i8, g, r);
   /*  23 */ PACK(U8X24, x24i8, r);
   /*  24 */ PACK(U32, i32, r);
   /*  25 */ PACK(S32, i32, r);
   /*  26 */ UDEF(SE9995);
   /*  28 */ UDEF(F16F16F16F16);
   /*  29 */ PACK(U16U16U16U16, i16i16i16i16, a, b, g, r);
   /*  30 */ PACK(S16S16S16S16, i16i16i16i16, a, b, g, r);
   /*  35 */ PACK(U32U32, i32i32, g, r);
   /*  36 */ PACK(S32S32, i32i32, g, r);
   /*  61 */ UDEF(F32F32F32F32);
   /*  62 */ PACK(U32U32U32U32, i32i32i32i32, a, b, g, r);
   /*  63 */ PACK(S32S32S32S32, i32i32i32i32, a, b, g, r);
   /*  64 */ UDEF(F32F32F32);
   /*  65 */ PACK(U32U32U32, i32i32i32, b, g, r);
   /*  66 */ PACK(S32S32S32, i32i32i32, b, g, r);
   /*  88 */ UDEF(F10F11F11);
}

static void
pvr_pack_border_color_floats(struct pvr_border_color_table_entry *const entry,
                             const float color[const static 4])
{
   const float r = color[0];
   const float g = color[1];
   const float b = color[2];
   const float a = color[3];

   /*   0 */ PACK(U8, n8, r);
   /*   1 */ PACK(S8, s8, r);
   /*   2 */ PACK(A4R4G4B4, n4n4n4n4, b, g, r, a);
   /*   4 */ PACK(A1R5G5B5, n5n5n5n1, b, g, r, a);
   /*   5 */ PACK(R5G6B5, n5n6n5, b, g, r);
   /*   7 */ PACK(U8U8, n8n8, g, r);
   /*   8 */ PACK(S8S8, s8s8, g, r);
   /*   9 */ PACK(U16, n16, r);
   /*  10 */ PACK(S16, s16, r);
   /*  11 */ PACK(F16, f16, r);
   /*  12 */ PACK(U8U8U8U8, n8n8n8n8, a, b, g, r);
   /*  13 */ PACK(S8S8S8S8, s8s8s8s8, a, b, g, r);
   /*  14 */ PACK(A2R10B10G10, n10n10n10n2, r, g, b, a);
   /*  15 */ PACK(U16U16, n16n16, g, r);
   /*  16 */ PACK(S16S16, s16s16, g, r);
   /*  17 */ PACK(F16F16, f16f16, g, r);
   /*  18 */ PACK(F32, f32, r);
   /*  22 */ PACK(ST8U24, n24n8, g, r);
   /*  26 */ PACK(SE9995, e9e9e9x5, r, g, b);
   /*  28 */ PACK(F16F16F16F16, f16f16f16f16, a, b, g, r);
   /*  29 */ PACK(U16U16U16U16, n16n16n16n16, a, b, g, r);
   /*  30 */ PACK(S16S16S16S16, s16s16s16s16, a, b, g, r);
   /*  34 */ PACK(F32F32, f32f32, g, r);
   /*  61 */ PACK(F32F32F32F32, f32f32f32f32, a, b, g, r);
   /*  64 */ PACK(F32F32F32, f32f32f32, b, g, r);
   /*  88 */ PACK(F10F11F11, f11f11f10, b, g, r);
}

#undef PACK
#undef UDEF

#define PACKC(format_, layout_, channels_...)                              \
   entry->compressed_formats[PVRX(TEXSTATE_FORMAT_COMPRESSED_##format_)] = \
      pvr_pack_border_color_##layout_(channels_)

static void pvr_pack_border_color_compressed(
   struct pvr_border_color_table_entry *const entry,
   const VkClearColorValue color[const static 4])
{
   const uint32_t r = color->uint32[0];
   const uint32_t g = color->uint32[1];
   const uint32_t b = color->uint32[2];
   const uint32_t a = color->uint32[3];

   /*  68 */ PACKC(ETC2_RGB, i8i8i8i8, a, b, g, r);
   /*  69 */ PACKC(ETC2A_RGBA, i8i8i8i8, a, b, g, r);
   /*  70 */ PACKC(ETC2_PUNCHTHROUGHA, i8i8i8i8, a, b, g, r);
   /*  71 */ PACKC(EAC_R11_UNSIGNED, i16i16i16i16, a, b, g, r);
   /*  72 */ PACKC(EAC_R11_SIGNED, i16i16i16i16, a, b, g, r);
   /*  73 */ PACKC(EAC_RG11_UNSIGNED, i16i16i16i16, a, b, g, r);
   /*  74 */ PACKC(EAC_RG11_SIGNED, i16i16i16i16, a, b, g, r);
}

#undef PACKC

static int32_t
pvr_border_color_table_alloc_entry(struct pvr_border_color_table *const table)
{
   const int32_t index = BITSET_FFS(table->unused_entries);

   /* BITSET_FFS() returns 0 if there are no set bits; we have to determine
    * whether a value of 0 means "no set bits" or "zero is the first set bit".
    */
   if (index == 0 && !pvr_border_color_table_is_index_valid(table, 0)) {
      return -1;
   }

   BITSET_CLEAR(table->unused_entries, index);

   return index;
}

static void
pvr_border_color_table_free_entry(struct pvr_border_color_table *const table,
                                  const uint32_t index)
{
   assert(BITSET_TEST(table->unused_entries, index));
   BITSET_SET(table->unused_entries, index);
}

static void
pvr_border_color_table_fill_entry(struct pvr_border_color_table *const table,
                                  const struct pvr_device *const device,
                                  const uint32_t index,
                                  const VkClearColorValue *const color,
                                  const bool is_int)
{
   struct pvr_border_color_table_entry *const entries = table->table->bo->map;
   const struct pvr_device_info *const dev_info = &device->pdevice->dev_info;
   struct pvr_border_color_table_entry *entry;

   assert(pvr_border_color_table_is_index_valid(table, index));
   assert(entries);

   entry = &entries[index];
   memset(entry, 0, sizeof(*entry));

   if (is_int)
      pvr_pack_border_color_ints(entry, color->uint32);
   else
      pvr_pack_border_color_floats(entry, color->float32);

   if (PVR_HAS_FEATURE(dev_info, tpu_border_colour_enhanced)) {
      pvr_pack_border_color_compressed(entry, color);
   } else {
      pvr_finishme("Devices without tpu_border_colour_enhanced require entries "
                   "for compressed formats to be stored in the table "
                   "pre-compressed.");
   }
}

VkResult pvr_border_color_table_init(struct pvr_border_color_table *const table,
                                     struct pvr_device *const device)
{
   const struct pvr_device_info *const dev_info = &device->pdevice->dev_info;
   const uint32_t cache_line_size = rogue_get_slc_cache_line_size(dev_info);
   const uint32_t table_size = sizeof(struct pvr_border_color_table_entry) *
                               PVR_BORDER_COLOR_TABLE_NR_ENTRIES;

   VkResult result;

   /* Initialize to ones so ffs can be used to find unused entries. */
   BITSET_ONES(table->unused_entries);

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         table_size,
                         cache_line_size,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &table->table);
   if (result != VK_SUCCESS)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   BITSET_CLEAR_RANGE(table->unused_entries,
                      0,
                      PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES - 1);

   for (uint32_t i = 0; i < PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES; i++) {
      const VkClearColorValue color = vk_border_color_value(i);
      const bool is_int = vk_border_color_is_int(i);

      pvr_border_color_table_fill_entry(table, device, i, &color, is_int);
   }

   pvr_bo_cpu_unmap(device, table->table);

   return VK_SUCCESS;
}

void pvr_border_color_table_finish(struct pvr_border_color_table *const table,
                                   struct pvr_device *const device)
{
   pvr_bo_free(device, table->table);
}

VkResult pvr_border_color_table_get_or_create_entry(
   UNUSED struct pvr_border_color_table *const table,
   const struct VkSamplerCreateInfo *const sampler_create_info,
   uint32_t *const index_out)
{
   const VkBorderColor vk_type = sampler_create_info->borderColor;

   if (vk_type <= PVR_BORDER_COLOR_TABLE_NR_BUILTIN_ENTRIES) {
      *index_out = vk_type;
      return VK_SUCCESS;
   }

   pvr_finishme("VK_EXT_custom_border_color is currently unsupported.");
   return vk_error(NULL, VK_ERROR_EXTENSION_NOT_PRESENT);
}
