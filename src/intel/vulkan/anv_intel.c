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

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"

VkResult anv_CreateDmaBufImageINTEL(
    VkDevice                                    _device,
    const VkDmaBufImageCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMem,
    VkImage*                                    pImage)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_device_memory *mem;
   struct anv_image *image;
   VkResult result;
   VkImage image_h;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DMA_BUF_IMAGE_CREATE_INFO_INTEL);

   mem = vk_alloc2(&device->alloc, pAllocator, sizeof(*mem), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t gem_handle = anv_gem_fd_to_handle(device, pCreateInfo->fd);
   if (!gem_handle) {
      result = vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);
      goto fail;
   }

   uint64_t size = (uint64_t)pCreateInfo->strideInBytes * pCreateInfo->extent.height;

   anv_bo_init(&mem->bo, gem_handle, size);

   anv_image_create(_device,
      &(struct anv_image_create_info) {
         .isl_tiling_flags = ISL_TILING_X_BIT,
         .stride = pCreateInfo->strideInBytes,
         .vk_info =
      &(VkImageCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = pCreateInfo->format,
         .extent = pCreateInfo->extent,
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = 1,
         /* FIXME: Need a way to use X tiling to allow scanout */
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
         .flags = 0,
      }},
      pAllocator, &image_h);

   image = anv_image_from_handle(image_h);
   image->bo = &mem->bo;
   image->offset = 0;

   assert(image->extent.width > 0);
   assert(image->extent.height > 0);
   assert(image->extent.depth == 1);

   *pMem = anv_device_memory_to_handle(mem);
   *pImage = anv_image_to_handle(image);

   return VK_SUCCESS;

 fail:
   vk_free2(&device->alloc, pAllocator, mem);

   return result;
}
