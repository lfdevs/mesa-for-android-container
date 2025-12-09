/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
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

#include "pan_afbc.h"
#include "pan_props.h"

#include "panvk_android.h"
#include "panvk_device.h"
#include "panvk_device_memory.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"
#include "panvk_sparse.h"

#include "drm-uapi/drm_fourcc.h"
#include "util/u_atomic.h"
#include "util/u_debug.h"
#include "util/u_drm.h"

#include "vk_format.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_util.h"

bool
panvk_image_can_use_afbc(
   struct panvk_physical_device *phys_dev, VkFormat fmt,
   VkImageUsageFlags usage, VkImageType type, VkImageTiling tiling,
   VkImageCreateFlags flags)
{
   unsigned arch = pan_arch(phys_dev->kmod.props.gpu_id);
   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);

   /* Disallow AFBC if either of these is true
    * - PANVK_DEBUG does not have the 'afbc' flag set
    * - storage image views are requested
    * - host image copies are requested
    * - the GPU doesn't support AFBC
    * - the format is not AFBC-able
    * - tiling is set to linear
    * - this is a 1D image
    * - this is a 3D image on a pre-v7 GPU
    * - this is a mutable format image on v7- (format re-interpretation is
    *   not possible on Bifrost hardware)
    * - this is a sparse image
    *
    * Some of these checks are redundant with tests provided by the AFBC mod
    * handler when pan_image_test_props() is called, but we need them because
    * panvk_image_can_use_afbc() is also called from
    * GetPhysicalDeviceImageFormatProperties2() and we don't have enough
    * information to conduct a full image property check in this context.
    */
   return !PANVK_DEBUG(NO_AFBC) &&
          !(usage &
            (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT)) &&
          pan_query_afbc(&phys_dev->kmod.props) &&
          pan_afbc_supports_format(arch, pfmt) &&
          tiling != VK_IMAGE_TILING_LINEAR && type != VK_IMAGE_TYPE_1D &&
          (type != VK_IMAGE_TYPE_3D || arch >= 7) &&
          (!(flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) || arch >= 9) &&
          (!(flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT));
}

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
       (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
      iusage.bind |= PAN_BIND_SAMPLER_VIEW;

   if (image->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT)
      iusage.bind |= PAN_BIND_STORAGE_IMAGE;

   if (image->vk.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
      iusage.bind |= PAN_BIND_DEPTH_STENCIL;

   if (image->vk.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      iusage.bind |= PAN_BIND_RENDER_TARGET;

   iusage.host_copy =
      !!(image->vk.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT);
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
   unsigned arch = pan_arch(phys_dev->kmod.props.gpu_id);

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
   if (panvk_image_is_planar_depth_stencil(image)) {
      return plane > 0 ? select_stencil_plane_pfmt(image)
                       : select_depth_plane_pfmt(image, mod);
   }

   VkFormat plane_format = vk_format_get_plane_format(image->vk.format, plane);
   return vk_format_to_pipe_format(plane_format);
}

static bool
panvk_image_can_use_mod(struct panvk_image *image,
                        const struct pan_image_usage *iusage, uint64_t mod,
                        bool optimal_only)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   unsigned arch = pan_arch(phys_dev->kmod.props.gpu_id);
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

      /* Can't do AFBC if store/host copy is requested. */
      if ((image->vk.usage | image->vk.stencil_usage) &
          (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT))
         return false;

      /* Can't do AFBC on v7- if mutable format is requested. */
      if ((image->vk.create_flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
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
      if ((image->vk.create_flags & VK_IMAGE_CREATE_ALIAS_BIT) &&
          (image->vk.create_flags & VK_IMAGE_CREATE_DISJOINT_BIT))
         return false;
   }

   if (mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
      /* Multiplanar YUV with U-interleaving isn't supported by the HW. We
       * also need to make sure images that can be aliased to planes of
       * multi-planar images remain compatible with the aliased images, so
       * don't allow U-interleaving for those either.
       */
      if (vk_format_get_plane_count(image->vk.format) > 1 ||
          vk_image_can_be_aliased_to_yuv_plane(&image->vk))
         return false;

      /* If we're dealing with a compressed format that requires non-compressed
       * views we can't use U_INTERLEAVED tiling because the tiling is different
       * between compressed and non-compressed formats. If we wanted to support
       * format re-interpretation we would have to specialize the shaders
       * accessing non-compressed image views (coordinate patching for
       * sampled/storage image, frag_coord patching for color attachments). Let's
       * keep things simple for now and make all compressed images that
       * have VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT set linear. */
      return !(image->vk.create_flags &
               VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT);
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
         pan_image_test_props(&phys_dev->kmod.props, &iprops, iusage);
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
   assert(image->vk.array_layers == 1);
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
   if (iusage.legacy_scanout)
      return DRM_FORMAT_MOD_LINEAR;

   /* Without external dependencies, pick the best modifier that supports the image. */
   return panvk_image_get_mod_from_list(image, &iusage, NULL, 0);
}

static bool
is_disjoint(const struct panvk_image *image)
{
   assert((image->plane_count > 1 &&
           !vk_format_is_depth_or_stencil(image->vk.format)) ||
          (image->vk.create_flags & VK_IMAGE_CREATE_ALIAS_BIT) ||
          !(image->vk.create_flags & VK_IMAGE_CREATE_DISJOINT_BIT));
   return image->vk.create_flags & VK_IMAGE_CREATE_DISJOINT_BIT;
}

static bool
strict_import(struct panvk_image *image, uint32_t plane)
{
   /* We can't do strict imports for AFBC because a Vulkan-based compositor
    * might be importing buffers from clients that are relying on the old
    * behavior. The only exception is AFBC(YUV) because support for these
    * formats was added after we started enforcing WSI pitch. */
   if (drm_is_afbc(image->vk.drm_format_mod) &&
       !pan_format_is_yuv(image->planes[plane].image.props.format))
      return false;

   return true;
}

static VkResult
panvk_image_init_layouts(struct panvk_image *image,
                         const VkImageCreateInfo *pCreateInfo)
{
   struct panvk_device *dev = to_panvk_device(image->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   unsigned arch = pan_arch(phys_dev->kmod.props.gpu_id);
   const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_info =
      vk_find_struct_const(
         pCreateInfo->pNext,
         IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

   const struct pan_mod_handler *mod_handler =
      pan_mod_get_handler(arch, image->vk.drm_format_mod);
   struct pan_image_layout_constraints plane_layout = {
      .offset_B = 0,
   };
   if (pCreateInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) {
      plane_layout.array_align_B = panvk_get_sparse_block_desc(pCreateInfo->imageType, pCreateInfo->format).size_B;
      plane_layout.u_tiled.row_align_B = panvk_get_gpu_page_size(dev);
   }
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      enum pipe_format pfmt =
         select_plane_pfmt(image, image->vk.drm_format_mod, plane);

      if (explicit_info) {
         plane_layout = (struct pan_image_layout_constraints){
            .offset_B = explicit_info->pPlaneLayouts[plane].offset,
            .wsi_row_pitch_B = explicit_info->pPlaneLayouts[plane].rowPitch,
         };
      }

      image->planes[plane].image = (struct pan_image){
         .props = {
            .modifier = image->vk.drm_format_mod,
            .format = pfmt,
            .dim = panvk_image_type_to_mali_tex_dim(image->vk.image_type),
            .extent_px = {
               .width = vk_format_get_plane_width(image->vk.format, plane,
                                                  image->vk.extent.width),
               .height = vk_format_get_plane_height(image->vk.format, plane,
                                                    image->vk.extent.height),
               .depth = image->vk.extent.depth,
            },
            .array_size = image->vk.array_layers,
            .nr_samples = image->vk.samples,
            .nr_slices = image->vk.mip_levels,
         },
         .mod_handler = mod_handler,
         .planes = {&image->planes[plane].plane},
      };

      plane_layout.strict = strict_import(image, plane);
      if (!pan_image_layout_init(arch, &image->planes[plane].image, 0,
                                 &plane_layout)) {
         return panvk_error(image->vk.base.device,
                            VK_ERROR_INITIALIZATION_FAILED);
      }

      if (!is_disjoint(image) && !explicit_info)
         plane_layout.offset_B += image->planes[plane].plane.layout.data_size_B;
   }

   return VK_SUCCESS;
}

static void
panvk_image_pre_mod_select_meta_adjustments(struct panvk_image *image)
{
   const VkImageAspectFlags aspects = vk_format_aspects(image->vk.format);
   const VkImageUsageFlags all_usage =
      image->vk.usage | image->vk.stencil_usage;

   /* We do image blit/resolve with vk_meta, so when an image is flagged as
    * being a potential transfer source, we also need to add the sampled usage.
    */
   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   if (image->vk.stencil_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
      image->vk.stencil_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

   /* Similarly, image that can be a transfer destination can be attached
    * as a color or depth-stencil attachment by vk_meta. */
   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         image->vk.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

      if (aspects & VK_IMAGE_ASPECT_COLOR_BIT)
         image->vk.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
   }

   if (image->vk.stencil_usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
      image->vk.stencil_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

   /* vk_meta creates 2D array views of 3D images. */
   if (all_usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT &&
       image->vk.image_type == VK_IMAGE_TYPE_3D)
      image->vk.create_flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;

   /* Needed for resolve operations. */
   if (image->vk.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

   if (image->vk.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT &&
       aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

   if (image->vk.stencil_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
      image->vk.stencil_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

   if ((image->vk.usage &
        (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) &&
       vk_format_is_compressed(image->vk.format)) {
      /* We need to be able to create RGBA views of compressed formats for
       * vk_meta copies. */
      image->vk.create_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                                VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
   }
}

static void
panvk_image_post_mod_select_meta_adjustments(struct panvk_image *image)
{
   const VkImageAspectFlags aspects = vk_format_aspects(image->vk.format);

   /* If the image didn't end up using AFBC, we should add the storage flag
    * to allow vkmeta to take the compute based copying path. */
   if ((image->vk.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) &&
       (aspects & VK_IMAGE_ASPECT_COLOR_BIT) &&
       !drm_is_afbc(image->vk.drm_format_mod)) {
      image->vk.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
   }
}

static uint64_t
panvk_image_get_total_size(const struct panvk_image *image)
{
   uint64_t size = 0;
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      const struct pan_image_layout *layout =
         &image->planes[plane].plane.layout;
      size = MAX2(size, layout->slices[0].offset_B + layout->data_size_B);
   }
   return size;
}

static uint64_t
panvk_image_get_sparse_size(const struct panvk_image *image)
{
   struct panvk_device *device = to_panvk_device(image->vk.base.device);
   uint64_t image_size = panvk_image_get_total_size(image);
   uint64_t page_size = panvk_get_gpu_page_size(device);
   return ALIGN_POT(image_size, page_size);
}

VkResult
panvk_image_init(struct panvk_image *image,
                 const VkImageCreateInfo *pCreateInfo)
{
   /* Needs to happen early for some panvk_image_ helpers to work. */
   image->plane_count = get_plane_count(image);

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
}

static void
panvk_image_plane_bind_addr(struct panvk_device *dev,
                            struct panvk_image_plane *plane,
                            uint64_t addr)
{
   plane->plane.base = addr;
}

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

   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(&phys_dev->wsi_device,
                                               pCreateInfo,
                                               swapchain_info->swapchain,
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
   if (size > UINT32_MAX) {
      result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto err_destroy_image;
   }

   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      uint64_t va_range = panvk_image_get_sparse_size(image);

      image->sparse.device_address = panvk_as_alloc(dev, va_range,
         pan_choose_gpu_va_alignment(dev->kmod.vm, va_range));
      if (!image->sparse.device_address) {
         result = panvk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_destroy_image;
      }

      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         panvk_image_plane_bind_addr(dev, &image->planes[plane],
                                     image->sparse.device_address);
      }

      if ((image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) ||
          PANVK_DEBUG(FORCE_BLACKHOLE)) {
         /* Map last so that we don't have a possibility of getting any more
          * errors, in which case we'd have to unmap.
          */
         result = panvk_map_to_blackhole(dev, image->sparse.device_address,
                                         va_range);
         if (result != VK_SUCCESS) {
            result = panvk_error(dev, result);
            goto err_free_va;
         }
      }
   }

   *pImage = panvk_image_to_handle(image);
   return VK_SUCCESS;

err_free_va:
   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT)
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

   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      uint64_t va_range = panvk_image_get_sparse_size(image);

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
   layout->size = slice_layout->size_B;
   layout->arrayPitch = image->planes[plane].plane.layout.array_stride_B;

   if (drm_is_afbc(image->vk.drm_format_mod)) {
      /* row/depth pitch expressed in (AFBC superblocks * payload size). */
      layout->rowPitch = pan_image_get_wsi_row_pitch(
         &image->planes[plane].image, plane, subres->mipLevel);
      layout->depthPitch = slice_layout->afbc.surface_stride_B;
   } else {
      layout->rowPitch = slice_layout->tiled_or_linear.row_stride_B;
      layout->depthPitch = slice_layout->tiled_or_linear.surface_stride_B;
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

   assert(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT);

   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)
      return panvk_get_sparse_block_desc(image->vk.image_type, image->vk.format).size_B;

   return panvk_get_gpu_page_size(dev);
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetImageMemoryRequirements2(VkDevice device,
                                  const VkImageMemoryRequirementsInfo2 *pInfo,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_image, image, pInfo->image);

   /* For sparse resources alignment specifies binding granularity, rather than
    * the alignment requirement. It's up to us to satisfy the alignment
    * requirement when allocating the VA range.
    */
   const uint64_t alignment =
      image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT
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
      image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT
         ? align64(size_non_sparse, alignment)
         : size_non_sparse;

   pMemoryRequirements->memoryRequirements.memoryTypeBits = 1;
   pMemoryRequirements->memoryRequirements.alignment = alignment;
   pMemoryRequirements->memoryRequirements.size = size;

   vk_foreach_struct_const(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *dedicated = (void *)ext;
         dedicated->requiresDedicatedAllocation =
            vk_image_is_android_hardware_buffer(&image->vk);
         dedicated->prefersDedicatedAllocation = dedicated->requiresDedicatedAllocation;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetDeviceImageMemoryRequirements(VkDevice device,
                                       const VkDeviceImageMemoryRequirements *pInfo,
                                       VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_device, dev, device);

   struct panvk_image image;
   vk_image_init(&dev->vk, &image.vk, pInfo->pCreateInfo);
   panvk_image_init(&image, pInfo->pCreateInfo);

   VkImageMemoryRequirementsInfo2 info2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = panvk_image_to_handle(&image),
   };
   panvk_GetImageMemoryRequirements2(device, &info2, pMemoryRequirements);
   vk_image_finish(&image.vk);
}

/* See Vulkan spec, 35.4.3. Standard Sparse Image Block Shapes for details */
enum {
   STANDARD_SPARSE_BLOCK_SIZE_B = 65536,
};

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
   case VK_IMAGE_TYPE_2D: {
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
         .standard = true,
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

   if (!(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
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

   struct panvk_image image;
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

static VkResult
panvk_image_bind(struct panvk_device *dev,
                 const VkBindImageMemoryInfo *bind_info)
{
   VK_FROM_HANDLE(panvk_image, image, bind_info->image);
   VK_FROM_HANDLE(panvk_device_memory, mem, bind_info->memory);
   uint64_t offset = bind_info->memoryOffset;

   assert(!(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT));

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
   } else {
      for (unsigned plane = 0; plane < image->plane_count; plane++)
         panvk_image_plane_bind_mem(dev, &image->planes[plane], mem, offset);
   }

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
