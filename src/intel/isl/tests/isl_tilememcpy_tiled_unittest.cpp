/*
 * Copyright 2021 Intel Corporation
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

#include <gtest/gtest.h>
#include <inttypes.h>

#include "util/u_math.h"
#include "isl/isl.h"
#include "isl/isl_priv.h"

#define LIN_OFF(y, tw, x) ((y * tw) + x)
#define IMAGE_FORMAT ISL_FORMAT_R32G32B32_UINT
#define TILEW_IMAGE_FORMAT ISL_FORMAT_R8_UINT

enum TILE_CONV {LIN_TO_TILE, TILE_TO_LIN};

typedef uint8_t *(*swizzle_func_t)(const uint8_t *base_addr, uint32_t pitch, uint32_t x_B, uint32_t y_px);

#define TILE_COORDINATES                 \
                 /* x1,  x2, y1, y2 */   \
   std::make_tuple(  0,   1,  0,  1),    \
   std::make_tuple(  0,   2,  0,  1),    \
   std::make_tuple(  0,   4,  0,  1),    \
   std::make_tuple(  0,   8,  0,  8),    \
   std::make_tuple(  0, 128,  0, 32),    \
   std::make_tuple( 19,  20, 25, 32),    \
   std::make_tuple( 59,  83, 13, 32),    \
   std::make_tuple( 10,  12,  5,  8),    \
   std::make_tuple( 64,  65, 16, 17),    \
   std::make_tuple(128, 129,  0, 32),    \
   std::make_tuple(245, 521,  5,  8)

#define FULL_TILEX_COORDINATES \
   std::make_tuple(  0,  32,  0,  8),    \
   std::make_tuple(  0,  64,  0, 16),    \
   std::make_tuple(  0,  64,  0, 32)

#define FULL_TILEY_COORDINATES \
   std::make_tuple(  0,   8,  0, 32),    \
   std::make_tuple(  0,  16,  0, 32),    \
   std::make_tuple(  0,  16,  0, 64)

#define FULL_TILEW_COORDINATES \
   std::make_tuple(  0,  64,  0, 64),    \
   std::make_tuple(  0, 128,  0, 64),    \
   std::make_tuple(  0, 128,  0,128)

struct tile_swizzle_ops {
   enum isl_tiling tiling;
   swizzle_func_t linear_to_tile_swizzle;
};

uint32_t swizzle_bitops(uint32_t num, uint8_t field, uint8_t curr_ind, uint8_t swizzle_ind)
{
   uint32_t bitmask = (1 << field) - 1;
   uint32_t maskednum = num & (bitmask << curr_ind);
   uint32_t bits = maskednum >> curr_ind;
   return bits << swizzle_ind;
}

uint8_t *linear_to_tileY_swizzle(const uint8_t *base_addr, uint32_t pitch, uint32_t x_B, uint32_t y_px)
{
   const uint32_t cu = 7, cv = 5;
   const uint32_t tile_id = (y_px >> cv) * (pitch >> cu) + (x_B >> cu);

   /* The table below represents the mapping from coordinate (x_B, y_px) to
    * byte offset in a 128x32px 1Bpp image:
    *
    *    Bit ind : 11 10  9  8  7  6  5  4  3  2  1  0
    *     Tile-Y : u6 u5 u4 v4 v3 v2 v1 v0 u3 u2 u1 u0
    */
   uint32_t tiled_off;

   tiled_off = tile_id * 4096 |
               swizzle_bitops(x_B, 4, 0, 0) |
               swizzle_bitops(y_px, 5, 0, 4) |
               swizzle_bitops(x_B, 3, 4, 9);

   return (uint8_t *)(base_addr + tiled_off);
}

uint8_t *linear_to_tile4_swizzle(const uint8_t * base_addr, uint32_t pitch, uint32_t x_B, uint32_t y_px)
{
   const uint32_t cu = 7, cv = 5;
   const uint32_t tile_id = (y_px >> cv) * (pitch >> cu) + (x_B >> cu);

   /* The table below represents the mapping from coordinate (x_B, y_px) to
    * byte offset in a 128x32px 1Bpp image:
    *
    *    Bit ind : 11 10  9  8  7  6  5  4  3  2  1  0
    *     Tile-Y : v4 v3 u6 v2 u5 u4 v1 v0 u3 u2 u1 u0
    */
   uint32_t tiled_off;

   tiled_off = tile_id * 4096 |
               swizzle_bitops(x_B, 4, 0, 0) |
               swizzle_bitops(y_px, 2, 0, 4) |
               swizzle_bitops(x_B, 2, 4, 6) |
               swizzle_bitops(y_px, 1, 2, 8) |
               swizzle_bitops(x_B, 1, 6, 9) |
	       swizzle_bitops(y_px, 2, 3, 10);

   return (uint8_t *) (base_addr + tiled_off);
}

uint8_t *linear_to_tileX_swizzle(const uint8_t * base_addr, uint32_t pitch, uint32_t x_B, uint32_t y_px)
{
   const uint32_t cu = 9, cv = 3;
   const uint32_t tile_id = (y_px >> cv) * (pitch >> cu) + (x_B >> cu);

   /* The table below represents the mapping from coordinate (x_B, y_px) to
    * byte offset in a 512x8px 1Bpp image:
    *
    *    Bit ind : 11 10  9  8  7  6  5  4  3  2  1  0
    *     Tile-X : v2 v1 v0 u8 u7 u6 u5 u4 u3 u2 u1 u0
    */
   uint32_t tiled_off;

   tiled_off = tile_id * 4096 |
               swizzle_bitops(x_B,  9, 0, 0) |
               swizzle_bitops(y_px, 3, 0, 9);

   return (uint8_t *) (base_addr + tiled_off);
}

uint8_t *linear_to_tileW_swizzle(const uint8_t *base_addr, uint32_t pitch, uint32_t x_B, uint32_t y_px)
{
   /* TileW is a special case with doubled physical tile width due to HW
    * programming requirements (see isl_tiling_get_info() in
    * src/intel/isl/isl.c)
    */
   pitch /= 2;

   const uint32_t cu = 6, cv = 6;
   const uint32_t tile_id = (y_px >> cv) * (pitch >> cu) + (x_B >> cu);

   /* The table below represents the mapping from coordinate (x_B, y_px) to
    * byte offset in a 64x64px 1Bpp image:
    *
    *    Bit ind : 11 10  9  8  7  6  5  4  3  2  1  0
    *     Tile-W : u5 u4 u3 v5 v4 v3 v2 u2 v1 u1 v0 u0
    */
   uint32_t tiled_off;

   tiled_off = tile_id * 4096 |
               swizzle_bitops(x_B,  1, 0, 0) |
               swizzle_bitops(y_px, 1, 0, 1) |
               swizzle_bitops(x_B,  1, 1, 2) |
               swizzle_bitops(y_px, 1, 1, 3) |
               swizzle_bitops(x_B,  1, 2, 4) |
               swizzle_bitops(y_px, 4, 2, 5) |
               swizzle_bitops(x_B,  3, 3, 9);

   return (uint8_t *) (base_addr + tiled_off);
}

struct tile_swizzle_ops swizzle_opers[] = {
   {ISL_TILING_Y0, linear_to_tileY_swizzle},
   {ISL_TILING_4, linear_to_tile4_swizzle},
   {ISL_TILING_X, linear_to_tileX_swizzle},
   {ISL_TILING_W, linear_to_tileW_swizzle},
};

class tileTFixture: public ::testing::Test {

protected:
   uint32_t x_max_el;
   uint32_t y_max_el;

   uint8_t *buf_dst;
   uint8_t *buf_src;
   uint32_t buf_dst_size_B;
   uint32_t buf_src_size_B;

   uint32_t tiled_pitch_B, tiled_height;
   uint32_t tiled_size_B;
   uint32_t linear_pitch_B;
   uint32_t linear_sz;
   uint32_t fmt_bs; /* format bytes per block */
   TILE_CONV conv;
   struct tile_swizzle_ops ops;
   bool print_results;
   struct isl_tile_info tile_info;

public:
   void test_setup(TILE_CONV convert, enum isl_tiling tiling_fmt,
              enum isl_format format,
              uint32_t max_width, uint32_t max_height);
   void TearDown();
   uint32_t swizzle_bitops(uint32_t num, uint8_t field,
                           uint8_t curr_ind, uint8_t swizzle_ind);
   void bounded_byte_fill(uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2);
   void hex_oword_print(const uint8_t *buf, uint32_t size);
   void convert_texture(uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2);
   void compare_conv_result(uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2);
   void run_test(uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2);
};

class tileYFixture : public tileTFixture,
                     public ::testing::WithParamInterface<std::tuple<int, int,
                                                                     int, int>>
{};

class tile4Fixture : public tileTFixture,
                     public ::testing::WithParamInterface<std::tuple<int, int,
                                                                     int, int>>
{};

class tileXFixture : public tileTFixture,
                     public ::testing::WithParamInterface<std::tuple<int, int,
                                                                     int, int>>
{};

class tileWFixture : public tileTFixture,
                     public ::testing::WithParamInterface<std::tuple<int, int,
                                                                     int, int>>
{};

void tileTFixture::test_setup(TILE_CONV convert,
                         enum isl_tiling tiling_fmt,
                         enum isl_format format,
                         uint32_t max_width,
                         uint32_t max_height)
{
   print_results = debug_get_bool_option("ISL_TEST_DEBUG", false);

   const struct isl_format_layout *fmtl = isl_format_get_layout(format);
   conv = convert;
   fmt_bs = fmtl->bpb / 8;
   ops.tiling = tiling_fmt;

   isl_tiling_get_info(tiling_fmt, ISL_SURF_DIM_2D, ISL_MSAA_LAYOUT_NONE,
		       fmtl->bpb, 1 , &tile_info);

   x_max_el = align(max_width, tile_info.logical_extent_el.w);
   y_max_el = align(max_height, tile_info.logical_extent_el.h);

   tiled_pitch_B = (x_max_el * (fmt_bs / (tile_info.format_bpb / 8)) /
                    tile_info.logical_extent_el.w) *
                   tile_info.phys_extent_B.w;
   tiled_height = y_max_el / tile_info.logical_extent_el.h *
                  tile_info.phys_extent_B.h;
   tiled_size_B = tiled_pitch_B * tiled_height;

   linear_pitch_B = x_max_el * fmt_bs;
   linear_sz = linear_pitch_B * y_max_el;

   buf_dst_size_B = convert == LIN_TO_TILE ? tiled_size_B : linear_sz;
   buf_src_size_B = convert == LIN_TO_TILE ? linear_sz : tiled_size_B;

   buf_src = (uint8_t *) calloc(buf_src_size_B, sizeof(uint8_t));
   ASSERT_TRUE(buf_src != nullptr);

   buf_dst = (uint8_t *) calloc(buf_dst_size_B, sizeof(uint8_t));
   ASSERT_TRUE(buf_src != nullptr);

   for (uint8_t i = 0; i < ARRAY_SIZE(swizzle_opers); i++)
      if (ops.tiling == swizzle_opers[i].tiling)
         ops.linear_to_tile_swizzle = swizzle_opers[i].linear_to_tile_swizzle;

   memset(buf_src, 0xcc, buf_src_size_B);
   memset(buf_dst, 0xcc, buf_dst_size_B);
}

void tileTFixture::TearDown()
{
   free(buf_src);
   buf_src = nullptr;

   free(buf_dst);
   buf_dst = nullptr;
}

void tileTFixture::bounded_byte_fill(uint32_t x1_el, uint32_t x2_el,
                                     uint32_t y1_el, uint32_t y2_el)
{
   for(auto y_el = y1_el; y_el < y2_el; y_el++)
      for (auto x_b = x1_el * fmt_bs; x_b < x2_el * fmt_bs; x_b++)
         if (conv == LIN_TO_TILE) {
            *(buf_src + LIN_OFF(y_el, linear_pitch_B, x_b)) = LIN_OFF(y_el, linear_pitch_B, x_b)%16;
         } else {
            *(ops.linear_to_tile_swizzle(buf_src, tiled_pitch_B, x_b, y_el)) =
               LIN_OFF(y_el, linear_pitch_B, x_b)%16;
         }
}

void tileTFixture::hex_oword_print(const uint8_t *buf, uint32_t size)
{
   uint64_t *itr;
   uint32_t i;

   for (itr = (uint64_t *)buf, i=0; itr < (uint64_t *)(buf + size); i++) {
      fprintf(stdout, "%.16" PRIx64 "%.16" PRIx64, util_bswap64(*(itr)), util_bswap64(*(itr+1)));

      itr = itr+2;

      if((i+1) % 8 == 0 && i > 0)
         printf("\n");
      else
         printf("  ");
   }
}

void tileTFixture::convert_texture(uint32_t x1_el, uint32_t x2_el, uint32_t y1_el, uint32_t y2_el)
{
   if (print_results) {
      printf("/************** Printing src ***************/\n");
      hex_oword_print((const uint8_t *)buf_src, buf_src_size_B);
   }

   uint32_t linear_offset_B = LIN_OFF(y1_el, linear_pitch_B, x1_el * fmt_bs);

   if (conv == LIN_TO_TILE)
      isl_memcpy_linear_to_tiled(x1_el * fmt_bs, x2_el * fmt_bs, y1_el, y2_el,
                                 (char *)buf_dst,
                                 (const char *)buf_src + linear_offset_B,
                                 tiled_pitch_B, linear_pitch_B,
                                 0, ops.tiling, ISL_MEMCPY);
   else
      isl_memcpy_tiled_to_linear(x1_el * fmt_bs, x2_el * fmt_bs, y1_el, y2_el,
                                 (char *)buf_dst + linear_offset_B,
                                 (const char *)buf_src,
                                 linear_pitch_B, tiled_pitch_B,
                                 0, ops.tiling, ISL_MEMCPY);

   if (print_results) {
      printf("/************** Printing dest **************/\n");
      hex_oword_print((const uint8_t *) buf_dst, buf_dst_size_B);
   }
}

void tileTFixture::compare_conv_result(uint32_t x1_el, uint32_t x2_el,
                                       uint32_t y1_el, uint32_t y2_el)
{
   for (uint32_t y_el = 0; y_el < y_max_el; y_el++) {
      for (uint32_t x_el = 0; x_el < x_max_el; x_el++) {
         for (uint32_t b = 0; b < fmt_bs; b++) {
            uint32_t x_b = x_el * fmt_bs + b;

            if (x_el < x1_el || x_el >= x2_el || y_el < y1_el || y_el >= y2_el) {
               if (conv == LIN_TO_TILE) {
                  EXPECT_EQ(*(buf_src + LIN_OFF(y_el, linear_pitch_B, x_b)), 0xcc)
                     << "Not matching for x:" << x_el << " and y:" << y_el << std::endl;
               } else {
                  EXPECT_EQ(*(buf_dst + LIN_OFF(y_el, linear_pitch_B, x_b)), 0xcc)
                     << "Not matching for x:" << x_el << " and y:" << y_el << std::endl;
               }
            } else {
               if (conv == LIN_TO_TILE) {
                  EXPECT_EQ(*(buf_src + LIN_OFF(y_el, linear_pitch_B, x_b)),
                            *(ops.linear_to_tile_swizzle(buf_dst, tiled_pitch_B, x_b, y_el)))
                     << "Not matching for x:" << x_el << " and y:" << y_el << std::endl;
               } else {
                  EXPECT_EQ(*(buf_dst + LIN_OFF(y_el, linear_pitch_B, x_b)),
                            *(ops.linear_to_tile_swizzle(buf_src, tiled_pitch_B, x_b, y_el)))
                     << "Not matching for x:" << x_el << " and y:" << y_el << std::endl;
               }
            }
         }
      }
   }
}

void tileTFixture::run_test(uint32_t x1, uint32_t x2,
                            uint32_t y1, uint32_t y2)
{
    bounded_byte_fill(x1, x2, y1, y2);
    convert_texture(x1, x2, y1, y2);
    compare_conv_result(x1, x2, y1, y2);
}

TEST_P(tileYFixture, lintotile)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(LIN_TO_TILE, ISL_TILING_Y0, IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}

TEST_P(tileYFixture, tiletolin)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(TILE_TO_LIN, ISL_TILING_Y0, IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}

TEST_P(tile4Fixture, lintotile)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(LIN_TO_TILE, ISL_TILING_4, IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}

TEST_P(tile4Fixture, tiletolin)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(TILE_TO_LIN, ISL_TILING_4, IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}

TEST_P(tileXFixture, lintotile)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(LIN_TO_TILE, ISL_TILING_X, IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}

TEST_P(tileXFixture, tiletolin)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(TILE_TO_LIN, ISL_TILING_X, IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}

TEST_P(tileWFixture, lintotile)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(LIN_TO_TILE, ISL_TILING_W, TILEW_IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}

TEST_P(tileWFixture, tiletolin)
{
    auto [x1, x2, y1, y2] = GetParam();
    test_setup(TILE_TO_LIN, ISL_TILING_W, TILEW_IMAGE_FORMAT, x2, y2);
    if (print_results)
       printf("Coordinates: x1=%d x2=%d y1=%d y2=%d \n", x1, x2, y1, y2);
    run_test(x1, x2, y1, y2);
}


INSTANTIATE_TEST_SUITE_P(tileY, tileYFixture, testing::Values(TILE_COORDINATES,
                                                              FULL_TILEY_COORDINATES));
INSTANTIATE_TEST_SUITE_P(tile4, tile4Fixture, testing::Values(TILE_COORDINATES,
                                                              FULL_TILEY_COORDINATES));
INSTANTIATE_TEST_SUITE_P(tileX, tileXFixture, testing::Values(TILE_COORDINATES,
                                                              FULL_TILEX_COORDINATES));
INSTANTIATE_TEST_SUITE_P(tileW, tileWFixture, testing::Values(TILE_COORDINATES,
                                                              FULL_TILEW_COORDINATES));
