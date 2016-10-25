/*
 * Copyright (C) 2016 Intel Corporation
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


/* Instructions, enums and structures for BRW.
 *
 * This file has been generated, do not hand edit.
 */

#ifndef GEN4_PACK_H
#define GEN4_PACK_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#ifndef __gen_validate_value
#define __gen_validate_value(x)
#endif

#ifndef __gen_field_functions
#define __gen_field_functions

union __gen_value {
   float f;
   uint32_t dw;
};

static inline uint64_t
__gen_mbo(uint32_t start, uint32_t end)
{
   return (~0ull >> (64 - (end - start + 1))) << start;
}

static inline uint64_t
__gen_uint(uint64_t v, uint32_t start, uint32_t end)
{
   __gen_validate_value(v);

#if DEBUG
   const int width = end - start + 1;
   if (width < 64) {
      const uint64_t max = (1ull << width) - 1;
      assert(v <= max);
   }
#endif

   return v << start;
}

static inline uint64_t
__gen_sint(int64_t v, uint32_t start, uint32_t end)
{
   const int width = end - start + 1;

   __gen_validate_value(v);

#if DEBUG
   if (width < 64) {
      const int64_t max = (1ll << (width - 1)) - 1;
      const int64_t min = -(1ll << (width - 1));
      assert(min <= v && v <= max);
   }
#endif

   const uint64_t mask = ~0ull >> (64 - width);

   return (v & mask) << start;
}

static inline uint64_t
__gen_offset(uint64_t v, uint32_t start, uint32_t end)
{
   __gen_validate_value(v);
#if DEBUG
   uint64_t mask = (~0ull >> (64 - (end - start + 1))) << start;

   assert((v & ~mask) == 0);
#endif

   return v;
}

static inline uint32_t
__gen_float(float v)
{
   __gen_validate_value(v);
   return ((union __gen_value) { .f = (v) }).dw;
}

static inline uint64_t
__gen_sfixed(float v, uint32_t start, uint32_t end, uint32_t fract_bits)
{
   __gen_validate_value(v);

   const float factor = (1 << fract_bits);

#if DEBUG
   const float max = ((1 << (end - start)) - 1) / factor;
   const float min = -(1 << (end - start)) / factor;
   assert(min <= v && v <= max);
#endif

   const int64_t int_val = llroundf(v * factor);
   const uint64_t mask = ~0ull >> (64 - (end - start + 1));

   return (int_val & mask) << start;
}

static inline uint64_t
__gen_ufixed(float v, uint32_t start, uint32_t end, uint32_t fract_bits)
{
   __gen_validate_value(v);

   const float factor = (1 << fract_bits);

#if DEBUG
   const float max = ((1 << (end - start + 1)) - 1) / factor;
   const float min = 0.0f;
   assert(min <= v && v <= max);
#endif

   const uint64_t uint_val = llroundf(v * factor);

   return uint_val << start;
}

#ifndef __gen_address_type
#error #define __gen_address_type before including this file
#endif

#ifndef __gen_user_data
#error #define __gen_combine_address before including this file
#endif

#endif


#define GEN4_RENDER_SURFACE_STATE_length       6
struct GEN4_RENDER_SURFACE_STATE {
   uint32_t                             SurfaceType;
#define SURFTYPE_1D                              0
#define SURFTYPE_2D                              1
#define SURFTYPE_3D                              2
#define SURFTYPE_CUBE                            3
#define SURFTYPE_BUFFER                          4
#define SURFTYPE_NULL                            7
   uint32_t                             DataReturnFormat;
#define DATA_RETURN_FLOAT32                      0
#define DATA_RETURN_S114                         1
   uint32_t                             SurfaceFormat;
   uint32_t                             ColorBufferComponentWriteDisables;
#define WRITEDISABLE_ALPHA                       8
#define WRITEDISABLE_RED                         4
#define WRITEDISABLE_GREEN                       2
#define WRITEDISABLE_BLUE                        1
   bool                                 ColorBlendEnable;
   uint32_t                             VerticalLineStride;
   uint32_t                             VerticalLineStrideOffset;
   uint32_t                             MIPMapLayoutMode;
#define MIPLAYOUT_BELOW                          0
#define MIPLAYOUT_RIGHT                          1
   uint32_t                             RenderCacheReadWriteMode;
#define WRITE_ONLY                               0
#define READ_WRITE                               1
   uint32_t                             MediaBoundaryPixelMode;
#define NORMAL_MODE                              0
   uint32_t                             CubeFaceEnables;
   __gen_address_type                   SurfaceBaseAddress;
   uint32_t                             Height;
   uint32_t                             Width;
   uint32_t                             MIPCountLOD;
   uint32_t                             Depth;
   uint32_t                             SurfacePitch;
   uint32_t                             TiledSurface;
   uint32_t                             TileWalk;
#define TILEWALK_XMAJOR                          0
#define TILEWALK_YMAJOR                          1
   uint32_t                             SurfaceMinLOD;
   uint32_t                             MinimumArrayElement;
   uint32_t                             RenderTargetViewExtent;
};

static inline void
GEN4_RENDER_SURFACE_STATE_pack(__gen_user_data *data, void * restrict dst,
                               const struct GEN4_RENDER_SURFACE_STATE * restrict values)
{
   uint32_t * restrict dw = (uint32_t * restrict) dst;

   dw[0] =
      __gen_uint(values->SurfaceType, 29, 31) |
      __gen_uint(values->DataReturnFormat, 27, 27) |
      __gen_uint(values->SurfaceFormat, 18, 26) |
      __gen_uint(values->ColorBufferComponentWriteDisables, 14, 17) |
      __gen_uint(values->ColorBlendEnable, 13, 13) |
      __gen_uint(values->VerticalLineStride, 12, 12) |
      __gen_uint(values->VerticalLineStrideOffset, 11, 11) |
      __gen_uint(values->MIPMapLayoutMode, 10, 10) |
      __gen_uint(values->RenderCacheReadWriteMode, 8, 8) |
      __gen_uint(values->MediaBoundaryPixelMode, 6, 7) |
      __gen_uint(values->CubeFaceEnables, 0, 5);

   dw[1] = __gen_combine_address(data, &dw[1], values->SurfaceBaseAddress, 0);

   dw[2] =
      __gen_uint(values->Height, 19, 31) |
      __gen_uint(values->Width, 6, 18) |
      __gen_uint(values->MIPCountLOD, 2, 5);

   dw[3] =
      __gen_uint(values->Depth, 21, 31) |
      __gen_uint(values->SurfacePitch, 3, 19) |
      __gen_uint(values->TiledSurface, 1, 1) |
      __gen_uint(values->TileWalk, 0, 0);

   dw[4] =
      __gen_uint(values->SurfaceMinLOD, 28, 31) |
      __gen_uint(values->MinimumArrayElement, 17, 27) |
      __gen_uint(values->RenderTargetViewExtent, 8, 16);

   dw[5] = 0;
}

#endif /* GEN4_PACK_H */
