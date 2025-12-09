/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_FORMAT_TABLE_H
#define FD6_FORMAT_TABLE_H

#include "util/format/u_format.h"
#include "util/u_math.h"

#include "common/freedreno_common.h"

#include "fd6_hw.h"

BEGINC;

struct fd_dev_info;

static inline enum a6xx_tex_swiz
fdl6_swiz(unsigned char swiz)
{
   STATIC_ASSERT((unsigned) A6XX_TEX_X == (unsigned) PIPE_SWIZZLE_X);
   STATIC_ASSERT((unsigned) A6XX_TEX_Y == (unsigned) PIPE_SWIZZLE_Y);
   STATIC_ASSERT((unsigned) A6XX_TEX_Z == (unsigned) PIPE_SWIZZLE_Z);
   STATIC_ASSERT((unsigned) A6XX_TEX_W == (unsigned) PIPE_SWIZZLE_W);
   STATIC_ASSERT((unsigned) A6XX_TEX_ZERO == (unsigned) PIPE_SWIZZLE_0);
   STATIC_ASSERT((unsigned) A6XX_TEX_ONE == (unsigned) PIPE_SWIZZLE_1);
   return (enum a6xx_tex_swiz) swiz;
}

static inline enum a8xx_tex_swiz
fdl8_swiz(unsigned char swiz)
{
   switch (swiz) {
   case PIPE_SWIZZLE_X: return A8XX_SWIZ_X;
   case PIPE_SWIZZLE_Y: return A8XX_SWIZ_Y;
   case PIPE_SWIZZLE_Z: return A8XX_SWIZ_Z;
   case PIPE_SWIZZLE_W: return A8XX_SWIZ_W;
   case PIPE_SWIZZLE_0: return A8XX_SWIZ_ZERO;
   case PIPE_SWIZZLE_1: return A8XX_SWIZ_ONE;
   default:             return A8XX_SWIZ_IDENTITY;
   }
}

enum a6xx_depth_format fd6_pipe2depth(enum pipe_format format);

enum a6xx_format fd6_vertex_format(enum pipe_format format) ATTRIBUTE_CONST;
enum a3xx_color_swap fd6_vertex_swap(enum pipe_format format) ATTRIBUTE_CONST;
enum a6xx_format fd6_texture_format(enum pipe_format format,
                                    enum a6xx_tile_mode tile_mode,
                                    bool is_mutable) ATTRIBUTE_CONST;
bool fd6_texture_format_supported(const struct fd_dev_info *info, enum pipe_format format,
                                  enum a6xx_tile_mode tile_mode, bool is_mutable)
                                  ATTRIBUTE_CONST;
enum a3xx_color_swap fd6_texture_swap(enum pipe_format format,
                                      enum a6xx_tile_mode tile_mode,
                                      bool is_mutable) ATTRIBUTE_CONST;
enum a6xx_format fd6_color_format(enum pipe_format format,
                                  enum a6xx_tile_mode tile_mode) ATTRIBUTE_CONST;
enum a3xx_color_swap fd6_color_swap(enum pipe_format format,
                                    enum a6xx_tile_mode tile_mode,
                                    bool is_mutable) ATTRIBUTE_CONST;

ENDC;

#endif /* FD6_FORMAT_TABLE_H */
