/* Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "compiler/brw/brw_disasm.h"

static inline uint32_t
shader_bo_index(struct anv_shader_heap *heap, uint64_t addr)
{
   uint64_t alloc_offset = addr - heap->va_range.addr;

   unsigned b;
   if (alloc_offset < heap->base_chunk_size) {
      b = alloc_offset < heap->start_chunk_size ? 0 :
         (util_last_bit64(alloc_offset) - heap->start_pot_size);
      assert(b < heap->small_chunk_count);
      return b;
   } else if (alloc_offset >= (heap->va_range.size - heap->base_chunk_size)) {
      alloc_offset = heap->va_range.size - alloc_offset - 1;
      b = alloc_offset < heap->start_chunk_size ? 0 :
         (util_last_bit64(alloc_offset) - heap->start_pot_size);
      assert(b < heap->small_chunk_count);
      b = heap->small_chunk_count + b;
   } else {
      b = 2 * heap->small_chunk_count +
          (alloc_offset / heap->base_chunk_size) - 1;
   }

   assert(addr >= heap->bos[b].addr &&
          addr < (heap->bos[b].addr + heap->bos[b].size));

   return b;
}

VkResult
anv_shader_heap_init(struct anv_shader_heap *heap,
                     struct anv_device *device,
                     struct anv_va_range va_range,
                     uint32_t start_pot_size,
                     uint32_t base_pot_size)
{
   assert((1ull << start_pot_size) >= device->info->mem_alignment);
   assert(base_pot_size >= start_pot_size);
   assert(va_range.size % (1ull << base_pot_size) == 0);
   assert((DIV_ROUND_UP(va_range.size, (1ull << base_pot_size)) -
           (base_pot_size - start_pot_size) - 1) <
          ARRAY_SIZE(heap->bos));

   memset(heap, 0, sizeof(*heap));

   heap->start_pot_size = start_pot_size;
   heap->base_pot_size = base_pot_size;
   heap->start_chunk_size = 1ull << start_pot_size;
   heap->base_chunk_size = 1ull << base_pot_size;
   heap->small_chunk_count = base_pot_size - start_pot_size + 1;
   heap->device = device;
   heap->va_range = va_range;

   for (uint32_t i = 0; i < heap->small_chunk_count; i++) {
      heap->bos[i].size =
         heap->bos[heap->small_chunk_count + i].size =
         1ull << (i == 0 ? start_pot_size : (start_pot_size + i - 1));


      heap->bos[i].addr = heap->va_range.addr +
         (i == 0 ? 0 : (1ull << (start_pot_size + i - 1)));
      heap->bos[heap->small_chunk_count + i].addr =
         heap->va_range.addr + heap->va_range.size -
         (1ull << (start_pot_size + i));
   }

   const uint64_t base_chunks_size =
      heap->va_range.size - 2 * heap->base_chunk_size;
   for (uint32_t i = 0; i < base_chunks_size / heap->base_chunk_size; i++) {
      heap->bos[2 * heap->small_chunk_count + i].addr =
         heap->va_range.addr + heap->base_chunk_size + i * heap->base_chunk_size;
      heap->bos[2 * heap->small_chunk_count + i].size = heap->base_chunk_size;
   }

   simple_mtx_init(&heap->mutex, mtx_plain);
   util_vma_heap_init(&heap->vma, va_range.addr, va_range.size - 64);

   BITSET_ZERO(heap->allocated_bos);

   return VK_SUCCESS;
}

void
anv_shader_heap_finish(struct anv_shader_heap *heap)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(heap->bos); i++) {
      if (heap->bos[i].bo) {
         ANV_DMR_BO_FREE(&heap->device->vk.base, heap->bos[i].bo);
         anv_device_release_bo(heap->device, heap->bos[i].bo);
         heap->bos[i].bo = NULL;
      }
   }

   util_vma_heap_finish(&heap->vma);
   simple_mtx_destroy(&heap->mutex);
}

struct anv_shader_alloc
anv_shader_heap_alloc(struct anv_shader_heap *heap,
                      uint64_t size,
                      uint64_t align,
                      bool capture_replay,
                      uint64_t requested_offset)
{
   assert(align <= heap->base_chunk_size);
   assert(size <= heap->base_chunk_size);

   simple_mtx_lock(&heap->mutex);

   heap->vma.nospan_shift = MAX2(21, util_last_bit64(size) - 1);
   if ((1ull << heap->vma.nospan_shift) < size)
      heap->vma.nospan_shift++;

   uint64_t addr = 0;
   if (requested_offset) {
      if (util_vma_heap_alloc_addr(&heap->vma,
                                   heap->va_range.addr + requested_offset,
                                   size)) {
         addr = heap->va_range.addr + requested_offset;
      }
   } else {
      if (capture_replay) {
         heap->vma.alloc_high = true;
         addr = util_vma_heap_alloc(&heap->vma, size, align);
      } else {
         heap->vma.alloc_high = false;
         addr = util_vma_heap_alloc(&heap->vma, size, align);
      }
   }

   struct anv_shader_alloc alloc = {};

   if (addr != 0) {
      /* BSpec 56653:
       *
       *    "Due to prefetch of the instruction stream, the EUs may attempt to
       *     access up to 56 cachelines (3584b) beyond the end of the kernel"
       *
       *    "Note that the General State Access Upper Bound field of the
       *     STATE_BASE_ADDRESS command can be used to prevent memory accesses
       *     past the end of the General State heap (where kernel programs
       *     must reside)."
       *
       * To avoid page faults, we add the required padding to the upper address
       * bound when allocating the buffers to back the shader allocation. If
       * the resulting padded address range goes beyond the end of the heap, we
       * can safely clamp it because we will program the instruction buffer
       * size in STATE_BASE_ADDRESS to be equal to the shader heap VA range.
       */
      const unsigned overfetch_size = 3584;
      uint64_t bound = addr + size + overfetch_size;
      bound = MIN2(bound, heap->va_range.addr + heap->va_range.size);

      const uint32_t bo_begin_idx = shader_bo_index(heap, addr);
      const uint32_t bo_end_idx = shader_bo_index(heap, bound - 1);
      for (uint32_t i = MIN2(bo_begin_idx, bo_end_idx);
           i <= MAX2(bo_begin_idx, bo_end_idx); i++) {
         if (heap->bos[i].bo != NULL)
            continue;

         VkResult result =
            anv_device_alloc_bo(heap->device, "shaders",
                                heap->bos[i].size,
                                ANV_BO_ALLOC_FIXED_ADDRESS |
                                ANV_BO_ALLOC_MAPPED |
                                ANV_BO_ALLOC_HOST_CACHED_COHERENT |
                                ANV_BO_ALLOC_CAPTURE |
                                ANV_BO_ALLOC_INTERNAL,
                                heap->bos[i].addr,
                                &heap->bos[i].bo);
         ANV_DMR_BO_ALLOC(&heap->device->vk.base, heap->bos[i].bo, result);
         if (result == VK_SUCCESS)
            BITSET_SET(heap->allocated_bos, i);
         else {
            util_vma_heap_free(&heap->vma, addr, size);
            addr = 0;
            break;
         }
      }

      if (addr != 0) {
         alloc.offset = addr - heap->va_range.addr;
         alloc.alloc_size = size;
      }
   }

   simple_mtx_unlock(&heap->mutex);

   return alloc;
}

void
anv_shader_heap_free(struct anv_shader_heap *heap, struct anv_shader_alloc alloc)
{
   simple_mtx_lock(&heap->mutex);

   util_vma_heap_free(&heap->vma, heap->va_range.addr + alloc.offset,
                      alloc.alloc_size);

   simple_mtx_unlock(&heap->mutex);
}

void
anv_shader_heap_upload(struct anv_shader_heap *heap,
                       struct anv_shader_alloc alloc,
                       const void *data, uint64_t size)
{
   const uint32_t bo_begin_idx = shader_bo_index(
      heap, heap->va_range.addr + alloc.offset);
   const uint32_t bo_end_idx = shader_bo_index(
      heap, heap->va_range.addr + alloc.offset + size - 1);

   const uint64_t upload_addr = heap->va_range.addr + alloc.offset;
   for (uint32_t i = MIN2(bo_begin_idx, bo_end_idx);
        i <= MAX2(bo_begin_idx, bo_end_idx); i++) {
      const uint64_t bo_offset =
         MAX2(upload_addr, heap->bos[i].addr) - heap->bos[i].addr;
      const uint32_t data_offset =
         upload_addr - (heap->bos[i].addr + bo_offset);
      const uint64_t copy_size =
         MIN2(heap->bos[i].size - bo_offset, size - data_offset);

      memcpy(heap->bos[i].bo->map + bo_offset, data, copy_size);
   }
}
