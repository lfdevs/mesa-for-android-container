/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

static void
anv_fill_buffer_view_surface_state(struct anv_device *device,
                                   struct anv_buffer_state *state,
                                   enum isl_format format,
                                   struct isl_swizzle swizzle,
                                   isl_surf_usage_flags_t usage,
                                   struct anv_address address,
                                   uint32_t range, uint32_t stride)
{
   anv_fill_buffer_surface_state(device,
                                 state->state_data.data,
                                 format, swizzle, usage,
                                 address, range, stride);

   if (state->state.map)
      memcpy(state->state.map, state->state_data.data, ANV_SURFACE_STATE_SIZE);
}

VkResult
anv_CreateBufferView(VkDevice _device,
                     const VkBufferViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkBufferView *pView)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer, buffer, pCreateInfo->buffer);
   struct anv_buffer_view *view;

   view = vk_buffer_view_create(&device->vk, pCreateInfo,
                                pAllocator, sizeof(*view));
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   const VkBufferUsageFlags2CreateInfoKHR *view_usage_info =
      vk_find_struct_const(pCreateInfo->pNext, BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR);
   const VkBufferUsageFlags buffer_usage =
      view_usage_info != NULL ? view_usage_info->usage : buffer->vk.usage;

   struct anv_format_plane format;
   format = anv_get_format_plane(device->info, pCreateInfo->format,
                                 0, VK_IMAGE_TILING_LINEAR);

   const uint32_t format_bs = isl_format_get_layout(format.isl_format)->bpb / 8;
   const uint32_t align_range =
      align_down_npot_u32(view->vk.range, format_bs);

   view->address = anv_address_add(buffer->address, pCreateInfo->offset);

   if (buffer_usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT) {
      view->general.state = anv_device_maybe_alloc_surface_state(device, NULL);

      anv_fill_buffer_view_surface_state(device,
                                         &view->general,
                                         format.isl_format,
                                         format.swizzle,
                                         ISL_SURF_USAGE_TEXTURE_BIT,
                                         view->address, align_range, format_bs);
   } else {
      view->general.state = ANV_STATE_NULL;
   }

   if (buffer_usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT) {
      view->storage.state = anv_device_maybe_alloc_surface_state(device, NULL);

      anv_fill_buffer_view_surface_state(device,
                                         &view->storage,
                                         format.isl_format, format.swizzle,
                                         ISL_SURF_USAGE_STORAGE_BIT,
                                         view->address, align_range, format_bs);
   } else {
      view->storage.state = ANV_STATE_NULL;
   }

   *pView = anv_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

void
anv_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
                      const VkAllocationCallbacks *pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer_view, view, bufferView);

   if (!view)
      return;

   if (view->general.state.alloc_size > 0) {
      anv_state_pool_free(&device->bindless_surface_state_pool,
                          view->general.state);
   }

   if (view->storage.state.alloc_size > 0) {
      anv_state_pool_free(&device->bindless_surface_state_pool,
                          view->storage.state);
   }

   vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
}
