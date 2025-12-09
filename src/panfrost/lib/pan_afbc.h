/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#ifndef __PAN_AFBC_H
#define __PAN_AFBC_H

#include "pan_format.h"
#include "pan_layout.h"

#include "drm-uapi/drm_fourcc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Arm FrameBuffer Compression (AFBC) is a lossless compression scheme natively
 * implemented in Mali GPUs (as well as many display controllers paired with
 * Mali GPUs, etc). Where possible, Panfrost prefers to use AFBC for both
 * rendering and texturing. In most cases, this is a performance-win due to a
 * dramatic reduction in memory bandwidth and cache locality compared to a
 * linear resources.
 *
 * AFBC divides the framebuffer into 16x16 tiles (other sizes possible, TODO:
 * do we need to support this?). So, the width and height each must be aligned
 * up to 16 pixels. This is inherently good for performance; note that for a 4
 * byte-per-pixel format like RGBA8888, that means that rows are 16*4=64 byte
 * aligned, which is the cache-line size.
 *
 * For each AFBC-compressed resource, there is a single contiguous
 * (CPU/GPU-shared) buffer. This buffer itself is divided into two parts:
 * header and body, placed immediately after each other.
 *
 * The AFBC header contains 16 bytes of metadata per tile.
 *
 * The AFBC body is the same size as the original linear resource (padded to
 * the nearest tile). Although the body comes immediately after the header, it
 * must also be cache-line aligned, so there can sometimes be a bit of padding
 * between the header and body.
 *
 * As an example, a 64x64 RGBA framebuffer contains 64/16 = 4 tiles horizontally
 * and 4 tiles vertically. There are 4*4=16 tiles in total, each containing 16
 * bytes of metadata, so there is a 16*16=256 byte header. 64x64 is already
 * tile aligned, so the body is 64*64 * 4 bytes per pixel = 16384 bytes of
 * body.
 *
 * From userspace, Panfrost needs to be able to calculate these sizes. It
 * explicitly does not and can not know the format of the data contained within
 * this header and body. The GPU has native support for AFBC encode/decode. For
 * an internal FBO or a framebuffer used for scanout with an AFBC-compatible
 * winsys/display-controller, the buffer is maintained AFBC throughout flight,
 * and the driver never needs to know the internal data. For edge cases where
 * the driver really does need to read/write from the AFBC resource, we
 * generate a linear staging buffer and use the GPU to blit AFBC<--->linear.
 */

#define AFBC_HEADER_BYTES_PER_TILE 16

/* AFBC format mode. The ordering is intended to match the Valhall hardware enum
 * ("AFBC Compression Mode"), but this enum is required in software on older
 * hardware for correct handling of texture views. Defining the enum lets us
 * unify these code paths.
 */
enum pan_afbc_mode {
   PAN_AFBC_MODE_R8,
   PAN_AFBC_MODE_R8G8,
   PAN_AFBC_MODE_R5G6B5,
   PAN_AFBC_MODE_R4G4B4A4,
   PAN_AFBC_MODE_R5G5B5A1,
   PAN_AFBC_MODE_R8G8B8,
   PAN_AFBC_MODE_R8G8B8A8,
   PAN_AFBC_MODE_R10G10B10A2,
   PAN_AFBC_MODE_R11G11B10,

   /* 16 bit modes, available on v10+ */
   PAN_AFBC_MODE_R16,
   PAN_AFBC_MODE_R16G16,
   PAN_AFBC_MODE_R16G16B16,
   PAN_AFBC_MODE_R16G16B16A16,

   /* YUV special modes */
   PAN_AFBC_MODE_YUV420_6C8,
   PAN_AFBC_MODE_YUV420_2C8,
   PAN_AFBC_MODE_YUV420_1C8,
   PAN_AFBC_MODE_YUV420_6C10,
   PAN_AFBC_MODE_YUV420_2C10,
   PAN_AFBC_MODE_YUV420_1C10,

   PAN_AFBC_MODE_YUV422_4C8,
   PAN_AFBC_MODE_YUV422_2C8,
   PAN_AFBC_MODE_YUV422_1C8,
   PAN_AFBC_MODE_YUV422_4C10,
   PAN_AFBC_MODE_YUV422_2C10,
   PAN_AFBC_MODE_YUV422_1C10,

   /* Sentintel signalling a format that cannot be compressed */
   PAN_AFBC_MODE_INVALID
};

/*
 * An AFBC header block provides access to an associated superblock payload of
 * 4x4 subblocks or to an embedded solid color.
 */
struct pan_afbc_headerblock {
   union {
      /* Superblock payload. */
      struct {
         /* Offset in bytes from the start of the AFBC buffer (1st header
          * block) to the start of the superblock payload data. */
         uint32_t offset;

         /* Sizes in bytes of the 4x4 6-bit subblocks. */
         uint8_t subblock_sizes[12];
      } payload;

      /* Solid color. */
      struct {
         uint64_t reserved;

         /* RGBA 8-8-8-8 color format. */
         /* XXX: Add other formats. */
         struct {
            uint8_t r, g, b, a;
            uint32_t reserved;
         } rgba8888;
      } color;

      /* Random access. */
      uint8_t u8[16];
      uint16_t u16[8];
      uint32_t u32[4];
      uint64_t u64[2];
   };
};

/*
 * An AFBC payload extent describes the extent of the payload data (compressed
 * superblock data) associated to a pan_afbc_headerblock.
 */
struct pan_afbc_payload_extent {
   uint32_t size;
   uint32_t offset;
};

/*
 * Given an AFBC modifier, return the superblock size.
 *
 * We do not yet have any use cases for multiplanar YCBCr formats with different
 * superblock sizes on the luma and chroma planes. These formats are unsupported
 * for now.
 */
static inline struct pan_image_block_size
pan_afbc_superblock_size(uint64_t modifier)
{
   unsigned index = (modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK);

   assert(drm_is_afbc(modifier));

   switch (index) {
   case AFBC_FORMAT_MOD_BLOCK_SIZE_16x16:
      return (struct pan_image_block_size){16, 16};
   case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8:
      return (struct pan_image_block_size){32, 8};
   case AFBC_FORMAT_MOD_BLOCK_SIZE_64x4:
      return (struct pan_image_block_size){64, 4};
   default:
      assert(!"Unsupported AFBC block size");
      return (struct pan_image_block_size){0, 0};
   }
}

/* Same as pan_afbc_superblock_size_el() but counted in block elements
 * instead of pixels. For anything non-YUV this is the same. */
static inline struct pan_image_block_size
pan_afbc_superblock_size_el(enum pipe_format format, uint64_t modifier)
{
   struct pan_image_block_size sb_size_px = pan_afbc_superblock_size(modifier);

   assert(sb_size_px.width % util_format_get_blockwidth(format) == 0);
   assert(sb_size_px.height % util_format_get_blockheight(format) == 0);

   return (struct pan_image_block_size){
      .width = sb_size_px.width / util_format_get_blockwidth(format),
      .height = sb_size_px.height / util_format_get_blockheight(format),
   };
}

/*
 * Given an AFBC modifier, return the render size.
 */
static inline struct pan_image_block_size
pan_afbc_renderblock_size(uint64_t modifier)
{
   struct pan_image_block_size blk_size = pan_afbc_superblock_size(modifier);

   /* The GPU needs to render 16x16 tiles. For wide tiles, that means we
    * have to extend the render region to have a height of 16 pixels.
    */
   blk_size.height = ALIGN_POT(blk_size.height, 16);
   return blk_size;
}


/* Same as pan_afbc_renderblock_size() but counted in block elements
 * instead of pixels. For anything non-YUV this is the same. */
static inline struct pan_image_block_size
pan_afbc_renderblock_size_el(enum pipe_format format, uint64_t modifier)
{
   struct pan_image_block_size rb_size_px = pan_afbc_renderblock_size(modifier);

   assert(rb_size_px.width % util_format_get_blockwidth(format) == 0);
   assert(rb_size_px.height % util_format_get_blockheight(format) == 0);

   return (struct pan_image_block_size){
      .width = rb_size_px.width / util_format_get_blockwidth(format),
      .height = rb_size_px.height / util_format_get_blockheight(format),
   };
}

/*
 * Given an AFBC modifier, return the width of the superblock.
 */
static inline unsigned
pan_afbc_superblock_width(uint64_t modifier)
{
   return pan_afbc_superblock_size(modifier).width;
}

/*
 * Given an AFBC modifier, return the height of the superblock.
 */
static inline unsigned
pan_afbc_superblock_height(uint64_t modifier)
{
   return pan_afbc_superblock_size(modifier).height;
}

/*
 * Given an AFBC modifier, return if "wide blocks" are used. Wide blocks are
 * defined as superblocks wider than 16 pixels, the minimum (and default) super
 * block width.
 */
static inline bool
pan_afbc_is_wide(uint64_t modifier)
{
   return pan_afbc_superblock_width(modifier) > 16;
}

/*
 * Given an AFBC modifier, return the subblock size (subdivision of a
 * superblock). This is always 4x4 for now as we only support one AFBC
 * superblock layout.
 */
static inline struct pan_image_block_size
pan_afbc_subblock_size(uint64_t modifier)
{
   return (struct pan_image_block_size){4, 4};
}

/*
 * Given an AFBC header block, return the size of the subblock at the given
 * index in the range [0, 15].
 */
static inline unsigned
pan_afbc_header_subblock_size(struct pan_afbc_headerblock header,
                              uint32_t index)
{
   uint64_t mask = BITFIELD_MASK(6);

   switch (index) {
   case  0: return  (header.u64[0] >> 32)  & mask; break;
   case  1: return  (header.u64[0] >> 38)  & mask; break;
   case  2: return  (header.u64[0] >> 44)  & mask; break;
   case  3: return  (header.u64[0] >> 50)  & mask; break;
   case  4: return  (header.u64[0] >> 56)  & mask; break;
   case  5: return ((header.u64[0] >> 62) |
                    (header.u64[1] <<  2)) & mask; break;
   case  6: return  (header.u64[1] >>  4)  & mask; break;
   case  7: return  (header.u64[1] >> 10)  & mask; break;
   case  8: return  (header.u64[1] >> 16)  & mask; break;
   case  9: return  (header.u64[1] >> 22)  & mask; break;
   case 10: return  (header.u64[1] >> 28)  & mask; break;
   case 11: return  (header.u64[1] >> 34)  & mask; break;
   case 12: return  (header.u64[1] >> 40)  & mask; break;
   case 13: return  (header.u64[1] >> 46)  & mask; break;
   case 14: return  (header.u64[1] >> 52)  & mask; break;
   case 15: return  (header.u64[1] >> 58)  & mask; break;
   default: UNREACHABLE("invalid index"); return 0;
   }
}

/*
 * Given an AFBC header block, return the size in bytes of the associated
 * superblock payload data (for the superblock layouts 0, 3, 4 and 7).
 */
static inline uint32_t
pan_afbc_payload_size(unsigned arch,
                      struct pan_afbc_headerblock header,
                      uint32_t uncompressed_size)
{
   /* Skip sum if the 1st subblock is 0 (solid color encoding). */
   if (arch >= 7 && pan_afbc_header_subblock_size(header, 0) == 0)
      return 0;

   uint64_t size = 0;

   for (unsigned i = 0; i < 16; i++) {
      unsigned sub_size = pan_afbc_header_subblock_size(header, i);
      size += sub_size != 1 ? sub_size : uncompressed_size;
   }

   return ALIGN_POT(size, 16);
}

/*
 * Given a format and a modifier, return the size in bytes of an uncompressed
 * superblock payload.
 */
static inline uint32_t
pan_afbc_payload_uncompressed_size(enum pipe_format format, uint64_t modifier)
{
   struct pan_image_block_size size_px = pan_afbc_subblock_size(modifier);
   uint32_t size_B = util_format_get_blocksizebits(format) / 8;
   size_B *= size_px.width * size_px.height;

   assert(size_B == ALIGN_POT(size_B, 16));

   return size_B;
}

/*
 * Calculate the size of each AFBC superblock payload data from the given
 * header blocks, generate a packed AFBC payload layout and return the body
 * size.
 */
uint32_t
pan_afbc_payload_layout_packed(unsigned arch,
                               const struct pan_afbc_headerblock *headers,
                               struct pan_afbc_payload_extent *layout,
                               uint32_t nr_blocks, enum pipe_format format,
                               uint64_t modifier);

static inline uint32_t
pan_afbc_header_row_stride_align(unsigned arch, enum pipe_format format,
                                 uint64_t modifier)
{
   if (arch <= 7 || !(modifier & AFBC_FORMAT_MOD_TILED))
      return 16;

   if (util_format_get_blocksizebits(format) <= 32)
      return 1024;
   else
      return 256;
}

static inline uint32_t
pan_afbc_header_align(unsigned arch, uint64_t modifier)
{
   if (modifier & AFBC_FORMAT_MOD_TILED)
      return 4096;
   else if (arch >= 6)
      return 128;
   else
      return 64;
}

/*
 * Determine the required alignment for the body offset of an AFBC image. For
 * now, this depends only on whether tiling is in use. These minimum alignments
 * are required on all current GPUs.
 */
static inline uint32_t
pan_afbc_body_align(unsigned arch, uint64_t modifier)
{
   /* Body and header alignments are actually the same. */
   return pan_afbc_header_align(arch, modifier);
}

/* Get the body offset for a given AFBC header size. */
static inline uint32_t
pan_afbc_body_offset(unsigned arch, uint64_t modifier, uint32_t header_size)
{
   return ALIGN_POT(header_size, pan_afbc_body_align(arch, modifier));
}

/*
 * Determine the tile size used by AFBC. This tiles superblocks themselves.
 * Current GPUs support either 8x8 tiling or no tiling (1x1) for most
 * formats; but note that formats with more than 32bpp use 4x4 tiling
 */
static inline unsigned
pan_afbc_tile_size(enum pipe_format format, uint64_t modifier)
{
   if ( !(modifier & AFBC_FORMAT_MOD_TILED) ) {
      return 1;
   }
   return util_format_get_blocksizebits(format) <= 32 ? 8 : 4;
}

/*
 * Determine the number of bytes between header rows for an AFBC image. For an
 * image with linear headers, this is simply the number of header blocks
 * (=superblocks) per row times the numbers of bytes per header block. For an
 * image with tiled headers, this is multipled by the number of rows of
 * header blocks are in a tile together.
 */
static inline uint32_t
pan_afbc_row_stride(enum pipe_format format, uint64_t modifier, uint32_t width)
{
   unsigned block_width = pan_afbc_superblock_width(modifier);

   return (width / block_width) * pan_afbc_tile_size(format, modifier) *
          AFBC_HEADER_BYTES_PER_TILE;
}

/*
 * Determine the number of header blocks between header rows. This is equal to
 * the number of bytes between header rows divided by the bytes per blocks of a
 * header tile. This is also divided by the tile size to give a "line stride" in
 * blocks, rather than a real row stride. This is required by Bifrost.
 */
static inline uint32_t
pan_afbc_stride_blocks(enum pipe_format format, uint64_t modifier, uint32_t row_stride_bytes)
{
   return row_stride_bytes /
      (AFBC_HEADER_BYTES_PER_TILE * pan_afbc_tile_size(format, modifier));
}

/* Returns a height in superblocks taking into account the tile alignment
 * requirement coming from the modifier.
 */
static inline uint32_t
pan_afbc_height_blocks(enum pipe_format format, uint64_t modifier, uint32_t height_px)
{
   return ALIGN_POT(
      DIV_ROUND_UP(height_px, pan_afbc_superblock_height(modifier)),
      pan_afbc_tile_size(format, modifier));
}

static inline enum pipe_format
pan_afbc_unswizzled_format(unsigned arch, enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_A8_UNORM:
   case PIPE_FORMAT_L8_UNORM:
   case PIPE_FORMAT_I8_UNORM:
      return PIPE_FORMAT_R8_UNORM;

   case PIPE_FORMAT_L8A8_UNORM:
      return PIPE_FORMAT_R8G8_UNORM;

   case PIPE_FORMAT_B8G8R8_UNORM:
      return PIPE_FORMAT_R8G8B8_UNORM;

   case PIPE_FORMAT_R8G8B8X8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      return PIPE_FORMAT_R8G8B8A8_UNORM;
   case PIPE_FORMAT_A8R8G8B8_UNORM:
   case PIPE_FORMAT_X8R8G8B8_UNORM:
   case PIPE_FORMAT_X8B8G8R8_UNORM:
   case PIPE_FORMAT_A8B8G8R8_UNORM:
      /* v7 can only support AFBC for RGB and BGR */
      return arch == 7 ? format : PIPE_FORMAT_R8G8B8A8_UNORM;

   case PIPE_FORMAT_B5G6R5_UNORM:
      return PIPE_FORMAT_R5G6B5_UNORM;

   case PIPE_FORMAT_B5G5R5A1_UNORM:
      return PIPE_FORMAT_R5G5B5A1_UNORM;

   case PIPE_FORMAT_R10G10B10X2_UNORM:
   case PIPE_FORMAT_B10G10R10A2_UNORM:
   case PIPE_FORMAT_B10G10R10X2_UNORM:
      return PIPE_FORMAT_R10G10B10A2_UNORM;

   case PIPE_FORMAT_B4G4R4A4_UNORM:
      return PIPE_FORMAT_R4G4B4A4_UNORM;
   case PIPE_FORMAT_A4B4G4R4_UNORM:
      /* v7 can only support AFBC for RGB and BGR */
      return arch == 7 ? format : PIPE_FORMAT_R4G4B4A4_UNORM;

   default:
      return format;
   }
}

/* AFBC supports compressing a few canonical formats. Additional formats are
 * available by using a canonical internal format. Given a PIPE format, find
 * the canonical AFBC internal format if it exists, or NONE if the format
 * cannot be compressed. */

static inline enum pan_afbc_mode
pan_afbc_format(unsigned arch, enum pipe_format format, unsigned plane_idx)
{
   assert(plane_idx < util_format_get_num_planes(format));

   switch (format) {
   case PIPE_FORMAT_R8_G8B8_420_UNORM:
   case PIPE_FORMAT_R8_B8G8_420_UNORM:
      return plane_idx == 0 ? PAN_AFBC_MODE_YUV420_1C8
                            : PAN_AFBC_MODE_YUV420_2C8;
   case PIPE_FORMAT_R8_G8_B8_420_UNORM:
   case PIPE_FORMAT_R8_B8_G8_420_UNORM:
      return PAN_AFBC_MODE_YUV420_1C8;
   case PIPE_FORMAT_R8_G8B8_422_UNORM:
      return plane_idx == 0 ? PAN_AFBC_MODE_YUV422_1C8
                            : PAN_AFBC_MODE_YUV422_2C8;
   case PIPE_FORMAT_R10_G10B10_420_UNORM:
      return plane_idx == 0 ? PAN_AFBC_MODE_YUV420_1C10
                            : PAN_AFBC_MODE_YUV420_2C10;
   case PIPE_FORMAT_R10_G10B10_422_UNORM:
      return plane_idx == 0 ? PAN_AFBC_MODE_YUV422_1C10
                            : PAN_AFBC_MODE_YUV422_2C10;
   case PIPE_FORMAT_R8G8B8_420_UNORM_PACKED:
      return PAN_AFBC_MODE_YUV420_6C8;
   case PIPE_FORMAT_R10G10B10_420_UNORM_PACKED:
      return PAN_AFBC_MODE_YUV420_6C10;
   case PIPE_FORMAT_R8G8_R8B8_UNORM:
   case PIPE_FORMAT_R8B8_R8G8_UNORM:
      return PAN_AFBC_MODE_YUV422_4C8;
   case PIPE_FORMAT_X6R10X6G10_X6R10X6B10_422_UNORM:
      return PAN_AFBC_MODE_YUV422_4C10;
   default:
      break;
   }

   /* sRGB does not change the pixel format itself, only the
    * interpretation. The interpretation is handled by conversion hardware
    * independent to the compression hardware, so we can compress sRGB
    * formats by using the corresponding linear format.
    */
   format = util_format_linear(format);

   /* AFBC is type agnostic on v9+ (it just doesn't support float types except
    * for D32 which is mapped to RGBA8 internally), but most of the following
    * tests are done on UNORM for simplicity, so make sure we convert to _UNORM
    * first in that case.
    */
   if (arch >= 9 && !util_format_is_float(format))
      format = util_format_any_to_unorm(format);

   /* Luminance-alpha not supported for AFBC on v7+ */
   /* 16 bit modes only available for AFBC on v10 and later */
   switch (format) {
   case PIPE_FORMAT_A8_UNORM:
   case PIPE_FORMAT_L8_UNORM:
   case PIPE_FORMAT_I8_UNORM:
   case PIPE_FORMAT_L8A8_UNORM:
      if (arch >= 7)
         return PAN_AFBC_MODE_INVALID;
      else
         break;
   case PIPE_FORMAT_R16_UNORM:
   case PIPE_FORMAT_R16G16_UNORM:
   case PIPE_FORMAT_R16G16B16_UNORM:
   case PIPE_FORMAT_R16G16B16A16_UNORM:
      if (arch < 10)
         return PAN_AFBC_MODE_INVALID;
      else
         break;
   default:
      break;
   }

   /* We handle swizzling orthogonally to AFBC */
   format = pan_afbc_unswizzled_format(arch, format);

   /* clang-format off */
   switch (format) {
   case PIPE_FORMAT_R8_UNORM:          return PAN_AFBC_MODE_R8;
   case PIPE_FORMAT_R8G8_UNORM:        return PAN_AFBC_MODE_R8G8;
   case PIPE_FORMAT_R8G8B8_UNORM:      return PAN_AFBC_MODE_R8G8B8;
   case PIPE_FORMAT_R8G8B8A8_UNORM:    return PAN_AFBC_MODE_R8G8B8A8;
   case PIPE_FORMAT_R5G6B5_UNORM:      return PAN_AFBC_MODE_R5G6B5;
   case PIPE_FORMAT_R5G5B5A1_UNORM:    return PAN_AFBC_MODE_R5G5B5A1;
   case PIPE_FORMAT_R10G10B10A2_UNORM: return PAN_AFBC_MODE_R10G10B10A2;
   case PIPE_FORMAT_R4G4B4A4_UNORM:    return PAN_AFBC_MODE_R4G4B4A4;
   case PIPE_FORMAT_Z16_UNORM:         return PAN_AFBC_MODE_R8G8;

   case PIPE_FORMAT_Z32_FLOAT:         return PAN_AFBC_MODE_R8G8B8A8;
   case PIPE_FORMAT_Z24_UNORM_S8_UINT: return PAN_AFBC_MODE_R8G8B8A8;
   case PIPE_FORMAT_Z24X8_UNORM:       return PAN_AFBC_MODE_R8G8B8A8;
   case PIPE_FORMAT_X24S8_UINT:        return PAN_AFBC_MODE_R8G8B8A8;
   case PIPE_FORMAT_Z24_UNORM_PACKED:  return PAN_AFBC_MODE_R8G8B8;

   /* AFBC(S8) only supported on v9+ */
   case PIPE_FORMAT_S8_UINT:
      return arch >= 9 ? PAN_AFBC_MODE_R8 : PAN_AFBC_MODE_INVALID;

   case PIPE_FORMAT_R16_UNORM:         return PAN_AFBC_MODE_R16;
   case PIPE_FORMAT_R16G16_UNORM:      return PAN_AFBC_MODE_R16G16;
   case PIPE_FORMAT_R16G16B16_UNORM:   return PAN_AFBC_MODE_R16G16B16;
   case PIPE_FORMAT_R16G16B16A16_UNORM:
                                       return PAN_AFBC_MODE_R16G16B16A16;

   default:                            return PAN_AFBC_MODE_INVALID;
   }
   /* clang-format on */
}

/* A format may be compressed as AFBC if it has an AFBC internal format */

static inline bool
pan_afbc_supports_format(unsigned arch, enum pipe_format format)
{
   unsigned plane_count = util_format_get_num_planes(format);

   for (unsigned i = 0; i < plane_count; i++) {
      if (pan_afbc_format(arch, format, i) == PAN_AFBC_MODE_INVALID)
         return false;
   }

   return true;
}

/* The lossless colour transform (AFBC_FORMAT_MOD_YTR) requires RGB. */

static inline bool
pan_afbc_can_ytr(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   /* YTR is only defined for RGB(A) */
   if (desc->nr_channels != 3 && desc->nr_channels != 4)
      return false;

   /* The fourth channel if it exists doesn't matter */
   return desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB;
}

static inline bool
pan_afbc_can_split(unsigned arch, enum pan_afbc_mode mode, uint64_t modifier)
{
   unsigned block_width = pan_afbc_superblock_width(modifier);

   if (arch < 6)
      return false;

   if (block_width == 16) {
      return true;
   } else if (block_width == 32) {
      return (mode == PAN_AFBC_MODE_R8G8B8A8 ||
              mode == PAN_AFBC_MODE_R10G10B10A2);
   }

   return false;
}

/* Only support packing for RGB formats for now. */

static inline bool
pan_afbc_can_pack(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   return desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB;
}

/*
 * Check if a gen supports AFBC with tiled headers (and hence also solid
 * colour blocks).
 */
static inline bool
pan_afbc_can_tile(unsigned arch)
{
   return arch >= 7;
}

#if PAN_ARCH >= 9
static inline enum mali_afbc_compression_mode
pan_afbc_compression_mode(enum pan_afbc_mode mode)
{
   /* Map canonical formats to the hardware enum. This only
    * needs to handle the subset of formats returned by
    * pan_afbc_format.
    */
   switch (mode) {
   case PAN_AFBC_MODE_R8:
      return MALI_AFBC_COMPRESSION_MODE_R8;
   case PAN_AFBC_MODE_R8G8:
      return MALI_AFBC_COMPRESSION_MODE_R8G8;
   case PAN_AFBC_MODE_R5G6B5:
      return MALI_AFBC_COMPRESSION_MODE_R5G6B5;
   case PAN_AFBC_MODE_R4G4B4A4:
      return MALI_AFBC_COMPRESSION_MODE_R4G4B4A4;
   case PAN_AFBC_MODE_R5G5B5A1:
      return MALI_AFBC_COMPRESSION_MODE_R5G5B5A1;
   case PAN_AFBC_MODE_R8G8B8:
      return MALI_AFBC_COMPRESSION_MODE_R8G8B8;
   case PAN_AFBC_MODE_R8G8B8A8:
      return MALI_AFBC_COMPRESSION_MODE_R8G8B8A8;
   case PAN_AFBC_MODE_R10G10B10A2:
      return MALI_AFBC_COMPRESSION_MODE_R10G10B10A2;
   case PAN_AFBC_MODE_R11G11B10:
      return MALI_AFBC_COMPRESSION_MODE_R11G11B10;
#if PAN_ARCH >= 10
   case PAN_AFBC_MODE_R16:
      return MALI_AFBC_COMPRESSION_MODE_R16;
   case PAN_AFBC_MODE_R16G16:
      return MALI_AFBC_COMPRESSION_MODE_R16G16;
   case PAN_AFBC_MODE_R16G16B16:
      return MALI_AFBC_COMPRESSION_MODE_R16G16B16;
   case PAN_AFBC_MODE_R16G16B16A16:
      return MALI_AFBC_COMPRESSION_MODE_R16G16B16A16;
#endif
   case PAN_AFBC_MODE_YUV420_6C8:
      return MALI_AFBC_COMPRESSION_MODE_YUV420_6C8;
   case PAN_AFBC_MODE_YUV420_2C8:
      return MALI_AFBC_COMPRESSION_MODE_YUV420_2C8;
   case PAN_AFBC_MODE_YUV420_1C8:
      return MALI_AFBC_COMPRESSION_MODE_YUV420_1C8;
   case PAN_AFBC_MODE_YUV420_6C10:
      return MALI_AFBC_COMPRESSION_MODE_YUV420_6C10;
   case PAN_AFBC_MODE_YUV420_2C10:
      return MALI_AFBC_COMPRESSION_MODE_YUV420_2C10;
   case PAN_AFBC_MODE_YUV420_1C10:
      return MALI_AFBC_COMPRESSION_MODE_YUV420_1C10;
   case PAN_AFBC_MODE_YUV422_4C8:
      return MALI_AFBC_COMPRESSION_MODE_YUV422_4C8;
   case PAN_AFBC_MODE_YUV422_2C8:
      return MALI_AFBC_COMPRESSION_MODE_YUV422_2C8;
   case PAN_AFBC_MODE_YUV422_1C8:
      return MALI_AFBC_COMPRESSION_MODE_YUV422_1C8;
   case PAN_AFBC_MODE_YUV422_4C10:
      return MALI_AFBC_COMPRESSION_MODE_YUV422_4C10;
   case PAN_AFBC_MODE_YUV422_2C10:
      return MALI_AFBC_COMPRESSION_MODE_YUV422_2C10;
   case PAN_AFBC_MODE_YUV422_1C10:
      return MALI_AFBC_COMPRESSION_MODE_YUV422_1C10;
#if PAN_ARCH == 9
   case PAN_AFBC_MODE_R16:
   case PAN_AFBC_MODE_R16G16:
   case PAN_AFBC_MODE_R16G16B16:
   case PAN_AFBC_MODE_R16G16B16A16:
#endif
   case PAN_AFBC_MODE_INVALID:
      UNREACHABLE("Invalid AFBC format");
   }

   UNREACHABLE("all AFBC formats handled");
}

static inline enum mali_afbc_compression_mode
pan_afbc_decompression_mode(enum pipe_format view_format,
                            enum pan_afbc_mode img_mode)
{
   /* There are special cases for texture views of stencil images. */
   if (img_mode == PAN_AFBC_MODE_R8G8B8A8 &&
       view_format == PIPE_FORMAT_X24S8_UINT)
      return MALI_AFBC_COMPRESSION_MODE_X24S8;
   else if (view_format == PIPE_FORMAT_S8_UINT)
      return MALI_AFBC_COMPRESSION_MODE_S8;

   return pan_afbc_compression_mode(img_mode);
}
#endif

static inline bool
format_requires_afbc(enum pipe_format format) {
   switch (format) {
   case PIPE_FORMAT_R8G8B8_420_UNORM_PACKED:
   case PIPE_FORMAT_R10G10B10_420_UNORM_PACKED:
   case PIPE_FORMAT_X6R10X6G10_X6R10X6B10_422_UNORM:
      return true;
   default:
      return false;
   }
}

#ifdef __cplusplus
} /* extern C */
#endif

#endif
