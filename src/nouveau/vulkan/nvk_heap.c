/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_heap.h"

#include "nvk_device.h"
#include "nvk_physical_device.h"
#include "nvk_queue.h"

#include "util/macros.h"

#include "nv_push.h"
#include "nv_push_cl90b5.h"

VkResult
nvk_heap_init(struct nvk_device *dev, struct nvk_heap *heap,
              enum nvkmd_mem_flags mem_flags,
              enum nvkmd_mem_map_flags map_flags,
              uint32_t overalloc, bool contiguous)
{
   VkResult result;

   memset(heap, 0, sizeof(*heap));

   heap->mem_flags = mem_flags;
   if (map_flags)
      heap->mem_flags |= NVKMD_MEM_CAN_MAP;
   heap->map_flags = map_flags;
   heap->overalloc = overalloc;

   if (contiguous) {
      result = nvkmd_dev_alloc_va(dev->nvkmd, &dev->vk.base,
                                  0 /* va_flags */, 0 /* pte_kind */,
                                  NVK_HEAP_MAX_SIZE, 0 /* align_B */,
                                  0 /* fixed_addr */,
                                  &heap->contig_va);
      if (result != VK_SUCCESS)
         return result;
   }

   simple_mtx_init(&heap->mutex, mtx_plain);
   util_vma_heap_init(&heap->heap, 0, 0);

   heap->total_size = 0;
   heap->mem_count = 0;

   return VK_SUCCESS;
}

void
nvk_heap_finish(struct nvk_device *dev, struct nvk_heap *heap)
{
   /* Freeing the VA will unbind all the memory */
   if (heap->contig_va)
      nvkmd_va_free(heap->contig_va);

   for (uint32_t mem_idx = 0; mem_idx < heap->mem_count; mem_idx++)
      nvkmd_mem_unref(heap->mem[mem_idx].mem);

   util_vma_heap_finish(&heap->heap);
   simple_mtx_destroy(&heap->mutex);
}

static uint64_t
encode_vma(uint32_t mem_idx, uint64_t mem_offset)
{
   assert(mem_idx < UINT16_MAX - 1);
   assert(mem_offset < (1ull << 48));
   return ((uint64_t)(mem_idx + 1) << 48) | mem_offset;
}

static uint32_t
vma_mem_idx(uint64_t offset)
{
   offset = offset >> 48;
   assert(offset > 0);
   return offset - 1;
}

static uint64_t
vma_mem_offset(uint64_t offset)
{
   return offset & BITFIELD64_MASK(48);
}

static VkResult
nvk_heap_grow_locked(struct nvk_device *dev, struct nvk_heap *heap)
{
   VkResult result;

   if (heap->mem_count >= NVK_HEAP_MAX_BO_COUNT) {
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Heap has already hit its maximum size");
   }

   /* First two BOs are MIN_SIZE, double after that */
   const uint64_t new_mem_size =
      NVK_HEAP_MIN_SIZE << (MAX2(heap->mem_count, 1) - 1);

   struct nvkmd_mem *mem;
   if (heap->map_flags) {
      result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &dev->vk.base,
                                          new_mem_size, 0, heap->mem_flags,
                                          heap->map_flags, &mem);
   } else {
      result = nvkmd_dev_alloc_mem(dev->nvkmd, &dev->vk.base,
                                   new_mem_size, 0, heap->mem_flags, &mem);
   }
   if (result != VK_SUCCESS)
      return result;

   assert(mem->size_B == new_mem_size);

   uint64_t addr;
   if (heap->contig_va != NULL) {
      result = nvkmd_va_bind_mem(heap->contig_va, &dev->vk.base,
                                 heap->total_size, mem, 0, new_mem_size);
      if (result != VK_SUCCESS) {
         nvkmd_mem_unref(mem);
         return result;
      }
      addr = heap->contig_va->addr + heap->total_size;

      /* For contiguous heaps, we can now free the padding from the previous
       * BO because the BO we just added will provide the needed padding. For
       * non-contiguous heaps, we have to leave each BO padded individually.
       */
      if (heap->mem_count > 0) {
         struct nvkmd_mem *prev_mem = heap->mem[heap->mem_count - 1].mem;
         assert(heap->overalloc < prev_mem->size_B);
         const uint64_t pad_vma =
            encode_vma(heap->mem_count - 1, prev_mem->size_B - heap->overalloc);
         util_vma_heap_free(&heap->heap, pad_vma, heap->overalloc);
      }
   } else {
      addr = mem->va->addr;
   }

   uint64_t vma = encode_vma(heap->mem_count, 0);
   assert(heap->overalloc < new_mem_size);
   util_vma_heap_free(&heap->heap, vma, new_mem_size - heap->overalloc);

   heap->mem[heap->mem_count++] = (struct nvk_heap_mem) {
      .mem = mem,
      .addr = addr,
   };
   heap->total_size += new_mem_size;

   return VK_SUCCESS;
}

static VkResult
nvk_heap_alloc_locked(struct nvk_device *dev, struct nvk_heap *heap,
                      uint64_t size, uint32_t alignment,
                      uint64_t *addr_out, void **map_out)
{
   while (1) {
      uint64_t vma = util_vma_heap_alloc(&heap->heap, size, alignment);
      if (vma != 0) {
         uint32_t mem_idx = vma_mem_idx(vma);
         uint64_t mem_offset = vma_mem_offset(vma);

         assert(mem_idx < heap->mem_count);
         assert(heap->mem[mem_idx].mem != NULL);
         assert(mem_offset + size <= heap->mem[mem_idx].mem->size_B);

         *addr_out = heap->mem[mem_idx].addr + mem_offset;
         if (map_out != NULL) {
            if (heap->mem[mem_idx].mem->map != NULL)
               *map_out = (char *)heap->mem[mem_idx].mem->map + mem_offset;
            else
               *map_out = NULL;
         }

         return VK_SUCCESS;
      }

      VkResult result = nvk_heap_grow_locked(dev, heap);
      if (result != VK_SUCCESS)
         return result;
   }
}

static void
nvk_heap_free_locked(struct nvk_device *dev, struct nvk_heap *heap,
                     uint64_t addr, uint64_t size)
{
   assert(addr + size > addr);

   for (uint32_t mem_idx = 0; mem_idx < heap->mem_count; mem_idx++) {
      if (addr < heap->mem[mem_idx].addr)
         continue;

      uint64_t mem_offset = addr - heap->mem[mem_idx].addr;
      if (mem_offset >= heap->mem[mem_idx].mem->size_B)
         continue;

      assert(mem_offset + size <= heap->mem[mem_idx].mem->size_B);
      uint64_t vma = encode_vma(mem_idx, mem_offset);

      util_vma_heap_free(&heap->heap, vma, size);
      return;
   }
   assert(!"Failed to find heap BO");
}

VkResult
nvk_heap_alloc(struct nvk_device *dev, struct nvk_heap *heap,
               uint64_t size, uint32_t alignment,
               uint64_t *addr_out, void **map_out)
{
   simple_mtx_lock(&heap->mutex);
   VkResult result = nvk_heap_alloc_locked(dev, heap, size, alignment,
                                           addr_out, map_out);
   simple_mtx_unlock(&heap->mutex);

   return result;
}

VkResult
nvk_heap_upload(struct nvk_device *dev, struct nvk_heap *heap,
                const void *data, size_t size, uint32_t alignment,
                uint64_t *addr_out)
{
   simple_mtx_lock(&heap->mutex);
   void *map = NULL;
   VkResult result = nvk_heap_alloc_locked(dev, heap, size, alignment,
                                           addr_out, &map);
   simple_mtx_unlock(&heap->mutex);

   if (result != VK_SUCCESS)
      return result;

   if (map != NULL && (heap->map_flags & NVKMD_MEM_MAP_WR)) {
      /* If we have a map, copy directly with memcpy */
      memcpy(map, data, size);
   } else {
      /* Otherwise, kick off an upload with the upload queue.
       *
       * This is a queued operation that the driver ensures happens before any
       * more client work via semaphores.  Because this is asynchronous and
       * heap allocations are synchronous we have to be a bit careful here.
       * The heap only ever tracks the current known CPU state of everything
       * while the upload queue makes that state valid at some point in the
       * future.
       *
       * This can be especially tricky for very fast upload/free cycles such
       * as if the client compiles a shader, throws it away without using it,
       * and then compiles another shader that ends up at the same address.
       * What makes this all correct is the fact that the everything on the
       * upload queue happens in a well-defined device-wide order.  In this
       * case the first shader will get uploaded and then the second will get
       * uploaded over top of it.  As long as we don't free the memory out
       * from under the upload queue, everything will end up in the correct
       * state by the time the client's shaders actually execute.
       */
      result = nvk_upload_queue_upload(dev, &dev->upload, *addr_out, data, size);
      if (result != VK_SUCCESS) {
         nvk_heap_free(dev, heap, *addr_out, size);
         return result;
      }
   }

   return VK_SUCCESS;
}

void
nvk_heap_free(struct nvk_device *dev, struct nvk_heap *heap,
              uint64_t addr, uint64_t size)
{
   simple_mtx_lock(&heap->mutex);
   nvk_heap_free_locked(dev, heap, addr, size);
   simple_mtx_unlock(&heap->mutex);
}
