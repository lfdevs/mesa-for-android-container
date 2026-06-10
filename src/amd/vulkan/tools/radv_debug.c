/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_debug.h"
#include "radv_buffer.h"
#include "radv_device.h"
#include "radv_entrypoints.h"
#include "vk_common_entrypoints.h"

static uint32_t
radv_find_memory_index(const struct radv_physical_device *pdev, VkMemoryPropertyFlags flags)
{
   const VkPhysicalDeviceMemoryProperties *mem_properties = &pdev->memory_properties;
   for (uint32_t i = 0; i < mem_properties->memoryTypeCount; ++i) {
      if (mem_properties->memoryTypes[i].propertyFlags == flags) {
         return i;
      }
   }
   UNREACHABLE("invalid memory properties");
}

VkResult
radv_backed_buffer_init(struct radv_device *device, struct radv_backed_buffer *buffer, uint64_t size,
                        enum radv_memory_type memory_type, VkBufferUsageFlags2 usage, bool map)
{
   VkMemoryPropertyFlags memory_property_flags = 0;
   switch (memory_type) {
   case radv_memory_type_invisible_vram:
      memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
   case radv_memory_type_visible_vram:
      memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      break;
   case radv_memory_type_gtt:
      memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                              VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      break;
   }
   uint32_t memory_type_index = radv_find_memory_index(radv_device_physical(device), memory_property_flags);

   memset(buffer, 0, sizeof(*buffer));

   VkResult result;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext =
         &(VkBufferUsageFlags2CreateInfo){
            .sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO,
            .usage = usage,
         },
      .size = size,
   };
   result = radv_CreateBuffer(radv_device_to_handle(device), &buffer_create_info, NULL, &buffer->buffer);
   if (result != VK_SUCCESS)
      return result;

   VkDeviceBufferMemoryRequirements buffer_mem_req_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
      .pCreateInfo = &buffer_create_info,
   };
   VkMemoryRequirements2 mem_reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   radv_GetDeviceBufferMemoryRequirements(radv_device_to_handle(device), &buffer_mem_req_info, &mem_reqs);

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_reqs.memoryRequirements.size,
      .memoryTypeIndex = memory_type_index,
   };
   result = radv_AllocateMemory(radv_device_to_handle(device), &alloc_info, NULL, &buffer->memory);
   if (result != VK_SUCCESS)
      goto fail;

   VkBindBufferMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer = buffer->buffer,
      .memory = buffer->memory,
   };
   result = radv_BindBufferMemory2(radv_device_to_handle(device), 1, &bind_info);
   if (result != VK_SUCCESS)
      goto fail;

   if (map) {
      VkMemoryMapInfo mem_map_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO,
         .memory = buffer->memory,
         .size = VK_WHOLE_SIZE,
      };
      result = radv_MapMemory2(radv_device_to_handle(device), &mem_map_info, &buffer->map);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return result;

fail:
   radv_backed_buffer_finish(device, buffer);
   return result;
}

void
radv_backed_buffer_finish(struct radv_device *device, struct radv_backed_buffer *buffer)
{
   if (buffer->map) {
      VkMemoryUnmapInfo unmap_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO,
         .memory = buffer->memory,
      };
      radv_UnmapMemory2(radv_device_to_handle(device), &unmap_info);
   }

   radv_DestroyBuffer(radv_device_to_handle(device), buffer->buffer, NULL);

   radv_FreeMemory(radv_device_to_handle(device), buffer->memory, NULL);

   memset(buffer, 0, sizeof(*buffer));
}

uint64_t
radv_backed_buffer_get_va(struct radv_device *device, struct radv_backed_buffer *buffer)
{
   if (!buffer->buffer)
      return 0;

   VkBufferDeviceAddressInfo addr_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = buffer->buffer,
   };
   return vk_common_GetBufferDeviceAddress(radv_device_to_handle(device), &addr_info);
}
