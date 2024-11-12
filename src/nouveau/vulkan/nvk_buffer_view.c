/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_buffer_view.h"

#include "nil.h"
#include "nvk_buffer.h"
#include "nvk_entrypoints.h"
#include "nvk_device.h"
#include "nvk_format.h"
#include "nvk_physical_device.h"

#include "vk_format.h"

#include "clb097.h"

VkFormatFeatureFlags2
nvk_get_buffer_format_features(struct nvk_physical_device *pdev,
                               VkFormat vk_format)
{
   VkFormatFeatureFlags2 features = 0;

   if (nvk_get_va_format(pdev, vk_format))
      features |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;

   enum pipe_format p_format = nvk_format_to_pipe_format(vk_format);
   if (nil_format_supports_buffer(&pdev->info, p_format)) {
      features |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;

      if (nil_format_supports_storage(&pdev->info, p_format)) {
         features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                     VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
         if (pdev->info.cls_eng3d >= MAXWELL_A)
            features |= VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT;
      }

      if (nvk_format_supports_atomics(&pdev->info, p_format))
         features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   return features;
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateBufferView(VkDevice _device,
                     const VkBufferViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkBufferView *pBufferView)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_buffer, buffer, pCreateInfo->buffer);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   struct nvk_buffer_view *view;
   VkResult result;

   view = vk_buffer_view_create(&dev->vk, pCreateInfo,
                                 pAllocator, sizeof(*view));
   if (!view)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   const uint64_t addr = nvk_buffer_address(buffer, view->vk.offset);
   enum pipe_format format = nvk_format_to_pipe_format(view->vk.format);

   if (nvk_use_edb_buffer_views(pdev)) {
      view->edb_desc =
         nvk_edb_bview_cache_get_descriptor(dev, &dev->edb_bview_cache,
                                            addr, view->vk.range, format);
   } else {
      uint32_t desc[8];
      nil_buffer_fill_tic(&pdev->info, addr, nil_format(format),
                          view->vk.elements, &desc);

      uint32_t desc_index;
      result = nvk_descriptor_table_add(dev, &dev->images,
                                        desc, sizeof(desc),
                                        &desc_index);
      if (result != VK_SUCCESS) {
         vk_buffer_view_destroy(&dev->vk, pAllocator, &view->vk);
         return result;
      }

      view->desc = (struct nvk_buffer_view_descriptor) {
         .image_index = desc_index,
      };
   }

   *pBufferView = nvk_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyBufferView(VkDevice _device,
                      VkBufferView bufferView,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_device, dev, _device);
   VK_FROM_HANDLE(nvk_buffer_view, view, bufferView);
   struct nvk_physical_device *pdev = nvk_device_physical(dev);

   if (!view)
      return;

   if (!nvk_use_edb_buffer_views(pdev))
      nvk_descriptor_table_remove(dev, &dev->images, view->desc.image_index);

   vk_buffer_view_destroy(&dev->vk, pAllocator, &view->vk);
}
