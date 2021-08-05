/*
 * Copyright © 2021 Collabora Ltd.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nir/nir_builder.h"
#include "pan_blitter.h"
#include "pan_encoder.h"

#include "panvk_private.h"

#include "vk_format.h"

void
panvk_CmdBlitImage(VkCommandBuffer commandBuffer,
                   VkImage srcImage,
                   VkImageLayout srcImageLayout,
                   VkImage destImage,
                   VkImageLayout destImageLayout,
                   uint32_t regionCount,
                   const VkImageBlit *pRegions,
                   VkFilter filter)

{
   panvk_stub();
}

void
panvk_CmdCopyImage(VkCommandBuffer commandBuffer,
                   VkImage srcImage,
                   VkImageLayout srcImageLayout,
                   VkImage destImage,
                   VkImageLayout destImageLayout,
                   uint32_t regionCount,
                   const VkImageCopy *pRegions)
{
   panvk_stub();
}

void
panvk_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                           VkBuffer srcBuffer,
                           VkImage destImage,
                           VkImageLayout destImageLayout,
                           uint32_t regionCount,
                           const VkBufferImageCopy *pRegions)
{
   panvk_stub();
}

void
panvk_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                           VkImage srcImage,
                           VkImageLayout srcImageLayout,
                           VkBuffer destBuffer,
                           uint32_t regionCount,
                           const VkBufferImageCopy *pRegions)
{
   panvk_stub();
}

void
panvk_CmdCopyBuffer(VkCommandBuffer commandBuffer,
                    VkBuffer srcBuffer,
                    VkBuffer destBuffer,
                    uint32_t regionCount,
                    const VkBufferCopy *pRegions)
{
   panvk_stub();
}

void
panvk_CmdResolveImage(VkCommandBuffer cmd_buffer_h,
                      VkImage src_image_h,
                      VkImageLayout src_image_layout,
                      VkImage dest_image_h,
                      VkImageLayout dest_image_layout,
                      uint32_t region_count,
                      const VkImageResolve *regions)
{
   panvk_stub();
}

void
panvk_CmdFillBuffer(VkCommandBuffer commandBuffer,
                    VkBuffer dstBuffer,
                    VkDeviceSize dstOffset,
                    VkDeviceSize fillSize,
                    uint32_t data)
{
   panvk_stub();
}

void
panvk_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                      VkBuffer dstBuffer,
                      VkDeviceSize dstOffset,
                      VkDeviceSize dataSize,
                      const void *pData)
{
   panvk_stub();
}

void
panvk_CmdClearColorImage(VkCommandBuffer commandBuffer,
                         VkImage image,
                         VkImageLayout imageLayout,
                         const VkClearColorValue *pColor,
                         uint32_t rangeCount,
                         const VkImageSubresourceRange *pRanges)
{
   panvk_stub();
}

void
panvk_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                                VkImage image_h,
                                VkImageLayout imageLayout,
                                const VkClearDepthStencilValue *pDepthStencil,
                                uint32_t rangeCount,
                                const VkImageSubresourceRange *pRanges)
{
   panvk_stub();
}

void
panvk_CmdClearAttachments(VkCommandBuffer commandBuffer,
                          uint32_t attachmentCount,
                          const VkClearAttachment *pAttachments,
                          uint32_t rectCount,
                          const VkClearRect *pRects)
{
   panvk_stub();
}

void
panvk_meta_init(struct panvk_physical_device *dev)
{
   panvk_pool_init(&dev->meta.bin_pool, &dev->pdev, NULL, PAN_BO_EXECUTE,
                   16 * 1024, "panvk_meta binary pool", false);
   panvk_pool_init(&dev->meta.desc_pool, &dev->pdev, NULL, 0,
                   16 * 1024, "panvk_meta descriptor pool", false);
   panvk_pool_init(&dev->meta.blitter.bin_pool, &dev->pdev, NULL,
                   PAN_BO_EXECUTE, 16 * 1024,
                   "panvk_meta blitter binary pool", false);
   panvk_pool_init(&dev->meta.blitter.desc_pool, &dev->pdev, NULL,
                   0, 16 * 1024, "panvk_meta blitter descriptor pool",
                   false);
   pan_blitter_init(&dev->pdev, &dev->meta.blitter.bin_pool.base,
                    &dev->meta.blitter.desc_pool.base);
}

void
panvk_meta_cleanup(struct panvk_physical_device *dev)
{
   pan_blitter_cleanup(&dev->pdev);
   panvk_pool_cleanup(&dev->meta.blitter.desc_pool);
   panvk_pool_cleanup(&dev->meta.blitter.bin_pool);
   panvk_pool_cleanup(&dev->meta.desc_pool);
   panvk_pool_cleanup(&dev->meta.bin_pool);
}
