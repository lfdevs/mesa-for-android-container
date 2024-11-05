/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_image.h"

#include "nvk_device.h"
#include "nvk_device_memory.h"
#include "nvk_entrypoints.h"
#include "nvk_format.h"
#include "nvk_physical_device.h"
#include "nvkmd/nvkmd.h"

#include "vk_enum_to_str.h"
#include "vk_format.h"
#include "nil.h"
#include "vk_enum_defines.h"
#include "vk_format.h"

#include "clb097.h"
#include "clb197.h"
#include "clc097.h"

static VkFormatFeatureFlags2
nvk_get_image_plane_format_features(struct nvk_physical_device *pdev,
                                    VkFormat vk_format, VkImageTiling tiling,
                                    uint64_t drm_format_mod)
{
   VkFormatFeatureFlags2 features = 0;

   if (tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT &&
       drm_format_mod != DRM_FORMAT_MOD_LINEAR &&
       !fourcc_mod_is_vendor(drm_format_mod, NVIDIA))
      return 0;

   enum pipe_format p_format = vk_format_to_pipe_format(vk_format);
   if (p_format == PIPE_FORMAT_NONE)
      return 0;

   /* You can't tile a non-power-of-two */
   if (!util_is_power_of_two_nonzero(util_format_get_blocksize(p_format)))
      return 0;

   if (nil_format_supports_texturing(&pdev->info, p_format)) {
      features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
      features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;
   }

   if (nil_format_supports_filtering(&pdev->info, p_format)) {
      features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      if (pdev->info.cls_eng3d >= MAXWELL_B)
         features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT;
   }

   /* TODO: VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT */
   if (vk_format_has_depth(vk_format)) {
      features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
   }

   if (nil_format_supports_color_targets(&pdev->info, p_format) && 
       tiling != VK_IMAGE_TILING_LINEAR) {
      features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
      if (nil_format_supports_blending(&pdev->info, p_format))
         features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
      features |= VK_FORMAT_FEATURE_2_BLIT_DST_BIT;
   }

   if (vk_format_is_depth_or_stencil(vk_format)) {
      if (!nil_format_supports_depth_stencil(&pdev->info, p_format) ||
          tiling == VK_IMAGE_TILING_LINEAR)
         return 0;

      features |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   }

   if (nil_format_supports_storage(&pdev->info, p_format)) {
      features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
      if (pdev->info.cls_eng3d >= MAXWELL_A)
         features |= VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT;
   }

   if (nvk_format_supports_atomics(&pdev->info, p_format))
      features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;

   if (features != 0) {
      features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
      features |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
   }

   return features;
}

VkFormatFeatureFlags2
nvk_get_image_format_features(struct nvk_physical_device *pdev,
                              VkFormat vk_format, VkImageTiling tiling,
                              uint64_t drm_format_mod)
{
   const struct vk_format_ycbcr_info *ycbcr_info =
         vk_format_get_ycbcr_info(vk_format);
   if (ycbcr_info == NULL) {
      return nvk_get_image_plane_format_features(pdev, vk_format, tiling,
                                                 drm_format_mod);
   }

   /* For multi-plane, we get the feature flags of each plane separately,
    * then take their intersection as the overall format feature flags
    */
   VkFormatFeatureFlags2 features = ~0ull;
   bool cosited_chroma = false;
   for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
      const struct vk_format_ycbcr_plane *plane_info = &ycbcr_info->planes[plane];
      features &= nvk_get_image_plane_format_features(pdev, plane_info->format,
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
   features &= ~(VK_FORMAT_FEATURE_2_BLIT_SRC_BIT |
                 VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
                 VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                 VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT |
                 VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT);

   /* This is supported on all YCbCr formats */
   features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

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
      features |= VK_FORMAT_FEATURE_DISJOINT_BIT |
                  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT |
                  VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;
   }

   if (cosited_chroma)
      features |= VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT;

   return features;
}

void
nvk_get_drm_format_modifier_properties_list(struct nvk_physical_device *pdev,
                                            VkFormat vk_format,
                                            VkBaseOutStructure *ext)
{
   assert(ext->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT ||
          ext->sType == VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT);

   /* The two top-level data structures are the same.  It's only when
    * you get to walking the actual list of modifier properties that
    * they differ.
    */
   VkDrmFormatModifierPropertiesListEXT *p = (void *)ext;

   /* We don't support modifiers for YCbCr images */
   if (vk_format_get_ycbcr_info(vk_format) != NULL) {
      p->drmFormatModifierCount = 0;
      return;
   }

   /* Check that we actually support the format so we don't try to query
    * modifiers for formats NIL doesn't support.
    */
   const VkFormatFeatureFlags2 tiled_features =
      nvk_get_image_plane_format_features(pdev, vk_format,
                                          VK_IMAGE_TILING_OPTIMAL,
                                          DRM_FORMAT_MOD_INVALID);
   if (tiled_features == 0) {
      p->drmFormatModifierCount = 0;
      return;
   }

   uint64_t mods[NIL_MAX_DRM_FORMAT_MODS];
   size_t mod_count = NIL_MAX_DRM_FORMAT_MODS;
   enum pipe_format p_format = vk_format_to_pipe_format(vk_format);
   nil_drm_format_mods_for_format(&pdev->info, nil_format(p_format),
                                  &mod_count, &mods);
   if (mod_count == 0) {
      p->drmFormatModifierCount = 0;
      return;
   }

   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT: {
      VK_OUTARRAY_MAKE_TYPED(VkDrmFormatModifierPropertiesEXT, out,
                             p->pDrmFormatModifierProperties,
                             &p->drmFormatModifierCount);

      for (uint32_t i = 0; i < mod_count; i++) {
         const VkFormatFeatureFlags2 features2 =
            nvk_get_image_format_features(pdev, vk_format,
                                          VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                          mods[i]);
         if (features2 != 0) {
            vk_outarray_append_typed(VkDrmFormatModifierPropertiesEXT, &out, mp) {
               mp->drmFormatModifier = mods[i];
               mp->drmFormatModifierPlaneCount = 1;
               mp->drmFormatModifierTilingFeatures =
                  vk_format_features2_to_features(features2);
            }
         }
      }
      break;
   }

   case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT: {
      VkDrmFormatModifierPropertiesList2EXT *p2 = (void *)p;
      VK_OUTARRAY_MAKE_TYPED(VkDrmFormatModifierProperties2EXT, out,
                             p2->pDrmFormatModifierProperties,
                             &p2->drmFormatModifierCount);

      for (uint32_t i = 0; i < mod_count; i++) {
         const VkFormatFeatureFlags2 features2 =
            nvk_get_image_format_features(pdev, vk_format,
                                          VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                          mods[i]);
         if (features2 != 0) {
            vk_outarray_append_typed(VkDrmFormatModifierProperties2EXT, &out, mp) {
               mp->drmFormatModifier = mods[i];
               mp->drmFormatModifierPlaneCount = 1;
               mp->drmFormatModifierTilingFeatures = features2;
            }
         }
      }
      break;
   }

   default:
      unreachable("Invalid structure type");
   }
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
   default:
      return 0;
   }
}

uint32_t
nvk_image_max_dimension(const struct nv_device_info *info,
                        VkImageType image_type)
{
   switch (image_type) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      return info->cls_eng3d >= PASCAL_A ? 0x8000 : 0x4000;
   case VK_IMAGE_TYPE_3D:
      return 0x4000;
   default:
      unreachable("Invalid image type");
   }
}

static uint64_t
get_explicit_drm_format_mod(const void *pNext)
{
   const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *drm_format_mod_info =
      vk_find_struct_const(pNext,
                           PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);
   if (drm_format_mod_info)
      return drm_format_mod_info->drmFormatModifier;
   else
      return DRM_FORMAT_MOD_INVALID;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   VK_FROM_HANDLE(nvk_physical_device, pdev, physicalDevice);

   const VkPhysicalDeviceExternalImageFormatInfo *external_info =
      vk_find_struct_const(pImageFormatInfo->pNext,
                           PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);

   /* Initialize to zero in case we return VK_ERROR_FORMAT_NOT_SUPPORTED */
   memset(&pImageFormatProperties->imageFormatProperties, 0,
          sizeof(pImageFormatProperties->imageFormatProperties));

   uint64_t drm_format_mod =
      get_explicit_drm_format_mod(pImageFormatInfo->pNext);
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(pImageFormatInfo->format);

   /* For the purposes of these checks, we don't care about all the extra
    * YCbCr features and we just want the accumulation of features available
    * to all planes of the given format.
    */
   VkFormatFeatureFlags2 features;
   if (ycbcr_info == NULL) {
      features = nvk_get_image_plane_format_features(
         pdev, pImageFormatInfo->format, pImageFormatInfo->tiling,
         drm_format_mod);
   } else {
      features = ~0ull;
      assert(ycbcr_info->n_planes > 0);
      for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
         const VkFormat plane_format = ycbcr_info->planes[plane].format;
         features &= nvk_get_image_plane_format_features(
            pdev, plane_format, pImageFormatInfo->tiling, drm_format_mod);
      }
   }

   if (features == 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR &&
       pImageFormatInfo->type != VK_IMAGE_TYPE_2D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (ycbcr_info && pImageFormatInfo->type != VK_IMAGE_TYPE_2D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* Maxwell B and earlier don't support sparse residency */
   if ((pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) &&
       pdev->info.cls_eng3d < MAXWELL_B)
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

   const uint32_t max_dim =
      nvk_image_max_dimension(&pdev->info, VK_IMAGE_TYPE_1D);
   VkExtent3D maxExtent;
   uint32_t maxArraySize;
   switch (pImageFormatInfo->type) {
   case VK_IMAGE_TYPE_1D:
      maxExtent = (VkExtent3D) { max_dim, 1, 1 };
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent = (VkExtent3D) { max_dim, max_dim, 1 };
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent = (VkExtent3D) { max_dim, max_dim, max_dim };
      maxArraySize = 1;
      break;
   default:
      unreachable("Invalid image type");
   }
   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR)
      maxArraySize = 1;

   assert(util_is_power_of_two_nonzero(max_dim));
   uint32_t maxMipLevels = util_logbase2(max_dim) + 1;
   if (ycbcr_info != NULL || pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR)
       maxMipLevels = 1;

   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      maxArraySize = 1;
      maxMipLevels = 1;
   }

   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_OPTIMAL &&
       pImageFormatInfo->type == VK_IMAGE_TYPE_2D &&
       ycbcr_info == NULL &&
       (features & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                    VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(pImageFormatInfo->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)) {
      sampleCounts = VK_SAMPLE_COUNT_1_BIT |
                     VK_SAMPLE_COUNT_2_BIT |
                     VK_SAMPLE_COUNT_4_BIT |
                     VK_SAMPLE_COUNT_8_BIT;
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
      bool tiling_has_explicit_layout;
      switch (pImageFormatInfo->tiling) {
      case VK_IMAGE_TILING_LINEAR:
      case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
         tiling_has_explicit_layout = true;
         break;
      case VK_IMAGE_TILING_OPTIMAL:
         tiling_has_explicit_layout = false;
         break;
      default:
         unreachable("Unsupported VkImageTiling");
      }

      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
         /* No special restrictions */
         if (tiling_has_explicit_layout) {
            /* With an explicit memory layout, we don't care which type of
             * fd the image belongs too. Both OPAQUE_FD and DMA_BUF are
             * interchangeable here.
             */
            ext_mem_props = &nvk_dma_buf_mem_props;
         } else {
            ext_mem_props = &nvk_opaque_fd_mem_props;
         }
         break;

      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
         if (!tiling_has_explicit_layout) {
            return vk_errorf(pdev, VK_ERROR_FORMAT_NOT_SUPPORTED,
                             "VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT "
                             "requires VK_IMAGE_TILING_LINEAR or "
                             "VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT");
         }
         ext_mem_props = &nvk_dma_buf_mem_props;
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
                           vk_ExternalMemoryHandleTypeFlagBits_to_str(external_info->handleType));
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

   pImageFormatProperties->imageFormatProperties = (VkImageFormatProperties) {
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
         VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = (void *) s;
         ycbcr_props->combinedImageSamplerDescriptorCount = plane_count;
         break;
      }
      default:
         vk_debug_ignored_stype(s->sType);
         break;
      }
   }

   return VK_SUCCESS;
}

static enum nil_image_dim
vk_image_type_to_nil_dim(VkImageType type)
{
   switch (type) {
   case VK_IMAGE_TYPE_1D:  return NIL_IMAGE_DIM_1D;
   case VK_IMAGE_TYPE_2D:  return NIL_IMAGE_DIM_2D;
   case VK_IMAGE_TYPE_3D:  return NIL_IMAGE_DIM_3D;
   default:
      unreachable("Invalid image type");
   }
}

static VkSparseImageFormatProperties
nvk_fill_sparse_image_fmt_props(VkImageAspectFlags aspects,
                                const enum pipe_format format,
                                const enum nil_image_dim dim,
                                const enum nil_sample_layout sample_layout)
{
   struct nil_Extent4D_Pixels sparse_block_extent_px =
      nil_sparse_block_extent_px(nil_format(format), dim, sample_layout);

   assert(sparse_block_extent_px.array_len == 1);

   VkSparseImageFormatProperties sparse_format_props = {
      .aspectMask = aspects,
      .flags = VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT,
      .imageGranularity = {
         .width = sparse_block_extent_px.width,
         .height = sparse_block_extent_px.height,
         .depth = sparse_block_extent_px.depth,
      },
   };

   return sparse_format_props;
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetPhysicalDeviceSparseImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2* pFormatInfo,
    uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2 *pProperties)
{
   VkResult result;

   /* Check if the given format info is valid first before returning sparse
    * props.  The easiest way to do this is to just call
    * nvk_GetPhysicalDeviceImageFormatProperties2()
    */
   const VkPhysicalDeviceImageFormatInfo2 img_fmt_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = pFormatInfo->format,
      .type = pFormatInfo->type,
      .tiling = pFormatInfo->tiling,
      .usage = pFormatInfo->usage,
      .flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
               VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
   };

   VkImageFormatProperties2 img_fmt_props2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = NULL,
   };

   result = nvk_GetPhysicalDeviceImageFormatProperties2(physicalDevice,
                                                        &img_fmt_info,
                                                        &img_fmt_props2);
   if (result != VK_SUCCESS) {
      *pPropertyCount = 0;
      return;
   }

   const VkImageFormatProperties *props = &img_fmt_props2.imageFormatProperties;
   if (!(pFormatInfo->samples & props->sampleCounts)) {
      *pPropertyCount = 0;
      return;
   }

   VK_OUTARRAY_MAKE_TYPED(VkSparseImageFormatProperties2, out,
                          pProperties, pPropertyCount);

   VkImageAspectFlags aspects = vk_format_aspects(pFormatInfo->format);
   const enum pipe_format pipe_format =
      vk_format_to_pipe_format(pFormatInfo->format);
   const enum nil_image_dim dim = vk_image_type_to_nil_dim(pFormatInfo->type);
   const enum nil_sample_layout sample_layout =
      nil_choose_sample_layout(pFormatInfo->samples);

   vk_outarray_append_typed(VkSparseImageFormatProperties2, &out, props) {
      props->properties = nvk_fill_sparse_image_fmt_props(aspects, pipe_format,
                                                          dim, sample_layout);
   }
}

static VkResult
nvk_image_init(struct nvk_device *dev,
               struct nvk_image *image,
               const VkImageCreateInfo *pCreateInfo)
{
   struct nvk_physical_device *pdev = nvk_device_physical(dev);

   vk_image_init(&dev->vk, &image->vk, pCreateInfo);

   if ((image->vk.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       image->vk.samples > 1) {
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
      image->vk.stencil_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   }

   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   nil_image_usage_flags usage = 0;
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR)
      usage |= NIL_IMAGE_USAGE_LINEAR_BIT;
   if (pCreateInfo->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT)
      usage |= NIL_IMAGE_USAGE_2D_VIEW_BIT;
   if (pCreateInfo->flags & VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT)
      usage |= NIL_IMAGE_USAGE_2D_VIEW_BIT;

   /* In order to be able to clear 3D depth/stencil images, we need to bind
    * them as 2D arrays.  Fortunately, 3D depth/stencil shouldn't be common.
    */
   if ((image->vk.aspects & (VK_IMAGE_ASPECT_DEPTH_BIT |
                             VK_IMAGE_ASPECT_STENCIL_BIT)) &&
       pCreateInfo->imageType == VK_IMAGE_TYPE_3D)
      usage |= NIL_IMAGE_USAGE_2D_VIEW_BIT;

   image->plane_count = vk_format_get_plane_count(pCreateInfo->format);
   image->disjoint = image->plane_count > 1 &&
                     (pCreateInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT);

   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) {
      /* Sparse multiplane is not supported */
      assert(image->plane_count == 1);
      usage |= NIL_IMAGE_USAGE_SPARSE_RESIDENCY_BIT;
   }

   uint32_t explicit_row_stride_B = 0;
   if (image->vk.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      /* Modifiers are not supported with YCbCr */
      assert(image->plane_count == 1);

      const struct VkImageDrmFormatModifierExplicitCreateInfoEXT *mod_explicit_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
      if (mod_explicit_info) {
         image->vk.drm_format_mod = mod_explicit_info->drmFormatModifier;
         /* Normally with explicit modifiers, the client specifies all strides,
          * however in our case, we can only really make use of this in the linear
          * case, and we can only create 2D non-array linear images, so ultimately
          * we only care about the row stride. 
          */
         explicit_row_stride_B = mod_explicit_info->pPlaneLayouts->rowPitch;
      } else {
         const struct VkImageDrmFormatModifierListCreateInfoEXT *mod_list_info =
            vk_find_struct_const(pCreateInfo->pNext,
                                 IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);

         enum pipe_format p_format =
            vk_format_to_pipe_format(pCreateInfo->format);
         image->vk.drm_format_mod =
            nil_select_best_drm_format_mod(&pdev->info, nil_format(p_format),
                                           mod_list_info->drmFormatModifierCount,
                                           mod_list_info->pDrmFormatModifiers);
         assert(image->vk.drm_format_mod != DRM_FORMAT_MOD_INVALID);
      }

      if (image->vk.drm_format_mod == DRM_FORMAT_MOD_LINEAR) {
         /* We only have one shadow plane per nvk_image */
         assert(image->plane_count == 1);

         struct nil_image_init_info tiled_shadow_nil_info = {
            .dim = vk_image_type_to_nil_dim(pCreateInfo->imageType),
            .format = nil_format(vk_format_to_pipe_format(image->vk.format)),
            .modifier = DRM_FORMAT_MOD_INVALID,
            .extent_px = {
               .width = pCreateInfo->extent.width,
               .height = pCreateInfo->extent.height,
               .depth = pCreateInfo->extent.depth,
               .array_len = pCreateInfo->arrayLayers,
            },
            .levels = pCreateInfo->mipLevels,
            .samples = pCreateInfo->samples,
            .usage = usage & ~NIL_IMAGE_USAGE_LINEAR_BIT,
            .explicit_row_stride_B = 0,
         };
         image->linear_tiled_shadow.nil =
            nil_image_new(&pdev->info, &tiled_shadow_nil_info);
      }
   }

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(pCreateInfo->format);
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      VkFormat format = ycbcr_info ?
         ycbcr_info->planes[plane].format : pCreateInfo->format;
      const uint8_t width_scale = ycbcr_info ?
         ycbcr_info->planes[plane].denominator_scales[0] : 1;
      const uint8_t height_scale = ycbcr_info ?
         ycbcr_info->planes[plane].denominator_scales[1] : 1;
      struct nil_image_init_info nil_info = {
         .dim = vk_image_type_to_nil_dim(pCreateInfo->imageType),
         .format = nil_format(vk_format_to_pipe_format(format)),
         .modifier = image->vk.drm_format_mod,
         .extent_px = {
            .width = pCreateInfo->extent.width / width_scale,
            .height = pCreateInfo->extent.height / height_scale,
            .depth = pCreateInfo->extent.depth,
            .array_len = pCreateInfo->arrayLayers,
         },
         .levels = pCreateInfo->mipLevels,
         .samples = pCreateInfo->samples,
         .usage = usage,
         .explicit_row_stride_B = explicit_row_stride_B,
      };

      image->planes[plane].nil = nil_image_new(&pdev->info, &nil_info);
   }

   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      struct nil_image_init_info stencil_nil_info = {
         .dim = vk_image_type_to_nil_dim(pCreateInfo->imageType),
         .format = nil_format(PIPE_FORMAT_R32_UINT),
         .modifier = DRM_FORMAT_MOD_INVALID,
         .extent_px = {
            .width = pCreateInfo->extent.width,
            .height = pCreateInfo->extent.height,
            .depth = pCreateInfo->extent.depth,
            .array_len = pCreateInfo->arrayLayers,
         },
         .levels = pCreateInfo->mipLevels,
         .samples = pCreateInfo->samples,
         .usage = usage,
         .explicit_row_stride_B = 0,
      };

      image->stencil_copy_temp.nil =
         nil_image_new(&pdev->info, &stencil_nil_info);
   }

   return VK_SUCCESS;
}

static void
nvk_image_plane_size_align_B(struct nvk_device *dev,
                             const struct nvk_image *image,
                             const struct nvk_image_plane *plane,
                             uint64_t *size_B_out, uint64_t *align_B_out)
{
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   const bool sparse_bound =
      image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT;

   assert(util_is_power_of_two_or_zero64(plane->nil.align_B));
   if (sparse_bound || plane->nil.pte_kind) {
      *align_B_out = MAX2(plane->nil.align_B, pdev->nvkmd->bind_align_B);
   } else {
      *align_B_out = plane->nil.align_B;
   }
   *size_B_out = align64(plane->nil.size_B, *align_B_out);
}

static VkResult
nvk_image_plane_alloc_va(struct nvk_device *dev,
                         const struct nvk_image *image,
                         struct nvk_image_plane *plane)
{
   VkResult result;

   const bool sparse_bound =
      image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
   const bool sparse_resident =
      image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
   assert(sparse_bound || !sparse_resident);

   if (sparse_bound || plane->nil.pte_kind) {
      enum nvkmd_va_flags va_flags = 0;
      if (sparse_resident)
         va_flags |= NVKMD_VA_SPARSE;

      uint64_t va_size_B, va_align_B;
      nvk_image_plane_size_align_B(dev, image, plane, &va_size_B, &va_align_B);

      result = nvkmd_dev_alloc_va(dev->nvkmd, &dev->vk.base,
                                  va_flags, plane->nil.pte_kind,
                                  va_size_B, va_align_B,
                                  0 /* fixed_addr */, &plane->va);
      if (result != VK_SUCCESS)
         return result;

      plane->addr = plane->va->addr;
   }

   return VK_SUCCESS;
}

static void
nvk_image_plane_finish(struct nvk_device *dev,
                       struct nvk_image_plane *plane,
                       VkImageCreateFlags create_flags,
                       const VkAllocationCallbacks *pAllocator)
{
   if (plane->va != NULL)
      nvkmd_va_free(plane->va);
}

static void
nvk_image_finish(struct nvk_device *dev, struct nvk_image *image,
                 const VkAllocationCallbacks *pAllocator)
{
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      nvk_image_plane_finish(dev, &image->planes[plane],
                             image->vk.create_flags, pAllocator);
   }

   if (image->stencil_copy_temp.nil.size_B > 0) {
      nvk_image_plane_finish(dev, &image->stencil_copy_temp,
                             image->vk.create_flags, pAllocator);
   }

   assert(image->linear_tiled_shadow.va == NULL);
   if (image->linear_tiled_shadow_mem != NULL)
      nvkmd_mem_unref(image->linear_tiled_shadow_mem);

   vk_image_finish(&image->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateImage(VkDevice _device,
                const VkImageCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkImage *pImage)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_image *image;
   VkResult result;

#ifdef NVK_USE_WSI_PLATFORM
   /* Ignore swapchain creation info on Android. Since we don't have an
    * implementation in Mesa, we're guaranteed to access an Android object
    * incorrectly.
    */
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(&pdev->wsi_device,
                                               pCreateInfo,
                                               swapchain_info->swapchain,
                                               pImage);
   }
#endif

   image = vk_zalloc2(&dev->vk.alloc, pAllocator, sizeof(*image), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = nvk_image_init(dev, image, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->vk.alloc, pAllocator, image);
      return result;
   }

   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      result = nvk_image_plane_alloc_va(dev, image, &image->planes[plane]);
      if (result != VK_SUCCESS) {
         nvk_image_finish(dev, image, pAllocator);
         vk_free2(&dev->vk.alloc, pAllocator, image);
         return result;
      }
   }

   if (image->stencil_copy_temp.nil.size_B > 0) {
      result = nvk_image_plane_alloc_va(dev, image, &image->stencil_copy_temp);
      if (result != VK_SUCCESS) {
         nvk_image_finish(dev, image, pAllocator);
         vk_free2(&dev->vk.alloc, pAllocator, image);
         return result;
      }
   }

   if (image->linear_tiled_shadow.nil.size_B > 0) {
      struct nvk_image_plane *shadow = &image->linear_tiled_shadow;
      result = nvkmd_dev_alloc_tiled_mem(dev->nvkmd, &dev->vk.base,
                                         shadow->nil.size_B, shadow->nil.align_B,
                                         shadow->nil.pte_kind, shadow->nil.tile_mode,
                                         NVKMD_MEM_LOCAL,
                                         &image->linear_tiled_shadow_mem);
      if (result != VK_SUCCESS) {
         nvk_image_finish(dev, image, pAllocator);
         vk_free2(&dev->vk.alloc, pAllocator, image);
         return result;
      }
      shadow->addr = image->linear_tiled_shadow_mem->va->addr;
   }

   *pImage = nvk_image_to_handle(image);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyImage(VkDevice device,
                 VkImage _image,
                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_image, image, _image);

   if (!image)
      return;

   nvk_image_finish(dev, image, pAllocator);
   vk_free2(&dev->vk.alloc, pAllocator, image);
}

static void
nvk_image_plane_add_req(struct nvk_device *dev,
                        const struct nvk_image *image,
                        const struct nvk_image_plane *plane,
                        uint64_t *size_B, uint32_t *align_B)
{
   assert(util_is_power_of_two_or_zero64(*align_B));
   uint64_t plane_size_B, plane_align_B;
   nvk_image_plane_size_align_B(dev, image, plane,
                                &plane_size_B, &plane_align_B);

   *align_B = MAX2(*align_B, plane_align_B);
   *size_B = align64(*size_B, plane_align_B);
   *size_B += plane_size_B;
}

static void
nvk_get_image_memory_requirements(struct nvk_device *dev,
                                  struct nvk_image *image,
                                  VkImageAspectFlags aspects,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   uint32_t memory_types = (1 << pdev->mem_type_count) - 1;

   // TODO hope for the best?

   uint64_t size_B = 0;
   uint32_t align_B = 0;
   if (image->disjoint) {
      uint8_t plane = nvk_image_memory_aspects_to_plane(image, aspects);
      nvk_image_plane_add_req(dev, image, &image->planes[plane],
                              &size_B, &align_B);
   } else {
      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         nvk_image_plane_add_req(dev, image, &image->planes[plane],
                                 &size_B, &align_B);
      }
   }

   if (image->stencil_copy_temp.nil.size_B > 0) {
      nvk_image_plane_add_req(dev, image, &image->stencil_copy_temp,
                              &size_B, &align_B);
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
nvk_GetImageMemoryRequirements2(VkDevice device,
                                const VkImageMemoryRequirementsInfo2 *pInfo,
                                VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_image, image, pInfo->image);

   const VkImagePlaneMemoryRequirementsInfo *plane_info =
      vk_find_struct_const(pInfo->pNext, IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO);
   const VkImageAspectFlags aspects =
      image->disjoint ? plane_info->planeAspect : image->vk.aspects;

   nvk_get_image_memory_requirements(dev, image, aspects,
                                     pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDeviceImageMemoryRequirements(VkDevice device,
                                     const VkDeviceImageMemoryRequirements *pInfo,
                                     VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   ASSERTED VkResult result;
   struct nvk_image image = {0};

   result = nvk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   const VkImageAspectFlags aspects =
      image.disjoint ? pInfo->planeAspect : image.vk.aspects;

   nvk_get_image_memory_requirements(dev, &image, aspects,
                                     pMemoryRequirements);

   nvk_image_finish(dev, &image, NULL);
}

static VkSparseImageMemoryRequirements
nvk_fill_sparse_image_memory_reqs(const struct nil_image *nil,
                                  const struct nil_image *stencil_tmp,
                                  VkImageAspectFlags aspects)
{
   VkSparseImageFormatProperties sparse_format_props =
      nvk_fill_sparse_image_fmt_props(aspects, nil->format.p_format,
                                      nil->dim, nil->sample_layout);

   assert(nil->mip_tail_first_lod <= nil->num_levels);
   VkSparseImageMemoryRequirements sparse_memory_reqs = {
      .formatProperties = sparse_format_props,
      .imageMipTailFirstLod = nil->mip_tail_first_lod,
      .imageMipTailStride = 0,
   };

   if (nil->mip_tail_first_lod == 0) {
      sparse_memory_reqs.imageMipTailSize = nil->size_B;
      sparse_memory_reqs.imageMipTailOffset = 0;
   } else if (nil->mip_tail_first_lod < nil->num_levels) {
      sparse_memory_reqs.imageMipTailSize =
         nil_image_mip_tail_size_B(nil) * nil->extent_px.array_len;
      sparse_memory_reqs.imageMipTailOffset = NVK_MIP_TAIL_START_OFFSET;
   } else {
      sparse_memory_reqs.imageMipTailSize = 0;
      sparse_memory_reqs.imageMipTailOffset = NVK_MIP_TAIL_START_OFFSET;
   }

   if (stencil_tmp != NULL)
      sparse_memory_reqs.imageMipTailSize += stencil_tmp->size_B;

   return sparse_memory_reqs;
}

static void
nvk_get_image_sparse_memory_requirements(
   struct nvk_device *dev,
   struct nvk_image *image,
   VkImageAspectFlags aspects,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_OUTARRAY_MAKE_TYPED(VkSparseImageMemoryRequirements2, out,
                          pSparseMemoryRequirements,
                          pSparseMemoryRequirementCount);

   /* From the Vulkan 1.3.279 spec:
    *
    *    "The sparse image must have been created using the
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT flag to retrieve valid sparse
    *    image memory requirements."
    */
   if (!(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
      return;

   /* We don't support multiplane sparse for now */
   if (image->plane_count > 1)
      return;

   const struct nil_image *stencil_tmp = NULL;
   if (image->stencil_copy_temp.nil.size_B > 0)
      stencil_tmp = &image->stencil_copy_temp.nil;

   vk_outarray_append_typed(VkSparseImageMemoryRequirements2, &out, reqs) {
      reqs->memoryRequirements =
         nvk_fill_sparse_image_memory_reqs(&image->planes[0].nil,
                                           stencil_tmp, aspects);
   };
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetImageSparseMemoryRequirements2(
   VkDevice device,
   const VkImageSparseMemoryRequirementsInfo2* pInfo,
   uint32_t* pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_image, image, pInfo->image);

   const VkImageAspectFlags aspects = image->vk.aspects;

   nvk_get_image_sparse_memory_requirements(dev, image, aspects,
                                            pSparseMemoryRequirementCount,
                                            pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDeviceImageSparseMemoryRequirements(
   VkDevice device,
   const VkDeviceImageMemoryRequirements* pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   ASSERTED VkResult result;
   struct nvk_image image = {0};

   result = nvk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   const VkImageAspectFlags aspects =
      image.disjoint ? pInfo->planeAspect : image.vk.aspects;

   nvk_get_image_sparse_memory_requirements(dev, &image, aspects,
                                            pSparseMemoryRequirementCount,
                                            pSparseMemoryRequirements);

   nvk_image_finish(dev, &image, NULL);
}

static void
nvk_get_image_subresource_layout(struct nvk_device *dev,
                                 struct nvk_image *image,
                                 const VkImageSubresource2KHR *pSubresource,
                                 VkSubresourceLayout2KHR *pLayout)
{
   const VkImageSubresource *isr = &pSubresource->imageSubresource;

   const uint8_t p = nvk_image_memory_aspects_to_plane(image, isr->aspectMask);
   const struct nvk_image_plane *plane = &image->planes[p];

   uint64_t offset_B = 0;
   if (!image->disjoint) {
      uint32_t align_B = 0;
      for (unsigned plane = 0; plane < p; plane++) {
         nvk_image_plane_add_req(dev, image, &image->planes[plane],
                                 &offset_B, &align_B);
      }
   }
   offset_B += nil_image_level_layer_offset_B(&plane->nil, isr->mipLevel,
                                              isr->arrayLayer);

   pLayout->subresourceLayout = (VkSubresourceLayout) {
      .offset = offset_B,
      .size = nil_image_level_size_B(&plane->nil, isr->mipLevel),
      .rowPitch = plane->nil.levels[isr->mipLevel].row_stride_B,
      .arrayPitch = plane->nil.array_stride_B,
      .depthPitch = nil_image_level_depth_stride_B(&plane->nil, isr->mipLevel),
   };
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetImageSubresourceLayout2KHR(VkDevice device,
                                  VkImage _image,
                                  const VkImageSubresource2KHR *pSubresource,
                                  VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VK_FROM_HANDLE(nvk_image, image, _image);

   nvk_get_image_subresource_layout(dev, image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL
nvk_GetDeviceImageSubresourceLayoutKHR(
    VkDevice device,
    const VkDeviceImageSubresourceInfoKHR *pInfo,
    VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   ASSERTED VkResult result;
   struct nvk_image image = {0};

   result = nvk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   nvk_get_image_subresource_layout(dev, &image, pInfo->pSubresource, pLayout);

   nvk_image_finish(dev, &image, NULL);
}

static VkResult
nvk_image_plane_bind(struct nvk_device *dev,
                     struct nvk_image *image,
                     struct nvk_image_plane *plane,
                     struct nvk_device_memory *mem,
                     uint64_t *offset_B)
{
   uint64_t plane_size_B, plane_align_B;
   nvk_image_plane_size_align_B(dev, image, plane,
                                &plane_size_B, &plane_align_B);
   *offset_B = align64(*offset_B, plane_align_B);

   if (plane->va != NULL) {
      VkResult result = nvkmd_va_bind_mem(plane->va, &image->vk.base, 0,
                                          mem->mem, *offset_B,
                                          plane->va->size_B);
      if (result != VK_SUCCESS)
         return result;
   } else {
      assert(plane->nil.pte_kind == 0);
      plane->addr = mem->mem->va->addr + *offset_B;
   }

   *offset_B += plane_size_B;

   return VK_SUCCESS;
}

static VkResult
nvk_bind_image_memory(struct nvk_device *dev,
                      const VkBindImageMemoryInfo *info)
{
   VK_FROM_HANDLE(nvk_device_memory, mem, info->memory);
   VK_FROM_HANDLE(nvk_image, image, info->image);
   VkResult result;

   /* Ignore this struct on Android, we cannot access swapchain structures there. */
#ifdef NVK_USE_WSI_PLATFORM
   const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
      vk_find_struct_const(info->pNext, BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);

   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      VkImage _wsi_image = wsi_common_get_image(swapchain_info->swapchain,
                                                swapchain_info->imageIndex);
      VK_FROM_HANDLE(nvk_image, wsi_img, _wsi_image);

      assert(image->plane_count == 1);
      assert(wsi_img->plane_count == 1);

      struct nvk_image_plane *plane = &image->planes[0];
      struct nvk_image_plane *swapchain_plane = &wsi_img->planes[0];

      /* Copy memory binding information from swapchain image to the current image's plane. */
      plane->addr = swapchain_plane->addr;

      return VK_SUCCESS;
   }
#endif

   uint64_t offset_B = info->memoryOffset;
   if (image->disjoint) {
      const VkBindImagePlaneMemoryInfo *plane_info =
         vk_find_struct_const(info->pNext, BIND_IMAGE_PLANE_MEMORY_INFO);
      const uint8_t plane =
         nvk_image_memory_aspects_to_plane(image, plane_info->planeAspect);
      result = nvk_image_plane_bind(dev, image, &image->planes[plane],
                                    mem, &offset_B);
      if (result != VK_SUCCESS)
         return result;
   } else {
      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         result = nvk_image_plane_bind(dev, image, &image->planes[plane],
                                       mem, &offset_B);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   if (image->stencil_copy_temp.nil.size_B > 0) {
      result = nvk_image_plane_bind(dev, image, &image->stencil_copy_temp,
                                    mem, &offset_B);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_BindImageMemory2(VkDevice device,
                     uint32_t bindInfoCount,
                     const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(nvk_device, dev, device);
   VkResult first_error_or_success = VK_SUCCESS;

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VkResult result = nvk_bind_image_memory(dev, &pBindInfos[i]);

      const VkBindMemoryStatusKHR *status =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_MEMORY_STATUS_KHR);
      if (status != NULL && status->pResult != NULL)
         *status->pResult = VK_SUCCESS;

      if (first_error_or_success == VK_SUCCESS)
         first_error_or_success = result;
   }

   return first_error_or_success;
}


static VkResult
queue_image_plane_bind(struct nvk_queue *queue,
                       const struct nvk_image_plane *plane,
                       const VkSparseImageMemoryBind *bind)
{
   VK_FROM_HANDLE(nvk_device_memory, mem, bind->memory);
   uint64_t image_bind_offset_B;

   const uint64_t mem_bind_offset_B = bind->memoryOffset;
   const uint32_t layer = bind->subresource.arrayLayer;
   const uint32_t level = bind->subresource.mipLevel;

   const struct nil_tiling plane_tiling = plane->nil.levels[level].tiling;
   const uint32_t tile_size_B = nil_tiling_size_B(&plane_tiling);

   const struct nil_Extent4D_Pixels bind_extent_px = {
      .width = bind->extent.width,
      .height = bind->extent.height,
      .depth = bind->extent.depth,
      .array_len = 1,
   };
   const struct nil_Offset4D_Pixels bind_offset_px = {
      .x = bind->offset.x,
      .y = bind->offset.y,
      .z = bind->offset.z,
      .a = layer,
   };

   const struct nil_Extent4D_Pixels level_extent_px =
      nil_image_level_extent_px(&plane->nil, level);
   const struct nil_Extent4D_Tiles level_extent_tl =
      nil_extent4d_px_to_tl(level_extent_px, &plane_tiling,
                            plane->nil.format,
                            plane->nil.sample_layout);

   /* Convert the extent and offset to tiles */
   const struct nil_Extent4D_Tiles bind_extent_tl =
      nil_extent4d_px_to_tl(bind_extent_px, &plane_tiling,
                            plane->nil.format,
                            plane->nil.sample_layout);
   const struct nil_Offset4D_Tiles bind_offset_tl =
      nil_offset4d_px_to_tl(bind_offset_px, &plane_tiling,
                            plane->nil.format,
                            plane->nil.sample_layout);

   image_bind_offset_B =
      nil_image_level_layer_offset_B(&plane->nil, level, layer);

   /* We can only bind contiguous ranges, so we'll split the image into rows
    * of tiles that are guaranteed to be contiguous, and bind in terms of
    * these rows
    */

   /* First, get the size of the bind. Since we have the extent in terms of
    * tiles already, we just need to multiply that by the tile size to get
    * the size in bytes
    */
   uint64_t row_bind_size_B = bind_extent_tl.width * tile_size_B;

   const uint32_t nvkmd_bind_count = bind_extent_tl.depth *
                                     bind_extent_tl.height;
   STACK_ARRAY(struct nvkmd_ctx_bind, nvkmd_binds, nvkmd_bind_count);
   uint32_t nvkmd_bind_idx = 0;

   /* Second, start walking the binding region in units of tiles, starting
    * from the third dimension
    */
   for (uint32_t z_tl = 0; z_tl < bind_extent_tl.depth; z_tl++) {
      /* Start walking the rows to be bound */
      for (uint32_t y_tl = 0; y_tl < bind_extent_tl.height; y_tl++) {
         /* For the bind offset, get a memory offset to the start of the row
          * in terms of the bind extent
          */
         const uint64_t mem_row_start_tl =
            y_tl * bind_extent_tl.width +
            z_tl * bind_extent_tl.width * bind_extent_tl.height;

         const uint32_t image_x_tl = bind_offset_tl.x;
         const uint32_t image_y_tl = bind_offset_tl.y + y_tl;
         const uint32_t image_z_tl = bind_offset_tl.z + z_tl;

         /* The image offset is calculated in terms of the level extent */
         const uint64_t image_row_start_tl =
            image_x_tl +
            image_y_tl * level_extent_tl.width +
            image_z_tl * level_extent_tl.width * level_extent_tl.height;

         nvkmd_binds[nvkmd_bind_idx++] = (struct nvkmd_ctx_bind) {
            .op = mem ? NVKMD_BIND_OP_BIND : NVKMD_BIND_OP_UNBIND,
            .va = plane->va,
            .va_offset_B = image_bind_offset_B +
                           image_row_start_tl * tile_size_B,
            .mem = mem ? mem->mem : NULL,
            .mem_offset_B = mem_bind_offset_B +
                            mem_row_start_tl * tile_size_B,
            .range_B = row_bind_size_B,
         };
      }
   }

   assert(nvkmd_bind_idx == nvkmd_bind_count);
   VkResult result = nvkmd_ctx_bind(queue->bind_ctx, &queue->vk.base,
                                    nvkmd_bind_count, nvkmd_binds);

   STACK_ARRAY_FINISH(nvkmd_binds);

   return result;
}

VkResult
nvk_queue_image_bind(struct nvk_queue *queue,
                     const VkSparseImageMemoryBindInfo *bind_info)
{
   VK_FROM_HANDLE(nvk_image, image, bind_info->image);
   VkResult result;

   /* Sparse residency with multiplane is currently not supported */
   assert(image->plane_count == 1);

   for (unsigned i = 0; i < bind_info->bindCount; i++) {
      result = queue_image_plane_bind(queue, &image->planes[0],
                                      &bind_info->pBinds[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static bool
next_opaque_bind_plane(const VkSparseMemoryBind *bind,
                       uint64_t size_B, uint32_t align_B,
                       uint64_t *plane_offset_B,
                       uint64_t *mem_offset_B,
                       uint64_t *bind_size_B,
                       uint64_t *image_plane_offset_B_iter)
{
   /* Figure out the offset to thise plane and increment _iter up-front so
    * that we're free to early return elsewhere in the function.
    */
   *image_plane_offset_B_iter = align64(*image_plane_offset_B_iter, align_B);
   const uint64_t image_plane_offset_B = *image_plane_offset_B_iter;
   *image_plane_offset_B_iter += size_B;

   /* Offset into the image or image mip tail, as appropriate */
   uint64_t bind_offset_B = bind->resourceOffset;
   if (bind_offset_B >= NVK_MIP_TAIL_START_OFFSET)
      bind_offset_B -= NVK_MIP_TAIL_START_OFFSET;

   if (bind_offset_B < image_plane_offset_B) {
      /* The offset of the plane within the bind */
      const uint64_t bind_plane_offset_B =
         image_plane_offset_B - bind_offset_B;

      /* If this plane lies above the bound range, skip this plane */
      if (bind_plane_offset_B >= bind->size)
         return false;

      *plane_offset_B = 0;
      *mem_offset_B = bind->memoryOffset + bind_plane_offset_B;
      *bind_size_B = MIN2(bind->size - bind_plane_offset_B, size_B);
   } else {
      /* The offset of the bind within the plane */
      const uint64_t plane_bind_offset_B =
         bind_offset_B - image_plane_offset_B;

      /* If this plane lies below the bound range, skip this plane */
      if (plane_bind_offset_B >= size_B)
         return false;

      *plane_offset_B = plane_bind_offset_B;
      *mem_offset_B = bind->memoryOffset;
      *bind_size_B = MIN2(bind->size, size_B - plane_bind_offset_B);
   }

   return true;
}

static VkResult
queue_image_plane_opaque_bind(struct nvk_queue *queue,
                              struct nvk_image *image,
                              struct nvk_image_plane *plane,
                              const VkSparseMemoryBind *bind,
                              uint64_t *image_plane_offset_B)
{
   uint64_t plane_size_B, plane_align_B;
   nvk_image_plane_size_align_B(nvk_queue_device(queue), image, plane,
                                &plane_size_B, &plane_align_B);

   uint64_t plane_offset_B, mem_offset_B, bind_size_B;
   if (!next_opaque_bind_plane(bind, plane_size_B, plane_align_B,
                               &plane_offset_B, &mem_offset_B, &bind_size_B,
                               image_plane_offset_B))
      return VK_SUCCESS;

   VK_FROM_HANDLE(nvk_device_memory, mem, bind->memory);

   assert(plane_offset_B + bind_size_B <= plane->va->size_B);
   assert(!mem || mem_offset_B + bind_size_B <= mem->vk.size);

   const struct nvkmd_ctx_bind nvkmd_bind = {
      .op = mem ? NVKMD_BIND_OP_BIND : NVKMD_BIND_OP_UNBIND,
      .va = plane->va,
      .va_offset_B = plane_offset_B,
      .mem = mem ? mem->mem : NULL,
      .mem_offset_B = mem_offset_B,
      .range_B = bind_size_B,
   };
   return nvkmd_ctx_bind(queue->bind_ctx, &queue->vk.base, 1, &nvkmd_bind);
}

static VkResult
queue_image_plane_bind_mip_tail(struct nvk_queue *queue,
                                struct nvk_image *image,
                                struct nvk_image_plane *plane,
                                const VkSparseMemoryBind *bind,
                                uint64_t *image_plane_offset_B)
{
   uint64_t plane_size_B, plane_align_B;
   nvk_image_plane_size_align_B(nvk_queue_device(queue), image, plane,
                                &plane_size_B, &plane_align_B);

   const uint64_t mip_tail_offset_B =
      nil_image_mip_tail_offset_B(&plane->nil);
   const uint64_t mip_tail_size_B =
      nil_image_mip_tail_size_B(&plane->nil);
   const uint64_t mip_tail_stride_B = plane->nil.array_stride_B;

   const uint64_t whole_mip_tail_size_B =
      mip_tail_size_B * plane->nil.extent_px.array_len;

   uint64_t plane_offset_B, mem_offset_B, bind_size_B;
   if (!next_opaque_bind_plane(bind, whole_mip_tail_size_B, plane_align_B,
                               &plane_offset_B, &mem_offset_B, &bind_size_B,
                               image_plane_offset_B))
      return VK_SUCCESS;

   VK_FROM_HANDLE(nvk_device_memory, mem, bind->memory);

   /* Range within the virtual mip_tail space */
   const uint64_t mip_bind_start_B = plane_offset_B;
   const uint64_t mip_bind_end_B = mip_bind_start_B + bind_size_B;

   /* Range of array slices covered by this bind */
   const uint32_t start_a = mip_bind_start_B / mip_tail_size_B;
   const uint32_t end_a = DIV_ROUND_UP(mip_bind_end_B, mip_tail_size_B);

   const uint32_t nvkmd_bind_count = end_a - start_a;
   STACK_ARRAY(struct nvkmd_ctx_bind, nvkmd_binds, nvkmd_bind_count);
   uint32_t nvkmd_bind_idx = 0;

   for (uint32_t a = start_a; a < end_a; a++) {
      /* Range within the virtual mip_tail space of this array slice */
      const uint64_t a_mip_bind_start_B =
         MAX2(a * mip_tail_size_B, mip_bind_start_B);
      const uint64_t a_mip_bind_end_B =
         MIN2((a + 1) * mip_tail_size_B, mip_bind_end_B);

      /* Offset and range within this mip_tail slice */
      const uint64_t a_offset_B = a_mip_bind_start_B - a * mip_tail_size_B;
      const uint64_t a_range_B = a_mip_bind_end_B - a_mip_bind_start_B;

      /* Offset within the current bind operation */
      const uint64_t a_bind_offset_B =
         a_mip_bind_start_B - mip_bind_start_B;

      /* Offset within the image */
      const uint64_t a_image_offset_B =
         mip_tail_offset_B + (a * mip_tail_stride_B) + a_offset_B;

      nvkmd_binds[nvkmd_bind_idx++] = (struct nvkmd_ctx_bind) {
         .op = mem ? NVKMD_BIND_OP_BIND : NVKMD_BIND_OP_UNBIND,
         .va = plane->va,
         .va_offset_B = a_image_offset_B,
         .mem = mem ? mem->mem : NULL,
         .mem_offset_B = mem_offset_B + a_bind_offset_B,
         .range_B = a_range_B,
      };
   }

   assert(nvkmd_bind_idx == nvkmd_bind_count);
   VkResult result = nvkmd_ctx_bind(queue->bind_ctx, &queue->vk.base,
                                    nvkmd_bind_count, nvkmd_binds);

   STACK_ARRAY_FINISH(nvkmd_binds);

   return result;
}

VkResult
nvk_queue_image_opaque_bind(struct nvk_queue *queue,
                            const VkSparseImageOpaqueMemoryBindInfo *bind_info)
{
   VK_FROM_HANDLE(nvk_image, image, bind_info->image);
   VkResult result;

   for (unsigned i = 0; i < bind_info->bindCount; i++) {
      const VkSparseMemoryBind *bind = &bind_info->pBinds[i];

      uint64_t image_plane_offset_B = 0;
      for (unsigned plane = 0; plane < image->plane_count; plane++) {
         if (bind->resourceOffset >= NVK_MIP_TAIL_START_OFFSET) {
            result = queue_image_plane_bind_mip_tail(queue, image,
                                                     &image->planes[plane],
                                                     bind,
                                                     &image_plane_offset_B);
         } else {
            result = queue_image_plane_opaque_bind(queue, image,
                                                   &image->planes[plane],
                                                   bind,
                                                   &image_plane_offset_B);
         }
         if (result != VK_SUCCESS)
            return result;
      }
      if (image->stencil_copy_temp.nil.size_B > 0) {
         result = queue_image_plane_opaque_bind(queue, image,
                                                &image->stencil_copy_temp,
                                                bind,
                                                &image_plane_offset_B);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   return VK_SUCCESS;
}
