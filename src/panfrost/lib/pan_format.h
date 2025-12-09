/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __PAN_FORMAT_H
#define __PAN_FORMAT_H

#include "genxml/gen_macros.h"

#include "util/format/u_format.h"

#include "drm-uapi/drm_fourcc.h"

/*
 * List of supported modifiers, in descending order of preference. AFBC is
 * faster than u-interleaved tiling which is faster than linear. Within AFBC,
 * enabling the YUV-like transform is typically a win where possible.
 * AFRC is only used if explicitly asked for (only for RGB formats).
 * Similarly MTK 16L32 is only used if explicitly asked for.
 */
#define PAN_SUPPORTED_MODIFIERS(__name)                                        \
   static const uint64_t __name[] = {                                          \
      DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |                \
                              AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SPLIT), \
      DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |                \
                              AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SPLIT | \
                              AFBC_FORMAT_MOD_YTR),                            \
                                                                               \
      DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |               \
                              AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC |     \
                              AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_YTR),   \
                                                                               \
      DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |               \
                              AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC |     \
                              AFBC_FORMAT_MOD_SPARSE),                         \
                                                                               \
      DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |               \
                              AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_YTR),   \
                                                                               \
      DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |               \
                              AFBC_FORMAT_MOD_SPARSE),                         \
                                                                               \
      DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED,                            \
      DRM_FORMAT_MOD_LINEAR,                                                   \
                                                                               \
      DRM_FORMAT_MOD_ARM_AFRC(                                                 \
         AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_16)),              \
      DRM_FORMAT_MOD_ARM_AFRC(                                                 \
         AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_24)),              \
      DRM_FORMAT_MOD_ARM_AFRC(                                                 \
         AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_32)),              \
      DRM_FORMAT_MOD_ARM_AFRC(                                                 \
         AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_16) |              \
         AFRC_FORMAT_MOD_LAYOUT_SCAN),                                         \
      DRM_FORMAT_MOD_ARM_AFRC(                                                 \
         AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_24) |              \
         AFRC_FORMAT_MOD_LAYOUT_SCAN),                                         \
      DRM_FORMAT_MOD_ARM_AFRC(                                                 \
         AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_32) |              \
         AFRC_FORMAT_MOD_LAYOUT_SCAN),                                         \
   }

/* DRM modifier helper */

#define drm_is_afbc(mod)                                                       \
   ((mod >> 52) ==                                                             \
    (DRM_FORMAT_MOD_ARM_TYPE_AFBC | (DRM_FORMAT_MOD_VENDOR_ARM << 4)))

#define drm_is_afrc(mod)                                                       \
   ((mod >> 52) ==                                                             \
    (DRM_FORMAT_MOD_ARM_TYPE_AFRC | (DRM_FORMAT_MOD_VENDOR_ARM << 4)))

static inline bool
pan_u_tiled_or_linear_supports_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R8G8B8_420_UNORM_PACKED:
   case PIPE_FORMAT_R10G10B10_420_UNORM_PACKED:
      return false;

   default:
      return true;
   }
}

/* Formats */

static inline unsigned
pan_format_get_plane_blocksize(enum pipe_format format, unsigned plane_idx)
{
   switch (format) {
   case PIPE_FORMAT_R8_G8B8_420_UNORM:
   case PIPE_FORMAT_R8_B8G8_420_UNORM:
   case PIPE_FORMAT_R8_G8B8_422_UNORM:
   case PIPE_FORMAT_R8_B8G8_422_UNORM:
      return plane_idx ? 2 : 1;
   case PIPE_FORMAT_R10_G10B10_420_UNORM:
   case PIPE_FORMAT_R10_G10B10_422_UNORM:
      return plane_idx ? 10 : 5;
   case PIPE_FORMAT_R8_G8_B8_420_UNORM:
   case PIPE_FORMAT_R8_B8_G8_420_UNORM:
      return 1;
   default:
      assert(util_format_get_num_planes(format) == 1);
      return util_format_get_blocksize(format);
   }
}

typedef uint32_t mali_pixel_format;

/* pan bind flags */
#define PAN_BIND_DEPTH_STENCIL BITFIELD_BIT(0)
#define PAN_BIND_RENDER_TARGET BITFIELD_BIT(1)
#define PAN_BIND_SAMPLER_VIEW  BITFIELD_BIT(2)
#define PAN_BIND_VERTEX_BUFFER BITFIELD_BIT(3)
#define PAN_BIND_STORAGE_IMAGE BITFIELD_BIT(4)
#define PAN_BIND_TEXEL_BUFFER  BITFIELD_BIT(5)

struct pan_format {
   uint32_t hw          : 21;
   uint32_t texfeat_bit : 5;
   uint32_t bind        : 6;
};

struct pan_blendable_format {
   /* enum mali_color_buffer_internal_format */ uint16_t internal;
   /* enum mali_mfbd_color_format */ uint16_t writeback;

   /* Indexed by the dithered? flag. So _PU first, then _AU */
   mali_pixel_format bifrost[2];
};

#define pan_blendable_formats_v4 pan_blendable_formats_v5
extern const struct pan_blendable_format
   pan_blendable_formats_v5[PIPE_FORMAT_COUNT];
extern const struct pan_blendable_format
   pan_blendable_formats_v6[PIPE_FORMAT_COUNT];
extern const struct pan_blendable_format
   pan_blendable_formats_v7[PIPE_FORMAT_COUNT];
extern const struct pan_blendable_format
   pan_blendable_formats_v9[PIPE_FORMAT_COUNT];
extern const struct pan_blendable_format
   pan_blendable_formats_v10[PIPE_FORMAT_COUNT];
extern const struct pan_blendable_format
   pan_blendable_formats_v12[PIPE_FORMAT_COUNT];
extern const struct pan_blendable_format
   pan_blendable_formats_v13[PIPE_FORMAT_COUNT];

uint8_t pan_raw_format_mask_midgard(enum pipe_format *formats);

static inline const struct pan_blendable_format *
pan_blendable_format_table(unsigned arch)
{
   switch (arch) {
#define FMT_TABLE(x) case x: return pan_blendable_formats_v ## x
   FMT_TABLE(4);
   FMT_TABLE(5);
   FMT_TABLE(6);
   FMT_TABLE(7);
   FMT_TABLE(9);
   FMT_TABLE(10);
   FMT_TABLE(12);
   FMT_TABLE(13);
#undef FMT_TABLE
   default:
      assert(!"Unsupported architecture");
      return NULL;
   }
}

#define pan_pipe_format_v4 pan_pipe_format_v5
extern const struct pan_format pan_pipe_format_v5[PIPE_FORMAT_COUNT];
extern const struct pan_format pan_pipe_format_v6[PIPE_FORMAT_COUNT];
extern const struct pan_format pan_pipe_format_v7[PIPE_FORMAT_COUNT];
extern const struct pan_format pan_pipe_format_v9[PIPE_FORMAT_COUNT];
extern const struct pan_format pan_pipe_format_v10[PIPE_FORMAT_COUNT];
extern const struct pan_format pan_pipe_format_v12[PIPE_FORMAT_COUNT];
extern const struct pan_format pan_pipe_format_v13[PIPE_FORMAT_COUNT];

static inline const struct pan_format *
pan_format_table(unsigned arch)
{
   switch (arch) {
#define FMT_TABLE(x) case x: return pan_pipe_format_v ## x
   FMT_TABLE(4);
   FMT_TABLE(5);
   FMT_TABLE(6);
   FMT_TABLE(7);
   FMT_TABLE(9);
   FMT_TABLE(10);
   FMT_TABLE(12);
   FMT_TABLE(13);
#undef FMT_TABLE
   default:
      assert(!"Unsupported architecture");
      return NULL;
   }
}

/* Helpers to construct swizzles */

#define PAN_V6_SWIZZLE(R, G, B, A)                                             \
   (((MALI_CHANNEL_##R) << 0) | ((MALI_CHANNEL_##G) << 3) |                    \
    ((MALI_CHANNEL_##B) << 6) | ((MALI_CHANNEL_##A) << 9))

static inline unsigned
pan_get_default_swizzle(unsigned components)
{
   switch (components) {
   case 1:
      return PAN_V6_SWIZZLE(R, 0, 0, 1);
   case 2:
      return PAN_V6_SWIZZLE(R, G, 0, 1);
   case 3:
      return PAN_V6_SWIZZLE(R, G, B, 1);
   case 4:
      return PAN_V6_SWIZZLE(R, G, B, A);
   default:
      UNREACHABLE("Invalid number of components");
   }
}

#if PAN_ARCH == 7 || PAN_ARCH >= 10
struct pan_decomposed_swizzle {
   /* Component ordering to apply first */
   enum mali_rgb_component_order pre;

   /* Bijective swizzle applied after */
   unsigned char post[4];
};

struct pan_decomposed_swizzle
   GENX(pan_decompose_swizzle)(enum mali_rgb_component_order order);
#endif

#define MALI_SRGB_L (0)
#define MALI_SRGB_S (1)

#if PAN_ARCH <= 6

#define MALI_V6_0000 PAN_V6_SWIZZLE(0, 0, 0, 0)
#define MALI_V6_000R PAN_V6_SWIZZLE(0, 0, 0, R)
#define MALI_V6_0R00 PAN_V6_SWIZZLE(0, R, 0, 0)
#define MALI_V6_0A00 PAN_V6_SWIZZLE(0, A, 0, 0)
#define MALI_V6_AAAA PAN_V6_SWIZZLE(A, A, A, A)
#define MALI_V6_A001 PAN_V6_SWIZZLE(A, 0, 0, 1)
#define MALI_V6_ABG1 PAN_V6_SWIZZLE(A, B, G, 1)
#define MALI_V6_ABGR PAN_V6_SWIZZLE(A, B, G, R)
#define MALI_V6_ARGB PAN_V6_SWIZZLE(A, R, G, B)
#define MALI_V6_BGR1 PAN_V6_SWIZZLE(B, G, R, 1)
#define MALI_V6_BGRA PAN_V6_SWIZZLE(B, G, R, A)
#define MALI_V6_GBA1 PAN_V6_SWIZZLE(G, B, A, 1)
#define MALI_V6_GBAR PAN_V6_SWIZZLE(G, B, A, R)
#define MALI_V6_R000 PAN_V6_SWIZZLE(R, 0, 0, 0)
#define MALI_V6_R001 PAN_V6_SWIZZLE(R, 0, 0, 1)
#define MALI_V6_RG01 PAN_V6_SWIZZLE(R, G, 0, 1)
#define MALI_V6_RGB1 PAN_V6_SWIZZLE(R, G, B, 1)
#define MALI_V6_RGBA PAN_V6_SWIZZLE(R, G, B, A)
#define MALI_V6_RRR1 PAN_V6_SWIZZLE(R, R, R, 1)
#define MALI_V6_RRRG PAN_V6_SWIZZLE(R, R, R, G)
#define MALI_V6_RRRR PAN_V6_SWIZZLE(R, R, R, R)
#define MALI_V6_GGGG PAN_V6_SWIZZLE(G, G, G, G)

#define MALI_PACK_FMT(mali, swizzle, srgb)                                     \
   (MALI_V6_##swizzle) | ((MALI_##mali) << 12) | (((MALI_SRGB_##srgb)) << 20)

#else

#define MALI_RGB_COMPONENT_ORDER_R001 MALI_RGB_COMPONENT_ORDER_RGB1
#define MALI_RGB_COMPONENT_ORDER_RG01 MALI_RGB_COMPONENT_ORDER_RGB1
#define MALI_RGB_COMPONENT_ORDER_GBAR MALI_RGB_COMPONENT_ORDER_ARGB
#define MALI_RGB_COMPONENT_ORDER_GBA1 MALI_RGB_COMPONENT_ORDER_1RGB
#define MALI_RGB_COMPONENT_ORDER_ABG1 MALI_RGB_COMPONENT_ORDER_1BGR

#define MALI_PACK_FMT(mali, swizzle, srgb)                                     \
   (MALI_RGB_COMPONENT_ORDER_##swizzle) | ((MALI_##mali) << 12) |              \
      (((MALI_SRGB_##srgb)) << 20)

#endif

#define MALI_EXTRACT_INDEX(pixfmt) (((pixfmt) >> 12) & 0xFF)

static inline bool
pan_format_is_yuv(enum pipe_format f)
{
   enum util_format_layout layout = util_format_description(f)->layout;

   /* Mesa's subsampled RGB formats are considered YUV formats on Mali */
   return layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED ||
          layout == UTIL_FORMAT_LAYOUT_PLANAR2 ||
          layout == UTIL_FORMAT_LAYOUT_PLANAR3;
}

#ifdef PAN_ARCH
static inline const struct pan_format *
GENX(pan_format_from_pipe_format)(enum pipe_format f)
{
   return &GENX(pan_pipe_format)[f];
}

static inline const struct pan_blendable_format *
GENX(pan_blendable_format_from_pipe_format)(enum pipe_format f)
{
   return &GENX(pan_blendable_formats)[f];
}

#if PAN_ARCH >= 6
static inline unsigned
GENX(pan_dithered_format_from_pipe_format)(enum pipe_format f, bool dithered)
{
   mali_pixel_format pixfmt = GENX(pan_blendable_formats)[f].bifrost[dithered];

   /* Formats requiring blend shaders are stored raw in the tilebuffer and will
    * have 0 as their pixel format. Assumes dithering is set, I don't know of a
    * case when it makes sense to turn off dithering. */
   return pixfmt ?: GENX(pan_format_from_pipe_format)(f)->hw;
}
#endif
#endif

#endif
