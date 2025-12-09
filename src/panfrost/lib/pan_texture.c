/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright (C) 2024 Arm Ltd.
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

#include "pan_texture.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "pan_afbc.h"
#include "pan_afrc.h"
#include "pan_desc.h"
#include "pan_format.h"
#include "pan_image.h"
#include "pan_pool.h"
#include "pan_util.h"

#if PAN_ARCH >= 5
/*
 * Arm Scalable Texture Compression (ASTC) corresponds to just a few formats.
 * The block dimension is not part of the format. Instead, it is encoded as a
 * 6-bit tag on the payload pointer. Map the block size for a single dimension.
 */
static inline enum mali_astc_2d_dimension
pan_astc_dim_2d(unsigned dim)
{
   switch (dim) {
   case 4:
      return MALI_ASTC_2D_DIMENSION_4;
   case 5:
      return MALI_ASTC_2D_DIMENSION_5;
   case 6:
      return MALI_ASTC_2D_DIMENSION_6;
   case 8:
      return MALI_ASTC_2D_DIMENSION_8;
   case 10:
      return MALI_ASTC_2D_DIMENSION_10;
   case 12:
      return MALI_ASTC_2D_DIMENSION_12;
   default:
      UNREACHABLE("Invalid ASTC dimension");
   }
}

static inline enum mali_astc_3d_dimension
pan_astc_dim_3d(unsigned dim)
{
   switch (dim) {
   case 3:
      return MALI_ASTC_3D_DIMENSION_3;
   case 4:
      return MALI_ASTC_3D_DIMENSION_4;
   case 5:
      return MALI_ASTC_3D_DIMENSION_5;
   case 6:
      return MALI_ASTC_3D_DIMENSION_6;
   default:
      UNREACHABLE("Invalid ASTC dimension");
   }
}
#endif

#if PAN_ARCH >= 5 && PAN_ARCH <= 8
/* Texture addresses are tagged with information about compressed formats.
 * AFBC uses a bit for whether the colorspace transform is enabled (RGB and
 * RGBA only). */
static unsigned
afbc_compression_tag(enum mali_texture_dimension dim, uint64_t modifier)
{
   unsigned flags =
      (modifier & AFBC_FORMAT_MOD_YTR) ? MALI_AFBC_SURFACE_FLAG_YTR : 0;

#if PAN_ARCH >= 6
   /* Prefetch enable */
   flags |= MALI_AFBC_SURFACE_FLAG_PREFETCH;

   if (pan_afbc_is_wide(modifier))
      flags |= MALI_AFBC_SURFACE_FLAG_WIDE_BLOCK;

   if (modifier & AFBC_FORMAT_MOD_SPLIT)
      flags |= MALI_AFBC_SURFACE_FLAG_SPLIT_BLOCK;
#endif

#if PAN_ARCH >= 7
   /* Tiled headers */
   if (modifier & AFBC_FORMAT_MOD_TILED)
      flags |= MALI_AFBC_SURFACE_FLAG_TILED_HEADER;

   /* Used to make sure AFBC headers don't point outside the AFBC
    * body. HW is using the AFBC surface stride to do this check,
    * which doesn't work for 3D textures because the surface
    * stride does not cover the body. Only supported on v7+.
    */
   if (dim != MALI_TEXTURE_DIMENSION_3D)
      flags |= MALI_AFBC_SURFACE_FLAG_CHECK_PAYLOAD_RANGE;
#endif

   return flags;
}

/* For ASTC, this is a "stretch factor" encoding the block size. */
static unsigned
astc_compression_tag(const struct util_format_description *desc)
{
   if (desc->block.depth > 1) {
      return (pan_astc_dim_3d(desc->block.depth) << 4) |
             (pan_astc_dim_3d(desc->block.height) << 2) |
             pan_astc_dim_3d(desc->block.width);
   } else {
      return (pan_astc_dim_2d(desc->block.height) << 3) |
             pan_astc_dim_2d(desc->block.width);
   }
}
#endif

/* Following the texture descriptor is a number of descriptors. How many? */

static unsigned
pan_texture_num_elements(const struct pan_image_view *iview)
{
   unsigned levels = 1 + iview->last_level - iview->first_level;
   unsigned layers = 1 + iview->last_layer - iview->first_layer;
   unsigned nr_samples = pan_image_view_get_nr_samples(iview);

   return levels * layers * MAX2(nr_samples, 1);
}

/* Conservative estimate of the size of the texture payload a priori.
 * Average case, size equal to the actual size. Worst case, off by 2x (if
 * a manual stride is not needed on a linear texture). Returned value
 * must be greater than or equal to the actual size, so it's safe to use
 * as an allocation amount */

unsigned
GENX(pan_texture_estimate_payload_size)(const struct pan_image_view *iview)
{
   size_t element_size;

#if PAN_ARCH >= 9
   /* All plane descriptors are the same size. */
   element_size = pan_size(NULL_PLANE);

   /* 2-plane and 3-plane YUV use two plane descriptors. */
   if (pan_format_is_yuv(iview->format) && iview->planes[1].image != NULL)
      element_size *= 2;
#elif PAN_ARCH == 7
   if (pan_format_is_yuv(iview->format))
      element_size = pan_size(MULTIPLANAR_SURFACE);
   else
      element_size = pan_size(SURFACE_WITH_STRIDE);
#else
   /* Assume worst case. Overestimates on Midgard, but that's ok. */
   element_size = pan_size(SURFACE_WITH_STRIDE);
#endif

   unsigned elements = pan_texture_num_elements(iview);

   return element_size * elements;
}

#if PAN_ARCH < 9
static void
pan_emit_bview_surface_with_stride(const struct pan_buffer_view *bview,
                                   void *payload)
{
   uint64_t base = bview->base;

#if PAN_ARCH >= 5
   const struct util_format_description *desc =
      util_format_description(bview->format);
   if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC)
      base |= astc_compression_tag(desc);
#endif

   pan_cast_and_pack(payload, SURFACE_WITH_STRIDE, cfg) {
      cfg.pointer = base;
      cfg.row_stride = 0;
      cfg.surface_stride = 0;
   }
}
#endif

#if PAN_ARCH >= 9

/* clang-format off */
#define CLUMP_FMT(pipe, mali) [PIPE_FORMAT_ ## pipe] = MALI_CLUMP_FORMAT_ ## mali
static enum mali_clump_format special_clump_formats[PIPE_FORMAT_COUNT] = {
   CLUMP_FMT(X32_S8X24_UINT,  X32S8X24),
   CLUMP_FMT(X24S8_UINT,      X24S8),
   CLUMP_FMT(S8X24_UINT,      S8X24),
   CLUMP_FMT(S8_UINT,         S8),
   CLUMP_FMT(L4A4_UNORM,      L4A4),
   CLUMP_FMT(L8A8_UNORM,      L8A8),
   CLUMP_FMT(L8A8_UINT,       L8A8),
   CLUMP_FMT(L8A8_SINT,       L8A8),
   CLUMP_FMT(A8_UNORM,        A8),
   CLUMP_FMT(A8_UINT,         A8),
   CLUMP_FMT(A8_SINT,         A8),
   CLUMP_FMT(ETC1_RGB8,       ETC2_RGB8),
   CLUMP_FMT(ETC2_RGB8,       ETC2_RGB8),
   CLUMP_FMT(ETC2_SRGB8,      ETC2_RGB8),
   CLUMP_FMT(ETC2_RGB8A1,     ETC2_RGB8A1),
   CLUMP_FMT(ETC2_SRGB8A1,    ETC2_RGB8A1),
   CLUMP_FMT(ETC2_RGBA8,      ETC2_RGBA8),
   CLUMP_FMT(ETC2_SRGBA8,     ETC2_RGBA8),
   CLUMP_FMT(ETC2_R11_UNORM,  ETC2_R11_UNORM),
   CLUMP_FMT(ETC2_R11_SNORM,  ETC2_R11_SNORM),
   CLUMP_FMT(ETC2_RG11_UNORM, ETC2_RG11_UNORM),
   CLUMP_FMT(ETC2_RG11_SNORM, ETC2_RG11_SNORM),
   CLUMP_FMT(DXT1_RGB,        BC1_UNORM),
   CLUMP_FMT(DXT1_RGBA,       BC1_UNORM),
   CLUMP_FMT(DXT1_SRGB,       BC1_UNORM),
   CLUMP_FMT(DXT1_SRGBA,      BC1_UNORM),
   CLUMP_FMT(DXT3_RGBA,       BC2_UNORM),
   CLUMP_FMT(DXT3_SRGBA,      BC2_UNORM),
   CLUMP_FMT(DXT5_RGBA,       BC3_UNORM),
   CLUMP_FMT(DXT5_SRGBA,      BC3_UNORM),
   CLUMP_FMT(RGTC1_UNORM,     BC4_UNORM),
   CLUMP_FMT(RGTC1_SNORM,     BC4_SNORM),
   CLUMP_FMT(RGTC2_UNORM,     BC5_UNORM),
   CLUMP_FMT(RGTC2_SNORM,     BC5_SNORM),
   CLUMP_FMT(BPTC_RGB_FLOAT,  BC6H_SF16),
   CLUMP_FMT(BPTC_RGB_UFLOAT, BC6H_UF16),
   CLUMP_FMT(BPTC_RGBA_UNORM, BC7_UNORM),
   CLUMP_FMT(BPTC_SRGBA,      BC7_UNORM),
};
#undef CLUMP_FMT
/* clang-format on */

static enum mali_clump_format
pan_clump_format(enum pipe_format format)
{
   /* First, try a special clump format. Note that the 0 encoding is for a
    * raw clump format, which will never be in the special table.
    */
   if (special_clump_formats[format])
      return special_clump_formats[format];

   /* Else, it's a raw format. Raw formats must not be compressed. */
   assert(!util_format_is_compressed(format));

   /* YUV-sampling has special cases */
   if (pan_format_is_yuv(format)) {
      switch (format) {
      case PIPE_FORMAT_R8G8_R8B8_UNORM:
      case PIPE_FORMAT_G8R8_B8R8_UNORM:
      case PIPE_FORMAT_R8B8_R8G8_UNORM:
      case PIPE_FORMAT_B8R8_G8R8_UNORM:
      case PIPE_FORMAT_R8_G8B8_422_UNORM:
      case PIPE_FORMAT_R8_B8G8_422_UNORM:
         return MALI_CLUMP_FORMAT_Y8_UV8_422;
      case PIPE_FORMAT_R8_G8B8_420_UNORM:
      case PIPE_FORMAT_R8_B8G8_420_UNORM:
      case PIPE_FORMAT_R8_G8_B8_420_UNORM:
      case PIPE_FORMAT_R8_B8_G8_420_UNORM:
      case PIPE_FORMAT_R8G8B8_420_UNORM_PACKED:
         return MALI_CLUMP_FORMAT_Y8_UV8_420;
      case PIPE_FORMAT_R10_G10B10_420_UNORM:
      case PIPE_FORMAT_R10G10B10_420_UNORM_PACKED:
         return MALI_CLUMP_FORMAT_Y10_UV10_420;
      case PIPE_FORMAT_R10_G10B10_422_UNORM:
      case PIPE_FORMAT_X6R10X6G10_X6R10X6B10_422_UNORM:
         return MALI_CLUMP_FORMAT_Y10_UV10_422;
      default:
         UNREACHABLE("unhandled clump format");
      }
   }

   /* Select the appropriate raw format. */
   switch (util_format_get_blocksize(format)) {
   case 1:
      return MALI_CLUMP_FORMAT_RAW8;
   case 2:
      return MALI_CLUMP_FORMAT_RAW16;
   case 3:
      return MALI_CLUMP_FORMAT_RAW24;
   case 4:
      return MALI_CLUMP_FORMAT_RAW32;
   case 6:
      return MALI_CLUMP_FORMAT_RAW48;
   case 8:
      return MALI_CLUMP_FORMAT_RAW64;
   case 12:
      return MALI_CLUMP_FORMAT_RAW96;
   case 16:
      return MALI_CLUMP_FORMAT_RAW128;
   default:
      UNREACHABLE("Invalid bpp");
   }
}

static enum mali_afbc_superblock_size
translate_superblock_size(uint64_t modifier)
{
   assert(drm_is_afbc(modifier));

   switch (modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK) {
   case AFBC_FORMAT_MOD_BLOCK_SIZE_16x16:
      return MALI_AFBC_SUPERBLOCK_SIZE_16X16;
   case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8:
      return MALI_AFBC_SUPERBLOCK_SIZE_32X8;
   case AFBC_FORMAT_MOD_BLOCK_SIZE_64x4:
      return MALI_AFBC_SUPERBLOCK_SIZE_64X4;
   default:
      UNREACHABLE("Invalid superblock size");
   }
}

#if PAN_ARCH >= 10
#define PLANE_SET_EXTENT(__cfg, w, h)                                          \
   do {                                                                        \
      (__cfg).width = w;                                                       \
      (__cfg).height = h;                                                      \
   } while (0)
#else
#define PLANE_SET_EXTENT(cfg, w, h)                                            \
   do {                                                                        \
   } while (0)
#endif

#if PAN_ARCH > 10
#define PLANE_SET_SIZE(cfg__, size__)                                          \
   do {                                                                        \
      (cfg__).size = size__ & BITFIELD_MASK(32);                               \
      (cfg__).size_hi = size__ >> 32;                                          \
   } while (0)
#define PLANE_SET_SLICE_STRIDE(cfg__, size__)                                  \
   do {                                                                        \
      (cfg__).slice_stride = size__ & BITFIELD_MASK(32);                       \
      (cfg__).slice_stride_hi = size__ >> 32;                                  \
   } while (0)
#elif PAN_ARCH >= 9
#define PLANE_SET_SIZE(cfg__, size__) (cfg__).size = size__
#define PLANE_SET_SLICE_STRIDE(cfg__, size__)                                  \
   (cfg__).slice_stride = size__
#endif

static void
get_linear_or_u_tiled_plane_props(const struct pan_image_view *iview,
                                  int plane_idx, unsigned mip_level,
                                  unsigned layer_or_z_slice, uint64_t *pointer,
                                  uint32_t *row_stride, uint64_t *slice_stride,
                                  uint64_t *size)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      util_format_has_stencil(desc)
         ? pan_image_view_get_s_plane(iview)
         : pan_image_view_get_plane(iview, plane_idx);

   assert(pref.image != NULL);

   const struct pan_image_plane *plane = pref.image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slayout =
      &plane->layout.slices[mip_level];

   *pointer = plane->base + slayout->offset_B;
   *size = slayout->size_B;
   *row_stride = slayout->tiled_or_linear.row_stride_B;

   if (pref.image->props.dim == MALI_TEXTURE_DIMENSION_3D) {
      *pointer += layer_or_z_slice * slayout->tiled_or_linear.surface_stride_B;
      *size -= layer_or_z_slice * slayout->tiled_or_linear.surface_stride_B;
      *slice_stride = slayout->tiled_or_linear.surface_stride_B;
   } else {
      *pointer += layer_or_z_slice * plane->layout.array_stride_B;
      *slice_stride = pref.image->props.nr_samples > 1
                         ? slayout->tiled_or_linear.surface_stride_B
                         : 0;
   }
}

static void
emit_generic_plane(const struct pan_image_view *iview, int plane_idx,
                   unsigned mip_level, unsigned layer_or_z_slice, void *payload)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      util_format_has_stencil(desc)
         ? pan_image_view_get_s_plane(iview)
         : pan_image_view_get_plane(iview, plane_idx);
   const struct pan_image_props *props = &pref.image->props;
   uint64_t plane_addr, plane_size, slice_stride;
   uint32_t row_stride;

   /* 3-planar formats must use Chroma 2p planes for the U V planes. */
   assert(plane_idx == 0 || desc->layout != UTIL_FORMAT_LAYOUT_PLANAR3);
   assert(props->modifier == DRM_FORMAT_MOD_LINEAR ||
          props->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);

   get_linear_or_u_tiled_plane_props(iview, plane_idx, mip_level,
                                     layer_or_z_slice, &plane_addr, &row_stride,
                                     &slice_stride, &plane_size);

   pan_cast_and_pack(payload, GENERIC_PLANE, cfg) {
      cfg.clump_ordering =
         props->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED
            ? MALI_CLUMP_ORDERING_TILED_U_INTERLEAVED
            : MALI_CLUMP_ORDERING_LINEAR;
      cfg.clump_format = pan_clump_format(iview->format);
      PLANE_SET_SIZE(cfg, plane_size);
      cfg.pointer = plane_addr;
      cfg.row_stride = row_stride;
      PLANE_SET_SLICE_STRIDE(cfg, slice_stride);
      PLANE_SET_EXTENT(cfg, u_minify(props->extent_px.width, mip_level),
                       u_minify(props->extent_px.height, mip_level));
   }
}

static void
emit_astc_plane(const struct pan_image_view *iview, int plane_idx,
                unsigned mip_level, unsigned layer_or_z_slice, void *payload)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      pan_image_view_get_plane(iview, plane_idx);
   const struct pan_image_props *props = &pref.image->props;
   bool srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
   uint64_t plane_addr, plane_size, slice_stride;
   uint32_t row_stride;

   /* sRGB formats decode to RGBA8 sRGB, which is narrow.
    *
    * Non-sRGB formats decode to RGBA16F which is wide except if decode
    * precision is set to GL_RGBA8 for that texture.
    */
   bool wide = !srgb && !iview->astc.narrow;

   assert(desc->layout == UTIL_FORMAT_LAYOUT_ASTC && desc->block.depth == 1);
   assert(props->modifier == DRM_FORMAT_MOD_LINEAR ||
          props->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);

   get_linear_or_u_tiled_plane_props(iview, plane_idx, mip_level,
                                     layer_or_z_slice, &plane_addr, &row_stride,
                                     &slice_stride, &plane_size);

#define ASTC_PLANE_SET_COMMON_PROPS()                                          \
   cfg.clump_ordering =                                                        \
      props->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED          \
         ? MALI_CLUMP_ORDERING_TILED_U_INTERLEAVED                             \
         : MALI_CLUMP_ORDERING_LINEAR;                                         \
   cfg.decode_hdr = iview->astc.hdr;                                           \
   cfg.decode_wide = wide;                                                     \
   PLANE_SET_SIZE(cfg, plane_size);                                            \
   cfg.pointer = plane_addr;                                                   \
   cfg.row_stride = row_stride;                                                \
   PLANE_SET_SLICE_STRIDE(cfg, slice_stride);                                  \
   PLANE_SET_EXTENT(cfg, u_minify(props->extent_px.width, mip_level),          \
                    u_minify(props->extent_px.height, mip_level))

   if (desc->block.depth > 1) {
      pan_cast_and_pack(payload, ASTC_3D_PLANE, cfg) {
         ASTC_PLANE_SET_COMMON_PROPS();
         cfg.block_width = pan_astc_dim_3d(desc->block.width);
         cfg.block_height = pan_astc_dim_3d(desc->block.height);
         cfg.block_depth = pan_astc_dim_3d(desc->block.depth);
      }
   } else {
      pan_cast_and_pack(payload, ASTC_2D_PLANE, cfg) {
         ASTC_PLANE_SET_COMMON_PROPS();
         cfg.block_width = pan_astc_dim_2d(desc->block.width);
         cfg.block_height = pan_astc_dim_2d(desc->block.height);
      }
   }

#undef ASTC_PLANE_SET_COMMON_PROPS
}

static void
emit_linear_or_u_tiled_plane(const struct pan_image_view *iview,
                             int plane_index, unsigned mip_level,
                             unsigned layer_or_z_slice, void *payload)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);

   if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC) {
      emit_astc_plane(iview, plane_index, mip_level, layer_or_z_slice, payload);
   } else {
      emit_generic_plane(iview, plane_index, mip_level, layer_or_z_slice,
                         payload);
   }
}

#define emit_linear_plane  emit_linear_or_u_tiled_plane
#define emit_u_tiled_plane emit_linear_or_u_tiled_plane

static void
emit_linear_or_u_tiled_chroma_2p_plane(const struct pan_image_view *iview,
                                       unsigned mip_level,
                                       unsigned layer_or_z_slice, void *payload)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref1 = pan_image_view_get_plane(iview, 1);
   const struct pan_image_props *props = &pref1.image->props;
   uint64_t cplane1_addr, cplane2_addr, cplane1_slice_stride, cplane1_size;
   ASSERTED uint64_t cplane2_slice_stride, cplane2_size;
   ASSERTED uint32_t cplane2_row_stride;
   uint32_t cplane1_row_stride;

   get_linear_or_u_tiled_plane_props(iview, 1, mip_level,
                                     layer_or_z_slice, &cplane1_addr, &cplane1_row_stride,
                                     &cplane1_slice_stride, &cplane1_size);
   get_linear_or_u_tiled_plane_props(iview, 2, mip_level,
                                     layer_or_z_slice, &cplane2_addr, &cplane2_row_stride,
                                     &cplane2_slice_stride, &cplane2_size);

   assert(cplane1_size == cplane2_size &&
          cplane1_row_stride == cplane2_row_stride &&
          cplane1_slice_stride == cplane2_slice_stride);

   assert(desc->layout == UTIL_FORMAT_LAYOUT_PLANAR3);
   assert(props->modifier == DRM_FORMAT_MOD_LINEAR ||
          props->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);

   pan_cast_and_pack(payload, CHROMA_2P_PLANE, cfg) {
      cfg.clump_ordering =
         props->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED
            ? MALI_CLUMP_ORDERING_TILED_U_INTERLEAVED
            : MALI_CLUMP_ORDERING_LINEAR;
      cfg.clump_format = pan_clump_format(iview->format);
      PLANE_SET_SIZE(cfg, cplane1_size);
      cfg.pointer = cplane1_addr;
      cfg.row_stride = cplane1_row_stride;
      PLANE_SET_EXTENT(cfg, u_minify(props->extent_px.width, mip_level),
                       u_minify(props->extent_px.height, mip_level));
      cfg.secondary_pointer = cplane2_addr;
   }
}

#define emit_linear_chroma_2p_plane  emit_linear_or_u_tiled_chroma_2p_plane
#define emit_u_tiled_chroma_2p_plane emit_linear_or_u_tiled_chroma_2p_plane

static void
get_afbc_plane_props(const struct pan_image_view *iview, int plane_idx,
                     unsigned mip_level, unsigned layer_or_z_slice,
                     uint64_t *header_pointer, uint32_t *header_row_stride,
                     uint32_t *header_slice_size, uint64_t *header_slice_stride,
                     uint64_t *size)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      util_format_has_stencil(desc)
         ? pan_image_view_get_s_plane(iview)
         : pan_image_view_get_plane(iview, plane_idx);

   assert(pref.image != NULL);

   const struct pan_image_plane *plane = pref.image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slayout =
      &plane->layout.slices[mip_level];

   *header_pointer = plane->base + slayout->offset_B;
   *header_row_stride = slayout->afbc.header.row_stride_B;
   *header_slice_size = slayout->afbc.header.surface_size_B;
   *header_slice_stride = 0;
   *size = slayout->size_B;

   if (iview->dim == MALI_TEXTURE_DIMENSION_3D) {
      assert(pref.image->props.dim == MALI_TEXTURE_DIMENSION_3D);
      assert(layer_or_z_slice == 0);

      *header_slice_stride = slayout->afbc.surface_stride_B;
   } else if (pref.image->props.dim == MALI_TEXTURE_DIMENSION_3D) {
      assert(iview->dim == MALI_TEXTURE_DIMENSION_2D);
      /* When viewing 3D image as 2D-array, each plane describes a single Z
       * slice. The header pointer is moved to the right slice, and the size is
       * set to a single slice. */
      *header_pointer += layer_or_z_slice * slayout->afbc.surface_stride_B;
      *header_slice_stride = slayout->afbc.surface_stride_B;
      *size = slayout->afbc.surface_stride_B;
   } else {
      *header_pointer += layer_or_z_slice * plane->layout.array_stride_B;
   }
}

static void
emit_afbc_plane(const struct pan_image_view *iview, int plane_idx,
                unsigned mip_level, unsigned layer_or_z_slice, void *payload)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      util_format_has_stencil(desc)
         ? pan_image_view_get_s_plane(iview)
         : pan_image_view_get_plane(iview, plane_idx);
   const struct pan_image *image = pref.image;
   const struct pan_image_props *props = &image->props;
   uint64_t header_slice_stride, plane_size, header_addr;
   uint32_t header_slice_size, header_row_stride;

   get_afbc_plane_props(iview, plane_idx, mip_level, layer_or_z_slice,
                        &header_addr, &header_row_stride, &header_slice_size,
                        &header_slice_stride, &plane_size);

   /* We can't do 3-planar formats with AFBC. */
   assert(desc->layout != UTIL_FORMAT_LAYOUT_PLANAR3);
   assert(drm_is_afbc(props->modifier));
   assert(props->nr_samples == 1);

   pan_cast_and_pack(payload, AFBC_PLANE, cfg) {
      cfg.superblock_size = translate_superblock_size(props->modifier);
      cfg.ytr = (props->modifier & AFBC_FORMAT_MOD_YTR);
      cfg.split_block = (props->modifier & AFBC_FORMAT_MOD_SPLIT);
      cfg.tiled_header = (props->modifier & AFBC_FORMAT_MOD_TILED);
      cfg.prefetch = true;
      cfg.compression_mode = pan_afbc_decompression_mode(
         iview->format, image->planes[pref.plane_idx]->layout.afbc.mode);
      PLANE_SET_SIZE(cfg, plane_size);
      cfg.pointer = header_addr;
      cfg.header_row_stride = header_row_stride;
      cfg.header_slice_size = header_slice_size;
#if PAN_ARCH <= 10
      cfg.header_slice_stride = header_slice_stride;
#else
      cfg.header_slice_stride = header_slice_stride & BITFIELD_MASK(32);
      cfg.header_slice_stride_hi = header_slice_stride >> 32;
#endif
      PLANE_SET_EXTENT(cfg, u_minify(props->extent_px.width, mip_level),
                       u_minify(props->extent_px.height, mip_level));
   }
}

static void
emit_afbc_chroma_2p_plane(const struct pan_image_view *iview,
                          unsigned mip_level, unsigned layer_or_z_slice,
                          void *payload)
{
   UNREACHABLE("AFBC chroma 2p plane not supported");
}

#if PAN_ARCH >= 10
static void
get_afrc_plane_props(const struct pan_image_view *iview, int plane_idx,
                     unsigned mip_level, unsigned layer_or_z_slice,
                     uint64_t *pointer, uint32_t *row_stride,
                     uint32_t *slice_stride, uint32_t *size)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      util_format_has_stencil(desc)
         ? pan_image_view_get_s_plane(iview)
         : pan_image_view_get_plane(iview, plane_idx);

   assert(pref.image != NULL);

   const struct pan_image_plane *plane = pref.image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slayout =
      &plane->layout.slices[mip_level];

   *pointer = plane->base + slayout->offset_B;
   *size = slayout->size_B;
   *row_stride = slayout->tiled_or_linear.row_stride_B;
   *slice_stride = slayout->tiled_or_linear.surface_stride_B;

   if (pref.image->props.dim == MALI_TEXTURE_DIMENSION_3D) {
      *pointer += layer_or_z_slice * slayout->tiled_or_linear.surface_stride_B;
      *size -= layer_or_z_slice * slayout->tiled_or_linear.surface_stride_B;
   } else {
      *pointer += layer_or_z_slice * plane->layout.array_stride_B;
   }
}

static void
emit_afrc_plane(const struct pan_image_view *iview, int plane_index,
                unsigned mip_level, unsigned layer_or_z_slice, void *payload)
{
   const struct pan_afrc_format_info finfo =
      pan_afrc_get_format_info(iview->format);
   const struct pan_image_plane_ref pref =
      pan_image_view_get_plane(iview, plane_index);
   const struct pan_image *image = pref.image;
   const struct pan_image_props *props = &image->props;
   uint32_t plane_size, plane_row_stride, plane_slice_stride;
   uint64_t plane_addr;

   get_afrc_plane_props(iview, plane_index, mip_level, layer_or_z_slice,
                        &plane_addr, &plane_row_stride, &plane_slice_stride,
                        &plane_size);

   assert(drm_is_afrc(props->modifier));

   pan_cast_and_pack(payload, AFRC_PLANE, cfg) {
      cfg.block_size = pan_afrc_block_size(props->modifier, plane_index);
      cfg.format = pan_afrc_format(finfo, props->modifier, plane_index);
      cfg.size = plane_size;
      cfg.pointer = plane_addr;
      cfg.row_stride = plane_row_stride;
      cfg.slice_stride = plane_slice_stride;
      PLANE_SET_EXTENT(cfg, u_minify(props->extent_px.width, mip_level),
                       u_minify(props->extent_px.height, mip_level));
   }
}

static void
emit_afrc_chroma_2p_plane(const struct pan_image_view *iview,
                          unsigned mip_level, unsigned layer_or_z_slice,
                          void *payload)
{
   const struct pan_afrc_format_info finfo =
      pan_afrc_get_format_info(iview->format);
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref1 =
      pan_image_view_get_plane(iview, 1);
   const struct pan_image_plane_ref pref2 =
      pan_image_view_get_plane(iview, 2);

   assert(pref1.image != NULL && pref2.image != NULL);

   const struct pan_image_props *props = &pref1.image->props;
   uint32_t cplane1_row_stride, cplane1_slice_stride, cplane1_size;
   ASSERTED uint32_t cplane2_row_stride, cplane2_slice_stride, cplane2_size;
   uint64_t cplane1_addr, cplane2_addr;

   get_afrc_plane_props(iview, 1, mip_level, layer_or_z_slice, &cplane1_addr,
                        &cplane1_row_stride, &cplane1_slice_stride,
                        &cplane1_size);
   get_afrc_plane_props(iview, 2, mip_level, layer_or_z_slice, &cplane2_addr,
                        &cplane2_row_stride, &cplane2_slice_stride,
                        &cplane2_size);

   assert(cplane1_size == cplane2_size &&
          cplane1_slice_stride == cplane2_slice_stride &&
          cplane1_row_stride == cplane2_row_stride);

   assert(desc->layout == UTIL_FORMAT_LAYOUT_PLANAR3);
   assert(props->modifier == DRM_FORMAT_MOD_LINEAR ||
          props->modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED);

   pan_cast_and_pack(payload, AFRC_CHROMA_2P_PLANE, cfg) {
      cfg.block_size = pan_afrc_block_size(props->modifier, 1);
      cfg.format = pan_afrc_format(finfo, props->modifier, 1);
      cfg.size = cplane1_size;
      cfg.pointer = cplane1_addr;
      cfg.row_stride = cplane1_row_stride;
      PLANE_SET_EXTENT(cfg, u_minify(props->extent_px.width, mip_level),
                       u_minify(props->extent_px.height, mip_level));
      cfg.secondary_pointer = cplane2_addr;
   }
}
#endif

#else
static void
get_linear_or_u_tiled_surface_props(const struct pan_image_view *iview,
                                    unsigned plane_idx, unsigned mip_level,
                                    unsigned layer_or_z_slice, unsigned sample,
                                    uint64_t *pointer, uint32_t *row_stride,
                                    uint32_t *surf_stride)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      util_format_has_stencil(desc)
         ? pan_image_view_get_s_plane(iview)
         : pan_image_view_get_plane(iview, plane_idx);

   assert(pref.image != NULL);

   const struct pan_image_plane *plane = pref.image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slayout =
      &plane->layout.slices[mip_level];
   uint64_t plane_addr = plane->base + slayout->offset_B;
   unsigned tag = 0;

#if PAN_ARCH >= 5
   if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC)
      tag = astc_compression_tag(desc);
#endif

   if (pref.image->props.dim == MALI_TEXTURE_DIMENSION_3D) {
      plane_addr +=
         layer_or_z_slice * slayout->tiled_or_linear.surface_stride_B;
   } else {
      plane_addr += (layer_or_z_slice * plane->layout.array_stride_B) +
                    (sample * slayout->tiled_or_linear.surface_stride_B);
   }

   *pointer = plane_addr | tag;
   *row_stride = slayout->tiled_or_linear.row_stride_B;
   *surf_stride = slayout->tiled_or_linear.surface_stride_B;
}

static void
get_afbc_surface_props(const struct pan_image_view *iview,
                       unsigned plane_idx, unsigned mip_level,
                       unsigned layer_or_z_slice, unsigned sample,
                       uint64_t *pointer, uint32_t *row_stride,
                       uint32_t *surf_stride)
{
   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref pref =
      util_format_has_stencil(desc)
         ? pan_image_view_get_s_plane(iview)
         : pan_image_view_get_plane(iview, plane_idx);

   assert(pref.image != NULL);

   const struct pan_image_plane *plane = pref.image->planes[pref.plane_idx];
   const struct pan_image_slice_layout *slayout =
      &plane->layout.slices[mip_level];
   uint64_t stride_B = pref.image->props.dim == MALI_TEXTURE_DIMENSION_3D
                          ? slayout->afbc.surface_stride_B
                          : plane->layout.array_stride_B;
   uint64_t plane_header_addr =
      plane->base + slayout->offset_B + (stride_B * layer_or_z_slice);
   unsigned tag = 0;

#if PAN_ARCH >= 5
   tag =
      afbc_compression_tag(pref.image->props.dim, pref.image->props.modifier);
#endif

   assert(sample == 0);

   /* On 2D views, surface stride is used to do a bound check, so we can't set
    * it to zero.
    */
   *surf_stride = slayout->afbc.surface_stride_B;
   *pointer = plane_header_addr | tag;
   *row_stride = slayout->afbc.header.row_stride_B;
}

static void
emit_linear_or_u_tiled_surface(const struct pan_image_view *iview,
                               unsigned mip_level, unsigned layer_or_z_slice,
                               unsigned sample, void *payload)
{
   uint32_t row_stride, surf_stride;
   uint64_t pointer;

   get_linear_or_u_tiled_surface_props(iview, 0, mip_level, layer_or_z_slice,
                                       sample, &pointer, &row_stride,
                                       &surf_stride);

   pan_cast_and_pack(payload, SURFACE_WITH_STRIDE, cfg) {
      cfg.pointer = pointer;
      cfg.row_stride = row_stride;
      cfg.surface_stride = surf_stride;
   }
}

#define emit_linear_surface  emit_linear_or_u_tiled_surface
#define emit_u_tiled_surface emit_linear_or_u_tiled_surface

static void
emit_afbc_surface(const struct pan_image_view *iview, unsigned mip_level,
                  unsigned layer_or_z_slice, unsigned sample, void *payload)
{
   uint32_t row_stride, surf_stride;
   uint64_t pointer;

   get_afbc_surface_props(iview, 0, mip_level, layer_or_z_slice, sample,
                          &pointer, &row_stride, &surf_stride);

   pan_cast_and_pack(payload, SURFACE_WITH_STRIDE, cfg) {
      cfg.pointer = pointer;
      cfg.row_stride = row_stride;
      cfg.surface_stride = surf_stride;
   }
}

#if PAN_ARCH >= 7
static void
emit_linear_or_u_tiled_multiplane_surface(const struct pan_image_view *iview,
                                          unsigned mip_level,
                                          unsigned layer_or_z_slice,
                                          unsigned sample, void *payload)
{
   uint64_t yplane_addr, cplane1_addr, cplane2_addr = 0;
   uint32_t yplane_row_stride, cplane1_row_stride, cplane2_row_stride = 0;
   uint32_t yplane_surf_stride, cplane1_surf_stride, cplane2_surf_stride = 0;
   unsigned nplanes = util_format_get_num_planes(iview->format);

   assert(nplanes == 2 || nplanes == 3);

   get_linear_or_u_tiled_surface_props(iview, 0, mip_level, layer_or_z_slice,
                                       sample, &yplane_addr, &yplane_row_stride,
                                       &yplane_surf_stride);
   get_linear_or_u_tiled_surface_props(iview, 1, mip_level, layer_or_z_slice,
                                       sample, &cplane1_addr, &cplane1_row_stride,
                                       &cplane1_surf_stride);
   if (nplanes == 3) {
      get_linear_or_u_tiled_surface_props(
         iview, 2, mip_level, layer_or_z_slice, sample, &cplane2_addr,
         &cplane2_row_stride, &cplane2_surf_stride);
      assert(cplane2_row_stride == cplane1_row_stride);
   }

   pan_cast_and_pack(payload, MULTIPLANAR_SURFACE, cfg) {
      cfg.plane_0_pointer = yplane_addr;
      cfg.plane_0_row_stride = yplane_row_stride;
      cfg.plane_1_2_row_stride = cplane1_row_stride;
      cfg.plane_1_pointer = cplane1_addr;
      cfg.plane_2_pointer = cplane2_addr;
   }
}

#define emit_linear_multiplane_surface emit_linear_or_u_tiled_multiplane_surface
#define emit_u_tiled_multiplane_surface                                        \
   emit_linear_or_u_tiled_multiplane_surface

static void
emit_afbc_multiplane_surface(const struct pan_image_view *iview,
                             unsigned mip_level, unsigned layer_or_z_slice,
                             unsigned sample, void *payload)
{
   uint64_t yplane_addr, cplane1_addr, cplane2_addr = 0;
   uint32_t yplane_row_stride, cplane1_row_stride, cplane2_row_stride = 0;
   uint32_t yplane_surf_stride, cplane1_surf_stride, cplane2_surf_stride = 0;
   unsigned nplanes = util_format_get_num_planes(iview->format);

   assert(nplanes == 2 || nplanes == 3);

   get_afbc_surface_props(iview, 0, mip_level, layer_or_z_slice, sample,
                          &yplane_addr, &yplane_row_stride,
                          &yplane_surf_stride);
   get_afbc_surface_props(iview, 1, mip_level, layer_or_z_slice, sample,
                          &cplane1_addr, &cplane1_row_stride,
                          &cplane1_surf_stride);
   if (nplanes == 3) {
      get_afbc_surface_props(iview, 2, mip_level, layer_or_z_slice, sample,
                             &cplane2_addr, &cplane2_row_stride,
                             &cplane2_surf_stride);
      assert(cplane2_row_stride == cplane1_row_stride);
   }

   pan_cast_and_pack(payload, MULTIPLANAR_SURFACE, cfg) {
      cfg.plane_0_pointer = yplane_addr;
      cfg.plane_0_row_stride = yplane_row_stride;
      cfg.plane_1_2_row_stride = cplane1_row_stride;
      cfg.plane_1_pointer = cplane1_addr;
      cfg.plane_2_pointer = cplane2_addr;
   }
}
#endif

#endif

#if PAN_ARCH >= 9
#define PAN_TEX_EMIT_HELPER(mod)                                               \
   void GENX(pan_tex_emit_##mod##_payload_entry)(                              \
      const struct pan_image_view *iview, unsigned mip_level,                  \
      unsigned layer_or_z_slice, unsigned sample, void **payload)              \
   {                                                                           \
      assert(sample == 0);                                                     \
      const unsigned nplanes = util_format_get_num_planes(iview->format);      \
                                                                               \
      emit_##mod##_plane(iview, 0, mip_level, layer_or_z_slice, *payload);     \
                                                                               \
      /* We use NULL_PLANE here, but we could use any other kind of            \
       * descriptor, since they are all the same size. */                      \
      *payload += pan_size(NULL_PLANE);                                        \
                                                                               \
      if (nplanes == 2) {                                                      \
         emit_##mod##_plane(iview, 1, mip_level, layer_or_z_slice, *payload);  \
         *payload += pan_size(NULL_PLANE);                                     \
      } else if (nplanes == 3) {                                               \
         emit_##mod##_chroma_2p_plane(iview, mip_level, layer_or_z_slice,      \
                                      *payload);                               \
         *payload += pan_size(NULL_PLANE);                                     \
      }                                                                        \
   }
#elif PAN_ARCH >= 7
#define PAN_TEX_EMIT_HELPER(mod)                                               \
   void GENX(pan_tex_emit_##mod##_payload_entry)(                              \
      const struct pan_image_view *iview, unsigned mip_level,                  \
      unsigned layer_or_z_slice, unsigned sample, void **payload)              \
   {                                                                           \
      if (util_format_get_num_planes(iview->format) == 1) {                    \
         emit_##mod##_surface(iview, mip_level, layer_or_z_slice, sample,      \
                              *payload);                                       \
         *payload += pan_size(SURFACE_WITH_STRIDE);                            \
      } else {                                                                 \
         emit_##mod##_multiplane_surface(iview, mip_level, layer_or_z_slice,   \
                                         sample, *payload);                    \
         *payload += pan_size(MULTIPLANAR_SURFACE);                            \
      }                                                                        \
   }
#else
#define PAN_TEX_EMIT_HELPER(mod)                                               \
   void GENX(pan_tex_emit_##mod##_payload_entry)(                              \
      const struct pan_image_view *iview, unsigned mip_level,                  \
      unsigned layer_or_z_slice, unsigned sample, void **payload)              \
   {                                                                           \
      assert(util_format_get_num_planes(iview->format) == 1);                  \
      emit_##mod##_surface(iview, mip_level, layer_or_z_slice, sample,         \
                           *payload);                                          \
      *payload += pan_size(SURFACE_WITH_STRIDE);                               \
   }
#endif

PAN_TEX_EMIT_HELPER(linear)
PAN_TEX_EMIT_HELPER(u_tiled)
PAN_TEX_EMIT_HELPER(afbc)
#if PAN_ARCH >= 10
PAN_TEX_EMIT_HELPER(afrc)
#endif

static void
pan_emit_iview_texture_payload(const struct pan_image_view *iview,
                               void *payload)
{
   const struct pan_image_plane_ref pref =
      pan_image_view_get_first_plane(iview);
   const struct pan_mod_handler *mod_handler = pref.image->mod_handler;
   unsigned nr_samples =
      PAN_ARCH < 9 ? pan_image_view_get_nr_samples(iview) : 1;

   /* Inject the addresses in, interleaving array indices, mip levels,
    * cube faces, and strides in that order. On Bifrost and older, each
    * sample had its own surface descriptor; on Valhall, they are fused
    * into a single plane descriptor.
    */

#if PAN_ARCH >= 7
   /* V7 and later treats faces as extra layers */
   for (int layer = iview->first_layer; layer <= iview->last_layer; ++layer) {
      for (int sample = 0; sample < nr_samples; ++sample) {
         for (int level = iview->first_level; level <= iview->last_level;
              ++level) {
            mod_handler->emit_tex_payload_entry(iview, level, layer, sample,
                                                &payload);
         }
      }
   }
#else
   unsigned first_layer = iview->first_layer, last_layer = iview->last_layer;
   unsigned face_count = 1;

   if (iview->dim == MALI_TEXTURE_DIMENSION_CUBE) {
      first_layer /= 6;
      last_layer /= 6;
      face_count = 6;
   }

   /* V6 and earlier has a different memory-layout */
   for (int layer = first_layer; layer <= last_layer; ++layer) {
      for (int level = iview->first_level; level <= iview->last_level;
           ++level) {
         /* order of face and sample doesn't matter; we can only have multiple
          * of one or the other (no support for multisampled cubemaps)
          */
         for (int face = 0; face < face_count; ++face) {
            for (int sample = 0; sample < nr_samples; ++sample) {
               mod_handler->emit_tex_payload_entry(
                  iview, level, (face_count * layer) + face, sample, &payload);
            }
         }
      }
   }
#endif
}

#if PAN_ARCH < 9
/* Map modifiers to mali_texture_layout for packing in a texture descriptor */

static enum mali_texture_layout
pan_modifier_to_layout(uint64_t modifier)
{
   if (drm_is_afbc(modifier))
      return MALI_TEXTURE_LAYOUT_AFBC;
   else if (modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED)
      return MALI_TEXTURE_LAYOUT_TILED;
   else if (modifier == DRM_FORMAT_MOD_LINEAR)
      return MALI_TEXTURE_LAYOUT_LINEAR;
   else
      UNREACHABLE("Invalid modifer");
}
#endif

#if PAN_ARCH >= 7
void
GENX(pan_texture_swizzle_replicate_x)(struct pan_image_view *iview)
{
   /* v7+ doesn't have an _RRRR component order, combine the
    * user swizzle with a .XXXX swizzle to emulate that. */
   assert(util_format_is_depth_or_stencil(iview->format));

   static const unsigned char replicate_x[4] = {
      PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_X,
   };

   util_format_compose_swizzles(replicate_x, iview->swizzle, iview->swizzle);
}
#endif

#if PAN_ARCH == 7
void
GENX(pan_texture_afbc_reswizzle)(struct pan_image_view *iview)
{
   /* v7 (only) restricts component orders when AFBC is in use.
    * Rather than restrict AFBC for all non-canonical component orders, we use
    * an allowed component order with an invertible swizzle composed.
    * This allows us to support AFBC(BGR) as well as AFBC(RGB).
    */
   assert(!util_format_is_depth_or_stencil(iview->format));
   assert(!pan_format_is_yuv(iview->format));
   assert(pan_afbc_supports_format(PAN_ARCH, iview->format));

   uint32_t mali_format = GENX(pan_format_from_pipe_format)(iview->format)->hw;

   enum mali_rgb_component_order orig = mali_format & BITFIELD_MASK(12);
   struct pan_decomposed_swizzle decomposed = GENX(pan_decompose_swizzle)(orig);

   /* Apply the new component order */
   if (orig != decomposed.pre)
      iview->format = util_format_rgb_to_bgr(iview->format);
   /* Only RGB<->BGR should be allowed for AFBC */
   assert(iview->format != PIPE_FORMAT_NONE);
   assert(decomposed.pre ==
          (GENX(pan_format_from_pipe_format)(iview->format)->hw &
           BITFIELD_MASK(12)));

   /* Compose the new swizzle */
   util_format_compose_swizzles(decomposed.post, iview->swizzle,
                                iview->swizzle);
}
#endif

static unsigned
pan_texture_get_array_size(const struct pan_image_view *iview)
{
   unsigned array_size = iview->last_layer - iview->first_layer + 1;

   /* If this is a cubemap, we expect the number of layers to be a multiple
    * of 6.
    */
   if (iview->dim == MALI_TEXTURE_DIMENSION_CUBE) {
      assert(array_size % 6 == 0);
      array_size /= 6;
   }

   /* Multiplanar YUV textures require 2 surface descriptors. */
   if (pan_format_is_yuv(iview->format) && PAN_ARCH >= 9 &&
       pan_image_view_get_plane(iview, 1).image != NULL)
      array_size *= 2;

   return array_size;
}

static struct pan_image_extent
pan_texture_get_extent(const struct pan_image_view *iview,
                       const struct pan_image_props *iprops)
{
   struct pan_image_extent extent_px = {
      .width = u_minify(iprops->extent_px.width, iview->first_level),
      .height = u_minify(iprops->extent_px.height, iview->first_level),
      .depth = u_minify(iprops->extent_px.depth, iview->first_level),
   };

   if (util_format_is_compressed(iprops->format) &&
       !util_format_is_compressed(iview->format)) {
      extent_px.width = DIV_ROUND_UP(
         extent_px.width, util_format_get_blockwidth(iprops->format));
      extent_px.height = DIV_ROUND_UP(
         extent_px.height, util_format_get_blockheight(iprops->format));
      extent_px.depth = DIV_ROUND_UP(
         extent_px.depth, util_format_get_blockdepth(iprops->format));
      assert(util_format_get_blockwidth(iview->format) == 1);
      assert(util_format_get_blockheight(iview->format) == 1);
      assert(util_format_get_blockheight(iview->format) == 1);
      assert(iview->last_level == iview->first_level);
   }

   return extent_px;
}

/*
 * Generates a texture descriptor. Ideally, descriptors are immutable after the
 * texture is created, so we can keep these hanging around in GPU memory in a
 * dedicated BO and not have to worry. In practice there are some minor gotchas
 * with this (the driver sometimes will change the format of a texture on the
 * fly for compression) but it's fast enough to just regenerate the descriptor
 * in those cases, rather than monkeypatching at drawtime. A texture descriptor
 * consists of a 32-byte header followed by pointers.
 */
void
GENX(pan_sampled_texture_emit)(const struct pan_image_view *iview,
                               struct mali_texture_packed *out,
                               const struct pan_ptr *payload)
{
   pan_image_view_check(iview);

   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref first_plane = pan_image_view_get_first_plane(iview);
   const struct pan_image_props *props = &first_plane.image->props;
   uint32_t mali_format = GENX(pan_format_from_pipe_format)(iview->format)->hw;

   if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC && iview->astc.narrow &&
       desc->colorspace != UTIL_FORMAT_COLORSPACE_SRGB) {
      mali_format = MALI_PACK_FMT(RGBA8_UNORM, RGBA, L);
   }

   pan_emit_iview_texture_payload(iview, payload->cpu);

   unsigned array_size = pan_texture_get_array_size(iview);

   struct pan_image_extent extent_px =
      pan_texture_get_extent(iview, props);

   pan_pack(out, TEXTURE, cfg) {
      cfg.dimension = iview->dim;
      cfg.format = mali_format;
      cfg.width = extent_px.width;
      cfg.height = extent_px.height;
      if (iview->dim == MALI_TEXTURE_DIMENSION_3D)
         cfg.depth = extent_px.depth;
      else
         cfg.sample_count = props->nr_samples;
      cfg.swizzle = pan_translate_swizzle_4(iview->swizzle);
#if PAN_ARCH >= 9
      cfg.texel_interleave = (props->modifier != DRM_FORMAT_MOD_LINEAR) ||
                             util_format_is_compressed(iview->format);
#else
      cfg.texel_ordering = pan_modifier_to_layout(props->modifier);
#endif
      cfg.levels = iview->last_level - iview->first_level + 1;
      cfg.array_size = array_size;

#if PAN_ARCH >= 6
      cfg.surfaces = payload->gpu;

      /* We specify API-level LOD clamps in the sampler descriptor
       * and use these clamps simply for bounds checking.
       */
      cfg.minimum_lod = 0;
      cfg.maximum_lod = cfg.levels - 1;
#endif
   }
}

#if PAN_ARCH >= 9
void
GENX(pan_storage_texture_emit)(const struct pan_image_view *iview,
                               struct mali_texture_packed *out,
                               const struct pan_ptr *payload)
{
   pan_image_view_check(iview);

   const struct util_format_description *desc =
      util_format_description(iview->format);
   const struct pan_image_plane_ref first_plane =
      pan_image_view_get_first_plane(iview);
   const struct pan_image_props *props = &first_plane.image->props;

   /* AFBC and AFRC cannot be used in storage operations. */
   assert(!drm_is_afbc(props->modifier));
   assert(!drm_is_afrc(props->modifier));

   uint32_t mali_format =
      GENX(pan_format_from_pipe_format)(iview->format)->hw;
   if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC && iview->astc.narrow &&
       desc->colorspace != UTIL_FORMAT_COLORSPACE_SRGB) {
      mali_format = MALI_PACK_FMT(RGBA8_UNORM, RGBA, L);
   }

   pan_emit_iview_texture_payload(iview, payload->cpu);

   unsigned array_size = pan_texture_get_array_size(iview);

   struct pan_image_extent extent_px =
      pan_texture_get_extent(iview, props);

   static const unsigned char rgba_swizzle[4] = {
      PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_Y,
      PIPE_SWIZZLE_Z,
      PIPE_SWIZZLE_W,
   };

   pan_pack(out, TEXTURE, cfg) {
      cfg.dimension = iview->dim;
      cfg.format = mali_format;
      cfg.width = extent_px.width;
      cfg.height = extent_px.height;
      if (iview->dim == MALI_TEXTURE_DIMENSION_3D)
         cfg.depth = extent_px.depth;
      else
         cfg.sample_count = props->nr_samples;
      cfg.texel_interleave = (props->modifier != DRM_FORMAT_MOD_LINEAR) ||
                             util_format_is_compressed(iview->format);
      cfg.levels = iview->last_level - iview->first_level + 1;
      cfg.array_size = array_size;

      cfg.surfaces = payload->gpu;

      /* Requirements for storage image use. */
      cfg.minimum_lod = 0;
      cfg.maximum_lod = 0;
      cfg.minimum_level = 0;
      cfg.swizzle = pan_translate_swizzle_4(rgba_swizzle);
   }
}
#endif

#if PAN_ARCH >= 9

void
GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                              struct mali_buffer_packed *out)
{
   unsigned stride = util_format_get_blocksize(bview->format);
   struct MALI_INTERNAL_CONVERSION conv = {
      .memory_format = GENX(pan_format_from_pipe_format)(bview->format)->hw,
      .raw = false,
   };

   pan_pack(out, BUFFER, cfg) {
      cfg.type = MALI_DESCRIPTOR_TYPE_BUFFER;
      cfg.buffer_type = MALI_BUFFER_TYPE_STRUCTURE;
      cfg.size = bview->width_el * stride;
      cfg.address = bview->base;
      cfg.stride = stride;
      cfg.conversion = conv;
   }
}

#else

void
GENX(pan_buffer_texture_emit)(const struct pan_buffer_view *bview,
                              struct mali_texture_packed *out,
                              const struct pan_ptr *payload)
{
   uint32_t mali_format = GENX(pan_format_from_pipe_format)(bview->format)->hw;
   static const unsigned char rgba_swizzle[4] = {
      PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_Y,
      PIPE_SWIZZLE_Z,
      PIPE_SWIZZLE_W,
   };

   pan_emit_bview_surface_with_stride(bview, payload->cpu);

   pan_pack(out, TEXTURE, cfg) {
      cfg.dimension = MALI_TEXTURE_DIMENSION_1D;
      cfg.format = mali_format;
      cfg.width = bview->width_el;
      cfg.height = 1;
      cfg.sample_count = 1;
      cfg.swizzle = pan_translate_swizzle_4(rgba_swizzle);
      cfg.texel_ordering = MALI_TEXTURE_LAYOUT_LINEAR;
      cfg.levels = 1;
      cfg.array_size = 1;

#if PAN_ARCH >= 6
      cfg.surfaces = payload->gpu;
      cfg.minimum_lod = 0;
      cfg.maximum_lod = 0;
#endif
   }
}

#endif
