/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_IMAGE_H
#define PANVK_IMAGE_H

#include "util/format/u_format.h"

#include "vk_format.h"
#include "vk_image.h"

#include "pan_image.h"

#define PANVK_MAX_PLANES 3

struct panvk_device_memory;
struct panvk_physical_device;

/* Right now, planar YUV images are treated as N different images, hence the 1:1
 * association between pan_image and pan_image_plane, but this can be optimized
 * once planar YUV support is hooked up. */
struct panvk_image_plane {
   struct pan_image image;
   struct pan_image_plane plane;

   struct panvk_device_memory *mem;

   /* Plane offset inside the memory object. */
   uint64_t mem_offset;
};

struct panvk_image {
   struct vk_image vk;

   struct {
      VkDeviceAddress device_address;
   } sparse;

   uint8_t plane_count;
   struct panvk_image_plane planes[PANVK_MAX_PLANES];

   /* One image each for 2x 4x 8x 16x. We don't support more than 16x. */
   VkImage ms_imgs[4];

   /* If an image can track validity of CRC even when external access is
    * possible. */
   bool crc_safe_external;

   /* BCn on HW without BC texturing: planes[0] keeps the raw blocks (linear),
    * bc_decoded holds GPU-decoded texels used by sampled views, placed
    * bc_decoded_offset bytes after the image base.
    *
    * bc_gfx: bc_decoded mirrors planes[0] of the internal colour image
    * bc_shadow (same memory), written by a fragment decode pass, so it gets
    * the regular modifier choice (AFBC, U-interleaved) and compact storage
    * formats. Otherwise bc_decoded is LINEAR and written by compute. */
   bool bc_emul;
   bool bc_gfx;
   VkFormat bc_format;
   struct panvk_image_plane bc_decoded;
   uint64_t bc_decoded_offset;
   VkImage bc_shadow;

   /* BC lowmem (csf-v11/197), on bc_gfx images only:
    * 1: BC1-BC5, bc_shadow is an ETC2/EAC image (U-interleaved) written by
    *    the panvk_bc_etc2 transcoder;
    * 2: BC6H/BC7, the exact bc_shadow, only set together with bc_no_raw
    *    or bc_raw_store (csf-v11/201);
    * 3: BC6H UF16, bc_shadow is an ASTC 4x4 HDR image (U-interleaved)
    *    written by the panvk_bc_astc transcoder (csf-v11/202).
    * bc_no_raw: the image can never be read back (no TRANSFER_SRC or
    * HOST_TRANSFER usage), so planes[0] (the raw blocks) gets no memory and
    * is never written: uploads and copies go straight to the shadow. */
   uint8_t bc_lowmem;
   bool bc_no_raw;

   /* BC raw store (csf-v11/201): a lowmem image that can be read back
    * (TRANSFER_SRC) keeps its raw blocks outside the image memory, in a
    * driver BO that only exists while command buffers use it and is
    * written to a spill file otherwise. planes[0].plane.base points at
    * the BO while it exists (0 otherwise): every raw access must go
    * through panvk_bc_raw_acquire() first. NULL for other images. */
   struct panvk_bc_raw *bc_raw;
   /* Set by panvk_image_init() when the image gets a raw store (bc_raw is
    * created by vkCreateImage). The raw blocks take no image memory. */
   bool bc_raw_store;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_image, vk.base, VkImage,
                               VK_OBJECT_TYPE_IMAGE)

/* The shadow is written by a transcoder (ETC2/EAC or ASTC). */
static inline bool
panvk_bc_xcoded(const struct panvk_image *image)
{
   return image->bc_lowmem == 1 || image->bc_lowmem == 3;
}

struct util_dynarray;

/* Make the raw blocks of a bc_raw image resident for a command buffer that
 * is being recorded: (re)creates the BO if needed (zeroed, or read back from
 * the spill file), sets planes[0].plane.base and adds the store to 'refs'
 * (an array of struct panvk_bc_raw *, released when the command buffer can
 * no longer be pending). 'write': the command buffer writes raw blocks;
 * 'full': it overwrites all of them (nothing to read back). */
VkResult panvk_bc_raw_acquire(struct panvk_image *image,
                              struct util_dynarray *refs, bool write,
                              bool full);

/* Drop the references of a command buffer that is reset or freed. Stores
 * no command buffer uses any more are spilled by a worker thread. */
void panvk_bc_raw_release(struct util_dynarray *refs);

/* Device teardown: wait for pending spills, close the spill file. */
struct panvk_device;
void panvk_bc_raw_device_finish(struct panvk_device *dev);

/* csf-v11/206: expand the EAC endpoint pair table of kind (0 alpha, 1 R11,
 * 2 signed R11) into out[2 * 65536] (bc/panvk_bc_eac_lut.h). */
void panvk_bc_eac_expand_table(int kind, uint32_t *out);


static inline void
panvk_image_set_compression_props(VkImageCompressionPropertiesEXT *props,
                                  bool compressed)
{
   /* AFBC is our only compression and it is lossless. Tiled modifiers are not
    * compression.
    */
   props->imageCompressionFlags = compressed
                                     ? VK_IMAGE_COMPRESSION_DEFAULT_EXT
                                     : VK_IMAGE_COMPRESSION_DISABLED_EXT;
   props->imageCompressionFixedRateFlags =
      VK_IMAGE_COMPRESSION_FIXED_RATE_NONE_EXT;
}

static inline bool
panvk_format_is_bc(VkFormat format)
{
   return format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK &&
          format <= VK_FORMAT_BC7_SRGB_BLOCK;
}

/* Storage format of the decoded plane. BC4/BC5 use 16-bit RG to keep the
 * interpolation precision; BC1-3/7 decode to RGBA8, BC6H to RGBA16F. */
static inline VkFormat
panvk_bc_decoded_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
   case VK_FORMAT_BC2_UNORM_BLOCK:
   case VK_FORMAT_BC3_UNORM_BLOCK:
   case VK_FORMAT_BC7_UNORM_BLOCK:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
   case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
   case VK_FORMAT_BC2_SRGB_BLOCK:
   case VK_FORMAT_BC3_SRGB_BLOCK:
   case VK_FORMAT_BC7_SRGB_BLOCK:
      return VK_FORMAT_R8G8B8A8_SRGB;
   case VK_FORMAT_BC4_UNORM_BLOCK:
   case VK_FORMAT_BC5_UNORM_BLOCK:
      return VK_FORMAT_R16G16_UNORM;
   case VK_FORMAT_BC4_SNORM_BLOCK:
   case VK_FORMAT_BC5_SNORM_BLOCK:
      return VK_FORMAT_R16G16_SNORM;
   case VK_FORMAT_BC6H_UFLOAT_BLOCK:
   case VK_FORMAT_BC6H_SFLOAT_BLOCK:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

/* Storage format of the decoded plane on the fragment decode path: the
 * narrowest renderable format that holds the decoder output. BC4/BC5 decode
 * 8-bit endpoints to R8/RG8 (max error 0.5 LSB), BC6H UF16 to B10G11R11
 * (6/5-bit mantissa). sRGB-ness comes from the view, not the storage. */
static inline VkFormat
panvk_bc_compact_format(VkFormat format, bool wide)
{
   switch (format) {
   case VK_FORMAT_BC4_UNORM_BLOCK:
      return wide ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8_UNORM;
   case VK_FORMAT_BC4_SNORM_BLOCK:
      return wide ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R8_SNORM;
   case VK_FORMAT_BC5_UNORM_BLOCK:
      return wide ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM;
   case VK_FORMAT_BC5_SNORM_BLOCK:
      return wide ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R8G8_SNORM;
   case VK_FORMAT_BC6H_UFLOAT_BLOCK:
      return wide ? VK_FORMAT_R16G16B16A16_SFLOAT
                  : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
   case VK_FORMAT_BC6H_SFLOAT_BLOCK:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
   default:
      return VK_FORMAT_R8G8B8A8_UNORM;
   }
}

/* ETC2/EAC storage format of a lowmem shadow (UNORM for the sRGB pairs; the
 * view picks sRGB), or VK_FORMAT_UNDEFINED for BC6H/BC7. */
static inline VkFormat
panvk_bc_lowmem_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
   case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
      return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
   case VK_FORMAT_BC2_UNORM_BLOCK:
   case VK_FORMAT_BC2_SRGB_BLOCK:
   case VK_FORMAT_BC3_UNORM_BLOCK:
   case VK_FORMAT_BC3_SRGB_BLOCK:
      return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
   case VK_FORMAT_BC4_UNORM_BLOCK:
      return VK_FORMAT_EAC_R11_UNORM_BLOCK;
   case VK_FORMAT_BC4_SNORM_BLOCK:
      return VK_FORMAT_EAC_R11_SNORM_BLOCK;
   case VK_FORMAT_BC5_UNORM_BLOCK:
      return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
   case VK_FORMAT_BC5_SNORM_BLOCK:
      return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

/* Texture format for a BC view of an emulated image: the image's storage
 * format, with the view picking sRGB or UNORM for RGBA8 and ETC2 storage. */
static inline enum pipe_format
panvk_bc_view_pfmt(const struct panvk_image *image, VkFormat view_format)
{
   VkFormat f = image->bc_format;
   const bool srgb = vk_format_is_srgb(view_format);

   switch (f) {
   case VK_FORMAT_R8G8B8A8_UNORM:
   case VK_FORMAT_R8G8B8A8_SRGB:
      f = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
      break;
   case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
      f = srgb ? VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK : f;
      break;
   case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
      f = srgb ? VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK : f;
      break;
   case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
      f = srgb ? VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK : f;
      break;
   default:
      break;
   }
   return vk_format_to_pipe_format(f);
}

static inline unsigned
panvk_plane_index(const struct panvk_image *image,
                  VkImageAspectFlags aspect_mask)
{
   switch (aspect_mask) {
   default:
      return 0;
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      return 2;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      assert(image->plane_count > 0);
      return image->plane_count - 1;
   }
}

static inline bool
panvk_image_use_yuv_tex(unsigned arch, VkFormat format)
{
   if (arch < 9 || arch >= 14)
      return false;

   switch (format) {
   case VK_FORMAT_G8B8G8R8_422_UNORM:
   case VK_FORMAT_B8G8R8G8_422_UNORM:
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
   case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
   case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
      return true;
   default:
      break;
   }

   return false;
}

static inline unsigned
panvk_image_get_tex_count(unsigned arch, VkFormat format)
{
   return panvk_image_use_yuv_tex(arch, format)
             ? 1
             : vk_format_get_plane_count(format);
}

#define PAN_IMAGE_FROM(arch, image, plane)                                     \
   (panvk_image_use_yuv_tex(arch, image->vk.format)                            \
       ? &image->planes[0].image                                               \
       : &image->planes[plane].image)

#define PAN_IMAGE_PLANE_INDEX_FROM(arch, image, plane)                         \
   (panvk_image_use_yuv_tex(arch, image->vk.format) ? plane : 0)

static inline bool
panvk_image_is_interleaved_depth_stencil(const struct panvk_image *image){
   return image->plane_count == 1 &&
          vk_format_aspects(image->vk.format) ==
             (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

static inline bool
panvk_image_is_planar_depth_stencil(const struct panvk_image *image){
   return image->plane_count > 1 &&
          vk_format_aspects(image->vk.format) ==
             (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

static inline enum pipe_format
panvk_image_depth_only_pfmt(const struct panvk_image *image)
{
   assert(vk_format_has_depth(image->vk.format));

   return util_format_get_depth_only(image->planes[0].image.props.format);
}

static inline enum pipe_format
panvk_image_stencil_only_pfmt(const struct panvk_image *image)
{
   assert(vk_format_has_stencil(image->vk.format));

   return util_format_stencil_only(
      image->planes[image->plane_count - 1].image.props.format);
}

VkResult panvk_image_init(struct panvk_image *image,
                          const VkImageCreateInfo *pCreateInfo);

struct panvk_sparse_block_desc {
   struct VkExtent3D extent;
   uint64_t size_B;

   /* Whether this is a standard (as defined by Vulkan) sparse block size, for
    * the given arguments */
   bool standard;
};

struct panvk_sparse_block_desc panvk_get_sparse_block_desc(VkImageType type, VkFormat format);

static inline bool
panvk_sparse_block_is_valid(struct panvk_sparse_block_desc desc) { return desc.size_B > 0; }

VkSparseImageFormatProperties panvk_get_sparse_image_fmt_props(VkImageType type, VkFormat format);

static inline uint64_t
panvk_image_plane_crc_header_addr(const struct panvk_image *image)
{
   const struct panvk_image_plane *img_plane = &image->planes[0];

   if (!img_plane->image.props.crc || !img_plane->plane.base)
      return 0;

   return img_plane->plane.base +
          img_plane->plane.layout.slices[0].crc.header_offset_B;
}

#endif
