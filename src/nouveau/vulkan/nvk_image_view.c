/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_image_view.h"

#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_format.h"
#include "nvk_image.h"
#include "nvk_sampler.h"
#include "nvk_physical_device.h"

#include "vk_format.h"

#include "clb097.h"

static enum nil_view_type
vk_image_view_type_to_nil_view_type(VkImageViewType view_type)
{
   switch (view_type) {
   case VK_IMAGE_VIEW_TYPE_1D:         return NIL_VIEW_TYPE_1D;
   case VK_IMAGE_VIEW_TYPE_2D:         return NIL_VIEW_TYPE_2D;
   case VK_IMAGE_VIEW_TYPE_3D:         return NIL_VIEW_TYPE_3D;
   case VK_IMAGE_VIEW_TYPE_CUBE:       return NIL_VIEW_TYPE_CUBE;
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:   return NIL_VIEW_TYPE_1D_ARRAY;
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:   return NIL_VIEW_TYPE_2D_ARRAY;
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY: return NIL_VIEW_TYPE_CUBE_ARRAY;
   default:
      UNREACHABLE("Invalid image view type");
   }
}

static enum pipe_swizzle
vk_swizzle_to_pipe(VkComponentSwizzle swizzle)
{
   switch (swizzle) {
   case VK_COMPONENT_SWIZZLE_R:     return PIPE_SWIZZLE_X;
   case VK_COMPONENT_SWIZZLE_G:     return PIPE_SWIZZLE_Y;
   case VK_COMPONENT_SWIZZLE_B:     return PIPE_SWIZZLE_Z;
   case VK_COMPONENT_SWIZZLE_A:     return PIPE_SWIZZLE_W;
   case VK_COMPONENT_SWIZZLE_ONE:   return PIPE_SWIZZLE_1;
   case VK_COMPONENT_SWIZZLE_ZERO:  return PIPE_SWIZZLE_0;
   default:
      UNREACHABLE("Invalid component swizzle");
   }
}

static void
image_single_level_view(struct nil_image *image,
                        struct nil_view *view,
                        uint64_t *base_addr)
{
   assert(view->num_levels == 1);

   uint64_t offset_B;
   *image = nil_image_for_level(image, view->base_level, &offset_B);
   *base_addr += offset_B;
   view->base_level = 0;
}

static void
image_uncompressed_view(struct nil_image *image,
                        struct nil_view *view,
                        uint64_t *base_addr)
{
   assert(view->num_levels == 1);

   uint64_t offset_B;
   *image = nil_image_level_as_uncompressed(image, view->base_level, &offset_B);
   *base_addr += offset_B;
   view->base_level = 0;
}

static void
image_3d_view_as_2d_array(struct nil_image *image,
                          struct nil_view *view,
                          uint64_t *base_addr)
{
   assert(view->view_type == NIL_VIEW_TYPE_2D ||
          view->view_type == NIL_VIEW_TYPE_2D_ARRAY);
   assert(view->num_levels == 1);

   uint64_t offset_B;
   *image = nil_image_3d_level_as_2d_array(image, view->base_level, &offset_B);
   *base_addr += offset_B;
   view->base_level = 0;
}

VkResult
nvk_image_view_init(struct nvk_device *dev,
                    struct nvk_image_view *view,
                    const VkImageViewCreateInfo *pCreateInfo)
{
   const struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VK_FROM_HANDLE(nvk_image, image, pCreateInfo->image);
   VkResult result;

   const VkOpaqueCaptureDescriptorDataCreateInfoEXT *cap_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           OPAQUE_CAPTURE_DESCRIPTOR_DATA_CREATE_INFO_EXT);
   struct nvk_image_view_capture cap = {};
   if (cap_info != NULL)
      memcpy(&cap, cap_info->opaqueCaptureDescriptorData, sizeof(cap));

   memset(view, 0, sizeof(*view));

   vk_image_view_init(&dev->vk, &view->vk, pCreateInfo);

   /* First, figure out which image planes we need.
    * For depth/stencil, we may only have plane so simply assert
    * and then map directly betweeen the image and view plane
    */
   if (image->vk.aspects & (VK_IMAGE_ASPECT_DEPTH_BIT |
                            VK_IMAGE_ASPECT_STENCIL_BIT)) {
      view->separate_zs =
         image->separate_zs &&
         view->vk.aspects == (VK_IMAGE_ASPECT_DEPTH_BIT |
                              VK_IMAGE_ASPECT_STENCIL_BIT);

      if (view->separate_zs) {
         assert(image->plane_count == 2);
         view->plane_count = 2;
         view->planes[0].image_plane = 0;
         view->planes[1].image_plane = 1;
      } else {
         view->plane_count = 1;
         view->planes[0].image_plane =
            nvk_image_aspects_to_plane(image, view->vk.aspects);
      }
   } else {
      /* For other formats, retrieve the plane count from the aspect mask
       * and then walk through the aspect mask to map each image plane
       * to its corresponding view plane
       */
      assert(util_bitcount(view->vk.aspects) ==
             vk_format_get_plane_count(view->vk.format));
      view->plane_count = 0;
      u_foreach_bit(aspect_bit, view->vk.aspects) {
         uint8_t image_plane = nvk_image_aspects_to_plane(image, 1u << aspect_bit);
         view->planes[view->plane_count++].image_plane = image_plane;
      }
   }

   /* Finally, fill in each view plane separately */
   for (unsigned view_plane = 0; view_plane < view->plane_count; view_plane++) {
      const uint8_t image_plane = view->planes[view_plane].image_plane;
      struct nil_image nil_image = image->planes[image_plane].nil;
      uint64_t base_addr = nvk_image_base_address(image, image_plane);

      const struct vk_format_ycbcr_info *ycbcr_info =
         vk_format_get_ycbcr_info(view->vk.format);
      assert(ycbcr_info || view_plane == 0 || view->separate_zs);
      VkFormat plane_format = ycbcr_info ?
         ycbcr_info->planes[view_plane].format : view->vk.format;

      enum pipe_format p_format = nvk_format_to_pipe_format(plane_format);
      if (image->separate_zs)
         p_format = nil_image.format.p_format;
      else if (view->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT)
         p_format = util_format_stencil_only(nil_image.format.p_format);
      else if (view->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         p_format = nil_image.format.p_format;

      struct nil_view nil_view = {
         .view_type = vk_image_view_type_to_nil_view_type(view->vk.view_type),
         .format = nil_format(p_format),
         .base_level = view->vk.base_mip_level,
         .num_levels = view->vk.level_count,
         .base_array_layer = view->vk.base_array_layer,
         .array_len = view->vk.layer_count,
         .swizzle = {
            vk_swizzle_to_pipe(view->vk.swizzle.r),
            vk_swizzle_to_pipe(view->vk.swizzle.g),
            vk_swizzle_to_pipe(view->vk.swizzle.b),
            vk_swizzle_to_pipe(view->vk.swizzle.a),
         },
         .min_lod_clamp = view->vk.min_lod,
      };

      if (util_format_is_compressed(nil_image.format.p_format) &&
         !util_format_is_compressed(nil_view.format.p_format))
         image_uncompressed_view(&nil_image, &nil_view, &base_addr);

      if (nil_image.dim == NIL_IMAGE_DIM_3D &&
         nil_view.view_type != NIL_VIEW_TYPE_3D)
         image_3d_view_as_2d_array(&nil_image, &nil_view, &base_addr);

      view->planes[view_plane].sample_layout = nil_image.sample_layout;

      if (view->vk.usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
         nil_view.access = NIL_VIEW_ACCESS_TEXTURE;
         const struct nil_descriptor desc =
            nil_image_view_descriptor(&pdev->info, &nil_image,
                                      &nil_view, base_addr);

         uint32_t desc_index = 0;
         if (cap_info != NULL) {
            desc_index = view->plane_count == 1
               ? cap.single_plane.sampled_desc_index
               : cap.ycbcr.planes[view_plane].desc_index;
            result = nvk_descriptor_table_insert(dev, &dev->images,
                                                 desc_index,
                                                 &desc, sizeof(desc));
         } else {
            result = nvk_descriptor_table_add(dev, &dev->images,
                                              &desc, sizeof(desc),
                                              &desc_index);
         }
         if (result != VK_SUCCESS) {
            nvk_image_view_finish(dev, view);
            return result;
         }

         view->planes[view_plane].sampled_desc_index = desc_index;
      }

      if (view->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
         nil_view.access = NIL_VIEW_ACCESS_STORAGE;

         /* For storage images, we can't have any cubes */
         if (view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
            view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
            nil_view.view_type = NIL_VIEW_TYPE_2D_ARRAY;

         if (view->vk.view_type == VK_IMAGE_VIEW_TYPE_3D) {
            /* Without VK_AMD_shader_image_load_store_lod, the client can only
             * get at the first LOD from the shader anyway.
             */
            assert(view->vk.base_array_layer == 0);
            assert(view->vk.layer_count == 1);
            nil_view.num_levels = 1;
            image_single_level_view(&nil_image, &nil_view, &base_addr);

            if (view->vk.storage.z_slice_offset > 0 ||
                view->vk.storage.z_slice_count < nil_image.extent_px.depth) {
               nil_view.view_type = NIL_VIEW_TYPE_3D_SLICED;
               nil_view.base_array_layer = view->vk.storage.z_slice_offset;
               nil_view.array_len = view->vk.storage.z_slice_count;
            }
         }

         if (pdev->info.cls_eng3d >= MAXWELL_A) {
            const struct nil_descriptor desc =
               nil_image_view_descriptor(&pdev->info, &nil_image,
                                         &nil_view, base_addr);

            uint32_t desc_index = 0;
            if (cap_info != NULL) {
               assert(view->plane_count == 1);
               desc_index = cap.single_plane.storage_desc_index;
               result = nvk_descriptor_table_insert(dev, &dev->images,
                                                    desc_index, &desc,
                                                    sizeof(desc));
            } else {
               result = nvk_descriptor_table_add(dev, &dev->images,
                                                 &desc, sizeof(desc),
                                                 &desc_index);
            }
            if (result != VK_SUCCESS) {
               nvk_image_view_finish(dev, view);
               return result;
            }

            view->planes[view_plane].storage_desc_index = desc_index;
         } else {
            assert(view_plane == 0);
            view->su_info = nil_fill_su_info(&pdev->info,
                                             &nil_image, &nil_view,
                                             base_addr);
         }
      }
   }

   return VK_SUCCESS;
}

void
nvk_image_view_finish(struct nvk_device *dev,
                      struct nvk_image_view *view)
{
   for (uint8_t plane = 0; plane < view->plane_count; plane++) {
      if (view->planes[plane].sampled_desc_index) {
      nvk_descriptor_table_remove(dev, &dev->images,
                                  view->planes[plane].sampled_desc_index);
      }

      if (view->planes[plane].storage_desc_index) {
         nvk_descriptor_table_remove(dev, &dev->images,
                                    view->planes[plane].storage_desc_index);
      }
   }

   vk_image_view_finish(&view->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateImageView(VkDevice _device,
                    const VkImageViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkImageView *pView)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   struct nvk_image_view *view;
   VkResult result;

   view = vk_alloc2(&dev->vk.alloc, pAllocator, sizeof(*view), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = nvk_image_view_init(dev, view, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->vk.alloc, pAllocator, view);
      return result;
   }

   *pView = nvk_image_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyImageView(VkDevice _device,
                     VkImageView imageView,
                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_image_view, view, imageView);

   if (!view)
      return;

   nvk_image_view_finish(dev, view);
   vk_free2(&dev->vk.alloc, pAllocator, view);
}


VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetImageViewOpaqueCaptureDescriptorDataEXT(
    VkDevice _device,
    const VkImageViewCaptureDescriptorDataInfoEXT *pInfo,
    void *pData)
{
   VK_FROM_HANDLE(nvk_image_view, view, pInfo->imageView);

   struct nvk_image_view_capture cap = {};
   if (view->plane_count == 1) {
      cap.single_plane.sampled_desc_index = view->planes[0].sampled_desc_index;
      cap.single_plane.storage_desc_index = view->planes[0].storage_desc_index;
   } else {
      for (uint8_t p = 0; p < view->plane_count; p++) {
         cap.ycbcr.planes[p].desc_index = view->planes[p].sampled_desc_index;
         assert(view->planes[p].storage_desc_index == 0);
      }
   }

   memcpy(pData, &cap, sizeof(cap));

   return VK_SUCCESS;
}


VKAPI_ATTR uint32_t VKAPI_CALL
nvk_GetImageViewHandleNVX(VkDevice _device,
                          const VkImageViewHandleInfoNVX *pInfo)
{
   VK_FROM_HANDLE(nvk_image_view, view, pInfo->imageView);
   VK_FROM_HANDLE(nvk_sampler, sampler, pInfo->sampler);
   assert(view->plane_count == 1);

   uint32_t handle = 0;
   if (pInfo->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
      struct nvk_storage_image_descriptor desc;
      STATIC_ASSERT(sizeof(desc) == sizeof(handle));

      desc.image_index = view->planes[0].storage_desc_index;
      memcpy(&handle, &desc, sizeof(uint32_t));
   } else {
      struct nvk_sampled_image_descriptor desc;
      STATIC_ASSERT(sizeof(desc) == sizeof(handle));

      desc.image_index = view->planes[0].sampled_desc_index;
      if (pInfo->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
         assert(sampler->plane_count == 1);
         desc.sampler_index = sampler->planes[0].desc_index;
      }
      memcpy(&handle, &desc, sizeof(uint32_t));
   }

   return handle;
}

VKAPI_ATTR uint64_t VKAPI_CALL
nvk_GetImageViewHandle64NVX(VkDevice _device,
                            const VkImageViewHandleInfoNVX *pInfo)
{
   /* NVK does not currently support 64-bit texture addressing,
    * so this is the same as vkGetImageViewHandleNVX. */

   return nvk_GetImageViewHandleNVX(_device, pInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_GetImageViewAddressNVX(VkDevice _device, VkImageView _imageView,
                           VkImageViewAddressPropertiesNVX *pProperties)
{
   VK_FROM_HANDLE(nvk_image_view, image_view, _imageView);
   const struct nvk_image *image = (struct nvk_image *)image_view->vk.image;

   const uint8_t plane = image_view->planes[0].image_plane;
   pProperties->deviceAddress = nvk_image_base_address(image, plane);
   pProperties->size = nvk_image_size_B(image, plane);

   return VK_SUCCESS;
}
