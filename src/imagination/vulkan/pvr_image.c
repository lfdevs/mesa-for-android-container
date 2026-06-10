/*
 * Copyright © 2022 Imagination Technologies Ltd.
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

#include "pvr_image.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "drm-uapi/drm_fourcc.h"
#include "pvr_buffer.h"
#include "pvr_device.h"
#include "pvr_device_info.h"
#include "pvr_entrypoints.h"
#include "pvr_formats.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"
#include "pvr_tex_state.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "vk_format.h"
#include "vk_image.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_util.h"
#include "wsi_common.h"

static void pvr_image_init_memlayout(struct pvr_image *image)
{
   switch (image->vk.tiling) {
   default:
      UNREACHABLE("bad VkImageTiling");
   case VK_IMAGE_TILING_OPTIMAL:
      if (image->vk.wsi_legacy_scanout ||
          vk_format_get_ycbcr_info(image->vk.format))
         image->memlayout = PVR_MEMLAYOUT_LINEAR;
      else if (image->vk.image_type == VK_IMAGE_TYPE_3D)
         image->memlayout = PVR_MEMLAYOUT_3DTWIDDLED;
      else
         image->memlayout = PVR_MEMLAYOUT_TWIDDLED;
      break;
   case VK_IMAGE_TILING_LINEAR:
      image->memlayout = PVR_MEMLAYOUT_LINEAR;
      break;
   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
      /* Support only LINEAR now */
      assert(image->vk.drm_format_mod == DRM_FORMAT_MOD_LINEAR);
      image->memlayout = PVR_MEMLAYOUT_LINEAR;
      break;
   }
}

static void pvr_image_plane_init_physical_extent(
   struct pvr_image *image,
   const VkImageCreateInfo *pCreateInfo,
   unsigned pbe_stride_align,
   const struct vk_format_ycbcr_info *ycbcr_info,
   uint8_t i)
{
   struct pvr_image_plane *plane = &image->planes[i];
   /* clang-format off */
   if (image->vk.mip_levels > 1 ||
      image->memlayout == PVR_MEMLAYOUT_TWIDDLED ||
      image->memlayout == PVR_MEMLAYOUT_3DTWIDDLED) {
      /* clang-format on */
      plane->physical_extent.width =
         util_next_power_of_two(image->vk.extent.width);
      plane->physical_extent.height =
         util_next_power_of_two(image->vk.extent.height);
      plane->physical_extent.depth =
         util_next_power_of_two(image->vk.extent.depth);
   } else {
      assert(image->memlayout == PVR_MEMLAYOUT_LINEAR);
      plane->physical_extent = image->vk.extent;

      /* Align the image for being rendered to (written by the PBE). */
      plane->physical_extent.width =
         align(plane->physical_extent.width, pbe_stride_align);

      if (image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
         const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_mod =
            vk_find_struct_const(
               pCreateInfo->pNext,
               IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
         if (explicit_mod) {
            const uint32_t bpp =
              vk_format_get_blocksize(image->vk.format);
            plane->physical_extent.width =
              explicit_mod->pPlaneLayouts[i].rowPitch / bpp;
         }
      }
   }

   if (ycbcr_info) {
      plane->physical_extent.width /=
         ycbcr_info->planes[i].denominator_scales[0];
      plane->physical_extent.height /=
         ycbcr_info->planes[i].denominator_scales[1];
   }
}

static void pvr_image_init_physical_extent(struct pvr_image *image,
                                           const VkImageCreateInfo *pCreateInfo,
                                           unsigned pbe_stride_align)
{
   assert(image->memlayout != PVR_MEMLAYOUT_UNDEFINED);

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(image->vk.format);

   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      pvr_image_plane_init_physical_extent(image,
                                           pCreateInfo,
                                           pbe_stride_align,
                                           ycbcr_info,
                                           plane);
   }
}

static void pvr_image_plane_setup_mip_levels(struct pvr_image *image, uint8_t i)
{
   struct pvr_image_plane *plane = &image->planes[i];
   VkFormat plane_format = vk_format_get_plane_format(image->vk.format, i);

   const uint32_t extent_alignment =
      image->vk.image_type == VK_IMAGE_TYPE_3D ? 4 : 1;
   const unsigned int cpp = vk_format_get_blocksize(plane_format);
   VkExtent3D extent =
      vk_image_extent_to_elements(&image->vk, plane->physical_extent);

   assert(image->vk.mip_levels <= ARRAY_SIZE(plane->mip_levels));

   plane->layer_size = 0;

   for (uint32_t i = 0; i < image->vk.mip_levels; i++) {
      struct pvr_mip_level *mip_level = &plane->mip_levels[i];

      mip_level->pitch = cpp * align(extent.width, extent_alignment);
      mip_level->height_pitch = align(extent.height, extent_alignment);
      mip_level->size = image->vk.samples * mip_level->pitch *
                        mip_level->height_pitch *
                        align(extent.depth, extent_alignment);
      mip_level->offset = plane->layer_size;

      plane->layer_size += mip_level->size;

      extent.height = u_minify(extent.height, 1);
      extent.width = u_minify(extent.width, 1);
      extent.depth = u_minify(extent.depth, 1);
   }

   if (image->vk.mip_levels > 1) {
      /* The hw calculates layer strides as if a full mip chain up until 1x1x1
       * were present so we need to account for that in the `layer_size`.
       */
      while (extent.height != 1 || extent.width != 1 || extent.depth != 1) {
         const uint32_t height_pitch = align(extent.height, extent_alignment);
         const uint32_t pitch = cpp * align(extent.width, extent_alignment);

         plane->layer_size += image->vk.samples * pitch * height_pitch *
                              align(extent.depth, extent_alignment);

         extent.height = u_minify(extent.height, 1);
         extent.width = u_minify(extent.width, 1);
         extent.depth = u_minify(extent.depth, 1);
      }
   }

   /* TODO: It might be useful to store the alignment in the image so it can be
    * checked (via an assert?) when setting
    * RGX_CR_TPU_TAG_CEM_4K_FACE_PACKING_EN, assuming this is where the
    * requirement comes from.
    */
   if (image->vk.array_layers > 1)
      plane->layer_size = align64(plane->layer_size, image->alignment);

   plane->size = plane->layer_size * image->vk.array_layers;
}

static void pvr_image_setup_mip_levels(struct pvr_image *image)
{
   VkDeviceSize offset = 0;
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      pvr_image_plane_setup_mip_levels(image, plane);

      offset = align(offset, image->alignment);
      image->planes[plane].offset = offset;
      offset += image->planes[plane].size;
   }

   image->total_size = offset;
}

static unsigned get_pbe_stride_align(const struct pvr_device_info *dev_info);

static VkResult pvr_pick_modifier(const VkImageCreateInfo *pCreateInfo,
                                  unsigned pbe_stride_align,
                                  uint64_t *modifier)
{
   const VkImageDrmFormatModifierListCreateInfoEXT *mod_list =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);

   const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_mod =
      vk_find_struct_const(
         pCreateInfo->pNext,
         IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

   /* Only support LINEAR now */
   *modifier = DRM_FORMAT_MOD_INVALID;

   if (mod_list)
      for (unsigned i = 0; i < mod_list->drmFormatModifierCount; i++)
         if (mod_list->pDrmFormatModifiers[i] == DRM_FORMAT_MOD_LINEAR)
            *modifier = DRM_FORMAT_MOD_LINEAR;

   if (explicit_mod) {
      const uint32_t bpp = vk_format_get_blocksize(pCreateInfo->format);
      assert(explicit_mod->drmFormatModifier == DRM_FORMAT_MOD_LINEAR &&
             explicit_mod->drmFormatModifierPlaneCount == 1);
      *modifier = explicit_mod->drmFormatModifier;

      if (explicit_mod->pPlaneLayouts[0].offset != 0)
         return VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT;

      if (explicit_mod->pPlaneLayouts[0].rowPitch %
          (bpp * pbe_stride_align) != 0) {
         return VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT;
      }

      if (explicit_mod->pPlaneLayouts[0].rowPitch <
          pCreateInfo->extent.width * bpp) {
         return VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT;
      }
   }

   return VK_SUCCESS;
}

VkResult pvr_CreateImage(VkDevice _device,
                         const VkImageCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkImage *pImage)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_image *image;

   if (wsi_common_is_swapchain_image(pCreateInfo)) {
      return wsi_common_create_swapchain_image(&device->pdevice->wsi_device,
                                               pCreateInfo,
                                               pImage);
   }

   image =
      vk_image_create(&device->vk, pCreateInfo, pAllocator, sizeof(*image));
   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_image_init(device, pCreateInfo, image);

   *pImage = pvr_image_to_handle(image);

   return VK_SUCCESS;
}

void pvr_DestroyImage(VkDevice _device,
                      VkImage _image,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_image, image, _image);

   if (!image)
      return;

   pvr_image_fini(device, image);
   vk_image_destroy(&device->vk, pAllocator, &image->vk);
}

void pvr_image_init(struct pvr_device *device,
                    const VkImageCreateInfo *pCreateInfo,
                    struct pvr_image *image)
{
   unsigned pbe_stride_align = get_pbe_stride_align(&device->pdevice->dev_info);

   image->plane_count = vk_format_get_plane_count(image->vk.format);
   image->alignment = device->pdevice->ws->page_size;

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      VkResult res = pvr_pick_modifier(pCreateInfo,
                                       pbe_stride_align,
                                       &image->vk.drm_format_mod);
      if (res != VK_SUCCESS)
         assert(res == VK_SUCCESS);

      assert(image->vk.drm_format_mod == DRM_FORMAT_MOD_LINEAR);
   }

   pvr_image_init_memlayout(image);
   pvr_image_init_physical_extent(image, pCreateInfo, pbe_stride_align);
   pvr_image_setup_mip_levels(image);
}

void pvr_image_fini(struct pvr_device *device, struct pvr_image *image)
{
   if (image->vma)
      pvr_unbind_memory(device, image->vma);
}

/* clang-format off */
/* Consider a 4 page buffer object.
 *   _________________________________________
 *  |         |          |         |          |
 *  |_________|__________|_________|__________|
 *                  |
 *                  \__ offset (0.5 page size)
 *
 *                  |___size(2 pages)____|
 *
 *            |__VMA size required (3 pages)__|
 *
 *                  |
 *                  \__ returned dev_addr = vma + offset % page_size
 *
 *   VMA size = align(size + offset % page_size, page_size);
 *
 *   Note: the above handling is currently divided between generic
 *   driver code and winsys layer. Given are the details of how this is
 *   being handled.
 *   * As winsys vma allocation interface does not have offset information,
 *     it can not calculate the extra size needed to adjust for the unaligned
 *     offset. So generic code is responsible for allocating a VMA that has
 *     extra space to deal with the above scenario.
 *   * Remaining work of mapping the vma to bo is done by vma_map interface,
 *     as it contains offset information, we don't need to do any adjustments
 *     in the generic code for this part.
 *
 *  TODO: Look into merging heap_alloc and vma_map into single interface.
 */
/* clang-format on */

VkResult pvr_BindImageMemory2(VkDevice _device,
                              uint32_t bindInfoCount,
                              const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   uint32_t i;

   for (i = 0; i < bindInfoCount; i++) {
      VK_FROM_HANDLE(pvr_device_memory, mem, pBindInfos[i].memory);
      VK_FROM_HANDLE(pvr_image, image, pBindInfos[i].image);
      VkDeviceSize offset = pBindInfos[i].memoryOffset;
      VkResult result;

#if defined(PVR_USE_WSI_PLATFORM)
      const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
         vk_find_struct_const(pBindInfos[i].pNext,
                              BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);

      if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
         VkDeviceMemory _swapchain_memory =
            wsi_common_get_memory(swapchain_info->swapchain,
                                  swapchain_info->imageIndex);
         VK_FROM_HANDLE(pvr_device_memory, swapchain_memory, _swapchain_memory);

         mem = swapchain_memory;
         offset = 0;
      }
#endif

      result = pvr_bind_memory(device,
                               mem,
                               offset,
                               image->total_size,
                               image->alignment,
                               &image->vma,
                               &image->dev_addr);
      if (result != VK_SUCCESS) {
         while (i--) {
            VK_FROM_HANDLE(pvr_image, image, pBindInfos[i].image);

            pvr_unbind_memory(device, image->vma);
         }

         return result;
      }
   }

   return VK_SUCCESS;
}

void pvr_get_image_subresource_layout(const struct pvr_image *image,
                                      const VkImageSubresource *subresource,
                                      VkSubresourceLayout *layout)
{
   const struct pvr_image_plane *plane =
      pvr_plane_from_aspect_const(image, subresource->aspectMask);
   const struct pvr_mip_level *mip_level =
      &plane->mip_levels[subresource->mipLevel];

   layout->offset = plane->offset +
                    subresource->arrayLayer * plane->layer_size +
                    mip_level->offset;
   layout->rowPitch = mip_level->pitch;
   layout->depthPitch = mip_level->pitch * mip_level->height_pitch;
   layout->arrayPitch = plane->layer_size;
   layout->size = mip_level->size;
}

void pvr_GetImageSubresourceLayout2(VkDevice device,
                                    VkImage _image,
                                    const VkImageSubresource2 *pSubresource,
                                    VkSubresourceLayout2 *pLayout)
{
   VK_FROM_HANDLE(pvr_image, image, _image);

   pvr_get_image_subresource_layout(image,
                                    &pSubresource->imageSubresource,
                                    &pLayout->subresourceLayout);
}

void pvr_GetDeviceImageSubresourceLayout(
   VkDevice _device,
   const VkDeviceImageSubresourceInfo *pInfo,
   VkSubresourceLayout2 *pLayout)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_image image = { 0 };

   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);
   pvr_image_init(device, pInfo->pCreateInfo, &image);

   pvr_GetImageSubresourceLayout2(_device,
                                  pvr_image_to_handle(&image),
                                  pInfo->pSubresource,
                                  pLayout);

   pvr_image_fini(device, &image);
   vk_image_finish(&image.vk);
}

/* Leave this at the very end, to avoid leakage of HW-defs here */
#define PVR_BUILD_ARCH_ROGUE
#include "pvr_csb.h"

static unsigned get_pbe_stride_align(const struct pvr_device_info *dev_info)
{
   return PVR_HAS_FEATURE(dev_info, pbe_stride_align_1pixel)
             ? 1
             : ROGUE_PBESTATE_REG_WORD0_LINESTRIDE_UNIT_SIZE;
}
