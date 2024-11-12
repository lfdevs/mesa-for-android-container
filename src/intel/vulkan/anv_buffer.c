/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

static void
anv_bind_buffer_memory(struct anv_device *device,
                       const VkBindBufferMemoryInfo *pBindInfo)
{
   ANV_FROM_HANDLE(anv_device_memory, mem, pBindInfo->memory);
   ANV_FROM_HANDLE(anv_buffer, buffer, pBindInfo->buffer);

   assert(pBindInfo->sType == VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO);
   assert(!anv_buffer_is_sparse(buffer));

   const VkBindMemoryStatusKHR *bind_status =
      vk_find_struct_const(pBindInfo->pNext, BIND_MEMORY_STATUS_KHR);

   if (mem) {
      assert(pBindInfo->memoryOffset < mem->vk.size);
      assert(mem->vk.size - pBindInfo->memoryOffset >= buffer->vk.size);
      buffer->address = (struct anv_address) {
         .bo = mem->bo,
         .offset = pBindInfo->memoryOffset,
      };
   } else {
      buffer->address = ANV_NULL_ADDRESS;
   }

   ANV_RMV(buffer_bind, device, buffer);

   if (bind_status)
      *bind_status->pResult = VK_SUCCESS;
}

VkResult anv_BindBufferMemory2(
    VkDevice                                    _device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfo*               pBindInfos)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; i++)
      anv_bind_buffer_memory(device, &pBindInfos[i]);

   return VK_SUCCESS;
}

// Buffer functions

static void
anv_get_buffer_memory_requirements(struct anv_device *device,
                                   VkBufferCreateFlags flags,
                                   VkDeviceSize size,
                                   VkBufferUsageFlags2KHR usage,
                                   bool is_sparse,
                                   VkMemoryRequirements2* pMemoryRequirements)
{
   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    *
    * We have special memory types for descriptor buffers.
    */
   uint32_t memory_types;
   if (flags & VK_BUFFER_CREATE_PROTECTED_BIT)
      memory_types = device->physical->memory.protected_mem_types;
   else if (usage & (VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                     VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT))
      memory_types = device->physical->memory.dynamic_visible_mem_types;
   else
      memory_types = device->physical->memory.default_buffer_mem_types;

   /* The GPU appears to write back to main memory in cachelines. Writes to a
    * buffers should not clobber with writes to another buffers so make sure
    * those are in different cachelines.
    */
   uint32_t alignment = 64;

   /* From the spec, section "Sparse Buffer and Fully-Resident Image Block
    * Size":
    *   "The sparse block size in bytes for sparse buffers and fully-resident
    *    images is reported as VkMemoryRequirements::alignment. alignment
    *    represents both the memory alignment requirement and the binding
    *    granularity (in bytes) for sparse resources."
    */
   if (is_sparse) {
      alignment = ANV_SPARSE_BLOCK_SIZE;
      size = align64(size, alignment);
   }

   pMemoryRequirements->memoryRequirements.size = size;
   pMemoryRequirements->memoryRequirements.alignment = alignment;

   /* Storage and Uniform buffers should have their size aligned to
    * 32-bits to avoid boundary checks when last DWord is not complete.
    * This would ensure that not internal padding would be needed for
    * 16-bit types.
    */
   if (device->robust_buffer_access &&
       (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ||
        usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
      pMemoryRequirements->memoryRequirements.size = align64(size, 4);

   pMemoryRequirements->memoryRequirements.memoryTypeBits = memory_types;

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *requirements = (void *)ext;
         requirements->prefersDedicatedAllocation = false;
         requirements->requiresDedicatedAllocation = false;
         break;
      }

      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

static VkBufferUsageFlags2KHR
get_buffer_usages(const VkBufferCreateInfo *create_info)
{
   const VkBufferUsageFlags2CreateInfoKHR *usage2_info =
      vk_find_struct_const(create_info->pNext,
                           BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR);
   return usage2_info != NULL ? usage2_info->usage : create_info->usage;
}

void anv_GetDeviceBufferMemoryRequirements(
    VkDevice                                    _device,
    const VkDeviceBufferMemoryRequirements*     pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   const bool is_sparse =
      pInfo->pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
   VkBufferUsageFlags2KHR usages = get_buffer_usages(pInfo->pCreateInfo);

   if ((device->physical->sparse_type == ANV_SPARSE_TYPE_NOT_SUPPORTED) &&
       INTEL_DEBUG(DEBUG_SPARSE) &&
       pInfo->pCreateInfo->flags & (VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                                    VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT |
                                    VK_BUFFER_CREATE_SPARSE_ALIASED_BIT))
      fprintf(stderr, "=== %s %s:%d flags:0x%08x\n", __func__, __FILE__,
              __LINE__, pInfo->pCreateInfo->flags);

   anv_get_buffer_memory_requirements(device,
                                      pInfo->pCreateInfo->flags,
                                      pInfo->pCreateInfo->size,
                                      usages,
                                      is_sparse,
                                      pMemoryRequirements);
}

VkResult anv_CreateBuffer(
    VkDevice                                    _device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_buffer *buffer;

   if ((device->physical->sparse_type == ANV_SPARSE_TYPE_NOT_SUPPORTED) &&
       INTEL_DEBUG(DEBUG_SPARSE) &&
       pCreateInfo->flags & (VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
                             VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT |
                             VK_BUFFER_CREATE_SPARSE_ALIASED_BIT))
      fprintf(stderr, "=== %s %s:%d flags:0x%08x\n", __func__, __FILE__,
              __LINE__, pCreateInfo->flags);

   if ((pCreateInfo->flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT) &&
       device->physical->sparse_type == ANV_SPARSE_TYPE_TRTT) {
      VkBufferUsageFlags2KHR usages = get_buffer_usages(pCreateInfo);
      if (usages & (VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
                    VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT)) {
         return vk_errorf(device, VK_ERROR_UNKNOWN,
                          "Cannot support sparse descriptor buffers with TRTT.");
      }
   }

   /* Don't allow creating buffers bigger than our address space.  The real
    * issue here is that we may align up the buffer size and we don't want
    * doing so to cause roll-over.  However, no one has any business
    * allocating a buffer larger than our GTT size.
    */
   if (pCreateInfo->size > device->physical->gtt_size)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   buffer = vk_buffer_create(&device->vk, pCreateInfo,
                             pAllocator, sizeof(*buffer));
   if (buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   buffer->address = ANV_NULL_ADDRESS;
   if (anv_buffer_is_sparse(buffer)) {
      enum anv_bo_alloc_flags alloc_flags = 0;
      uint64_t client_address = 0;

      if (buffer->vk.create_flags & VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT) {
         alloc_flags = ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS;
         const VkBufferOpaqueCaptureAddressCreateInfo *opaque_addr_info =
            vk_find_struct_const(pCreateInfo->pNext,
                                 BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO);
         if (opaque_addr_info)
            client_address = opaque_addr_info->opaqueCaptureAddress;
      }

      if (buffer->vk.create_flags & VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT) {
         alloc_flags = ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS;

         const VkOpaqueCaptureDescriptorDataCreateInfoEXT *opaque_info =
            vk_find_struct_const(pCreateInfo->pNext,
                                 OPAQUE_CAPTURE_DESCRIPTOR_DATA_CREATE_INFO_EXT);
         if (opaque_info)
            client_address = *((const uint64_t *)opaque_info->opaqueCaptureDescriptorData);
      }

      /* If this buffer will be used as a descriptor buffer, make sure we
       * allocate it on the correct heap.
       */
      if (buffer->vk.usage & (VK_BUFFER_USAGE_2_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
                              VK_BUFFER_USAGE_2_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT)) {
         alloc_flags |= ANV_BO_ALLOC_DYNAMIC_VISIBLE_POOL;
      }

      VkResult result = anv_init_sparse_bindings(device, buffer->vk.size,
                                                 &buffer->sparse_data,
                                                 alloc_flags, client_address,
                                                 &buffer->address);
      if (result != VK_SUCCESS) {
         vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
         return result;
      }
   }

   ANV_RMV(buffer_create, device, false, buffer);

   *pBuffer = anv_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

void anv_DestroyBuffer(
    VkDevice                                    _device,
    VkBuffer                                    _buffer,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   if (!buffer)
      return;

   ANV_RMV(buffer_destroy, device, buffer);

   if (anv_buffer_is_sparse(buffer)) {
      assert(buffer->address.offset == buffer->sparse_data.address);
      anv_free_sparse_bindings(device, &buffer->sparse_data);
   }

   vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
}

VkDeviceAddress anv_GetBufferDeviceAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo)
{
   ANV_FROM_HANDLE(anv_buffer, buffer, pInfo->buffer);

   assert(!anv_address_is_null(buffer->address));

   return anv_address_physical(buffer->address);
}

uint64_t anv_GetBufferOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkBufferDeviceAddressInfo*            pInfo)
{
   ANV_FROM_HANDLE(anv_buffer, buffer, pInfo->buffer);

   return anv_address_physical(buffer->address);
}

VkResult anv_GetBufferOpaqueCaptureDescriptorDataEXT(
    VkDevice                                    device,
    const VkBufferCaptureDescriptorDataInfoEXT* pInfo,
    void*                                       pData)
{
   ANV_FROM_HANDLE(anv_buffer, buffer, pInfo->buffer);

   *((uint64_t *)pData) = anv_address_physical(buffer->address);

   return VK_SUCCESS;
}

uint64_t anv_GetDeviceMemoryOpaqueCaptureAddress(
    VkDevice                                    device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo)
{
   ANV_FROM_HANDLE(anv_device_memory, memory, pInfo->memory);

   assert(memory->bo->alloc_flags & ANV_BO_ALLOC_CLIENT_VISIBLE_ADDRESS);

   return intel_48b_address(memory->bo->offset);
}

void
anv_fill_buffer_surface_state(struct anv_device *device,
                              void *surface_state_ptr,
                              enum isl_format format,
                              struct isl_swizzle swizzle,
                              isl_surf_usage_flags_t usage,
                              struct anv_address address,
                              uint32_t range, uint32_t stride)
{
   if (address.bo && address.bo->alloc_flags & ANV_BO_ALLOC_PROTECTED)
      usage |= ISL_SURF_USAGE_PROTECTED_BIT;
   isl_buffer_fill_state(&device->isl_dev, surface_state_ptr,
                         .address = anv_address_physical(address),
                         .mocs = isl_mocs(&device->isl_dev, usage,
                                          address.bo && anv_bo_is_external(address.bo)),
                         .size_B = range,
                         .format = format,
                         .swizzle = swizzle,
                         .stride_B = stride);
}
