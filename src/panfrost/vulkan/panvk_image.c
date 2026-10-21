/*
 * Copyright (C) 2026 NXP
 * Copyright © 2025 Arm Ltd.
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2026 Google LLC
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "pan_afbc.h"
#include "pan_props.h"

#include "panvk_android.h"
#include "panvk_device.h"
#include "panvk_device_memory.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"

#include "drm-uapi/drm_fourcc.h"
#include "util/u_atomic.h"
#include "util/u_debug.h"
#include "util/u_drm.h"

#include "vk_format.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_util.h"


static enum mali_texture_dimension
panvk_image_type_to_mali_tex_dim(VkImageType type)
{
   switch (type) {
   case VK_IMAGE_TYPE_1D:
      return MALI_TEXTURE_DIMENSION_1D;
   case VK_IMAGE_TYPE_2D:
      return MALI_TEXTURE_DIMENSION_2D;
   case VK_IMAGE_TYPE_3D:
      return MALI_TEXTURE_DIMENSION_3D;
   default:
      UNREACHABLE("Invalid image type");
   }
}

static struct pan_image_usage
get_iusage(struct panvk_image *image, const VkImageCreateInfo *create_info)
{
   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(create_info->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   struct pan_image_usage iusage = {0};

   if (image->vk.usage &
       (VK_IMAGE_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_IMAGE_USAGE_2_TRANSFER_DST_BIT_KHR |
        VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR | VK_IMAGE_USAGE_2_INPUT_ATTACHMENT_BIT_KHR))
      iusage.bind |= PAN_BIND_SAMPLER_VIEW;

   if (image->vk.usage & VK_IMAGE_USAGE_2_STORAGE_BIT_KHR)
      iusage.bind |= PAN_BIND_STORAGE_IMAGE;

   if (image->vk.usage & VK_IMAGE_USAGE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR)
      iusage.bind |= PAN_BIND_DEPTH_STENCIL;

   if (image->vk.usage & VK_IMAGE_USAGE_2_COLOR_ATTACHMENT_BIT_KHR)
      iusage.bind |= PAN_BIND_RENDER_TARGET;

   iusage.standard_sparse_mapping_granularity =
      !!(image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_RESIDENCY_BIT_KHR);
   iusage.host_copy =
      !!(image->vk.usage & VK_IMAGE_USAGE_2_HOST_TRANSFER_BIT_KHR);
   iusage.legacy_scanout = wsi_info && wsi_info->scanout;
   iusage.wsi = wsi_info != NULL;

   return iusage;
}

static unsigned
get_plane_count(struct panvk_image *image)
{
   bool combined_ds = vk_format_aspects(image->vk.format) ==
                      (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

   /* Only depth+stencil images can be made multiplanar behind the scene. */
   if (!combined_ds)
      return vk_format_get_plane_count(image->vk.format);

   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   unsigned arch = pan_arch(phys_dev->kmod.dev->props.gpu_id);

   /* Z32_S8X24 is not supported on v9+, and we don't want to use it
    * on v7- anyway, because it's less efficient than the multiplanar
    * alternative.
    */
   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      return 2;

   assert(image->vk.format == VK_FORMAT_D24_UNORM_S8_UINT);

   /* We can do AFBC(S8) on Valhall and we're thus better off using planar
    * Z24+S8 so we can use AFBC when separateDepthStencilLayouts=true.
    */
   return arch >= 9 ? 2 : 1;
}

static enum pipe_format
select_depth_plane_pfmt(struct panvk_image *image, uint64_t mod)
{
   switch (image->vk.format) {
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D24_UNORM_S8_UINT:
      /* We only use packed Z24 when AFBC is involved, to simplify copies on on
       * AFBC resources.
       */
      return drm_is_afbc(mod) ? PIPE_FORMAT_Z24_UNORM_PACKED
                              : PIPE_FORMAT_Z24X8_UNORM;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return PIPE_FORMAT_Z32_FLOAT;
   default:
      UNREACHABLE("Invalid depth+stencil format");
   }
}

static enum pipe_format
select_stencil_plane_pfmt(struct panvk_image *image)
{
   switch (image->vk.format) {
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return PIPE_FORMAT_S8_UINT;
   default:
      UNREACHABLE("Invalid depth+stencil format");
   }
}

static enum pipe_format
select_plane_pfmt(struct panvk_image *image, uint64_t mod, unsigned plane)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   unsigned arch = pan_arch(phys_dev->kmod.dev->props.gpu_id);
   if (panvk_image_is_planar_depth_stencil(image)) {
      return plane > 0 ? select_stencil_plane_pfmt(image)
                       : select_depth_plane_pfmt(image, mod);
   } else if (image->vk.format == VK_FORMAT_X8_D24_UNORM_PACK32 && arch >= 9) {
      /* X8_D24 can be lowered to D24 on Valhall if AFBC is enabled. */
      return select_depth_plane_pfmt(image, mod);
   }

   VkFormat plane_format = vk_format_get_plane_format(image->vk.format, plane);
   return vk_format_to_pipe_format(plane_format);
}

static bool
panvk_v7_cbc_afbc(void)
{
   static int on = -1;
   if (on < 0) {
      const char *e = getenv("PANVK_V7_CBC_AFBC");
      on = !(e && e[0] == '0');
   }
   return on;
}

static bool
panvk_image_can_use_mod(struct panvk_image *image,
                        const struct pan_image_usage *iusage, uint64_t mod,
                        bool optimal_only)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   unsigned arch = pan_arch(phys_dev->kmod.dev->props.gpu_id);
   const bool forced_linear = PANVK_DEBUG(LINEAR) ||
                              image->vk.tiling == VK_IMAGE_TILING_LINEAR ||
                              image->vk.image_type == VK_IMAGE_TYPE_1D;

   /* If the image is meant to be linear, don't bother testing the
    * other cases. */
   if (forced_linear)
      return mod == DRM_FORMAT_MOD_LINEAR;

   assert(image->vk.tiling == VK_IMAGE_TILING_OPTIMAL ||
          image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);

   if (drm_is_afbc(mod)) {
      /* AFBC explicitly disabled. */
      if (PANVK_DEBUG(NO_AFBC))
         return false;

      /* The application asked for an uncompressed image. */
      if (image->vk.compr_flags & VK_IMAGE_COMPRESSION_DISABLED_EXT)
         return false;

      /* Can't do AFBC if store/host copy is requested. */
      if ((image->vk.usage | image->vk.stencil_usage) &
          (VK_IMAGE_USAGE_2_STORAGE_BIT_KHR | VK_IMAGE_USAGE_2_HOST_TRANSFER_BIT_KHR))
         return false;

      /* customBorderColorWithoutFormat on v7 (jm-v7 044): AFBC needs an
       * RGB component order on v7, so an R/B-swapped format is sampled
       * through a swizzle, which the hardware does not apply to the border
       * color. A sampler without a format cannot pre-swizzle it, so keep
       * these formats uncompressed when the feature is enabled.
       */
      /* jm-v7 089 (review M11): the border color only matters when the
       * image is sampled, so an image without SAMPLED/INPUT_ATTACHMENT usage
       * (render targets, backbuffers, transfer-only images) keeps AFBC; its
       * views get the AFBC reswizzle from the image modifier. View usage
       * can only be a subset of the image usage.
       * PANVK_V7_CBC_AFBC=0 restores the old rule (no AFBC for any
       * R/B-swapped image).
       */
      const VkImageUsageFlags cbc_sampled =
         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
      if (arch == 7 &&
          image->vk.base.device->enabled_features
             .customBorderColorWithoutFormat &&
          ((image->vk.usage & cbc_sampled) || !panvk_v7_cbc_afbc())) {
         const struct util_format_description *fdesc =
            util_format_description(vk_format_to_pipe_format(image->vk.format));
         if (fdesc && fdesc->swizzle[0] == PIPE_SWIZZLE_Z &&
             fdesc->swizzle[2] == PIPE_SWIZZLE_X)
            return false;
      }

      /* Can't do AFBC on v7- if mutable format is requested. */
      if ((image->vk.create_flags & VK_IMAGE_CREATE_2_MUTABLE_FORMAT_BIT_KHR) &&
          arch <= 7)
         return false;

      /* Disable AFBC on YUV-planar for now. */
      if (vk_format_get_plane_count(image->vk.format) > 1)
         return false;

      /* We can't have separate depth/stencil layout transitions with
       * interleaved ZS, so make sure we disallow AFBC on ZS unless
       * it's using a planar layout.
       */
      if (image->vk.base.device->enabled_features.separateDepthStencilLayouts &&
          panvk_image_is_interleaved_depth_stencil(image))
         return false;

      /* Aliased images not supported yet for single <-> multiplanar.
       * The disjoint flag is what limits this to single <-> multiplanar.
       * TODO: this can be relaxed once we have multiplanar AFBC. */
      if ((image->vk.create_flags & VK_IMAGE_CREATE_2_ALIAS_BIT_KHR) &&
          (image->vk.create_flags & VK_IMAGE_CREATE_2_DISJOINT_BIT_KHR))
         return false;

      /* On v6 and earlier, we can't reliably resolve directly to AFBC images
       * (see avoid_direct_resolve_to() in panvk_vX_cmd_draw.c).  For MS2SS,
       * this means we know a priori that the single-sampled image is going to
       * be a resolve target.  It's better to leave it uncompressed than to
       * eat the separate resolves.
       */
      if ((image->vk.create_flags &
           VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT) &&
          arch < 7)
         return false;
   }

   /* Multiplanar YUV with U-interleaving or interleaved_64k isn't supported by
    * the HW. We also need to make sure images that can be aliased to planes of
    * multi-planar images remain compatible with the aliased images, so don't
    * allow U-interleaving or interleaved_64k for those either.
    */
   if ((mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED ||
        mod == DRM_FORMAT_MOD_ARM_INTERLEAVED_64K) &&
       (vk_format_get_plane_count(image->vk.format) > 1 ||
        vk_image_can_be_aliased_to_yuv_plane(&image->vk)))
      return false;

   if (mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
      /* If we're dealing with a compressed format that requires non-compressed
       * views we can't use U_INTERLEAVED tiling because the tiling is different
       * between compressed and non-compressed formats. If we wanted to support
       * format re-interpretation we would have to specialize the shaders
       * accessing non-compressed image views (coordinate patching for
       * sampled/storage image, frag_coord patching for color attachments). Let's
       * keep things simple for now and make all compressed images that
       * have VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT set linear. */
      if (image->vk.create_flags & VK_IMAGE_CREATE_2_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT_KHR)
         return false;
   }

   /* Defer the rest of the checks to the mod handler. */
   struct pan_image_props iprops = {
      .modifier = mod,
      .dim = panvk_image_type_to_mali_tex_dim(image->vk.image_type),
      .array_size = image->vk.array_layers,
      .nr_samples = image->vk.samples,
      .nr_slices = image->vk.mip_levels,
   };

   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      iprops.format = select_plane_pfmt(image, mod, plane);
      iprops.extent_px = (struct pan_image_extent){
         .width = vk_format_get_plane_width(image->vk.format, plane,
                                            image->vk.extent.width),
         .height = vk_format_get_plane_height(image->vk.format, plane,
                                              image->vk.extent.height),
         .depth = image->vk.extent.depth,
      };

      enum pan_mod_support supported =
         pan_image_test_props(&phys_dev->kmod.dev->props, &iprops, iusage);
      if (supported == PAN_MOD_NOT_SUPPORTED ||
          (supported == PAN_MOD_NOT_OPTIMAL && optimal_only))
         return false;
   }

   return true;
}

static uint64_t
panvk_image_get_explicit_mod(
   struct panvk_image *image, const struct pan_image_usage *iusage,
   const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit)
{
   uint64_t mod = explicit->drmFormatModifier;

   assert(!vk_format_is_depth_or_stencil(image->vk.format));
   assert(image->vk.samples == 1);
   assert(image->vk.image_type != VK_IMAGE_TYPE_3D);
   assert(panvk_image_can_use_mod(image, iusage, mod, false));

   return mod;
}

static uint64_t
panvk_image_get_mod_from_list(struct panvk_image *image,
                              const struct pan_image_usage *iusage,
                              const uint64_t *mods, uint32_t mod_count)
{
   PAN_SUPPORTED_MODIFIERS(supported_mods);

   for (unsigned pass = 0; pass < 2; pass++) {
      for (unsigned i = 0; i < ARRAY_SIZE(supported_mods); ++i) {
         if (!panvk_image_can_use_mod(image, iusage, supported_mods[i],
                                      pass == 0))
            continue;

         if (!mod_count ||
             drm_find_modifier(supported_mods[i], mods, mod_count))
            return supported_mods[i];
      }
   }

   /* If we reached that point without finding a proper modifier, there's
    * a serious issue. */
   assert(!"Invalid modifier");
   return DRM_FORMAT_MOD_INVALID;
}

static uint64_t
panvk_image_get_mod(struct panvk_image *image,
                    const VkImageCreateInfo *pCreateInfo)
{
   struct pan_image_usage iusage = get_iusage(image, pCreateInfo);

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_list =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
      const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_mod =
         vk_find_struct_const(
            pCreateInfo->pNext,
            IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

      if (explicit_mod)
         return panvk_image_get_explicit_mod(image, &iusage, explicit_mod);

      if (mod_list)
         return panvk_image_get_mod_from_list(image, &iusage,
                   mod_list->pDrmFormatModifiers,
                   mod_list->drmFormatModifierCount);

      assert(!"Missing modifier info");
   }

   /* legacy scanout (images without any external modifier info) should default to LINEAR. */
   if (iusage.legacy_scanout || image->bc_emul)
      return DRM_FORMAT_MOD_LINEAR;

   /* Without external dependencies, pick the best modifier that supports the image. */
   return panvk_image_get_mod_from_list(image, &iusage, NULL, 0);
}

static bool
is_disjoint(const struct panvk_image *image)
{
   assert((image->plane_count > 1 &&
           !vk_format_is_depth_or_stencil(image->vk.format)) ||
          (image->vk.create_flags & VK_IMAGE_CREATE_2_ALIAS_BIT_KHR) ||
          !(image->vk.create_flags & VK_IMAGE_CREATE_2_DISJOINT_BIT_KHR));
   return image->vk.create_flags & VK_IMAGE_CREATE_2_DISJOINT_BIT_KHR;
}

static inline bool
strict_import(struct panvk_image *image)
{
   /* We can't do strict imports for AFBC because a Vulkan-based compositor
    * might be importing buffers from clients that are relying on the old
    * behavior. The only exception is AFBC(YUV) because support for these
    * formats was added after we started enforcing WSI pitch. */
   return !drm_is_afbc(image->vk.drm_format_mod) ||
          vk_format_get_ycbcr_info(image->vk.format);
}

static struct pan_image_props
get_pan_image_props(const struct vk_image *image, enum pipe_format pfmt,
                    uint32_t plane, bool has_crc)
{
   return (struct pan_image_props){
      .modifier = image->drm_format_mod,
      .format = pfmt,
      .dim = panvk_image_type_to_mali_tex_dim(image->image_type),
      .extent_px =
         {
            .width = vk_format_get_plane_width(image->format, plane,
                                               image->extent.width),
            .height = vk_format_get_plane_height(image->format, plane,
                                                 image->extent.height),
            .depth = image->extent.depth,
         },
      .array_size = image->array_layers,
      .nr_samples = image->samples,
      .nr_slices = image->mip_levels,
      .crc = plane == 0 && has_crc,
   };
}

static bool
panvk_should_checksum(struct panvk_image *image,
                      const VkImageCreateInfo *pCreateInfo)
{
   if (PANVK_DEBUG(NO_CRC))
      return false;

   const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
   if (explicit_info)
      return false;

   /* Linear images are only allowed if they are WSI images created
    * internally. Otherwise, they can be modified on host without the CRC
    * tracking knowing about it. */
   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   if ((image->vk.tiling == VK_IMAGE_TILING_LINEAR ||
        image->vk.drm_format_mod == DRM_FORMAT_MOD_LINEAR) &&
       !wsi_info)
      return false;

   if (pCreateInfo->imageType != VK_IMAGE_TYPE_2D ||
       pCreateInfo->arrayLayers != 1 ||
       !(vk_image_usage_flags(pCreateInfo) & VK_IMAGE_USAGE_2_COLOR_ATTACHMENT_BIT_KHR) ||
       vk_image_create_flags(pCreateInfo) & (VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR |
                                             VK_IMAGE_CREATE_2_MUTABLE_FORMAT_BIT_KHR) ||
       vk_image_usage_flags(pCreateInfo) &
       (VK_IMAGE_USAGE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR |
           VK_IMAGE_USAGE_2_STORAGE_BIT_KHR | VK_IMAGE_USAGE_2_HOST_TRANSFER_BIT_KHR))
      return false;

   /* WSI images created internally are safe even when aliasing since they use
    * identical layouts and bind the same memory. */
   if ((vk_image_create_flags(pCreateInfo) & VK_IMAGE_CREATE_2_ALIAS_BIT_KHR) && !wsi_info)
      return false;

   enum pipe_format pfmt =
      select_plane_pfmt(image, image->vk.drm_format_mod, 0);
   unsigned bytes_per_pixel =
      MAX2(image->vk.samples, 1) * util_format_get_blocksize(pfmt);
   return bytes_per_pixel <= 4;
}

/* Internal colour image holding the decoded texels of a bc_gfx image. It is
 * only ever a fragment-decode render target and a texture through
 * bc_decoded. MUTABLE keeps CRC off: the decode never redraws a tile with
 * the same content, so CRC tracking would be pure overhead. */
static void
panvk_bc_shadow_info(const struct panvk_image *image, VkImageCreateInfo *info,
                     VkImageDrmFormatModifierListCreateInfoEXT *afrc)
{
   *info = (VkImageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
               (image->vk.create_flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT),
      .imageType = VK_IMAGE_TYPE_2D,
      .format = image->bc_format,
      .extent = image->vk.extent,
      .mipLevels = image->vk.mip_levels,
      .arrayLayers = image->vk.array_layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage =
         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   /* Fixed-rate AFRC on RGBA8 shadows (BC1/2/3/7) where the GPU has it.
    * PANVK_BC_AFRC is the coding-unit size in bytes: 32 (4 bits per
    * component, max 4-5 LSB error on real pictures), 24 or 16 (3 or 2 bits,
    * lossier), 0 = off. Unset, BC1/BC2 take 24 (12 bpp): their colour is a
    * 4-entry 565 palette and BC2 alpha is 4-bit, so 3 bits per component
    * leave the error against the source picture unchanged (35.03 vs 35.05
    * dB colour, BC2 alpha 31.35 vs 31.33 dB). BC1 punch-through blocks
    * are the weak spot (alpha max 34 LSB at 32, 70 at 24, no texel crosses
    * the 0.5 alpha-test threshold at either). BC3/BC7 keep 32: at 24 the
    * BC3 alpha loses 0.7 dB (max 46 LSB). R8/RG8 (BC4/BC5) stay
    * uncompressed: at the same rate their error is 25-50 LSB, too much for
    * normal and height maps. Cube-compatible images keep AFBC: AFRC cube
    * sampling is untested. */
   static const uint64_t afrc_mods[] = {
      DRM_FORMAT_MOD_ARM_AFRC(AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_16)),
      DRM_FORMAT_MOD_ARM_AFRC(AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_24)),
      DRM_FORMAT_MOD_ARM_AFRC(AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_32)),
   };
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);

   /* Lowmem ETC2/EAC shadow: only sampled, written by the transcoder in the
    * U-interleaved layout it computes itself. */
   static const uint64_t u_interleaved_mod =
      DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED;
   if (panvk_bc_xcoded(image)) {
      info->usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      *afrc = (VkImageDrmFormatModifierListCreateInfoEXT){
         .sType =
            VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
         .drmFormatModifierCount = 1,
         .pDrmFormatModifiers = &u_interleaved_mod,
      };
      info->tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      info->pNext = afrc;
      return;
   }

   const bool bc12 = image->vk.format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK &&
                     image->vk.format <= VK_FORMAT_BC2_SRGB_BLOCK;
   /* BC1 RGB (no alpha) and BC7: 16 (8 bpp), sampling cost unchanged. */
   const bool bc8 = image->vk.format == VK_FORMAT_BC1_RGB_UNORM_BLOCK ||
                    image->vk.format == VK_FORMAT_BC1_RGB_SRGB_BLOCK ||
                    image->vk.format == VK_FORMAT_BC7_UNORM_BLOCK ||
                    image->vk.format == VK_FORMAT_BC7_SRGB_BLOCK;
   const unsigned cu =
      debug_get_num_option("PANVK_BC_AFRC", bc8 ? 16 : bc12 ? 24 : 32);

   if ((cu == 16 || cu == 24 || cu == 32) &&
       image->bc_format == VK_FORMAT_R8G8B8A8_UNORM &&
       !(image->vk.create_flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       pan_query_afrc(&phys_dev->kmod.dev->props)) {
      *afrc = (VkImageDrmFormatModifierListCreateInfoEXT){
         .sType =
            VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT,
         .drmFormatModifierCount = 1,
         .pDrmFormatModifiers = &afrc_mods[cu / 8 - 2],
      };
      info->tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      info->pNext = afrc;
   }
}

static VkResult
panvk_image_init_layouts(struct panvk_image *image,
                         const VkImageCreateInfo *pCreateInfo)
{
   /* For single plane:
    * - 1 x panvk_image -> 1 x (1 x pan_image -> 1 x pan_image_plane)
    *
    * For multi-plane Z/S and sw YUV:
    * - 1 x panvk_image -> N x (1 x pan_image -> 1 x pan_image_plane)
    *
    * For hw YUV texturing:
    * - 1 x panvk_image -> 1 x (1 x pan_image -> N x pan_image_plane)
    */
   struct panvk_device *dev = to_panvk_device(image->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   unsigned arch = pan_arch(phys_dev->kmod.dev->props.gpu_id);
   const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_info =
      vk_find_struct_const(
         pCreateInfo->pNext,
         IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);

   const struct pan_mod_handler *mod_handler =
      pan_mod_get_handler(arch, image->vk.drm_format_mod);
   const bool should_checksum =
      arch >= 10 && panvk_should_checksum(image, pCreateInfo);
   image->crc_safe_external = should_checksum && wsi_info != NULL;

   /* initialize pan_image props and mod_handler */
   if (panvk_image_use_yuv_tex(arch, image->vk.format)) {
      const enum pipe_format pfmt = vk_format_to_pipe_format(image->vk.format);
      image->planes[0].image = (struct pan_image){
         .props = get_pan_image_props(&image->vk, pfmt, 0, should_checksum),
         .mod_handler = mod_handler,
      };
   } else {
      for (uint8_t plane = 0; plane < image->plane_count; plane++) {
         const enum pipe_format pfmt =
            select_plane_pfmt(image, image->vk.drm_format_mod, plane);
         image->planes[plane].image = (struct pan_image){
            .props =
               get_pan_image_props(&image->vk, pfmt, plane, should_checksum),
            .mod_handler = mod_handler,
         };
      }
   }

   /* initialize plane layout */
   const bool use_strict_import = strict_import(image);
   struct pan_image_layout_constraints plane_layout = {
      .offset_B = 0,
   };
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      if (explicit_info) {
         plane_layout = (struct pan_image_layout_constraints){
            .offset_B = explicit_info->pPlaneLayouts[plane].offset,
            .wsi_row_pitch_B = explicit_info->pPlaneLayouts[plane].rowPitch,
            .wsi_array_pitch_B = explicit_info->pPlaneLayouts[plane].arrayPitch,
            .strict = use_strict_import,
         };
      }

      struct pan_image *pan_img = PAN_IMAGE_FROM(arch, image, plane);
      const uint8_t pan_plane = PAN_IMAGE_PLANE_INDEX_FROM(arch, image, plane);

      pan_img->planes[pan_plane] = &image->planes[plane].plane;
      if (!pan_image_layout_init(arch, pan_img, pan_plane, &plane_layout)) {
         return panvk_error(image->vk.base.device,
                            VK_ERROR_INITIALIZATION_FAILED);
      }

      if (!is_disjoint(image) && !explicit_info)
         plane_layout.offset_B += image->planes[plane].plane.layout.data_size_B;
   }

   if (image->bc_gfx) {
      /* Lay the decoded plane out exactly as the bc_shadow image created in
       * panvk_CreateImage() will be, after the raw blocks. */
      VkImageCreateInfo sci;
      VkImageDrmFormatModifierListCreateInfoEXT afrc;
      struct panvk_image tmp = {0};

      panvk_bc_shadow_info(image, &sci, &afrc);
      vk_image_init(&dev->vk, &tmp.vk, &sci);
      VkResult result = panvk_image_init(&tmp, &sci);
      vk_image_finish(&tmp.vk);
      if (result != VK_SUCCESS)
         return result;

      image->bc_decoded = tmp.planes[0];
      image->bc_decoded.image.planes[0] = &image->bc_decoded.plane;
      /* Without a raw plane the shadow is all the memory the image has. */
      image->bc_decoded_offset =
         (image->bc_no_raw || image->bc_raw_store)
         ? 0
         : ALIGN_POT(plane_layout.offset_B, 4096);
   } else if (image->bc_emul) {
      struct pan_image *dec = &image->bc_decoded.image;
      enum pipe_format dec_pfmt = vk_format_to_pipe_format(image->bc_format);

      *dec = (struct pan_image){
         .props = get_pan_image_props(&image->vk, dec_pfmt, 0, false),
         .mod_handler = mod_handler,
      };
      dec->planes[0] = &image->bc_decoded.plane;
      plane_layout.offset_B = ALIGN_POT(plane_layout.offset_B, 4096);
      if (!pan_image_layout_init(arch, dec, 0, &plane_layout))
         return panvk_error(image->vk.base.device,
                            VK_ERROR_INITIALIZATION_FAILED);
   }

   return VK_SUCCESS;
}

static void
panvk_image_pre_mod_select_meta_adjustments(struct panvk_image *image)
{
   const VkImageAspectFlags aspects = vk_format_aspects(image->vk.format);
   const VkImageUsageFlags2KHR all_usage =
      image->vk.usage | image->vk.stencil_usage;

   /* We do image blit/resolve with vk_meta, so when an image is flagged as
    * being a potential transfer source, we also need to add the sampled usage.
    */
   if (image->vk.usage & VK_IMAGE_USAGE_2_TRANSFER_SRC_BIT_KHR)
      image->vk.usage |= VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR;
   if (image->vk.stencil_usage & VK_IMAGE_USAGE_2_TRANSFER_SRC_BIT_KHR)
      image->vk.stencil_usage |= VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR;

   /* Similarly, image that can be a transfer destination can be attached
    * as a color or depth-stencil attachment by vk_meta. */
   if (image->vk.usage & VK_IMAGE_USAGE_2_TRANSFER_DST_BIT_KHR) {
      if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         image->vk.usage |= VK_IMAGE_USAGE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR;

      if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
         image->vk.usage |= VK_IMAGE_USAGE_2_COLOR_ATTACHMENT_BIT_KHR;
   }

   if (image->vk.stencil_usage & VK_IMAGE_USAGE_2_TRANSFER_DST_BIT_KHR)
      image->vk.stencil_usage |= VK_IMAGE_USAGE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR;

   /* vk_meta creates 2D array views of 3D images. */
   if (all_usage & VK_IMAGE_USAGE_2_TRANSFER_DST_BIT_KHR &&
       image->vk.image_type == VK_IMAGE_TYPE_3D)
      image->vk.create_flags |= VK_IMAGE_CREATE_2_2D_ARRAY_COMPATIBLE_BIT_KHR;

   /* Needed for resolve operations. */
   if (image->vk.usage & VK_IMAGE_USAGE_2_COLOR_ATTACHMENT_BIT_KHR)
      image->vk.usage |= VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR;

   if (image->vk.usage & VK_IMAGE_USAGE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR &&
       aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR;

   if (image->vk.stencil_usage & VK_IMAGE_USAGE_2_DEPTH_STENCIL_ATTACHMENT_BIT_KHR)
      image->vk.stencil_usage |= VK_IMAGE_USAGE_2_SAMPLED_BIT_KHR;

   if ((image->vk.usage &
        (VK_IMAGE_USAGE_2_TRANSFER_DST_BIT_KHR | VK_IMAGE_USAGE_2_TRANSFER_SRC_BIT_KHR)) &&
       vk_format_is_compressed(image->vk.format)) {
      /* We need to be able to create RGBA views of compressed formats for
       * vk_meta copies.
       *
       * FIXME: this might cause LINEAR to be used instead of a better modifier.
       * See https://gitlab.freedesktop.org/panfrost/mesa/-/issues/271 for
       * details.
       */
      image->vk.create_flags |= VK_IMAGE_CREATE_2_MUTABLE_FORMAT_BIT_KHR |
                                VK_IMAGE_CREATE_2_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT_KHR;
   }
}

static void
panvk_image_post_mod_select_meta_adjustments(struct panvk_image *image)
{
   const VkImageAspectFlags aspects = vk_format_aspects(image->vk.format);

   /* If the image didn't end up using AFBC, we should add the storage flag
    * to allow vkmeta to take the compute based copying path. */
   if ((image->vk.usage & VK_IMAGE_USAGE_2_TRANSFER_DST_BIT_KHR) &&
       (aspects & VK_IMAGE_ASPECT_COLOR_BIT) &&
       !drm_is_afbc(image->vk.drm_format_mod)) {
      image->vk.usage |= VK_IMAGE_USAGE_2_STORAGE_BIT_KHR;
   }
}

static uint64_t
panvk_image_plane_size(const struct panvk_image *image, unsigned plane)
{
   const struct pan_image_layout *layout = &image->planes[plane].plane.layout;
   return layout->slices[0].offset_B + layout->data_size_B;
}

static uint64_t
panvk_image_get_total_size(const struct panvk_image *image)
{
   uint64_t size = 0;
   /* bc_no_raw: planes[0] (raw blocks) is never accessed, so it gets no
    * memory of its own and only aliases the start of the shadow.
    * bc_raw_store: the raw blocks live in the raw store (csf-v11/201). */
   for (uint8_t plane = 0;
        plane < image->plane_count && !image->bc_no_raw &&
        !image->bc_raw_store;
        plane++)
      size = MAX2(size, panvk_image_plane_size(image, plane));
   if (image->bc_emul) {
      const struct pan_image_layout *l = &image->bc_decoded.plane.layout;
      size = MAX2(size, image->bc_decoded_offset + l->slices[0].offset_B +
                           l->data_size_B);
   }
   return size;
}

/* Report the VA ranges of a bound non-sparse image. Disjoint images can place
 * each plane in a different allocation, so they are reported individually.
 * Otherwise the planes share a base and a single range covers the image.
 */
static void
panvk_image_report_binding(struct panvk_device *dev, struct panvk_image *image,
                           VkDeviceAddressBindingTypeEXT type)
{
   if (is_disjoint(image)) {
      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         if (!image->planes[plane].plane.base)
            continue;
         panvk_address_binding_report(dev, &image->vk.base,
                                      image->planes[plane].plane.base,
                                      panvk_image_plane_size(image, plane),
                                      type);
      }
   } else if (image->bc_raw_store) {
      /* planes[0] points at the raw store: the memory starts with the
       * shadow. */
      if (image->bc_decoded.plane.base)
         panvk_address_binding_report(dev, &image->vk.base,
                                      image->bc_decoded.plane.base,
                                      panvk_image_get_total_size(image), type);
   } else if (image->planes[0].plane.base) {
      panvk_address_binding_report(dev, &image->vk.base,
                                   image->planes[0].plane.base,
                                   panvk_image_get_total_size(image), type);
   }
}

static uint64_t
panvk_image_get_sparse_size(const struct panvk_image *image)
{
   struct panvk_device *device = to_panvk_device(image->vk.base.device);
   uint64_t image_size = panvk_image_get_total_size(image);
   uint64_t page_size = panvk_get_gpu_page_size(device);
   return ALIGN_POT(image_size, page_size);
}

/* Images the lowmem path may take: plain optimal-tiled 2D images whose
 * memory nothing but panvk touches. Sparse, aliased, disjoint, external and
 * host-transfer images, images with uncompressed (block-texel) views, and
 * BC4/BC5 images that may be viewed with the other signedness keep the
 * exact path. */
static bool
panvk_bc_lowmem_allowed(const struct panvk_image *image,
                        const VkImageCreateInfo *info)
{
   const VkImageCreateFlags reject =
      VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
      VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT |
      VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
      VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT |
      VK_IMAGE_CREATE_DISJOINT_BIT | VK_IMAGE_CREATE_ALIAS_BIT;

   /* Host image copies only see the raw plane. */
   if (image->vk.tiling != VK_IMAGE_TILING_OPTIMAL ||
       (image->vk.create_flags & reject) ||
       (image->vk.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT) ||
       vk_find_struct_const(info->pNext,
                            EXTERNAL_MEMORY_IMAGE_CREATE_INFO))
      return false;

   if ((image->vk.create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
       image->vk.format >= VK_FORMAT_BC4_UNORM_BLOCK &&
       image->vk.format <= VK_FORMAT_BC5_SNORM_BLOCK) {
      const VkImageFormatListCreateInfo *list =
         vk_find_struct_const(info->pNext, IMAGE_FORMAT_LIST_CREATE_INFO);

      if (!list || !list->viewFormatCount)
         return false;
      for (uint32_t i = 0; i < list->viewFormatCount; i++) {
         if (list->pViewFormats[i] != image->vk.format)
            return false;
      }
   }

   return true;
}

static VkResult panvk_bc_raw_create(struct panvk_device *dev,
                                    struct panvk_image *image);
static void panvk_bc_raw_destroy(struct panvk_image *image);
static void panvk_bc_raw_rebind(struct panvk_image *image);

/* csf-v11/202: BC6H UF16 lowmem shadows can be ASTC 4x4 HDR when the GPU
 * samples that natively. PANVK_BC_BC6H=exact (default) | astc: the ASTC
 * shadow is 4x smaller but filters at half rate (64 bpp decode, 2x the
 * sampling cost of B10G11R11) and is lossier, so it is opt-in. */
/* PANVK_BC_BC6H=exact (default) | astc (UF16 -> ASTC 4x4 HDR) | astc_sf
 * (UF16 and SF16 -> ASTC 4x4 HDR, SF16 negatives become 0, csf-v11/208). */
/* csf-v11/210: PANVK_BC_BC6H_DECODE=block (default) decodes the exact BC6H
 * shadow with one compute invocation per 4x4 block, written straight into
 * a U-interleaved shadow; fragment keeps the per-texel fragment decode of
 * csf-v11/150 (bc5). */
static bool
panvk_bc_bc6h_block_decode(void)
{
   static int on = -1;

   if (p_atomic_read(&on) < 0) {
      const char *mode = os_get_option("PANVK_BC_BC6H_DECODE");

      if (mode && strcmp(mode, "block") && strcmp(mode, "fragment"))
         mesa_logw("panvk: unknown PANVK_BC_BC6H_DECODE=%s, using block",
                   mode);
      p_atomic_set(&on, !mode || strcmp(mode, "fragment") != 0);
   }
   return p_atomic_read(&on);
}

static bool
panvk_bc_lowmem_astc(const struct panvk_physical_device *phys_dev,
                     VkFormat format)
{
   static int on = -1;

   if (p_atomic_read(&on) < 0) {
      const char *mode = os_get_option("PANVK_BC_BC6H");

      if (mode && strcmp(mode, "astc") && strcmp(mode, "astc_sf") &&
          strcmp(mode, "exact"))
         mesa_logw("panvk: unknown PANVK_BC_BC6H=%s, using exact", mode);
      p_atomic_set(&on, !mode ? 0 : !strcmp(mode, "astc_sf") ? 2
                                    : !strcmp(mode, "astc") ? 1 : 0);
   }
   if (p_atomic_read(&on) < (format == VK_FORMAT_BC6H_SFLOAT_BLOCK ? 2 : 1))
      return false;

   const struct pan_format fmt =
      phys_dev->formats.all[PIPE_FORMAT_ASTC_4x4_FLOAT];
   return fmt.hw &&
          (BITFIELD_BIT(fmt.texfeat_bit) &
           pan_query_compressed_formats(&phys_dev->kmod.dev->props));
}

/* PANVK_BC_RAW=lazy (default since csf-v11/207) | store | plane: lazy
 * gives readable BC1-BC5 images no raw blocks until the device first
 * reads a BC image back (bc_raw_lazy), store keeps a raw store for every
 * readable image (csf-v11/201-206), plane keeps the raw blocks in the
 * image memory. */
static bool
panvk_bc_raw_lazy_mode(void)
{
   static int lazy = -1;

   if (p_atomic_read(&lazy) < 0) {
      const char *mode = os_get_option("PANVK_BC_RAW");
      p_atomic_set(&lazy, !mode || !strcmp(mode, "lazy"));
   }
   return p_atomic_read(&lazy);
}

/* PANVK_BC_RAW_MIN (bytes, default 64 KiB): raw block planes at least that
 * big go to the raw store. */
static bool
panvk_bc_raw_store_wanted(const struct panvk_image *image)
{
   static int64_t min_B = -1;

   if (min_B < 0) {
      const char *mode = os_get_option("PANVK_BC_RAW");
      int64_t v = debug_get_num_option("PANVK_BC_RAW_MIN", 64 * 1024);

      if (mode && strcmp(mode, "store") && strcmp(mode, "plane") &&
          strcmp(mode, "lazy"))
         mesa_logw("panvk: unknown PANVK_BC_RAW=%s, using lazy", mode);
      p_atomic_set(&min_B,
                   (mode && !strcmp(mode, "plane")) ? INT64_MAX : MAX2(v, 0));
   }

   const uint32_t blk_B = vk_format_get_blocksize(image->vk.format);
   uint64_t size = 0;
   for (uint32_t l = 0; l < image->vk.mip_levels; l++) {
      const VkExtent3D e = vk_image_mip_level_extent(&image->vk, l);
      size += (uint64_t)DIV_ROUND_UP(e.width, 4) * DIV_ROUND_UP(e.height, 4) *
              blk_B;
   }
   size *= image->vk.array_layers;
   return size >= (uint64_t)p_atomic_read(&min_B);
}

VkResult
panvk_image_init(struct panvk_image *image,
                 const VkImageCreateInfo *pCreateInfo)
{
   /* Needs to happen early for some panvk_image_ helpers to work. */
   image->plane_count = get_plane_count(image);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   image->bc_emul = panvk_format_is_bc(image->vk.format) &&
                    panvk_bc_emul_enabled(phys_dev);
   /* Fragment decode needs layered rendering with gl_Layer in the FS
    * (geometryShader, v10+) and 2D layers; 3D images stay on compute. */
   image->bc_gfx = image->bc_emul &&
                   pan_arch(phys_dev->kmod.dev->props.gpu_id) >= 10 &&
                   image->vk.image_type == VK_IMAGE_TYPE_2D &&
                   !PANVK_DEBUG(BC_COMPUTE) && !PANVK_DEBUG(NO_GS);
   if (image->bc_emul) {
      image->bc_format =
         image->bc_gfx
            ? panvk_bc_compact_format(image->vk.format, PANVK_DEBUG(BC_WIDE))
            : panvk_bc_decoded_format(image->vk.format);
   }

   /* BC lowmem. Decided on the application's flags and usage, before the
    * meta adjustments below add MUTABLE/BLOCK_TEXEL_VIEW_COMPATIBLE. */
   image->bc_lowmem = 0;
   image->bc_no_raw = false;
   image->bc_raw_store = false;
   image->bc_raw_lazy = false;
   image->bc_raw_stale = false;
   if (image->bc_gfx && panvk_bc_lowmem_enabled(phys_dev) &&
       panvk_bc_lowmem_allowed(image, pCreateInfo)) {
      const VkFormat lm_format = panvk_bc_lowmem_format(image->vk.format);
      const bool no_raw =
         !(image->vk.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
      /* csf-v11/201: images that can be read back keep their raw blocks
       * in the raw store instead of the image memory. */
      const bool raw_store = !no_raw && panvk_bc_raw_store_wanted(image);

      if ((image->vk.format == VK_FORMAT_BC6H_UFLOAT_BLOCK ||
           image->vk.format == VK_FORMAT_BC6H_SFLOAT_BLOCK) &&
          !(image->vk.create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
          panvk_bc_lowmem_astc(phys_dev, image->vk.format)) {
         /* csf-v11/202: BC6H UF16 -> ASTC 4x4 HDR (8 bpp). MUTABLE images
          * stay exact: an SFLOAT view of the UF16 data would sample the
          * ASTC shadow as signed. */
         image->bc_lowmem = 3;
         image->bc_format = VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK;
         image->bc_no_raw = no_raw;
         image->bc_raw_store = raw_store;
      } else if (lm_format != VK_FORMAT_UNDEFINED) {
         struct panvk_device *dev = to_panvk_device(image->vk.base.device);
         /* csf-v11/207: no raw blocks until the device reads one back. */
         const bool lazy = raw_store &&
                           image->vk.image_type != VK_IMAGE_TYPE_3D &&
                           panvk_bc_raw_lazy_mode() &&
                           !p_atomic_read(&dev->bc_raw.needed);

         image->bc_lowmem = 1;
         image->bc_format = lm_format;
         image->bc_no_raw = no_raw || lazy;
         image->bc_raw_store = raw_store && !lazy;
         image->bc_raw_lazy = lazy;
      } else if ((image->vk.format == VK_FORMAT_BC6H_UFLOAT_BLOCK ||
                  image->vk.format == VK_FORMAT_BC6H_SFLOAT_BLOCK) &&
                 panvk_bc_bc6h_block_decode()) {
         /* csf-v11/210: BC6H decoded once per block by a compute shader
          * into the same B10G11R11 / RGBA16F shadow, U-interleaved. */
         image->bc_lowmem = 4;
         image->bc_no_raw = no_raw;
         image->bc_raw_store = raw_store;
      } else if (no_raw || raw_store) {
         image->bc_lowmem = 2;
         image->bc_no_raw = no_raw;
         image->bc_raw_store = raw_store;
      }
   }

   /* Add any create/usage flags that might be needed for meta operations.
    * This is run before the modifier selection because some
    * usage/create_flags influence the modifier selection logic. */
   panvk_image_pre_mod_select_meta_adjustments(image);

   /* Now that we've patched the create/usage flags, we can proceed with the
    * modifier selection. */
   image->vk.drm_format_mod = panvk_image_get_mod(image, pCreateInfo);

   /* Some modifiers like AFBC affect some decisions we make for vkmeta, but
    * we don't want to outright prevent these modifiers. If those weren't used,
    * we apply additional flags here. */
   panvk_image_post_mod_select_meta_adjustments(image);

   return panvk_image_init_layouts(image, pCreateInfo);
}

static void
panvk_image_plane_bind_mem(struct panvk_device *dev,
                           struct panvk_image_plane *plane,
                           struct panvk_device_memory *mem, uint64_t offset)
{
   plane->plane.base = mem->addr.dev + offset;
   plane->mem = mem;
   plane->mem_offset = offset;
   /* Zero-initialize CRC state. If CRC state is not mapped on the host, do a
    * temporary mapping just for this operation. */
   if (plane->image.props.crc) {
      const struct pan_image_slice_layout *slice =
         &plane->plane.layout.slices[0];
      uint64_t state_offset = offset + slice->crc.header_offset_B;

      bool temporary_map = mem->addr.host == NULL;
      uint8_t *cpu_map = temporary_map
                            ? pan_kmod_bo_mmap(mem->bo, PROT_READ | PROT_WRITE,
                                               MAP_SHARED, NULL)
                            : mem->addr.host;
      if (cpu_map == 0 || cpu_map == MAP_FAILED) {
         plane->image.props.crc = false;
         return;
      }

      size_t crc_size = PAN_CRC_HEADER_SIZE_B + slice->crc.size_B;
      memset(cpu_map + state_offset, 0, crc_size);
      pan_kmod_queue_bo_map_sync(mem->bo, state_offset, cpu_map + state_offset,
                                 crc_size, PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH);

      if (temporary_map) {
         int ret =
            pan_kmod_bo_munmap(mem->bo, cpu_map, pan_kmod_bo_size(mem->bo));
         assert(!ret);
      }
   }
}

static void
panvk_image_plane_bind_addr(struct panvk_device *dev,
                            struct panvk_image_plane *plane,
                            uint64_t addr)
{
   plane->plane.base = addr;
}

static void
drop_ms2ss_flag(VkImageCreateInfo *info,
                VkImageCreateFlags2CreateInfoKHR *flags2)
{
   *flags2 = (VkImageCreateFlags2CreateInfoKHR){
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_FLAGS_2_CREATE_INFO_KHR,
      .pNext = (void *)info->pNext,
      .flags = vk_image_create_flags(info) &
               ~VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT,
   };
   info->pNext = flags2;
   info->flags &=
      ~VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT;
}

static void
create_ms_images(struct panvk_device *dev, struct panvk_image *img,
                 const VkImageCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator)
{
   struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);

   VkImageCreateFlags2CreateInfoKHR info_flags2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_FLAGS_2_CREATE_INFO_KHR,
      .flags = vk_image_create_flags(pCreateInfo),
   };
   VkImageUsageFlags2CreateInfoKHR info_usage2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_USAGE_FLAGS_2_CREATE_INFO_KHR,
      .pNext = &info_flags2,
      .usage = vk_image_usage_flags(pCreateInfo),
   };
   VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = &info_usage2,
      .format = pCreateInfo->format,
      .type = pCreateInfo->imageType,
      .tiling = pCreateInfo->tiling,
   };
   VkImageFormatProperties2 properties = {};
   panvk_GetPhysicalDeviceImageFormatProperties2(
      vk_physical_device_to_handle(&pdev->vk), &info, &properties);

   VkImageCreateInfo ms_img_info = *pCreateInfo;
   VkImageCreateFlags2CreateInfoKHR ms_flags2_info;

   assert(img->vk.create_flags &
          VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT);
   drop_ms2ss_flag(&ms_img_info, &ms_flags2_info);

   for (uint32_t msaa_idx = 0; msaa_idx < ARRAY_SIZE(img->ms_imgs);
        ++msaa_idx) {
      VkSampleCountFlagBits msaa = 1 << (msaa_idx + 1);

      if ((properties.imageFormatProperties.sampleCounts & msaa) == 0) {
         img->ms_imgs[msaa_idx] = VK_NULL_HANDLE;
         continue;
      }

      ms_img_info.samples = msaa;

      panvk_CreateImage(panvk_device_to_handle(dev), &ms_img_info, pAllocator,
                        &img->ms_imgs[msaa_idx]);

      struct panvk_image *res = panvk_image_from_handle(img->ms_imgs[msaa_idx]);
      assert(res->vk.format == img->vk.format);
      assert(res->plane_count == img->plane_count);
   }
}

/* See Vulkan spec, 35.4.3. Standard Sparse Image Block Shapes for details */
enum {
   STANDARD_SPARSE_BLOCK_SIZE_B = 65536,
};

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
   VK_FROM_HANDLE(panvk_device, dev, device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   VkResult result;

   if (panvk_android_is_gralloc_image(pCreateInfo)) {
      return panvk_android_create_gralloc_image(device, pCreateInfo, pAllocator,
                                                pImage);
   }

   if (wsi_common_is_swapchain_image(pCreateInfo)) {
      return wsi_common_create_swapchain_image(&phys_dev->wsi_device,
                                               pCreateInfo,
                                               pImage);
   }

   struct panvk_image *image =
      vk_image_create(&dev->vk, pCreateInfo, pAllocator, sizeof(*image));
   if (!image)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = panvk_image_init(image, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_image_destroy(&dev->vk, pAllocator, &image->vk);
      return result;
   }

   uint64_t size = panvk_image_get_total_size(image);

   /*
    * From the Vulkan spec:
    *
    *    If the size of the resultant image would exceed maxResourceSize, then
    *    vkCreateImage must fail and return VK_ERROR_OUT_OF_DEVICE_MEMORY.
    */
   if (size > panvk_get_max_resource_size(phys_dev)) {
      result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_destroy_image;
   }

   if (image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR) {
      uint64_t va_range = panvk_image_get_sparse_size(image);
      /* Sparse images must be aligned to the sparse block size */
      uint64_t alignment =
         MAX2(pan_choose_gpu_va_alignment(dev->kmod.vm, va_range),
              STANDARD_SPARSE_BLOCK_SIZE_B);

      image->sparse.device_address =
         panvk_as_alloc(dev, PANVK_NO_EXEC_VA_HEAP, va_range, alignment);
      if (!image->sparse.device_address) {
         result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_destroy_image;
      }

      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         panvk_image_plane_bind_addr(dev, &image->planes[plane],
                                     image->sparse.device_address);
      }

      if ((image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_RESIDENCY_BIT_KHR) ||
          PANVK_DEBUG(FORCE_BLACKHOLE)) {
         /* Map last so that we don't have a possibility of getting any more
          * errors, in which case we'd have to unmap.
          */
         struct pan_kmod_vm_op map = {
            .type = PAN_KMOD_VM_OP_TYPE_MAP,
            .va = {
               .start = image->sparse.device_address,
               .size = va_range,
            },
            .flags = PAN_KMOD_VM_OP_OP_MAP_SPARSE,
         };

         int ret = pan_kmod_vm_bind(dev->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE,
                                    &map, 1);
         if (ret) {
            result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
            goto err_free_va;
         }
      }
   }

   if (image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR)
      panvk_address_binding_report(dev, &image->vk.base,
                                   image->sparse.device_address,
                                   panvk_image_get_sparse_size(image),
                                   VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

   if (vk_image_create_flags(pCreateInfo) &
       VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT)
      create_ms_images(dev, image, pCreateInfo, pAllocator);

   if (image->bc_gfx) {
      VkImageCreateInfo sci;
      VkImageDrmFormatModifierListCreateInfoEXT afrc;

      panvk_bc_shadow_info(image, &sci, &afrc);
      result = panvk_CreateImage(device, &sci, pAllocator, &image->bc_shadow);
      if (result != VK_SUCCESS)
         goto err_destroy_image;
   }

   if (image->bc_raw_store) {
      result = panvk_bc_raw_create(dev, image);
      if (result != VK_SUCCESS) {
         panvk_DestroyImage(device, image->bc_shadow, pAllocator);
         goto err_destroy_image;
      }
   }

   *pImage = panvk_image_to_handle(image);
   return VK_SUCCESS;

err_free_va:
   if (image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR)
      panvk_as_free(dev, image->sparse.device_address,
                    panvk_image_get_sparse_size(image));

err_destroy_image:
   vk_image_destroy(&dev->vk, pAllocator, &image->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_DestroyImage(VkDevice _device, VkImage _image,
                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image, image, _image);

   if (!image)
      return;

   panvk_DestroyImage(_device, image->bc_shadow, pAllocator);
   panvk_bc_raw_destroy(image);

   if (image->vk.create_flags &
       VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT) {
      for (uint32_t i = 0; i < ARRAY_SIZE(image->ms_imgs); ++i) {
         panvk_DestroyImage(_device, image->ms_imgs[i], pAllocator);
      }
   }

   if (image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR) {
      uint64_t va_range = panvk_image_get_sparse_size(image);

      panvk_address_binding_report(device, &image->vk.base,
                                   image->sparse.device_address, va_range,
                                   VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

      struct pan_kmod_vm_op unmap = {
         .type = PAN_KMOD_VM_OP_TYPE_UNMAP,
         .va = {
            .start = image->sparse.device_address,
            .size = va_range,
         },
      };
      ASSERTED int ret = pan_kmod_vm_bind(
         device->kmod.vm, PAN_KMOD_VM_OP_MODE_IMMEDIATE, &unmap, 1);
      assert(!ret);

      panvk_as_free(device, image->sparse.device_address, va_range);
   } else {
      panvk_image_report_binding(device, image,
                                 VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);
   }

   vk_image_destroy(&device->vk, pAllocator, &image->vk);
}

static void
get_image_subresource_layout(const struct panvk_image *image,
                             const VkImageSubresource2 *subres2,
                             VkSubresourceLayout2 *layout2)
{
   const VkImageSubresource *subres = &subres2->imageSubresource;
   VkSubresourceLayout *layout = &layout2->subresourceLayout;
   unsigned plane = panvk_plane_index(image, subres->aspectMask);
   assert(plane < PANVK_MAX_PLANES);

   const struct pan_image_slice_layout *slice_layout =
      &image->planes[plane].plane.layout.slices[subres->mipLevel];

   layout->offset =
      slice_layout->offset_B +
      (subres->arrayLayer * image->planes[plane].plane.layout.array_stride_B);
   layout->size = slice_layout->crc.size_B ? slice_layout->crc.header_offset_B -
                                                slice_layout->offset_B
                                           : slice_layout->size_B;
   layout->arrayPitch = image->planes[plane].plane.layout.array_stride_B;

   if (drm_is_afbc(image->vk.drm_format_mod)) {
      struct panvk_physical_device *phys_dev =
         to_panvk_physical_device(image->vk.base.device->physical);
      unsigned arch = pan_arch(phys_dev->kmod.dev->props.gpu_id);
      const struct pan_image *pan_img = PAN_IMAGE_FROM(arch, image, plane);
      const uint8_t pan_plane = PAN_IMAGE_PLANE_INDEX_FROM(arch, image, plane);

      /* row/depth pitch expressed in (AFBC superblocks * payload size). */
      layout->rowPitch =
         pan_image_get_wsi_row_pitch(pan_img, pan_plane, subres->mipLevel);
      layout->depthPitch = slice_layout->afbc.surface_stride_B;
   } else {
      layout->rowPitch = slice_layout->tiled_or_linear.row_stride_B;
      layout->depthPitch = slice_layout->tiled_or_linear.surface_stride_B;
   }

   VkImageCompressionPropertiesEXT *compression_props =
      vk_find_struct(layout2->pNext, IMAGE_COMPRESSION_PROPERTIES_EXT);
   if (compression_props) {
      panvk_image_set_compression_props(compression_props,
                                        drm_is_afbc(image->vk.drm_format_mod));
   }

   VkSubresourceHostMemcpySize *memcpy_size =
      vk_find_struct(layout2->pNext, SUBRESOURCE_HOST_MEMCPY_SIZE);
   if (memcpy_size) {
      /* When copying to/from a D24S8 image, we can't use the normal memcpy
       * path because we need to interleave the depth/stencil components. For
       * the stencil aspect, the copied data only needs 1 byte/px instead of 4.
       */
      if (image->vk.format == VK_FORMAT_D24_UNORM_S8_UINT &&
          image->plane_count == 1) {
         switch (subres->aspectMask) {
            case VK_IMAGE_ASPECT_DEPTH_BIT:
               memcpy_size->size = slice_layout->size_B;
               break;
            case VK_IMAGE_ASPECT_STENCIL_BIT:
               memcpy_size->size = slice_layout->size_B / 4;
               break;
            default:
               UNREACHABLE("invalid aspect");
         }
      } else {
         memcpy_size->size = slice_layout->size_B;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetImageSubresourceLayout2(VkDevice device, VkImage image,
                                 const VkImageSubresource2 *pSubresource,
                                 VkSubresourceLayout2 *pLayout)
{
   VK_FROM_HANDLE(panvk_image, img, image);

   get_image_subresource_layout(img, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetDeviceImageSubresourceLayoutKHR(
   VkDevice device, const VkDeviceImageSubresourceInfoKHR *pInfo,
   VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(panvk_device, dev, device);
   struct panvk_image image = {0};

   vk_image_init(&dev->vk, &image.vk, pInfo->pCreateInfo);
   panvk_image_init(&image, pInfo->pCreateInfo);
   get_image_subresource_layout(&image, pInfo->pSubresource, pLayout);
   vk_image_finish(&image.vk);
}

static uint64_t
panvk_image_get_sparse_binding_granularity(struct panvk_image *image)
{
   struct panvk_device *dev = to_panvk_device(image->vk.base.device);

   assert(image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR);

   if (image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_RESIDENCY_BIT_KHR)
      return panvk_get_sparse_block_desc(image->vk.image_type, image->vk.format).size_B;

   return panvk_get_gpu_page_size(dev);
}

static void
append_ms_to_ss_memory_reqs(VkMemoryRequirements2 *pMemoryRequirements,
                            const VkMemoryRequirements2 *append)
{
   pMemoryRequirements->memoryRequirements.alignment =
      MAX2(pMemoryRequirements->memoryRequirements.alignment,
           append->memoryRequirements.alignment);
   /* After the previous images, align this images start properly. */
   pMemoryRequirements->memoryRequirements.size =
      align(pMemoryRequirements->memoryRequirements.size,
            append->memoryRequirements.alignment);
   pMemoryRequirements->memoryRequirements.size +=
      append->memoryRequirements.size;
   pMemoryRequirements->memoryRequirements.memoryTypeBits &=
      append->memoryRequirements.memoryTypeBits;
   assert(pMemoryRequirements->memoryRequirements.memoryTypeBits != 0);
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetImageMemoryRequirements2(VkDevice device,
                                  const VkImageMemoryRequirementsInfo2 *pInfo,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_image, image, pInfo->image);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);

   /* For sparse resources alignment specifies binding granularity, rather than
    * the alignment requirement. It's up to us to satisfy the alignment
    * requirement when allocating the VA range.
    */
   const uint64_t alignment =
      image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR
         ? panvk_image_get_sparse_binding_granularity(image)
         : 4096;
   const VkImagePlaneMemoryRequirementsInfo *plane_info =
      vk_find_struct_const(pInfo->pNext, IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO);
   const bool disjoint = is_disjoint(image);
   const VkImageAspectFlags aspects =
      plane_info ? plane_info->planeAspect : image->vk.aspects;
   uint8_t plane = panvk_plane_index(image, aspects);
   const uint64_t size_non_sparse =
      disjoint ? image->planes[plane].plane.layout.data_size_B :
      panvk_image_get_total_size(image);
   const uint64_t size =
      image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR
         ? align64(size_non_sparse, alignment)
         : size_non_sparse;

   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      BITFIELD_MASK(phys_dev->memory.type_count);
   pMemoryRequirements->memoryRequirements.alignment = alignment;
   pMemoryRequirements->memoryRequirements.size = size;

   vk_foreach_struct(sType, ext, pMemoryRequirements->pNext) {
      switch (sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *dedicated = ext;
         dedicated->requiresDedicatedAllocation =
            vk_image_is_android_hardware_buffer(&image->vk);
         dedicated->prefersDedicatedAllocation = dedicated->requiresDedicatedAllocation;
         break;
      }
      default:
         vk_debug_ignored_stype(sType);
         break;
      }
   }

   if (image->vk.create_flags &
       VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT) {
      for (uint32_t i = 0; i < ARRAY_SIZE(image->ms_imgs); ++i) {
         if (image->ms_imgs[i] == VK_NULL_HANDLE)
            continue;

         VkImageMemoryRequirementsInfo2 info = *pInfo;
         info.image = image->ms_imgs[i];

         VkMemoryRequirements2 sub_reqs_2 = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            .memoryRequirements = {},
         };
         panvk_GetImageMemoryRequirements2(device, &info, &sub_reqs_2);
         append_ms_to_ss_memory_reqs(pMemoryRequirements, &sub_reqs_2);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetDeviceImageMemoryRequirements(VkDevice device,
                                       const VkDeviceImageMemoryRequirements *pInfo,
                                       VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_device, dev, device);

   /* Make a copy so we can turn off the ms2ss flag. */
   VkDeviceImageMemoryRequirements info = *pInfo;
   VkImageCreateInfo create_info = *(pInfo->pCreateInfo);
   VkImageCreateFlags2CreateInfoKHR flags2_info;
   drop_ms2ss_flag(&create_info, &flags2_info);
   info.pCreateInfo = &create_info;

   struct panvk_image image = {0};
   vk_image_init(&dev->vk, &image.vk, &create_info);
   panvk_image_init(&image, &create_info);

   VkImageMemoryRequirementsInfo2 info2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = panvk_image_to_handle(&image),
   };
   panvk_GetImageMemoryRequirements2(device, &info2, pMemoryRequirements);
   vk_image_finish(&image.vk);

   if (vk_image_create_flags(pInfo->pCreateInfo) &
       VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT) {
      for (uint32_t msaa_idx = 0; msaa_idx < ARRAY_SIZE(image.ms_imgs);
           ++msaa_idx) {
         /* idx 0 has sample count 2, 1 has sample count 4, ... */
         create_info.samples = 1 << (msaa_idx + 1);

         VkMemoryRequirements2 msaa_reqs = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            .memoryRequirements = {},
         };
         panvk_GetDeviceImageMemoryRequirements(device, &info, &msaa_reqs);
         append_ms_to_ss_memory_reqs(pMemoryRequirements, &msaa_reqs);
      }
   }
}

/* Sparse block extents, in texel blocks, single sample.
 * Indexed by log2(texel block size in bytes).
 * See Vulkan spec, 35.4.3. Standard Sparse Image Block Shapes for details. */
static struct VkExtent3D standard_sparse_2d_blocks[] = {
   /*  1 */ {256, 256, 1},
   /*  2 */ {256, 128, 1},
   /*  4 */ {128, 128, 1},
   /*  8 */ {128, 64, 1},
   /* 16 */ {64, 64, 1},
};

struct panvk_sparse_block_desc
panvk_get_sparse_block_desc(VkImageType type, VkFormat format)
{
   const struct util_format_description *fmt_desc = vk_format_description(format);

   uint32_t texel_block_size_B = fmt_desc->block.bits / 8;

   switch (type) {
   case VK_IMAGE_TYPE_2D:
   case VK_IMAGE_TYPE_3D: {
      if (!util_is_power_of_two_nonzero(texel_block_size_B))
         break;

      uint32_t texel_block_size_B_log2 = util_logbase2(texel_block_size_B);
      if (texel_block_size_B_log2 >= ARRAY_SIZE(standard_sparse_2d_blocks))
         break;
      struct VkExtent3D extent = standard_sparse_2d_blocks[texel_block_size_B_log2];

      assert(extent.width * extent.height * extent.depth * texel_block_size_B == STANDARD_SPARSE_BLOCK_SIZE_B);

      extent.width *= fmt_desc->block.width;
      extent.height *= fmt_desc->block.height;
      extent.depth *= fmt_desc->block.depth;

      return (struct panvk_sparse_block_desc){
         .extent = extent,
         .size_B = STANDARD_SPARSE_BLOCK_SIZE_B,
         .standard = type == VK_IMAGE_TYPE_2D,
      };
   }

   default:
      break;
   }

   return (struct panvk_sparse_block_desc){};
}

VkSparseImageFormatProperties
panvk_get_sparse_image_fmt_props(VkImageType type, VkFormat format)
{
   struct panvk_sparse_block_desc sblock_desc = panvk_get_sparse_block_desc(type, format);
   if (!panvk_sparse_block_is_valid(sblock_desc))
      return (VkSparseImageFormatProperties){};

   VkSparseImageFormatProperties fmt_props = {};
   fmt_props.aspectMask = vk_format_aspects(format);
   fmt_props.imageGranularity = sblock_desc.extent;
   if (!sblock_desc.standard)
      fmt_props.flags |= VK_SPARSE_IMAGE_FORMAT_NONSTANDARD_BLOCK_SIZE_BIT;
   return fmt_props;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetImageSparseMemoryRequirements2(
   VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_image, image, pInfo->image);
   VK_OUTARRAY_MAKE_TYPED(VkSparseImageMemoryRequirements2, out, pSparseMemoryRequirements,
                          pSparseMemoryRequirementCount);

   if (!(image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_RESIDENCY_BIT_KHR))
      return;

   /* We only support single-plane images right now. See
    * https://gitlab.freedesktop.org/panfrost/mesa/-/issues/243 details. */
   unsigned plane_idx = 0;

   struct panvk_sparse_block_desc sblock = panvk_get_sparse_block_desc(image->vk.image_type, image->vk.format);
   assert(panvk_sparse_block_is_valid(sblock));

   struct panvk_image_plane *plane = &image->planes[plane_idx];

   unsigned mip_tail_first_lod = 0;
   uint64_t mip_tail_begin = 0;
   for (unsigned level = 0; level < plane->image.props.nr_slices; level++) {
      uint64_t offset = plane->plane.layout.slices[level].offset_B;
      if (!util_is_aligned(offset, sblock.size_B))
         break;
      mip_tail_first_lod = level;
      mip_tail_begin = offset;
   }

   uint64_t mip_tail_end = plane->plane.layout.array_stride_B;

   vk_outarray_append_typed(VkSparseImageMemoryRequirements2, &out, p) {
      p->memoryRequirements = (VkSparseImageMemoryRequirements){
         .formatProperties = panvk_get_sparse_image_fmt_props(image->vk.image_type, image->vk.format),
         .imageMipTailFirstLod = mip_tail_first_lod,
         .imageMipTailSize = mip_tail_end - mip_tail_begin,
         .imageMipTailOffset = mip_tail_begin,
         .imageMipTailStride = plane->plane.layout.array_stride_B,
      };
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetDeviceImageSparseMemoryRequirements(VkDevice device,
                                             const VkDeviceImageMemoryRequirements *pInfo,
                                             uint32_t *pSparseMemoryRequirementCount,
                                             VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_device, dev, device);

   struct panvk_image image = {0};
   vk_image_init(&dev->vk, &image.vk, pInfo->pCreateInfo);
   panvk_image_init(&image, pInfo->pCreateInfo);

   VkImageSparseMemoryRequirementsInfo2 info2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2,
      .image = panvk_image_to_handle(&image),
   };
   panvk_GetImageSparseMemoryRequirements2(device, &info2,
      pSparseMemoryRequirementCount, pSparseMemoryRequirements);
   vk_image_finish(&image.vk);
}

static VkResult panvk_image_bind(struct panvk_device *dev,
                                 const VkBindImageMemoryInfo *bind_info);

static void
bind_ms_images(struct panvk_device *dev, const VkBindImageMemoryInfo *bind_info)
{
   VK_FROM_HANDLE(panvk_image, image, bind_info->image);

   uint64_t total_size = 0;
   {
      VkImageMemoryRequirementsInfo2 reqs_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
         .image = bind_info->image,
      };
      VkMemoryRequirements2 reqs2 = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
         .memoryRequirements = {},
      };
      panvk_GetImageMemoryRequirements2(panvk_device_to_handle(dev), &reqs_info,
                                        &reqs2);

      total_size = reqs2.memoryRequirements.size;
   }

   uint64_t sub_sz[ARRAY_SIZE(image->ms_imgs)];
   uint64_t sub_al[ARRAY_SIZE(image->ms_imgs)];

   for (uint32_t i = 0; i < ARRAY_SIZE(image->ms_imgs); ++i) {
      if (image->ms_imgs[i] == VK_NULL_HANDLE) {
         sub_sz[i] = 0;
         sub_al[i] = 1;
         continue;
      }

      VkImageMemoryRequirementsInfo2 reqs_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
         .image = image->ms_imgs[i],
      };
      VkMemoryRequirements2 reqs2 = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
         .memoryRequirements = {},
      };
      panvk_GetImageMemoryRequirements2(panvk_device_to_handle(dev), &reqs_info,
                                        &reqs2);

      sub_sz[i] = reqs2.memoryRequirements.size;
      sub_al[i] = reqs2.memoryRequirements.alignment;
   }

   /*
    *           sub_imgs_aligned_size
    *        ----------------------------
    * [ base, sub_0, sub_1, sub_2, sub_3 ]
    *  -->-> -->->       ...      ------>
    *  sz a  sz a                size only
    */
   uint64_t sub_imgs_aligned_size = 0;
   for (uint32_t i = 0; i < ARRAY_SIZE(image->ms_imgs); ++i) {
      sub_imgs_aligned_size += sub_sz[i];
      if (i < ARRAY_SIZE(image->ms_imgs) - 1) {
         sub_imgs_aligned_size = align(sub_imgs_aligned_size, sub_al[i + 1]);
      }
   }

   uint64_t sub_image_offset =
      bind_info->memoryOffset + total_size - sub_imgs_aligned_size;

   for (uint32_t i = 0; i < ARRAY_SIZE(image->ms_imgs); ++i) {
      if (image->ms_imgs[i] == VK_NULL_HANDLE)
         continue;

      sub_image_offset = align(sub_image_offset, sub_al[i]);

      VkBindImageMemoryInfo sub_bind_info = {
         .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
         .image = image->ms_imgs[i],
         .memory = bind_info->memory,
         .memoryOffset = sub_image_offset,
      };

      ASSERTED const VkResult res = panvk_image_bind(dev, &sub_bind_info);
      assert(res == VK_SUCCESS);

      sub_image_offset += sub_sz[i];
   }
}

static VkResult
panvk_image_bind(struct panvk_device *dev,
                 const VkBindImageMemoryInfo *bind_info)
{
   VK_FROM_HANDLE(panvk_image, image, bind_info->image);
   VK_FROM_HANDLE(panvk_device_memory, mem, bind_info->memory);
   uint64_t offset = bind_info->memoryOffset;

   assert(!(image->vk.create_flags & VK_IMAGE_CREATE_2_SPARSE_BINDING_BIT_KHR));

   if (!mem) {
      VkDeviceMemory mem_handle;
#ifdef VK_USE_PLATFORM_ANDROID_KHR
      VkResult result =
         panvk_android_get_wsi_memory(dev, bind_info, &mem_handle);
      if (result != VK_SUCCESS)
         return result;
#else
      const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
         vk_find_struct_const(bind_info->pNext,
                              BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);
      assert(swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE);
      mem_handle = wsi_common_get_memory(swapchain_info->swapchain,
                                         swapchain_info->imageIndex);
#endif
      mem = panvk_device_memory_from_handle(mem_handle);
      offset = 0;
   }

   assert(mem);
   if (is_disjoint(image)) {
      const VkBindImagePlaneMemoryInfo *plane_info =
         vk_find_struct_const(bind_info->pNext, BIND_IMAGE_PLANE_MEMORY_INFO);
      const uint8_t plane =
         panvk_plane_index(image, plane_info->planeAspect);
      panvk_image_plane_bind_mem(dev, &image->planes[plane], mem, offset);
      /* Disjoint planes are bound one per call, so report just this one to
       * avoid re-emitting a BIND for planes bound by earlier calls.
       */
      panvk_address_binding_report(dev, &image->vk.base,
                                   image->planes[plane].plane.base,
                                   panvk_image_plane_size(image, plane),
                                   VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   } else {
      for (unsigned plane = 0; plane < image->plane_count; plane++)
         panvk_image_plane_bind_mem(dev, &image->planes[plane], mem, offset);
      if (image->bc_raw)
         panvk_bc_raw_rebind(image);
      if (image->bc_emul) {
         const uint64_t dec_offset = offset + image->bc_decoded_offset;

         panvk_image_plane_bind_mem(dev, &image->bc_decoded, mem, dec_offset);
         if (image->bc_shadow) {
            panvk_image_plane_bind_mem(
               dev, &panvk_image_from_handle(image->bc_shadow)->planes[0], mem,
               dec_offset);
         }
      }
      panvk_image_report_binding(dev, image,
                                 VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   }

   if (!!(image->vk.create_flags &
          VK_IMAGE_CREATE_2_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT))
      bind_ms_images(dev, bind_info);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_BindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                       const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(panvk_device, dev, device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < bindInfoCount; i++) {
      const VkBindMemoryStatus *bind_status =
         vk_find_struct_const(&pBindInfos[i], BIND_MEMORY_STATUS);
      VkResult bind_result = panvk_image_bind(dev, &pBindInfos[i]);
      if (bind_status)
         *bind_status->pResult = bind_result;
      if (bind_result != VK_SUCCESS)
         result = bind_result;
   }

   return result;
}

/* ---- BC raw store (csf-v11/201) ----
 *
 * Vulkan requires the raw BC blocks back from image -> buffer copies and
 * from BC-source image copies, so a BC image with TRANSFER_SRC can not
 * just drop them (DXVK sets TRANSFER_SRC on every texture). Instead of a
 * plane in the image memory, such an image gets a panvk_bc_raw:
 *
 *  - the blocks live in a driver BO (CPU-cached mapping) while command
 *    buffers that touch the raw blocks exist. Every such command buffer
 *    takes a reference at record time (panvk_bc_raw_acquire()) and drops it
 *    when it is reset or freed, i.e. when it can no longer be pending, so
 *    the BO is never freed under queued GPU work;
 *  - once no command buffer references it, a worker thread writes the
 *    blocks to an unlinked spill file and frees the BO. A later raw access
 *    reads them back at record time into a new BO (an image that never got
 *    blocks gets a zeroed one). The GPU always sees one BO per image, so
 *    raw reads and writes keep Vulkan's ordering whatever the submission
 *    order of the command buffers.
 *
 * The spill copy is only committed if no acquire happened while the file
 * was written (generation count), and a store read back from the file
 * twice stays resident from then on (no ping-pong for images that are
 * updated or copied from all the time). Without a usable spill directory
 * ($PANVK_BC_RAW_DIR, $TMPDIR, the Android app cache directory,
 * /data/local/tmp, /tmp; the one used is logged) stores stay resident,
 * which costs what the in-image raw plane did.
 */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "util/detect_os.h"
#include "util/u_android_app_dir.h"
#include "bc/panvk_bc_eac_lut.h"

/* csf-v11/206: the EAC endpoint pair table data lives in this one
 * (non-per-arch) object. */
void
panvk_bc_eac_expand_table(int kind, uint32_t *out)
{
   panvk_bc_eac_expand(kind, out);
}
#include "util/u_queue.h"
#include "panvk_priv_bo.h"

struct panvk_bc_raw {
   struct panvk_device *dev;
   struct panvk_image *image; /* NULL once the image is destroyed */
   uint64_t size;
   struct panvk_priv_bo *bo;
   uint32_t refs;     /* image + command buffers + a queued spill job */
   uint32_t cmd_refs; /* command buffers that reference bo */
   uint32_t gen;      /* bumped by every acquire */
   uint32_t remat;    /* times read back from the spill file */
   bool written;      /* blocks were ever written (else all zero) */
   bool on_disk;      /* the spill file holds the blocks */
   bool pinned;       /* never spilled again */
   bool spill_queued;
   int64_t file_off;  /* -1: no space in the spill file yet */
};

static simple_mtx_t bc_raw_lock = SIMPLE_MTX_INITIALIZER;

static uint64_t
bc_raw_plane_size(const struct panvk_image *image)
{
   const struct pan_image_layout *l = &image->planes[0].plane.layout;
   return l->slices[0].offset_B + l->data_size_B;
}

static VkResult
panvk_bc_raw_create(struct panvk_device *dev, struct panvk_image *image)
{
   struct panvk_bc_raw *raw =
      vk_zalloc(&dev->vk.alloc, sizeof(*raw), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!raw)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   raw->dev = dev;
   raw->image = image;
   raw->size = bc_raw_plane_size(image);
   raw->refs = 1;
   raw->file_off = -1;
   image->bc_raw = raw;
   /* Nothing resident until the first raw access. */
   image->planes[0].plane.base = 0;
   return VK_SUCCESS;
}

/* Called with bc_raw_lock held, when refs drops to 0. */
static void
bc_raw_free(struct panvk_bc_raw *raw)
{
   struct panvk_device *dev = raw->dev;

   if (raw->bo) {
      dev->bc_raw.resident_B -= raw->size;
      panvk_priv_bo_unref(raw->bo);
   }
   if (raw->file_off >= 0 && dev->bc_raw.fd_plus1 > 0) {
#ifdef FALLOC_FL_PUNCH_HOLE
      fallocate(dev->bc_raw.fd_plus1 - 1,
                FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, raw->file_off,
                raw->size);
#endif
      if (raw->on_disk)
         dev->bc_raw.spilled_B -= raw->size;
   }
   vk_free(&dev->vk.alloc, raw);
}

static void
panvk_bc_raw_destroy(struct panvk_image *image)
{
   struct panvk_bc_raw *raw = image->bc_raw;

   if (!raw)
      return;

   simple_mtx_lock(&bc_raw_lock);
   raw->image = NULL;
   if (!--raw->refs)
      bc_raw_free(raw);
   simple_mtx_unlock(&bc_raw_lock);
   image->bc_raw = NULL;
}

/* vkBindImageMemory set planes[0] to the image memory: point it back at the
 * store (or at nothing). */
static void
panvk_bc_raw_rebind(struct panvk_image *image)
{
   simple_mtx_lock(&bc_raw_lock);
   image->planes[0].plane.base =
      image->bc_raw->bo ? image->bc_raw->bo->addr.dev : 0;
   simple_mtx_unlock(&bc_raw_lock);
}

/* The Android app's cache directory (u_android_app_dir.h). Android app
 * processes cannot write /data/local/tmp. */
static const char *
bc_raw_app_cache(char *buf, size_t size)
{
#if DETECT_OS_ANDROID
   return u_android_app_cache_dir(buf, size);
#else
   return NULL;
#endif
}

/* Spill file: one unlinked file per device, opened at the first spill.
 * Called with bc_raw_lock held. */
static int
bc_raw_file(struct panvk_device *dev)
{
   if (dev->bc_raw.fd_plus1)
      return dev->bc_raw.fd_plus1 - 1;

   char app_cache[512];
   const char *dirs[] = {
      os_get_option("PANVK_BC_RAW_DIR"),
      getenv("TMPDIR"),
      bc_raw_app_cache(app_cache, sizeof(app_cache)),
      "/data/local/tmp",
      "/tmp",
   };
   int fd = -1;

   for (unsigned i = 0; i < ARRAY_SIZE(dirs) && fd < 0; i++) {
      if (!dirs[i] || !dirs[i][0])
         continue;
#ifdef O_TMPFILE
      fd = open(dirs[i], O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
#endif
      if (fd < 0) {
         char path[4096];
         snprintf(path, sizeof(path), "%s/panvk-bc-raw-XXXXXX", dirs[i]);
         fd = mkostemp(path, O_CLOEXEC);
         if (fd >= 0)
            unlink(path);
      }
      if (fd >= 0)
         mesa_logi("panvk: BC raw store spills to %s", dirs[i]);
   }
   if (fd < 0)
      mesa_logw("panvk: no BC raw store spill directory, raw blocks stay "
                "resident");
   dev->bc_raw.fd_plus1 = fd < 0 ? -1 : fd + 1;
   return fd;
}

static void
bc_raw_spill_job(void *data, void *gdata, int thread_index)
{
   struct panvk_bc_raw *raw = data;
   struct panvk_device *dev = raw->dev;
   struct panvk_priv_bo *bo = NULL;
   bool ok = false;
   int fd = -1;

   simple_mtx_lock(&bc_raw_lock);
   raw->spill_queued = false;
   if (raw->cmd_refs || !raw->bo || !raw->image || raw->pinned)
      goto out;

   const uint32_t gen = raw->gen;
   const bool written = raw->written;
   bo = raw->bo;
   if (written) {
      fd = bc_raw_file(dev);
      if (fd < 0) {
         raw->pinned = true;
         p_atomic_inc(&panvk_mem_stats.bc_pins);
         goto out;
      }
      if (raw->file_off < 0) {
         raw->file_off = dev->bc_raw.file_end;
         dev->bc_raw.file_end += ALIGN_POT(raw->size, 4096);
      }
   }
   const int64_t off = raw->file_off;
   panvk_priv_bo_ref(bo);
   simple_mtx_unlock(&bc_raw_lock);

   /* GPU writes are complete (no command buffer references the BO). */
   ok = true;
   if (written) {
      pan_kmod_queue_bo_map_sync(bo->bo, 0, bo->addr.host, raw->size,
                                 PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH_AND_INVALIDATE);
      pan_kmod_flush_bo_map_syncs(dev->kmod.dev);
      ok = pwrite(fd, bo->addr.host, raw->size, off) == (ssize_t)raw->size;
#ifdef SYNC_FILE_RANGE_WRITE
      /* Start the writeback and drop the page cache copy: the point is to
       * get the blocks out of RAM. */
      if (ok)
         sync_file_range(fd, off, raw->size,
                         SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE |
                            SYNC_FILE_RANGE_WAIT_AFTER);
#endif
      if (ok)
         posix_fadvise(fd, off, raw->size, POSIX_FADV_DONTNEED);
   }

   simple_mtx_lock(&bc_raw_lock);
   panvk_priv_bo_unref(bo);
   if (!ok) {
      mesa_logw("panvk: BC raw store spill failed (%s), keeping it resident",
                strerror(errno));
      raw->pinned = true;
      p_atomic_inc(&panvk_mem_stats.bc_pins);
   } else if (raw->gen == gen && !raw->cmd_refs && raw->image &&
              raw->bo == bo) {
      raw->bo = NULL;
      raw->image->planes[0].plane.base = 0;
      dev->bc_raw.resident_B -= raw->size;
      if (written && !raw->on_disk)
         dev->bc_raw.spilled_B += raw->size;
      if (!written && raw->on_disk)
         dev->bc_raw.spilled_B -= raw->size;
      raw->on_disk = written;
      panvk_priv_bo_unref(bo);
      p_atomic_inc(&panvk_mem_stats.bc_spills);
   }
out:
   if (!--raw->refs)
      bc_raw_free(raw);
   simple_mtx_unlock(&bc_raw_lock);
}

/* csf-v11 225: a store read back from the spill file this many times stays
 * resident for good. DXVK touches streamed textures (uploads per mip,
 * relocation copies) in several command lists, so the old limit of 2
 * pinned most raw stores of a long session in memory.
 * PANVK_BC_RAW_PIN=<n> (default 0 = never pin; 2 = old). */
static uint32_t
bc_raw_pin_after(void)
{
   static int64_t v = -1;

   if (p_atomic_read(&v) < 0) {
      int64_t n = debug_get_num_option("PANVK_BC_RAW_PIN", 0);
      p_atomic_set(&v, n <= 0 ? (int64_t)UINT32_MAX : MIN2(n, UINT32_MAX));
   }
   return (uint32_t)p_atomic_read(&v);
}

/* Called with bc_raw_lock held. full: the caller overwrites every block,
 * so the old contents are neither read back nor zeroed. */
static VkResult
bc_raw_materialize(struct panvk_bc_raw *raw, bool full)
{
   struct panvk_device *dev = raw->dev;
   struct panvk_priv_bo *bo;
   VkResult result = panvk_priv_bo_create_kind(
      dev, raw->size, panvk_device_adjust_bo_flags(dev, PAN_KMOD_BO_FLAG_WB_MMAP),
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &bo, PANVK_MEM_BC_RAW);

   if (result != VK_SUCCESS)
      return result;

   bool from_file = false;
   if (full) {
      /* The GPU copy writes every block: no CPU write to flush. */
      raw->bo = bo;
      dev->bc_raw.resident_B += raw->size;
      return VK_SUCCESS;
   }
   if (raw->on_disk && dev->bc_raw.fd_plus1 > 0) {
      from_file = pread(dev->bc_raw.fd_plus1 - 1, bo->addr.host, raw->size,
                        raw->file_off) == (ssize_t)raw->size;
      if (!from_file) {
         mesa_loge("panvk: BC raw store read-back failed (%s)",
                   strerror(errno));
      } else {
         p_atomic_inc(&panvk_mem_stats.bc_remats);
         if (++raw->remat >= bc_raw_pin_after()) {
            raw->pinned = true;
            p_atomic_inc(&panvk_mem_stats.bc_pins);
         }
      }
   }
   if (!from_file)
      memset(bo->addr.host, 0, raw->size);
   pan_kmod_queue_bo_map_sync(bo->bo, 0, bo->addr.host, raw->size,
                              PAN_KMOD_BO_SYNC_CPU_CACHE_FLUSH);
   pan_kmod_flush_bo_map_syncs(dev->kmod.dev);

   raw->bo = bo;
   dev->bc_raw.resident_B += raw->size;
   return VK_SUCCESS;
}

VkResult
panvk_bc_raw_acquire(struct panvk_image *image, struct util_dynarray *refs,
                     bool write, bool full)
{
   struct panvk_bc_raw *raw = image->bc_raw;
   VkResult result = VK_SUCCESS;

   simple_mtx_lock(&bc_raw_lock);
   if (!raw->bo) {
      result = bc_raw_materialize(raw, write && full);
      if (result != VK_SUCCESS)
         goto out;
   }
   raw->gen++;
   if (write)
      raw->written = true;

   bool found = false;
   util_dynarray_foreach(refs, struct panvk_bc_raw *, r)
      found = found || *r == raw;
   if (!found) {
      util_dynarray_append_typed(refs, struct panvk_bc_raw *, raw);
      raw->cmd_refs++;
      raw->refs++;
   }
   image->planes[0].plane.base = raw->bo->addr.dev;
out:
   simple_mtx_unlock(&bc_raw_lock);
   return result;
}

void
panvk_bc_raw_release(struct util_dynarray *refs)
{
   if (!util_dynarray_num_elements(refs, struct panvk_bc_raw *))
      return;

   simple_mtx_lock(&bc_raw_lock);
   util_dynarray_foreach(refs, struct panvk_bc_raw *, r) {
      struct panvk_bc_raw *raw = *r;
      struct panvk_device *dev = raw->dev;

      raw->cmd_refs--;
      if (!--raw->refs) {
         bc_raw_free(raw);
         continue;
      }
      if (raw->cmd_refs || !raw->image || !raw->bo || raw->pinned ||
          raw->spill_queued)
         continue;

      if (!dev->bc_raw.queue) {
         struct util_queue *q =
            vk_zalloc(&dev->vk.alloc, sizeof(*q), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
         if (!q || !util_queue_init(q, "panvk_bcraw", 256, 1,
                                    UTIL_QUEUE_INIT_RESIZE_IF_FULL |
                                       UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY,
                                    NULL)) {
            vk_free(&dev->vk.alloc, q);
            raw->pinned = true;
            p_atomic_inc(&panvk_mem_stats.bc_pins);
            continue;
         }
         dev->bc_raw.queue = q;
      }
      raw->spill_queued = true;
      raw->refs++;
      util_queue_add_job(dev->bc_raw.queue, raw, NULL,
                         bc_raw_spill_job, NULL, 0);
   }
   simple_mtx_unlock(&bc_raw_lock);
   util_dynarray_clear(refs);
}

void
panvk_bc_raw_device_finish(struct panvk_device *dev)
{
   if (dev->bc_raw.queue) {
      util_queue_finish(dev->bc_raw.queue);
      util_queue_destroy(dev->bc_raw.queue);
      vk_free(&dev->vk.alloc, dev->bc_raw.queue);
      dev->bc_raw.queue = NULL;
   }
   if (dev->bc_raw.fd_plus1 > 0)
      close(dev->bc_raw.fd_plus1 - 1);
   dev->bc_raw.fd_plus1 = 0;
}
