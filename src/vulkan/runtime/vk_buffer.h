/*
 * Copyright © 2022 Collabora, Ltd.
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
#ifndef VK_BUFFER_H
#define VK_BUFFER_H

#include "vk_object.h"
#include "vk_util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_buffer {
   struct vk_object_base base;

   /** VkBufferCreateInfo::flags */
   VkBufferCreateFlags create_flags;

   /** VkBufferCreateInfo::size */
   VkDeviceSize size;

   /** VkBufferCreateInfo::usage or VkBufferUsageFlags2CreateInfoKHR::usage */
   VkBufferUsageFlags2 usage;

   /** Set by the implementation
    *
    * The implementation must set this at creation for sparse buffers or can
    * delay as far as the bind for non-sparse buffers.
    */
   VkDeviceAddress device_address;

   /** Inferred address flags from create_flags */
   VkAddressCommandFlagsKHR address_flags;

   /** Inferred copy flags from create_flags */
   VkAddressCopyFlagsKHR copy_flags;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vk_buffer, base, VkBuffer,
                               VK_OBJECT_TYPE_BUFFER);

void vk_buffer_init(struct vk_device *device,
                    struct vk_buffer *buffer,
                    const VkBufferCreateInfo *pCreateInfo);
void vk_buffer_finish(struct vk_buffer *buffer);

void *vk_buffer_create(struct vk_device *device,
                       const VkBufferCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *alloc,
                       size_t size);
void vk_buffer_destroy(struct vk_device *device,
                       const VkAllocationCallbacks *alloc,
                       struct vk_buffer *buffer);

static inline VkDeviceAddress
vk_buffer_address(const struct vk_buffer *buffer,
                  VkDeviceSize offset)
{
   assert(buffer->size == 0 || buffer->device_address != 0);
   return buffer->device_address + offset;
}

static inline uint64_t
vk_buffer_range(const struct vk_buffer *buffer,
                uint64_t offset, uint64_t range)
{
   assert(offset <= buffer->size);
   if (range == VK_WHOLE_SIZE) {
      return buffer->size - offset;
   } else {
      assert(range + offset >= range);
      assert(range + offset <= buffer->size);
      return range;
   }
}

static inline VkDeviceAddressRangeKHR
vk_device_address_range(const struct vk_buffer *buffer,
                        VkDeviceSize offset,
                        VkDeviceSize range)
{
   VkDeviceAddressRangeKHR addr_range = { 0 };

   if (buffer) {
      addr_range.address = vk_buffer_address(buffer, offset);
      addr_range.size = vk_buffer_range(buffer, offset, range);
   }

   return addr_range;
}

static inline VkStridedDeviceAddressRangeKHR
vk_strided_device_address_range(const struct vk_buffer *buffer,
                                VkDeviceSize offset,
                                VkDeviceSize range,
                                VkDeviceSize stride)
{
   VkStridedDeviceAddressRangeKHR addr_range = { 0 };

   if (buffer) {
      addr_range.address = vk_buffer_address(buffer, offset);
      addr_range.size = vk_buffer_range(buffer, offset, range);
      addr_range.stride = stride;
   }

   return addr_range;
}

static inline VkBufferUsageFlags2
vk_buffer_usage_flags(const VkBufferCreateInfo *info)
{
   const VkBufferUsageFlags2CreateInfo *usage2_info =
      vk_find_struct_const(info->pNext,
                           BUFFER_USAGE_FLAGS_2_CREATE_INFO);
   if (usage2_info)
      return usage2_info->usage;
   else
      return info->usage;
}

#ifdef __cplusplus
}
#endif

#endif /* VK_BUFFER_H */
