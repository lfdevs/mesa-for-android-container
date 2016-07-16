/*
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "anv_private.h"

/* This file contains utility functions for help debugging.  They can be
 * called from GDB or similar to help inspect images and buffers.
 */

void
anv_dump_image_to_ppm(struct anv_device *device,
                      struct anv_image *image, unsigned miplevel,
                      unsigned array_layer, const char *filename)
{
   VkDevice vk_device = anv_device_to_handle(device);
   MAYBE_UNUSED VkResult result;

   VkExtent2D extent = { image->extent.width, image->extent.height };
   for (unsigned i = 0; i < miplevel; i++) {
      extent.width = MAX2(1, extent.width / 2);
      extent.height = MAX2(1, extent.height / 2);
   }

   VkImage copy_image;
   result = anv_CreateImage(vk_device,
      &(VkImageCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = VK_FORMAT_R8G8B8A8_UNORM,
         .extent = (VkExtent3D) { extent.width, extent.height, 1 },
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = 1,
         .tiling = VK_IMAGE_TILING_LINEAR,
         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         .flags = 0,
      }, NULL, &copy_image);
   assert(result == VK_SUCCESS);

   VkMemoryRequirements reqs;
   anv_GetImageMemoryRequirements(vk_device, copy_image, &reqs);

   VkDeviceMemory memory;
   result = anv_AllocateMemory(vk_device,
      &(VkMemoryAllocateInfo) {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = reqs.size,
         .memoryTypeIndex = 0,
      }, NULL, &memory);
   assert(result == VK_SUCCESS);

   result = anv_BindImageMemory(vk_device, copy_image, memory, 0);
   assert(result == VK_SUCCESS);

   VkCommandPool commandPool;
   result = anv_CreateCommandPool(vk_device,
      &(VkCommandPoolCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .queueFamilyIndex = 0,
         .flags = 0,
      }, NULL, &commandPool);
   assert(result == VK_SUCCESS);

   VkCommandBuffer cmd;
   result = anv_AllocateCommandBuffers(vk_device,
      &(VkCommandBufferAllocateInfo) {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .commandPool = commandPool,
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      }, &cmd);
   assert(result == VK_SUCCESS);

   result = anv_BeginCommandBuffer(cmd,
      &(VkCommandBufferBeginInfo) {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      });
   assert(result == VK_SUCCESS);

   anv_CmdBlitImage(cmd,
      anv_image_to_handle(image), VK_IMAGE_LAYOUT_GENERAL,
      copy_image, VK_IMAGE_LAYOUT_GENERAL, 1,
      &(VkImageBlit) {
         .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = miplevel,
            .baseArrayLayer = array_layer,
            .layerCount = 1,
         },
         .srcOffsets = {
            { 0, 0, 0 },
            { extent.width, extent.height, 1 },
         },
         .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .dstOffsets = {
            { 0, 0, 0 },
            { extent.width, extent.height, 1 },
         },
      }, VK_FILTER_NEAREST);

   ANV_CALL(CmdPipelineBarrier)(cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      true, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier) {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_HOST_READ_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
         .newLayout = VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex = 0,
         .dstQueueFamilyIndex = 0,
         .image = copy_image,
         .subresourceRange = (VkImageSubresourceRange) {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
      });

   result = anv_EndCommandBuffer(cmd);
   assert(result == VK_SUCCESS);

   VkFence fence;
   result = anv_CreateFence(vk_device,
      &(VkFenceCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
         .flags = 0,
      }, NULL, &fence);
   assert(result == VK_SUCCESS);

   result = anv_QueueSubmit(anv_queue_to_handle(&device->queue), 1,
      &(VkSubmitInfo) {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd,
      }, fence);
   assert(result == VK_SUCCESS);

   result = anv_WaitForFences(vk_device, 1, &fence, true, UINT64_MAX);
   assert(result == VK_SUCCESS);

   anv_DestroyFence(vk_device, fence, NULL);
   anv_DestroyCommandPool(vk_device, commandPool, NULL);

   uint8_t *map;
   result = anv_MapMemory(vk_device, memory, 0, reqs.size, 0, (void **)&map);
   assert(result == VK_SUCCESS);

   VkSubresourceLayout layout;
   anv_GetImageSubresourceLayout(vk_device, copy_image,
      &(VkImageSubresource) {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .arrayLayer = 0,
      }, &layout);

   map += layout.offset;

   /* Now we can finally write the PPM file */
   FILE *file = fopen(filename, "wb");
   assert(file);

   fprintf(file, "P6\n%d %d\n255\n", extent.width, extent.height);
   for (unsigned y = 0; y < extent.height; y++) {
      uint8_t row[extent.width * 3];
      for (unsigned x = 0; x < extent.width; x++) {
         row[x * 3 + 0] = map[x * 4 + 0];
         row[x * 3 + 1] = map[x * 4 + 1];
         row[x * 3 + 2] = map[x * 4 + 2];
      }
      fwrite(row, 3, extent.width, file);

      map += layout.rowPitch;
   }
   fclose(file);

   anv_UnmapMemory(vk_device, memory);
   anv_DestroyImage(vk_device, copy_image, NULL);
   anv_FreeMemory(vk_device, memory, NULL);
}
