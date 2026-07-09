/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "gamma_util.h"
#include "gamma_app.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vulkan/vulkan_core.h>

void
gamma_check_vk_result_internal(VkResult result, const char *file, uint32_t line)
{
   if (result != VK_SUCCESS) {
      fprintf(stderr, "gamma: VkResult(%i) at %s:%u\n", result, file, line);
      exit(1);
   }
}

static uint32_t
gamma_find_memory_index(gamma_app *app, VkMemoryPropertyFlags flags)
{
   VkPhysicalDeviceMemoryProperties mem_properties;
   vkGetPhysicalDeviceMemoryProperties(app->pdev, &mem_properties);

   /* Try to find an exact match first. */
   for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
      if (mem_properties.memoryTypes[i].propertyFlags == flags)
         return i;
   }

   for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
      if ((mem_properties.memoryTypes[i].propertyFlags & flags) == flags)
         return i;
   }
   fprintf(stderr, "gamma: Unsupported memory properties\n");
   return 0;
}

gamma_backed_buffer::~gamma_backed_buffer()
{
   vkDestroyBuffer(app->device, buffer, NULL);

   if (map) {
      VkMemoryUnmapInfo unmap_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO,
         .memory = memory,
      };
      vkUnmapMemory2(app->device, &unmap_info);
   }

   vkFreeMemory(app->device, memory, NULL);
}

std::shared_ptr<gamma_backed_buffer>
gamma_create_backed_buffer(gamma_app *app, uint64_t size, enum gamma_memory_type memory_type, VkBufferUsageFlags usage,
                           bool map)
{
   VkMemoryPropertyFlags memory_property_flags = 0;
   switch (memory_type) {
   case gamma_memory_type_device_local:
      memory_property_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
   case gamma_memory_type_host_visible:
      memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      break;
   case gamma_memory_type_host_visible_cached:
      memory_property_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                              VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      break;
   }
   uint32_t memory_type_index = gamma_find_memory_index(app, memory_property_flags);

   std::shared_ptr<gamma_backed_buffer> buffer = std::make_shared<gamma_backed_buffer>();
   buffer->app = app;
   buffer->size = size;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
   };
   gamma_check_vk_result(vkCreateBuffer(app->device, &buffer_create_info, NULL, &buffer->buffer));

   VkDeviceBufferMemoryRequirements buffer_mem_req_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
      .pCreateInfo = &buffer_create_info,
   };
   VkMemoryRequirements2 mem_reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   vkGetDeviceBufferMemoryRequirements(app->device, &buffer_mem_req_info, &mem_reqs);

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_reqs.memoryRequirements.size,
      .memoryTypeIndex = memory_type_index,
   };
   gamma_check_vk_result(vkAllocateMemory(app->device, &alloc_info, NULL, &buffer->memory));

   VkBindBufferMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer = buffer->buffer,
      .memory = buffer->memory,
   };
   gamma_check_vk_result(vkBindBufferMemory2(app->device, 1, &bind_info));

   if (map) {
      VkMemoryMapInfo mem_map_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO,
         .memory = buffer->memory,
         .size = VK_WHOLE_SIZE,
      };
      gamma_check_vk_result(vkMapMemory2(app->device, &mem_map_info, &buffer->map));
   }

   return buffer;
}

gamma_backed_image::~gamma_backed_image()
{
   vkDestroyImageView(app->device, image_view, nullptr);
   vkDestroyImage(app->device, image, nullptr);
   vkFreeMemory(app->device, memory, nullptr);
}

std::shared_ptr<gamma_backed_image>
gamma_create_backed_image(gamma_app *app, VkExtent3D extent, const std::vector<VkFormat> &formats,
                          VkFormatFeatureFlags format_features, VkImageUsageFlags usage,
                          VkImageAspectFlagBits aspect_mask, uint32_t samples, uint32_t levels)
{
   VkFormat format = formats[0];
   for (VkFormat candidate_format : formats) {
      VkFormatProperties format_properties;
      vkGetPhysicalDeviceFormatProperties(app->pdev, candidate_format, &format_properties);
      if ((format_properties.optimalTilingFeatures & format_features) == format_features) {
         format = candidate_format;
         break;
      }
   }

   std::shared_ptr<gamma_backed_image> image = std::make_shared<gamma_backed_image>();
   image->app = app;

   VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = extent,
      .mipLevels = levels,
      .arrayLayers = 1,
      .samples = (VkSampleCountFlagBits)samples,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &app->queue_family_index,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };

   gamma_check_vk_result(vkCreateImage(app->device, &image_create_info, nullptr, &image->image));

   VkDeviceImageMemoryRequirements memory_requirements_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
      .pCreateInfo = &image_create_info,
      .planeAspect = aspect_mask,
   };
   VkMemoryRequirements2 memory_requirements = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   vkGetDeviceImageMemoryRequirements(app->device, &memory_requirements_info, &memory_requirements);

   uint32_t memory_type_index = gamma_find_memory_index(app, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = memory_requirements.memoryRequirements.size,
      .memoryTypeIndex = memory_type_index,
   };
   gamma_check_vk_result(vkAllocateMemory(app->device, &alloc_info, NULL, &image->memory));

   gamma_check_vk_result(vkBindImageMemory(app->device, image->image, image->memory, 0));

   VkComponentMapping view_component_mapping = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
   };

   VkImageSubresourceRange subresource_range = {
      .aspectMask = aspect_mask,
      .levelCount = levels,
      .layerCount = 1,
   };

   VkImageViewCreateInfo image_view_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image->image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .components = view_component_mapping,
      .subresourceRange = subresource_range,
   };
   gamma_check_vk_result(vkCreateImageView(app->device, &image_view_create_info, nullptr, &image->image_view));

   return image;
}

void
gamma_generate_cube_vertices(gamma_vertex *vertices, gamma_aabb aabb)
{
   *(vertices++) = {.position = {aabb.min.x, aabb.min.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.min.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.min.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.max.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.max.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.max.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.max.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.min.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.min.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.min.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.min.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.max.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.max.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.max.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.max.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.min.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.min.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.min.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.min.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.min.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.max.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.max.x, aabb.max.y, aabb.max.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.max.y, aabb.min.z}};
   *(vertices++) = {.position = {aabb.min.x, aabb.max.y, aabb.max.z}};
}

void
gamma_generate_filled_cube_vertices(gamma_vertex *vertices, gamma_aabb aabb, uint32_t geometry_index,
                                    uint32_t primitive_index)
{
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.min.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.max.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.max.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
   *(vertices++) = {
      .position = {aabb.max.x, aabb.min.y, aabb.min.z},
      .geometry_index = geometry_index,
      .primitive_index = primitive_index,
   };
}
