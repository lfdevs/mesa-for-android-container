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

#include <stdbool.h>

#include "pvr_formats.h"
#include "pvr_private.h"
#include "vk_enum_to_str.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"

#define FORMAT(vk, tex_fmt, pack_mode)                     \
   [VK_FORMAT_##vk] = {                                    \
      .vk_format = VK_FORMAT_##vk,                         \
      .tex_format = ROGUE_TEXSTATE_FORMAT_##tex_fmt,       \
      .pbe_packmode = ROGUE_PBESTATE_PACKMODE_##pack_mode, \
      .supported = true,                                   \
   }

struct pvr_format {
   VkFormat vk_format;
   uint32_t tex_format;
   uint32_t pbe_packmode;
   bool supported;
};

/* TODO: add all supported core formats */
static const struct pvr_format pvr_format_table[] = {
   /* VK_FORMAT_R8_UINT = 13. */
   FORMAT(R8_UINT, U8, U8),
   /* VK_FORMAT_B8G8R8A8_UNORM = 44. */
   FORMAT(B8G8R8A8_UNORM, U8U8U8U8, U8U8U8U8),
   /* VK_FORMAT_R32_UINT = 98. */
   FORMAT(R32_UINT, U32, U32),
   /* VK_FORMAT_R32G32B32A32_UINT = 107. */
   FORMAT(R32G32B32A32_UINT, U32U32U32U32, U32U32U32U32),
   /* VK_FORMAT_R32G32B32A32_SFLOAT = 109. */
   FORMAT(R32G32B32A32_SFLOAT, F32F32F32F32, F32F32F32F32),
   /* VK_FORMAT_D32_SFLOAT = 126. */
   FORMAT(D32_SFLOAT, F32, F32),
};

#undef FORMAT

static inline const struct pvr_format *pvr_get_format(VkFormat vk_format)
{
   if (vk_format < ARRAY_SIZE(pvr_format_table) &&
       pvr_format_table[vk_format].supported) {
      return &pvr_format_table[vk_format];
   }

   mesa_logd("Format %s(%d) not supported\n",
             vk_Format_to_str(vk_format),
             vk_format);

   return NULL;
}

uint32_t pvr_get_tex_format(VkFormat vk_format)
{
   const struct pvr_format *pvr_format = pvr_get_format(vk_format);
   if (pvr_format) {
      return pvr_format->tex_format;
   }

   return ROGUE_TEXSTATE_FORMAT_INVALID;
}

uint32_t pvr_get_pbe_packmode(VkFormat vk_format)
{
   const struct pvr_format *pvr_format = pvr_get_format(vk_format);
   if (pvr_format)
      return pvr_format->pbe_packmode;

   return ROGUE_PBESTATE_PACKMODE_INVALID;
}

static VkFormatFeatureFlags
pvr_get_image_format_features(const struct pvr_format *pvr_format,
                              VkImageTiling vk_tiling)
{
   VkFormatFeatureFlags flags = 0;
   VkImageAspectFlags aspects;

   if (!pvr_format)
      return 0;

   aspects = vk_format_aspects(pvr_format->vk_format);
   if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_BLIT_SRC_BIT;
   }

   return flags;
}

const uint8_t *pvr_get_format_swizzle(VkFormat vk_format)
{
   const struct util_format_description *vf = vk_format_description(vk_format);

   return vf->swizzle;
}

static VkFormatFeatureFlags
pvr_get_buffer_format_features(const struct pvr_format *pvr_format)
{
   VkFormatFeatureFlags flags = 0;

   if (!pvr_format)
      return 0;

   return flags;
}

void pvr_GetPhysicalDeviceFormatProperties2(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkFormatProperties2 *pFormatProperties)
{
   const struct pvr_format *pvr_format = pvr_get_format(format);

   pFormatProperties->formatProperties = (VkFormatProperties){
      .linearTilingFeatures =
         pvr_get_image_format_features(pvr_format, VK_IMAGE_TILING_LINEAR),
      .optimalTilingFeatures =
         pvr_get_image_format_features(pvr_format, VK_IMAGE_TILING_OPTIMAL),
      .bufferFeatures = pvr_get_buffer_format_features(pvr_format),
   };

   vk_foreach_struct (ext, pFormatProperties->pNext) {
      pvr_debug_ignored_stype(ext->sType);
   }
}

static VkResult
pvr_get_image_format_properties(struct pvr_physical_device *pdevice,
                                const VkPhysicalDeviceImageFormatInfo2 *info,
                                VkImageFormatProperties *pImageFormatProperties)
{
   assert(!"Unimplemented");
   return VK_SUCCESS;
}

VkResult pvr_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   PVR_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);
   VkExternalImageFormatProperties *external_props = NULL;
   VkResult result;

   result = pvr_get_image_format_properties(
      pdevice,
      pImageFormatInfo,
      &pImageFormatProperties->imageFormatProperties);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const (ext, pImageFormatInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *)ext;
         break;
      default:
         pvr_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct (ext, pImageFormatProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *)ext;
         break;
      default:
         pvr_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* From the Vulkan 1.0.42 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
         if (!external_props)
            break;

         external_props->externalMemoryProperties.externalMemoryFeatures =
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
         external_props->externalMemoryProperties.compatibleHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         external_props->externalMemoryProperties.exportFromImportedHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         break;
      default:
         return vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      }
   }

   return VK_SUCCESS;
}

void pvr_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   uint32_t samples,
   VkImageUsageFlags usage,
   VkImageTiling tiling,
   uint32_t *pNumProperties,
   VkSparseImageFormatProperties *pProperties)
{
   /* Sparse images are not yet supported. */
   *pNumProperties = 0;
}

void pvr_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{
   /* Sparse images are not yet supported. */
   *pPropertyCount = 0;
}

void pvr_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   /* The Vulkan 1.0.42 spec says "handleType must be a valid
    * VkExternalMemoryHandleTypeFlagBits value" in
    * VkPhysicalDeviceExternalBufferInfo. This differs from
    * VkPhysicalDeviceExternalImageFormatInfo, which surprisingly permits
    * handleType == 0.
    */
   assert(pExternalBufferInfo->handleType != 0);

   /* All of the current flags are for sparse which we don't support. */
   if (pExternalBufferInfo->flags)
      goto unsupported;

   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      /* clang-format off */
      pExternalBufferProperties->externalMemoryProperties.externalMemoryFeatures =
         VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      pExternalBufferProperties->externalMemoryProperties.exportFromImportedHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      pExternalBufferProperties->externalMemoryProperties.compatibleHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      /* clang-format on */
      return;
   default:
      break;
   }

unsupported:
   /* From the Vulkan 1.1.113 spec:
    *
    *    compatibleHandleTypes must include at least handleType.
    */
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties){
         .compatibleHandleTypes = pExternalBufferInfo->handleType,
      };
}

bool pvr_format_is_pbe_downscalable(VkFormat vk_format)
{
   if (vk_format_is_int(vk_format)) {
      /* PBE downscale behavior for integer formats does not match Vulkan
       * spec. Vulkan requires a single sample to be chosen instead of
       * taking the average sample color.
       */
      return false;
   }

   switch (pvr_get_pbe_packmode(vk_format)) {
   default:
      return true;

   case ROGUE_PBESTATE_PACKMODE_U16U16U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32F32F32:
   case ROGUE_PBESTATE_PACKMODE_U16U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32F32:
   case ROGUE_PBESTATE_PACKMODE_U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32:
   case ROGUE_PBESTATE_PACKMODE_U24ST8:
   case ROGUE_PBESTATE_PACKMODE_ST8U24:
   case ROGUE_PBESTATE_PACKMODE_U16:
   case ROGUE_PBESTATE_PACKMODE_S16:
   case ROGUE_PBESTATE_PACKMODE_U32:
   case ROGUE_PBESTATE_PACKMODE_S32:
   case ROGUE_PBESTATE_PACKMODE_F32:
   case ROGUE_PBESTATE_PACKMODE_X24U8F32:
   case ROGUE_PBESTATE_PACKMODE_X24X8F32:
   case ROGUE_PBESTATE_PACKMODE_X24G8X32:
   case ROGUE_PBESTATE_PACKMODE_X8U24:
   case ROGUE_PBESTATE_PACKMODE_U8X24:
   case ROGUE_PBESTATE_PACKMODE_PBYTE:
   case ROGUE_PBESTATE_PACKMODE_PWORD:
   case ROGUE_PBESTATE_PACKMODE_INVALID:
      return false;
   }
}
