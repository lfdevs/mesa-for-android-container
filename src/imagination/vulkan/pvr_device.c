/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv driver which is:
 * Copyright © 2019 Raspberry Pi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pvr_device.h"

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "hwdef/pvr_hw_utils.h"
#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "vk_debug_utils.h"
#include "pvr_buffer.h"
#include "pvr_entrypoints.h"
#include "pvr_framebuffer.h"
#include "pvr_free_list.h"
#include "pvr_hw_pass.h"
#include "pvr_image.h"
#include "pvr_macros.h"
#include "pvr_pass.h"
#include "pvr_pds.h"
#include "pvr_physical_device.h"
#include "pvr_rt_dataset.h"
#include "pvr_types.h"
#include "pvr_usc.h"
#include "pvr_util.h"
#include "pvr_winsys.h"
#include "pvr_wsi.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/mesa-blake3.h"
#include "util/os_misc.h"
#include "util/u_math.h"
#include "vk_device_memory.h"
#include "vk_extensions.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_physical_device_features.h"
#include "vk_physical_device_properties.h"
#include "vk_sampler.h"
#include "vk_util.h"

/** Gets the amount of memory to allocate per-core for a tile buffer. */
static uint32_t
pvr_get_tile_buffer_size_per_core(const struct pvr_device *device)
{
   uint32_t clusters =
      PVR_GET_FEATURE_VALUE(&device->pdevice->dev_info, num_clusters, 1U);

   /* Round the number of clusters up to the next power of two. */
   if (!PVR_HAS_FEATURE(&device->pdevice->dev_info, tile_per_usc))
      clusters = util_next_power_of_two(clusters);

   /* Tile buffer is (total number of partitions across all clusters) * 16 * 16
    * (quadrant size in pixels).
    */
   return device->pdevice->dev_runtime_info.total_reserved_partition_size *
          clusters * sizeof(uint32_t);
}

/**
 * Gets the amount of memory to allocate for a tile buffer on the current BVNC.
 */
static uint32_t pvr_get_tile_buffer_size(const struct pvr_device *device)
{
   /* On a multicore system duplicate the buffer for each core. */
   /* TODO: Optimise tile buffer size to use core_count, not max_num_cores. */
   return pvr_get_tile_buffer_size_per_core(device) *
          rogue_get_max_num_cores(&device->pdevice->dev_info);
}

/**
 * \brief Ensures that a certain amount of tile buffers are allocated.
 *
 * Make sure that \p capacity amount of tile buffers are allocated. If less were
 * present, append new tile buffers of \p size_in_bytes each to reach the quota.
 */
VkResult pvr_device_tile_buffer_ensure_cap(struct pvr_device *device,
                                           uint32_t capacity)
{
   uint32_t size_in_bytes = pvr_get_tile_buffer_size(device);
   struct pvr_device_tile_buffer_state *tile_buffer_state =
      &device->tile_buffer_state;
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   VkResult result;

   simple_mtx_lock(&tile_buffer_state->mtx);

   /* Clamping in release and asserting in debug. */
   assert(capacity <= ARRAY_SIZE(tile_buffer_state->buffers));
   capacity = CLAMP(capacity,
                    tile_buffer_state->buffer_count,
                    ARRAY_SIZE(tile_buffer_state->buffers));

   /* TODO: Implement bo multialloc? To reduce the amount of syscalls and
    * allocations.
    */
   for (uint32_t i = tile_buffer_state->buffer_count; i < capacity; i++) {
      result = pvr_bo_alloc(device,
                            device->heaps.general_heap,
                            size_in_bytes,
                            cache_line_size,
                            0,
                            &tile_buffer_state->buffers[i]);
      if (result != VK_SUCCESS) {
         for (uint32_t j = tile_buffer_state->buffer_count; j < i; j++)
            pvr_bo_free(device, tile_buffer_state->buffers[j]);

         goto err_release_lock;
      }
   }

   tile_buffer_state->buffer_count = capacity;

   simple_mtx_unlock(&tile_buffer_state->mtx);

   return VK_SUCCESS;

err_release_lock:
   simple_mtx_unlock(&tile_buffer_state->mtx);

   return result;
}

void pvr_rstate_entry_add(struct pvr_device *device,
                          struct pvr_render_state *rstate)
{
   simple_mtx_lock(&device->rs_mtx);
   list_addtail(&rstate->link, &device->render_states);
   simple_mtx_unlock(&device->rs_mtx);
}

void pvr_rstate_entry_remove(struct pvr_device *device,
                             const struct pvr_render_state *rstate)
{
   simple_mtx_lock(&device->rs_mtx);
   assert(rstate);

   list_for_each_entry_safe (struct pvr_render_state,
                             entry,
                             &device->render_states,
                             link) {
      if (entry != rstate)
         continue;

      pvr_render_state_cleanup(device, &device->vk.alloc, rstate);
      list_del(&entry->link);

      vk_free(&device->vk.alloc, entry);
   }

   simple_mtx_unlock(&device->rs_mtx);
}

static void pvr_memory_emit_report(struct pvr_device *device,
                                   struct pvr_device_memory *mem,
                                   bool is_alloc,
                                   VkResult result)
{
   struct vk_device *dev_vk = &device->vk;

   if (likely(!dev_vk->memory_reports))
      return;

   assert(mem);

   const struct vk_device_memory *mem_vk = &mem->vk;
   VkDeviceMemoryReportEventTypeEXT type;

   if (result != VK_SUCCESS) {
      type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATION_FAILED_EXT;
   } else if (is_alloc) {
      type = mem_vk->import_handle_type
                ? VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT
                : VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
   } else {
      type = mem_vk->import_handle_type
                ? VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT
                : VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
   }

   const uint64_t mem_obj_id = mem->bo ? (uintptr_t)mem->bo : 0;
   const uint64_t obj_handle =
      (type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATION_FAILED_EXT)
         ? 0
         : (uintptr_t)mem;

   assert(mem_vk->memory_type_index < device->pdevice->memory.memoryTypeCount);

   const VkMemoryType *mem_type =
      &device->pdevice->memory.memoryTypes[mem_vk->memory_type_index];

   vk_emit_device_memory_report(dev_vk,
                                type,
                                mem_obj_id,
                                mem_vk->size,
                                VK_OBJECT_TYPE_DEVICE_MEMORY,
                                obj_handle,
                                mem_type->heapIndex);
}

VkResult pvr_AllocateMemory(VkDevice _device,
                            const VkMemoryAllocateInfo *pAllocateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkDeviceMemory *pMem)
{
   const VkImportMemoryFdInfoKHR *fd_info = NULL;
   VK_FROM_HANDLE(pvr_device, device, _device);
   enum pvr_winsys_bo_type type = PVR_WINSYS_BO_TYPE_GPU;
   struct pvr_device_memory *mem;
   VkResult result;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
   assert(pAllocateInfo->allocationSize > 0);

   const VkMemoryType *mem_type =
      &device->pdevice->memory.memoryTypes[pAllocateInfo->memoryTypeIndex];
   const VkMemoryHeap *mem_heap =
      &device->pdevice->memory.memoryHeaps[mem_type->heapIndex];

   VkDeviceSize aligned_alloc_size =
      ALIGN_POT(pAllocateInfo->allocationSize, device->ws->page_size);

   if (aligned_alloc_size > mem_heap->size)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   mem = vk_device_memory_create(&device->vk,
                                 pAllocateInfo,
                                 pAllocator,
                                 sizeof(*mem));
   if (!mem)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_foreach_struct_const (ext, pAllocateInfo->pNext) {
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA:
         if (device->ws->display_fd >= 0)
            type = PVR_WINSYS_BO_TYPE_DISPLAY;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         fd_info = (void *)ext;
         break;
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         break;
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO:
         /* We don't have particular optimizations associated with memory
          * allocations that won't be suballocated to multiple resources.
          */
         break;
      case VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO:
         /* We're not yet using any of the flags provided. */
         break;
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }

   if (fd_info && fd_info->handleType) {
      assert(
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      result = device->ws->ops->buffer_create_from_fd(device->ws,
                                                      fd_info->fd,
                                                      &mem->bo);
      if (result != VK_SUCCESS)
         goto err_vk_device_memory_destroy;

      /* For security purposes, we reject importing the bo if it's smaller
       * than the requested allocation size. This prevents a malicious client
       * from passing a buffer to a trusted client, lying about the size, and
       * telling the trusted client to try and texture from an image that goes
       * out-of-bounds. This sort of thing could lead to GPU hangs or worse
       * in the trusted client. The trusted client can protect itself against
       * this sort of attack but only if it can trust the buffer size.
       */
      if (aligned_alloc_size > mem->bo->size) {
         result = vk_errorf(device,
                            VK_ERROR_INVALID_EXTERNAL_HANDLE,
                            "Aligned requested size too large for the given fd "
                            "%" PRIu64 "B > %" PRIu64 "B",
                            pAllocateInfo->allocationSize,
                            mem->bo->size);
         device->ws->ops->buffer_destroy(mem->bo);
         goto err_vk_device_memory_destroy;
      }

      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
   } else {
      /* Align physical allocations to the page size of the heap that will be
       * used when binding device memory (see pvr_bind_memory()) to ensure the
       * entire allocation can be mapped.
       */
      const uint64_t alignment = device->heaps.general_heap->page_size;

      /* FIXME: Need to determine the flags based on
       * device->pdevice->memory.memoryTypes[pAllocateInfo->memoryTypeIndex].propertyFlags.
       *
       * The alternative would be to store the flags alongside the memory
       * types as an array that's indexed by pAllocateInfo->memoryTypeIndex so
       * that they can be looked up.
       */
      result = device->ws->ops->buffer_create(device->ws,
                                              pAllocateInfo->allocationSize,
                                              alignment,
                                              type,
                                              PVR_WINSYS_BO_FLAG_CPU_ACCESS,
                                              &mem->bo);
      if (result != VK_SUCCESS)
         goto err_vk_device_memory_destroy;
   }

   *pMem = pvr_device_memory_to_handle(mem);

   pvr_memory_emit_report(device, mem, true, VK_SUCCESS);

   return VK_SUCCESS;

err_vk_device_memory_destroy:
   pvr_memory_emit_report(device, mem, true, result);

   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);

   return result;
}

VkResult pvr_GetMemoryFdKHR(VkDevice _device,
                            const VkMemoryGetFdInfoKHR *pGetFdInfo,
                            int *pFd)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_device_memory, mem, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   assert(
      pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
      pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   return device->ws->ops->buffer_get_fd(mem->bo, pFd);
}

VkResult
pvr_GetMemoryFdPropertiesKHR(VkDevice _device,
                             VkExternalMemoryHandleTypeFlagBits handleType,
                             int fd,
                             VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   VK_FROM_HANDLE(pvr_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      /* FIXME: This should only allow memory types having
       * VK_MEMORY_PROPERTY_HOST_CACHED_BIT flag set, as
       * dma-buf should be imported using cacheable memory types,
       * given exporter's mmap will always map it as cacheable.
       * Ref:
       * https://www.kernel.org/doc/html/latest/driver-api/dma-buf.html#c.dma_buf_ops
       */
      pMemoryFdProperties->memoryTypeBits =
         (1 << device->pdevice->memory.memoryTypeCount) - 1;
      return VK_SUCCESS;
   default:
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

void pvr_FreeMemory(VkDevice _device,
                    VkDeviceMemory _mem,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_device_memory, mem, _mem);

   if (!mem)
      return;

   pvr_memory_emit_report(device, mem, false, VK_SUCCESS);

   /* From the Vulkan spec (§11.2.13. Freeing Device Memory):
    *   If a memory object is mapped at the time it is freed, it is implicitly
    *   unmapped.
    */
   if (mem->bo->map)
      device->ws->ops->buffer_unmap(mem->bo, false);

   device->ws->ops->buffer_destroy(mem->bo);

   vk_device_memory_destroy(&device->vk, pAllocator, &mem->vk);
}

VkResult pvr_MapMemory2(VkDevice _device,
                        const VkMemoryMapInfo *pMemoryMapInfo,
                        void **ppData)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_device_memory, mem, pMemoryMapInfo->memory);
   VkDeviceSize offset;
   VkDeviceSize size;
   VkResult result;

   if (!mem) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   offset = pMemoryMapInfo->offset;
   size = vk_device_memory_range(&mem->vk, offset, pMemoryMapInfo->size);

   void *addr = NULL;
   if (pMemoryMapInfo->flags & VK_MEMORY_MAP_PLACED_BIT_EXT) {
      const VkMemoryMapPlacedInfoEXT *placed_info =
         vk_find_struct_const(pMemoryMapInfo->pNext,
                              MEMORY_MAP_PLACED_INFO_EXT);
      addr = placed_info->pPlacedAddress;
   }

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */

   assert(size > 0);
   assert(offset + size <= mem->bo->size);

   /* From the Vulkan 1.2.194 spec:
    *
    *    "memory must not be currently host mapped"
    */
   if (mem->bo->map != NULL) {
      return vk_errorf(device,
                       VK_ERROR_MEMORY_MAP_FAILED,
                       "Memory object already mapped.");
   }

   vk_foreach_struct_const (ext, pMemoryMapInfo->pNext) {
      vk_debug_ignored_stype(ext->sType);
   }

   /* Map it all at once */
   result = device->ws->ops->buffer_map(mem->bo, addr);
   if (result != VK_SUCCESS)
      return result;

   *ppData = (uint8_t *)mem->bo->map + offset;

   return VK_SUCCESS;
}

VkResult pvr_UnmapMemory2(VkDevice _device,
                          const VkMemoryUnmapInfo *pMemoryUnmapInfo)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_device_memory, mem, pMemoryUnmapInfo->memory);

   if (mem && mem->bo->map) {
      bool reserve =
         !!(pMemoryUnmapInfo->flags & VK_MEMORY_UNMAP_RESERVE_BIT_EXT);
      return device->ws->ops->buffer_unmap(mem->bo, reserve);
   }

   return VK_SUCCESS;
}

VkResult pvr_FlushMappedMemoryRanges(VkDevice _device,
                                     uint32_t memoryRangeCount,
                                     const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VkResult
pvr_InvalidateMappedMemoryRanges(VkDevice _device,
                                 uint32_t memoryRangeCount,
                                 const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

void pvr_GetImageSparseMemoryRequirements2(
   VkDevice device,
   const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void pvr_GetDeviceMemoryCommitment(VkDevice device,
                                   VkDeviceMemory memory,
                                   VkDeviceSize *pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VkResult pvr_bind_memory(struct pvr_device *device,
                         struct pvr_device_memory *mem,
                         VkDeviceSize offset,
                         VkDeviceSize size,
                         VkDeviceSize alignment,
                         struct pvr_winsys_vma **const vma_out,
                         pvr_dev_addr_t *const dev_addr_out)
{
   VkDeviceSize virt_size =
      size + (offset & (device->heaps.general_heap->page_size - 1));
   struct pvr_winsys_vma *vma;
   pvr_dev_addr_t dev_addr;
   VkResult result;

   /* Valid usage:
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetBufferMemoryRequirements with buffer"
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetImageMemoryRequirements with image"
    */
   assert(offset % alignment == 0);
   assert(offset < mem->bo->size);

   result = device->ws->ops->heap_alloc(device->heaps.general_heap,
                                        virt_size,
                                        alignment,
                                        &vma);
   if (result != VK_SUCCESS)
      goto err_out;

   result = device->ws->ops->vma_map(vma, mem->bo, offset, size, &dev_addr);
   if (result != VK_SUCCESS)
      goto err_free_vma;

   *dev_addr_out = dev_addr;
   *vma_out = vma;

   return VK_SUCCESS;

err_free_vma:
   device->ws->ops->heap_free(vma);

err_out:
   return result;
}

void pvr_unbind_memory(struct pvr_device *device, struct pvr_winsys_vma *vma)
{
   device->ws->ops->vma_unmap(vma);
   device->ws->ops->heap_free(vma);
}

VkResult pvr_BindBufferMemory2(VkDevice _device,
                               uint32_t bindInfoCount,
                               const VkBindBufferMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   uint32_t i;

   for (i = 0; i < bindInfoCount; i++) {
      VK_FROM_HANDLE(pvr_device_memory, mem, pBindInfos[i].memory);
      VK_FROM_HANDLE(pvr_buffer, buffer, pBindInfos[i].buffer);

      VkResult result = pvr_bind_memory(device,
                                        mem,
                                        pBindInfos[i].memoryOffset,
                                        buffer->vk.size,
                                        buffer->alignment,
                                        &buffer->vma,
                                        &buffer->dev_addr);
      if (result != VK_SUCCESS) {
         while (i--) {
            VK_FROM_HANDLE(pvr_buffer, buffer, pBindInfos[i].buffer);
            pvr_unbind_memory(device, buffer->vma);
         }

         return result;
      }
   }

   return VK_SUCCESS;
}

/* Event functions. */

VkResult pvr_CreateEvent(VkDevice _device,
                         const VkEventCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkEvent *pEvent)
{
   VK_FROM_HANDLE(pvr_device, device, _device);

   struct pvr_event *event = vk_object_alloc(&device->vk,
                                             pAllocator,
                                             sizeof(*event),
                                             VK_OBJECT_TYPE_EVENT);
   if (!event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   event->sync = NULL;
   event->state = PVR_EVENT_STATE_RESET_BY_HOST;

   *pEvent = pvr_event_to_handle(event);

   return VK_SUCCESS;
}

void pvr_DestroyEvent(VkDevice _device,
                      VkEvent _event,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_event, event, _event);

   if (!event)
      return;

   if (event->sync)
      vk_sync_destroy(&device->vk, event->sync);

   vk_object_free(&device->vk, pAllocator, event);
}

VkResult pvr_GetEventStatus(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_event, event, _event);
   VkResult result;

   switch (event->state) {
   case PVR_EVENT_STATE_SET_BY_DEVICE:
      if (!event->sync)
         return VK_EVENT_RESET;

      result =
         vk_sync_wait(&device->vk, event->sync, 0U, VK_SYNC_WAIT_COMPLETE, 0);
      result = (result == VK_SUCCESS) ? VK_EVENT_SET : VK_EVENT_RESET;
      break;

   case PVR_EVENT_STATE_RESET_BY_DEVICE:
      if (!event->sync)
         return VK_EVENT_RESET;

      result =
         vk_sync_wait(&device->vk, event->sync, 0U, VK_SYNC_WAIT_COMPLETE, 0);
      result = (result == VK_SUCCESS) ? VK_EVENT_RESET : VK_EVENT_SET;
      break;

   case PVR_EVENT_STATE_SET_BY_HOST:
      result = VK_EVENT_SET;
      break;

   case PVR_EVENT_STATE_RESET_BY_HOST:
      result = VK_EVENT_RESET;
      break;

   default:
      UNREACHABLE("Event object in unknown state");
   }

   return result;
}

VkResult pvr_SetEvent(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(pvr_event, event, _event);

   if (event->sync) {
      VK_FROM_HANDLE(pvr_device, device, _device);

      const VkResult result = vk_sync_signal(&device->vk, event->sync, 0);
      if (result != VK_SUCCESS)
         return result;
   }

   event->state = PVR_EVENT_STATE_SET_BY_HOST;

   return VK_SUCCESS;
}

VkResult pvr_ResetEvent(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(pvr_event, event, _event);

   if (event->sync) {
      VK_FROM_HANDLE(pvr_device, device, _device);

      const VkResult result = vk_sync_reset(&device->vk, event->sync);
      if (result != VK_SUCCESS)
         return result;
   }

   event->state = PVR_EVENT_STATE_RESET_BY_HOST;

   return VK_SUCCESS;
}

/* Buffer functions. */

VkResult pvr_CreateBuffer(VkDevice _device,
                          const VkBufferCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkBuffer *pBuffer)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   const uint32_t alignment = device->pdevice->ws->page_size;
   struct pvr_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
   assert(pCreateInfo->usage != 0);

   /* We check against (ULONG_MAX - alignment) to prevent overflow issues */
   if (pCreateInfo->size >= ULONG_MAX - alignment)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   buffer =
      vk_buffer_create(&device->vk, pCreateInfo, pAllocator, sizeof(*buffer));
   if (!buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   buffer->alignment = alignment;

   *pBuffer = pvr_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

VkDeviceAddress
pvr_GetBufferDeviceAddress(UNUSED VkDevice device,
                           const VkBufferDeviceAddressInfo *pInfo)
{
   VK_FROM_HANDLE(pvr_buffer, buffer, pInfo->buffer);

   return buffer->dev_addr.addr;
}

uint64_t
pvr_GetBufferOpaqueCaptureAddress(UNUSED VkDevice device,
                                  UNUSED const VkBufferDeviceAddressInfo *pInfo)
{
   pvr_finishme("Missing support for bufferDeviceAddressCaptureReplay");
   return 0;
}

uint64_t pvr_GetDeviceMemoryOpaqueCaptureAddress(
   UNUSED VkDevice device,
   UNUSED const VkDeviceMemoryOpaqueCaptureAddressInfo *pInfo)
{
   pvr_finishme("Missing support for bufferDeviceAddressCaptureReplay");
   return 0;
}

void pvr_DestroyBuffer(VkDevice _device,
                       VkBuffer _buffer,
                       const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_buffer, buffer, _buffer);

   if (!buffer)
      return;

   if (buffer->vma)
      pvr_unbind_memory(device, buffer->vma);

   vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
}

VkResult pvr_gpu_upload(struct pvr_device *device,
                        struct pvr_winsys_heap *heap,
                        const void *data,
                        size_t size,
                        uint64_t alignment,
                        struct pvr_suballoc_bo **const pvr_bo_out)
{
   struct pvr_suballoc_bo *suballoc_bo = NULL;
   struct pvr_suballocator *allocator;
   VkResult result;
   void *map;

   assert(size > 0);

   if (heap == device->heaps.general_heap)
      allocator = &device->suballoc_general;
   else if (heap == device->heaps.pds_heap)
      allocator = &device->suballoc_pds;
   else if (heap == device->heaps.transfer_frag_heap)
      allocator = &device->suballoc_transfer;
   else if (heap == device->heaps.usc_heap)
      allocator = &device->suballoc_usc;
   else
      UNREACHABLE("Unknown heap type");

   result = pvr_bo_suballoc(allocator, size, alignment, false, &suballoc_bo);
   if (result != VK_SUCCESS)
      return result;

   map = pvr_bo_suballoc_get_map_addr(suballoc_bo);
   if (data)
      memcpy(map, data, size);

   *pvr_bo_out = suballoc_bo;

   return VK_SUCCESS;
}

VkResult pvr_gpu_upload_usc(struct pvr_device *device,
                            const void *code,
                            size_t code_size,
                            uint64_t code_alignment,
                            struct pvr_suballoc_bo **const pvr_bo_out)
{
   struct pvr_suballoc_bo *suballoc_bo = NULL;
   VkResult result;
   void *map;

   assert(code_size > 0);

   /* The USC will prefetch the next instruction, so over allocate by 1
    * instruction to prevent reading off the end of a page into a potentially
    * unallocated page.
    */
   result = pvr_bo_suballoc(&device->suballoc_usc,
                            code_size + ROGUE_MAX_INSTR_BYTES,
                            code_alignment,
                            false,
                            &suballoc_bo);
   if (result != VK_SUCCESS)
      return result;

   map = pvr_bo_suballoc_get_map_addr(suballoc_bo);
   memcpy(map, code, code_size);

   *pvr_bo_out = suballoc_bo;

   return VK_SUCCESS;
}

/**
 * \brief Upload PDS program data and code segments from host memory to device
 * memory.
 *
 * \param[in] device            Logical device pointer.
 * \param[in] data              Pointer to PDS data segment to upload.
 * \param[in] data_size_dwords  Size of PDS data segment in dwords.
 * \param[in] data_alignment    Required alignment of the PDS data segment in
 *                              bytes. Must be a power of two.
 * \param[in] code              Pointer to PDS code segment to upload.
 * \param[in] code_size_dwords  Size of PDS code segment in dwords.
 * \param[in] code_alignment    Required alignment of the PDS code segment in
 *                              bytes. Must be a power of two.
 * \param[in] min_alignment     Minimum alignment of the bo holding the PDS
 *                              program in bytes.
 * \param[out] pds_upload_out   On success will be initialized based on the
 *                              uploaded PDS program.
 * \return VK_SUCCESS on success, or error code otherwise.
 */
VkResult pvr_gpu_upload_pds(struct pvr_device *device,
                            const uint32_t *data,
                            uint32_t data_size_dwords,
                            uint32_t data_alignment,
                            const uint32_t *code,
                            uint32_t code_size_dwords,
                            uint32_t code_alignment,
                            uint64_t min_alignment,
                            struct pvr_pds_upload *const pds_upload_out)
{
   /* All alignment and sizes below are in bytes. */
   const size_t data_size = PVR_DW_TO_BYTES(data_size_dwords);
   const size_t code_size = PVR_DW_TO_BYTES(code_size_dwords);
   const uint64_t data_aligned_size = ALIGN_POT(data_size, data_alignment);
   const uint64_t code_aligned_size = ALIGN_POT(code_size, code_alignment);
   const uint32_t code_offset = ALIGN_POT(data_aligned_size, code_alignment);
   const uint64_t bo_alignment = MAX2(min_alignment, data_alignment);
   const uint64_t bo_size = (!!code) ? (code_offset + code_aligned_size)
                                     : data_aligned_size;
   VkResult result;
   void *map;

   assert(code || data);
   assert(!code || (code_size_dwords != 0 && code_alignment != 0));
   assert(!data || (data_size_dwords != 0 && data_alignment != 0));

   result = pvr_bo_suballoc(&device->suballoc_pds,
                            bo_size,
                            bo_alignment,
                            true,
                            &pds_upload_out->pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   map = pvr_bo_suballoc_get_map_addr(pds_upload_out->pvr_bo);

   if (data) {
      memcpy(map, data, data_size);

      pds_upload_out->data_offset = pds_upload_out->pvr_bo->dev_addr.addr -
                                    device->heaps.pds_heap->base_addr.addr;

      /* Store data size in dwords. */
      assert(data_aligned_size % 4 == 0);
      pds_upload_out->data_size = data_aligned_size / 4;
   } else {
      pds_upload_out->data_offset = 0;
      pds_upload_out->data_size = 0;
   }

   if (code) {
      memcpy((uint8_t *)map + code_offset, code, code_size);

      pds_upload_out->code_offset =
         (pds_upload_out->pvr_bo->dev_addr.addr + code_offset) -
         device->heaps.pds_heap->base_addr.addr;

      /* Store code size in dwords. */
      assert(code_aligned_size % 4 == 0);
      pds_upload_out->code_size = code_aligned_size / 4;
   } else {
      pds_upload_out->code_offset = 0;
      pds_upload_out->code_size = 0;
   }

   return VK_SUCCESS;
}

void pvr_render_targets_fini(struct pvr_render_target *render_targets,
                             uint32_t render_targets_count)
{
   for (uint32_t i = 0; i < render_targets_count; i++) {
      pvr_render_targets_datasets_destroy(&render_targets[i]);
      pthread_mutex_destroy(&render_targets[i].mutex);
   }
}

void pvr_render_state_cleanup(struct pvr_device *device,
                              const VkAllocationCallbacks *pAllocator,
                              const struct pvr_render_state *rstate)
{
   if (!rstate)
      return;

   for (uint32_t i = 0; i < rstate->render_count; i++) {
      pvr_spm_finish_bgobj_state(device,
                                 &rstate->spm_bgobj_state_per_render[i]);

      pvr_spm_finish_eot_state(device, &rstate->spm_eot_state_per_render[i]);
   }

   pvr_spm_scratch_buffer_release(device, rstate->scratch_buffer);
   pvr_render_targets_fini(rstate->render_targets,
                           rstate->render_targets_count);
   pvr_bo_suballoc_free(rstate->ppp_state_bo);
   vk_free2(&device->vk.alloc, pAllocator, rstate->render_targets);
}

void pvr_GetBufferMemoryRequirements2(
   VkDevice _device,
   const VkBufferMemoryRequirementsInfo2 *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(pvr_buffer, buffer, pInfo->buffer);
   VK_FROM_HANDLE(pvr_device, device, _device);
   uint64_t size;

   /* The Vulkan 1.0.166 spec says:
    *
    *    memoryTypeBits is a bitmask and contains one bit set for every
    *    supported memory type for the resource. Bit 'i' is set if and only
    *    if the memory type 'i' in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported for the resource.
    *
    * All types are currently supported for buffers.
    */
   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      (1ul << device->pdevice->memory.memoryTypeCount) - 1;

   pMemoryRequirements->memoryRequirements.alignment = buffer->alignment;

   size = buffer->vk.size;

   if (size % device->ws->page_size == 0 ||
       size % device->ws->page_size >
          device->ws->page_size - PVR_BUFFER_MEMORY_PADDING_SIZE) {
      /* TODO: We can save memory by having one extra virtual page mapped
       * in and having the first and last virtual page mapped to the first
       * physical address.
       */
      size += PVR_BUFFER_MEMORY_PADDING_SIZE;
   }

   /* Use align64 to prevent overflow for large buffers (> 4GB). */
   pMemoryRequirements->memoryRequirements.size =
      align64(size, buffer->alignment);

   vk_foreach_struct (ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *)ext;

         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = false;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void pvr_GetDeviceBufferMemoryRequirements(
   VkDevice _device,
   const VkDeviceBufferMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_buffer buffer = { 0 };

   /* Initialize a minimal buffer structure */
   vk_buffer_init(&device->vk, &buffer.vk, pInfo->pCreateInfo);
   buffer.alignment = device->pdevice->ws->page_size;

   VkBufferMemoryRequirementsInfo2 buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
      .buffer = pvr_buffer_to_handle(&buffer),
   };

   pvr_GetBufferMemoryRequirements2(_device, &buffer_info, pMemoryRequirements);

   /* Clean up the temporary buffer */
   vk_buffer_finish(&buffer.vk);
}

void pvr_GetDeviceImageMemoryRequirements(
   VkDevice _device,
   const VkDeviceImageMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_image image = { 0 };

   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);
   pvr_image_init(device, pInfo->pCreateInfo, &image);

   VkImageMemoryRequirementsInfo2 image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = pvr_image_to_handle(&image),
   };

   pvr_GetImageMemoryRequirements2(_device, &image_info, pMemoryRequirements);

   pvr_image_fini(device, &image);
   vk_image_finish(&image.vk);
}

void pvr_GetDeviceImageSparseMemoryRequirements(
   VkDevice device,
   const VkDeviceImageMemoryRequirements *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   /* Sparse images are not yet supported */
   *pSparseMemoryRequirementCount = 0;
}

void pvr_GetImageMemoryRequirements2(VkDevice _device,
                                     const VkImageMemoryRequirementsInfo2 *pInfo,
                                     VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_image, image, pInfo->image);

   /* The Vulkan 1.0.166 spec says:
    *
    *    memoryTypeBits is a bitmask and contains one bit set for every
    *    supported memory type for the resource. Bit 'i' is set if and only
    *    if the memory type 'i' in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported for the resource.
    *
    * All types are currently supported for images.
    */
   const uint32_t memory_types =
      (1ul << device->pdevice->memory.memoryTypeCount) - 1;

   /* TODO: The returned size is aligned here in case of arrays/CEM (as is done
    * in GetImageMemoryRequirements()), but this should be known at image
    * creation time (pCreateInfo->arrayLayers > 1). This is confirmed in
    * ImageCreate()/ImageGetMipMapOffsetInBytes() where it aligns the size to
    * 4096 if pCreateInfo->arrayLayers > 1. So is the alignment here actually
    * necessary? If not, what should it be when pCreateInfo->arrayLayers == 1?
    *
    * Note: Presumably the 4096 alignment requirement comes from the Vulkan
    * driver setting RGX_CR_TPU_TAG_CEM_4K_FACE_PACKING_EN when setting up
    * render and compute jobs.
    */
   pMemoryRequirements->memoryRequirements.alignment = image->alignment;
   pMemoryRequirements->memoryRequirements.size =
      align64(image->total_size, image->alignment);
   pMemoryRequirements->memoryRequirements.memoryTypeBits = memory_types;

   vk_foreach_struct (ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         bool has_ext_handle_types = image->vk.external_handle_types != 0;
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *)ext;

         req->prefersDedicatedAllocation = has_ext_handle_types;
         req->requiresDedicatedAllocation = has_ext_handle_types;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}
