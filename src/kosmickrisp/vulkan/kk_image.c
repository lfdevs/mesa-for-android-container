/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_image.h"

#include "kk_device.h"
#include "kk_device_memory.h"
#include "kk_entrypoints.h"
#include "kk_format.h"
#include "kk_physical_device.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "vk_enum_defines.h"
#include "vk_enum_to_str.h"
#include "vk_format.h"
#include "wsi_common_private.h"

static VkFormatFeatureFlags2
kk_get_image_plane_format_features(struct kk_physical_device *pdev,
                                   VkFormat vk_format, VkImageTiling tiling,
                                   uint64_t drm_format_mod)
{
   VkFormatFeatureFlags2 features = 0;

   /* Metal does not support linear tiling for compressed formats */
   if (tiling == VK_IMAGE_TILING_LINEAR && vk_format_is_compressed(vk_format))
      return 0;

   enum pipe_format p_format = vk_format_to_pipe_format(vk_format);
   if (p_format == PIPE_FORMAT_NONE)
      return 0;

   /* You can't tile a non-power-of-two */
   if (!util_is_power_of_two_nonzero(util_format_get_blocksize(p_format)))
      return 0;

   const struct kk_va_format *va_format = kk_get_va_format(p_format);
   if (va_format == NULL)
      return 0;

   // Textures can at least be sampled
   features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
   features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;

   if (va_format->filter) {
      features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      features |=
         VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT; // TODO_KOSMICKRISP
                                                              // Understand if
                                                              // we want to
                                                              // expose this
   }

   /* TODO: VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT */
   if (vk_format_has_depth(vk_format)) {
      features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
   }

   /* We disable A8 format due to lower blend pass issues */
   if (va_format->color && tiling != VK_IMAGE_TILING_LINEAR &&
       vk_format != VK_FORMAT_A8_UNORM) {
      features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
      features |= VK_FORMAT_FEATURE_2_BLIT_DST_BIT;
      // TODO_KOSMICKRISP Support snorm formats once the following spec issue is
      // resolved: https://gitlab.khronos.org/vulkan/vulkan/-/issues/4293
      if (!vk_format_is_snorm(vk_format))
         features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
   }

   if (vk_format_is_depth_or_stencil(vk_format)) {
      if (tiling == VK_IMAGE_TILING_LINEAR)
         return 0;

      features |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   }

   if (va_format->write) {
      features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
      features |= VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT;
      features |= VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
   }

   if (va_format->atomic)
      features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;

   if (features != 0) {
      features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
      features |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
   }

   return features;
}

VkFormatFeatureFlags2
kk_get_image_format_features(struct kk_physical_device *pdev,
                             VkFormat vk_format, VkImageTiling tiling,
                             uint64_t drm_format_mod)
{
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(vk_format);
   if (ycbcr_info == NULL) {
      return kk_get_image_plane_format_features(pdev, vk_format, tiling,
                                                drm_format_mod);
   }

   /* For multi-plane, we get the feature flags of each plane separately,
    * then take their intersection as the overall format feature flags
    */
   VkFormatFeatureFlags2 features = ~0ull;
   bool cosited_chroma = false;
   for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
      const struct vk_format_ycbcr_plane *plane_info =
         &ycbcr_info->planes[plane];
      features &= kk_get_image_plane_format_features(pdev, plane_info->format,
                                                     tiling, drm_format_mod);
      if (plane_info->denominator_scales[0] > 1 ||
          plane_info->denominator_scales[1] > 1)
         cosited_chroma = true;
   }
   if (features == 0)
      return 0;

   /* Uh... We really should be able to sample from YCbCr */
   assert(features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT);
   assert(features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

   /* These aren't allowed for YCbCr formats */
   features &=
      ~(VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT);

   /* This is supported on all YCbCr formats */
   features |=
      VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

   if (ycbcr_info->n_planes > 1) {
      /* DISJOINT_BIT implies that each plane has its own separate binding,
       * while SEPARATE_RECONSTRUCTION_FILTER_BIT implies that luma and chroma
       * each have their own, separate filters, so these two bits make sense
       * for multi-planar formats only.
       *
       * For MIDPOINT_CHROMA_SAMPLES_BIT, NVIDIA HW on single-plane interleaved
       * YCbCr defaults to COSITED_EVEN, which is inaccurate and fails tests.
       * This can be fixed with a NIR tweak but for now, we only enable this bit
       * for multi-plane formats. See Issue #9525 on the mesa/main tracker.
       */
      features |=
         VK_FORMAT_FEATURE_DISJOINT_BIT |
         VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT |
         VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;
   }

   if (cosited_chroma)
      features |= VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT;

   return features;
}

static VkFormatFeatureFlags2
vk_image_usage_to_format_features(VkImageUsageFlagBits usage_flag)
{
   assert(util_bitcount(usage_flag) == 1);
   switch (usage_flag) {
   case VK_IMAGE_USAGE_TRANSFER_SRC_BIT:
      return VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
             VK_FORMAT_FEATURE_BLIT_SRC_BIT;
   case VK_IMAGE_USAGE_TRANSFER_DST_BIT:
      return VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
             VK_FORMAT_FEATURE_BLIT_DST_BIT;
   case VK_IMAGE_USAGE_SAMPLED_BIT:
      return VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
   case VK_IMAGE_USAGE_STORAGE_BIT:
      return VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
   case VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
   case VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   case VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
             VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   case VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR:
      return VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
   default:
      return 0;
   }
}

uint32_t
kk_image_max_dimension(VkImageType image_type)
{
   /* Values taken from Apple7
    * https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf */
   switch (image_type) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      return 16384;
   case VK_IMAGE_TYPE_3D:
      return 2048;
   default:
      UNREACHABLE("Invalid image type");
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   VK_FROM_HANDLE(kk_physical_device, pdev, physicalDevice);

   const VkPhysicalDeviceExternalImageFormatInfo *external_info =
      vk_find_struct_const(pImageFormatInfo->pNext,
                           PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);

   /* Initialize to zero in case we return VK_ERROR_FORMAT_NOT_SUPPORTED */
   memset(&pImageFormatProperties->imageFormatProperties, 0,
          sizeof(pImageFormatProperties->imageFormatProperties));

   /* Metal does not support depth/stencil textures that are not 2D (we make 1D
    * textures 2D) */
   if (vk_format_is_depth_or_stencil(pImageFormatInfo->format) &&
       pImageFormatInfo->type == VK_IMAGE_TYPE_3D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* Metal does not support EAC/ETC formats for 3D textures. */
   if (util_format_is_etc(vk_format_to_pipe_format(pImageFormatInfo->format)) &&
       pImageFormatInfo->type == VK_IMAGE_TYPE_3D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* Metal disallows reading compressed formats as uncompressed format.
    * VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT is only used with
    * compressed formats.
    */
   if (pImageFormatInfo->flags &
       VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(pImageFormatInfo->format);

   /* For the purposes of these checks, we don't care about all the extra
    * YCbCr features and we just want the accumulation of features available
    * to all planes of the given format.
    */
   VkFormatFeatureFlags2 features;
   if (ycbcr_info == NULL) {
      features = kk_get_image_plane_format_features(
         pdev, pImageFormatInfo->format, pImageFormatInfo->tiling, 0u);
   } else {
      features = ~0ull;
      assert(ycbcr_info->n_planes > 0);
      for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
         const VkFormat plane_format = ycbcr_info->planes[plane].format;
         features &= kk_get_image_plane_format_features(
            pdev, plane_format, pImageFormatInfo->tiling, 0u);
      }
   }

   if (features == 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR &&
       pImageFormatInfo->type == VK_IMAGE_TYPE_3D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* TODO_KOSMICKRISP We could allow linear images that are used as render
    * target as long as they are not used as input attachments. Main reason for
    * this is that we expect arrays when rendering and reading from input
    * attachments and Metal disallows arrays for linear textures.
    */
   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR &&
       (pImageFormatInfo->usage &
        (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (ycbcr_info && pImageFormatInfo->type != VK_IMAGE_TYPE_2D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* Don't support sparse residency */
   if ((pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* From the Vulkan 1.3.279 spec:
    *
    *    VUID-VkImageCreateInfo-tiling-04121
    *
    *    "If tiling is VK_IMAGE_TILING_LINEAR, flags must not contain
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT"
    *
    *    VUID-VkImageCreateInfo-imageType-00970
    *
    *    "If imageType is VK_IMAGE_TYPE_1D, flags must not contain
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT"
    */
   if ((pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) &&
       (pImageFormatInfo->type == VK_IMAGE_TYPE_1D ||
        pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* From the Vulkan 1.3.279 spec:
    *
    *    VUID-VkImageCreateInfo-flags-09403
    *
    *    "If flags contains VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT, flags
    *    must not include VK_IMAGE_CREATE_SPARSE_ALIASED_BIT,
    *    VK_IMAGE_CREATE_SPARSE_BINDING_BIT, or
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT"
    */
   if ((pImageFormatInfo->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT) &&
       (pImageFormatInfo->flags & (VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
                                   VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                                   VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT &&
       pImageFormatInfo->type != VK_IMAGE_TYPE_2D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   const uint32_t max_dim = kk_image_max_dimension(pImageFormatInfo->type);
   assert(util_is_power_of_two_nonzero(max_dim));
   uint32_t maxMipLevels = util_logbase2(max_dim) + 1;
   VkExtent3D maxExtent;
   uint32_t maxArraySize;
   switch (pImageFormatInfo->type) {
   case VK_IMAGE_TYPE_1D:
      maxExtent = (VkExtent3D){max_dim, 1, 1};
      maxArraySize = 2048u;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent = (VkExtent3D){max_dim, max_dim, 1};
      maxArraySize = 2048u;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent = (VkExtent3D){max_dim, max_dim, max_dim};
      maxArraySize = 1u;
      break;
   default:
      UNREACHABLE("Invalid image type");
   }
   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR)
      maxArraySize = 1;

   if (ycbcr_info != NULL || pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR)
      maxMipLevels = 1;

   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      maxArraySize = 1;
      maxMipLevels = 1;
   }

   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_OPTIMAL &&
       pImageFormatInfo->type == VK_IMAGE_TYPE_2D && ycbcr_info == NULL &&
       (features & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                    VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(pImageFormatInfo->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)) {
      sampleCounts =
         VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT |
         // TODO_KOSMICKRISP Modify sample count based on what pdev supports
         VK_SAMPLE_COUNT_4_BIT /* |
          VK_SAMPLE_COUNT_8_BIT */
         ;
   }

   /* From the Vulkan 1.2.199 spec:
    *
    *    "VK_IMAGE_CREATE_EXTENDED_USAGE_BIT specifies that the image can be
    *    created with usage flags that are not supported for the format the
    *    image is created with but are supported for at least one format a
    *    VkImageView created from the image can have."
    *
    * If VK_IMAGE_CREATE_EXTENDED_USAGE_BIT is set, views can be created with
    * different usage than the image so we can't always filter on usage.
    * There is one exception to this below for storage.
    */
   const VkImageUsageFlags image_usage = pImageFormatInfo->usage;
   VkImageUsageFlags view_usage = image_usage;
   if (pImageFormatInfo->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)
      view_usage = 0;

   u_foreach_bit(b, view_usage) {
      VkFormatFeatureFlags2 usage_features =
         vk_image_usage_to_format_features(1 << b);
      if (usage_features && !(features & usage_features))
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   const VkExternalMemoryProperties *ext_mem_props = NULL;
   if (external_info != NULL && external_info->handleType != 0) {
      /* We only support heaps since that's the backing for all our memory and
       * simplifies implementation */
      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT:
         ext_mem_props = &kk_mtlheap_mem_props;
         break;
      default:
         /* From the Vulkan 1.3.256 spec:
          *
          *    "If handleType is not compatible with the [parameters] in
          *    VkPhysicalDeviceImageFormatInfo2, then
          *    vkGetPhysicalDeviceImageFormatProperties2 returns
          *    VK_ERROR_FORMAT_NOT_SUPPORTED."
          */
         return vk_errorf(pdev, VK_ERROR_FORMAT_NOT_SUPPORTED,
                          "unsupported VkExternalMemoryHandleTypeFlagBits: %s ",
                          vk_ExternalMemoryHandleTypeFlagBits_to_str(
                             external_info->handleType));
      }
   }

   const unsigned plane_count =
      vk_format_get_plane_count(pImageFormatInfo->format);

   /* From the Vulkan 1.3.259 spec, VkImageCreateInfo:
    *
    *    VUID-VkImageCreateInfo-imageCreateFormatFeatures-02260
    *
    *    "If format is a multi-planar format, and if imageCreateFormatFeatures
    *    (as defined in Image Creation Limits) does not contain
    *    VK_FORMAT_FEATURE_DISJOINT_BIT, then flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    *
    * This is satisfied trivially because we support DISJOINT on all
    * multi-plane formats.  Also,
    *
    *    VUID-VkImageCreateInfo-format-01577
    *
    *    "If format is not a multi-planar format, and flags does not include
    *    VK_IMAGE_CREATE_ALIAS_BIT, flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    */
   if (plane_count == 1 &&
       !(pImageFormatInfo->flags & VK_IMAGE_CREATE_ALIAS_BIT) &&
       (pImageFormatInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (ycbcr_info &&
       ((pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) ||
        (pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if ((pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) &&
       (pImageFormatInfo->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   pImageFormatProperties->imageFormatProperties = (VkImageFormatProperties){
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,
      .maxResourceSize = UINT32_MAX, /* TODO */
   };

   vk_foreach_struct(s, pImageFormatProperties->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES: {
         VkExternalImageFormatProperties *p = (void *)s;
         /* From the Vulkan 1.3.256 spec:
          *
          *    "If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2
          *    will behave as if VkPhysicalDeviceExternalImageFormatInfo was
          *    not present, and VkExternalImageFormatProperties will be
          *    ignored."
          *
          * This is true if and only if ext_mem_props == NULL
          */
         if (ext_mem_props != NULL)
            p->externalMemoryProperties = *ext_mem_props;
         break;
      }
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: {
         VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = (void *)s;
         ycbcr_props->combinedImageSamplerDescriptorCount = plane_count;
         break;
      }
      case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT: {
         VkHostImageCopyDevicePerformanceQueryEXT *host_props = (void *)s;
         host_props->optimalDeviceAccess = true;
         host_props->identicalMemoryLayout = true;
         break;
      }
      default:
         vk_debug_ignored_stype(s->sType);
         break;
      }
   }

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties)
{
   *pPropertyCount = 0;
   return;
}

static VkResult
kk_image_init(struct kk_device *dev, struct kk_image *image,
              const VkImageCreateInfo *pCreateInfo)
{
   vk_image_init(&dev->vk, &image->vk, pCreateInfo);

   if ((image->vk.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       image->vk.samples > 1) {
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
      image->vk.stencil_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   }

   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      if (util_format_is_depth_or_stencil(
             vk_format_to_pipe_format(image->vk.format))) {
         image->vk.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
         image->vk.stencil_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
      } else {
         image->vk.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      }
   }

   image->plane_count = vk_format_get_plane_count(pCreateInfo->format);
   image->disjoint = image->plane_count > 1 &&
                     (pCreateInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT);

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(pCreateInfo->format);
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      VkFormat format =
         ycbcr_info ? ycbcr_info->planes[plane].format : pCreateInfo->format;
      const uint8_t width_scale =
         ycbcr_info ? ycbcr_info->planes[plane].denominator_scales[0] : 1;
      const uint8_t height_scale =
         ycbcr_info ? ycbcr_info->planes[plane].denominator_scales[1] : 1;
      kk_image_layout_init(dev, pCreateInfo, vk_format_to_pipe_format(format),
                           width_scale, height_scale,
                           &image->planes[plane].layout);
   }

   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      kk_image_layout_init(dev, pCreateInfo, PIPE_FORMAT_R32_UINT, 1, 1,
                           &image->stencil_copy_temp.layout);
   }

   return VK_SUCCESS;
}

static void
kk_image_plane_size_align_B(struct kk_device *dev, const struct kk_image *image,
                            const struct kk_image_plane *plane,
                            uint64_t *size_B_out, uint64_t *align_B_out)
{
   *size_B_out = plane->layout.size_B;
   *align_B_out = plane->layout.align_B;
}

static void
kk_image_plane_finish(struct kk_device *dev, struct kk_image_plane *plane,
                      VkImageCreateFlags create_flags,
                      const VkAllocationCallbacks *pAllocator)
{
   if (plane->mtl_handle != NULL)
      mtl_release(plane->mtl_handle);
   if (plane->mtl_handle_array != NULL)
      mtl_release(plane->mtl_handle_array);
}

static void
kk_image_finish(struct kk_device *dev, struct kk_image *image,
                const VkAllocationCallbacks *pAllocator)
{
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      kk_image_plane_finish(dev, &image->planes[plane], image->vk.create_flags,
                            pAllocator);
   }

   if (image->stencil_copy_temp.layout.size_B > 0) {
      kk_image_plane_finish(dev, &image->stencil_copy_temp,
                            image->vk.create_flags, pAllocator);
   }

   vk_image_finish(&image->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateImage(VkDevice _device, const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
   VK_FROM_HANDLE(kk_device, dev, _device);
   struct kk_physical_device *pdev = kk_device_physical(dev);
   struct kk_image *image;
   VkResult result;

#ifdef KK_USE_WSI_PLATFORM
   /* Ignore swapchain creation info on Android. Since we don't have an
    * implementation in Mesa, we're guaranteed to access an Android object
    * incorrectly.
    */
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(
         &pdev->wsi_device, pCreateInfo, swapchain_info->swapchain, pImage);
   }
#endif

   image = vk_zalloc2(&dev->vk.alloc, pAllocator, sizeof(*image), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = kk_image_init(dev, image, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->vk.alloc, pAllocator, image);
      return result;
   }

   *pImage = kk_image_to_handle(image);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyImage(VkDevice device, VkImage _image,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_image, image, _image);

   if (!image)
      return;

   kk_image_finish(dev, image, pAllocator);
   vk_free2(&dev->vk.alloc, pAllocator, image);
}

static void
kk_image_plane_add_req(struct kk_device *dev, const struct kk_image *image,
                       const struct kk_image_plane *plane, uint64_t *size_B,
                       uint32_t *align_B)
{
   assert(util_is_power_of_two_or_zero64(*align_B));
   uint64_t plane_size_B, plane_align_B;
   kk_image_plane_size_align_B(dev, image, plane, &plane_size_B,
                               &plane_align_B);

   *align_B = MAX2(*align_B, plane_align_B);
   *size_B = align64(*size_B, plane_align_B);
   *size_B += plane_size_B;
}

static void
kk_get_image_memory_requirements(struct kk_device *dev, struct kk_image *image,
                                 VkImageAspectFlags aspects,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   struct kk_physical_device *pdev = kk_device_physical(dev);
   uint32_t memory_types = (1 << pdev->mem_type_count) - 1;

   /* Remove non host visible heaps from the types for host image copy in case
    * of potential issues. This should be removed when we get ReBAR.
    */
   if (image->vk.usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) {
      for (uint32_t i = 0; i < pdev->mem_type_count; i++) {
         if (!(pdev->mem_types[i].propertyFlags &
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            memory_types &= ~BITFIELD_BIT(i);
      }
   }

   // TODO hope for the best?

   uint64_t size_B = 0;
   uint32_t align_B = 0;
   if (image->disjoint) {
      uint8_t plane = kk_image_memory_aspects_to_plane(image, aspects);
      kk_image_plane_add_req(dev, image, &image->planes[plane], &size_B,
                             &align_B);
   } else {
      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         kk_image_plane_add_req(dev, image, &image->planes[plane], &size_B,
                                &align_B);
      }
   }

   if (image->stencil_copy_temp.layout.size_B > 0) {
      kk_image_plane_add_req(dev, image, &image->stencil_copy_temp, &size_B,
                             &align_B);
   }

   pMemoryRequirements->memoryRequirements.memoryTypeBits = memory_types;
   pMemoryRequirements->memoryRequirements.alignment = align_B;
   pMemoryRequirements->memoryRequirements.size = size_B;

   vk_foreach_struct_const(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *dedicated = (void *)ext;
         dedicated->prefersDedicatedAllocation =
            image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         dedicated->requiresDedicatedAllocation =
            image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_GetImageMemoryRequirements2(VkDevice device,
                               const VkImageMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_image, image, pInfo->image);

   const VkImagePlaneMemoryRequirementsInfo *plane_info =
      vk_find_struct_const(pInfo->pNext, IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO);
   const VkImageAspectFlags aspects =
      image->disjoint ? plane_info->planeAspect : image->vk.aspects;

   kk_get_image_memory_requirements(dev, image, aspects, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
kk_GetDeviceImageMemoryRequirements(VkDevice device,
                                    const VkDeviceImageMemoryRequirements *pInfo,
                                    VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   ASSERTED VkResult result;
   struct kk_image image = {0};

   result = kk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   const VkImageAspectFlags aspects =
      image.disjoint ? pInfo->planeAspect : image.vk.aspects;

   kk_get_image_memory_requirements(dev, &image, aspects, pMemoryRequirements);

   kk_image_finish(dev, &image, NULL);
}

VKAPI_ATTR void VKAPI_CALL
kk_GetImageSparseMemoryRequirements2(
   VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0u;
}

VKAPI_ATTR void VKAPI_CALL
kk_GetDeviceImageSparseMemoryRequirements(
   VkDevice device, const VkDeviceImageMemoryRequirements *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0u;
}

static void
kk_get_image_subresource_layout(struct kk_device *dev, struct kk_image *image,
                                const VkImageSubresource2KHR *pSubresource,
                                VkSubresourceLayout2KHR *pLayout)
{
   const VkImageSubresource *isr = &pSubresource->imageSubresource;

   const uint8_t p = kk_image_memory_aspects_to_plane(image, isr->aspectMask);
   const struct kk_image_plane *plane = &image->planes[p];

   uint64_t offset_B = 0;
   if (!image->disjoint) {
      uint32_t align_B = 0;
      for (unsigned i = 0; i < p; i++) {
         kk_image_plane_add_req(dev, image, &image->planes[i], &offset_B,
                                &align_B);
      }
   }

   pLayout->subresourceLayout = (VkSubresourceLayout){
      .offset = offset_B,
      .size = plane->layout.size_B,
      .rowPitch = plane->layout.linear_stride_B,
      .arrayPitch = plane->layout.layer_stride_B,
      .depthPitch = 1u,
   };
}

VKAPI_ATTR void VKAPI_CALL
kk_GetImageSubresourceLayout2KHR(VkDevice device, VkImage _image,
                                 const VkImageSubresource2KHR *pSubresource,
                                 VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VK_FROM_HANDLE(kk_image, image, _image);

   kk_get_image_subresource_layout(dev, image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL
kk_GetDeviceImageSubresourceLayoutKHR(
   VkDevice device, const VkDeviceImageSubresourceInfoKHR *pInfo,
   VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   ASSERTED VkResult result;
   struct kk_image image = {0};

   result = kk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   kk_get_image_subresource_layout(dev, &image, pInfo->pSubresource, pLayout);

   kk_image_finish(dev, &image, NULL);
}

static VkResult
kk_image_plane_bind(struct kk_device *dev, struct kk_image *image,
                    struct kk_image_plane *plane, struct kk_device_memory *mem,
                    uint64_t *offset_B)
{
   uint64_t plane_size_B, plane_align_B;
   kk_image_plane_size_align_B(dev, image, plane, &plane_size_B,
                               &plane_align_B);
   *offset_B = align64(*offset_B, plane_align_B);

   /* Linear textures in Metal need to be allocated through a buffer... */
   if (plane->layout.optimized_layout)
      plane->mtl_handle = mtl_new_texture_with_descriptor(
         mem->bo->mtl_handle, &plane->layout, *offset_B);
   else
      plane->mtl_handle = mtl_new_texture_with_descriptor_linear(
         mem->bo->map, &plane->layout, *offset_B);
   plane->addr = mem->bo->gpu + *offset_B;

   /* Create auxiliary 2D array texture for 3D images so we can use 2D views of
    * it */
   if (plane->layout.type == MTL_TEXTURE_TYPE_3D &&
       (image->vk.create_flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT)) {
      struct kk_image_layout array_layout = plane->layout;
      array_layout.type = MTL_TEXTURE_TYPE_2D_ARRAY;
      // TODO_KOSMICKRISP We need to make sure that this doesn't go over Metal's
      // layer maximum which is 2048. Probably by limiting the dimensions and
      // layers for 3D images
      array_layout.layers = array_layout.layers * array_layout.depth_px;
      array_layout.depth_px = 1u;
      plane->mtl_handle_array = mtl_new_texture_with_descriptor(
         mem->bo->mtl_handle, &array_layout, *offset_B);
   }

   *offset_B += plane_size_B;

   return VK_SUCCESS;
}

static VkResult
kk_bind_image_memory(struct kk_device *dev, const VkBindImageMemoryInfo *info)
{
   VK_FROM_HANDLE(kk_device_memory, mem, info->memory);
   VK_FROM_HANDLE(kk_image, image, info->image);
   VkResult result;

   /* Ignore this struct on Android, we cannot access swapchain structures
    * there. */
#ifdef KK_USE_WSI_PLATFORM
   const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
      vk_find_struct_const(info->pNext, BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);

   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(wsi_swapchain, swapchain, swapchain_info->swapchain);
      VkImage _wsi_image =
         swapchain->get_wsi_image(swapchain, swapchain_info->imageIndex)->image;
      VK_FROM_HANDLE(kk_image, wsi_img, _wsi_image);

      assert(image->plane_count == 1);
      assert(wsi_img->plane_count == 1);

      struct kk_image_plane *plane = &image->planes[0];
      struct kk_image_plane *swapchain_plane = &wsi_img->planes[0];

      /* Copy swapchain plane data retaining relevant resources. */
      plane->layout = swapchain_plane->layout;
      plane->mtl_handle = mtl_retain(swapchain_plane->mtl_handle);
      plane->mtl_handle_array =
         swapchain_plane->mtl_handle_array
            ? mtl_retain(swapchain_plane->mtl_handle_array)
            : NULL;
      plane->addr = swapchain_plane->addr;

      return VK_SUCCESS;
   }
#endif

   uint64_t offset_B = info->memoryOffset;
   if (image->disjoint) {
      const VkBindImagePlaneMemoryInfo *plane_info =
         vk_find_struct_const(info->pNext, BIND_IMAGE_PLANE_MEMORY_INFO);
      const uint8_t plane =
         kk_image_memory_aspects_to_plane(image, plane_info->planeAspect);
      result =
         kk_image_plane_bind(dev, image, &image->planes[plane], mem, &offset_B);
      if (result != VK_SUCCESS)
         return result;
   } else {
      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         result = kk_image_plane_bind(dev, image, &image->planes[plane], mem,
                                      &offset_B);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   if (image->stencil_copy_temp.layout.size_B > 0) {
      result = kk_image_plane_bind(dev, image, &image->stencil_copy_temp, mem,
                                   &offset_B);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_BindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                    const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(kk_device, dev, device);
   VkResult first_error_or_success = VK_SUCCESS;

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VkResult result = kk_bind_image_memory(dev, &pBindInfos[i]);

      const VkBindMemoryStatusKHR *status =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_MEMORY_STATUS_KHR);
      if (status != NULL && status->pResult != NULL)
         *status->pResult = VK_SUCCESS;

      if (first_error_or_success == VK_SUCCESS)
         first_error_or_success = result;
   }

   return first_error_or_success;
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_GetImageOpaqueCaptureDescriptorDataEXT(
   VkDevice _device, const VkImageCaptureDescriptorDataInfoEXT *pInfo,
   void *pData)
{
   return VK_SUCCESS;
}
