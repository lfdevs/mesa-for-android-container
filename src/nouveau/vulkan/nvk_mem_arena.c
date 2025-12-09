/*
 * Copyright © 2025 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_mem_arena.h"

#include "nvk_device.h"

#include "util/u_atomic.h"

VkResult
nvk_mem_arena_init(struct nvk_device *dev, struct nvk_mem_arena *arena,
                   enum nvkmd_mem_flags mem_flags,
                   enum nvkmd_mem_map_flags map_flags,
                   bool contiguous, uint64_t max_size_B)
{
   VkResult result;

   memset(arena, 0, sizeof(*arena));

   arena->mem_flags = mem_flags;
   if (map_flags)
      arena->mem_flags |= NVKMD_MEM_CAN_MAP;
   arena->map_flags = map_flags;

   assert(util_is_power_of_two_nonzero64(max_size_B));
   assert(max_size_B >= NVK_MEM_ARENA_MIN_SIZE);
   assert(max_size_B <= NVK_MEM_ARENA_MAX_SIZE);
   arena->max_mem_count =
      util_logbase2(max_size_B) - NVK_MEM_ARENA_MIN_SIZE_LOG2 + 1;
   arena->mem_count = 0;

   if (contiguous) {
      result = nvkmd_dev_alloc_va(dev->nvkmd, &dev->vk.base,
                                  0 /* va_flags */, 0 /* pte_kind */,
                                  max_size_B, 0 /* align_B */,
                                  0 /* fixed_addr */,
                                  &arena->contig_va);
      if (result != VK_SUCCESS)
         return result;
   }

   simple_mtx_init(&arena->mutex, mtx_plain);

   return VK_SUCCESS;
}

void
nvk_mem_arena_finish(struct nvk_device *dev, struct nvk_mem_arena *arena)
{
   /* Freeing the VA will unbind all the memory */
   if (nvk_mem_arena_is_contiguous(arena))
      nvkmd_va_free(arena->contig_va);

   for (uint32_t mem_idx = 0; mem_idx < arena->mem_count; mem_idx++)
      nvkmd_mem_unref(arena->mem[mem_idx].mem);

   simple_mtx_destroy(&arena->mutex);
}

VkResult
nvk_mem_arena_grow_locked(struct nvk_device *dev, struct nvk_mem_arena *arena,
                          uint64_t *addr_out, uint64_t *new_mem_size_B_out)
{
   const uint32_t mem_count = nvk_mem_arena_mem_count(arena);
   VkResult result;

   if (mem_count >= arena->max_mem_count) {
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Arena has already hit its maximum size");
   }

   const uint64_t mem_size_B = nvk_mem_arena_mem_size_B(mem_count);

   struct nvkmd_mem *mem;
   if (nvk_mem_arena_is_mapped(arena)) {
      result = nvkmd_dev_alloc_mapped_mem(dev->nvkmd, &dev->vk.base,
                                          mem_size_B, 0, arena->mem_flags,
                                          arena->map_flags, &mem);
   } else {
      result = nvkmd_dev_alloc_mem(dev->nvkmd, &dev->vk.base,
                                   mem_size_B, 0, arena->mem_flags, &mem);
   }
   if (result != VK_SUCCESS)
      return result;

   uint64_t addr;
   if (nvk_mem_arena_is_contiguous(arena)) {
      const uint64_t mem_offset_B =
         nvk_contiguous_mem_arena_mem_offset_B(mem_count);
      result = nvkmd_va_bind_mem(arena->contig_va, &dev->vk.base,
                                 mem_offset_B, mem, 0, mem_size_B);
      if (result != VK_SUCCESS) {
         nvkmd_mem_unref(mem);
         return result;
      }
      addr = arena->contig_va->addr + mem_offset_B;
   } else {
      addr = mem->va->addr;
   }

   arena->mem[mem_count] = (struct nvk_arena_mem) {
      .mem = mem,
      .addr = addr,
   };
   if (p_atomic_xchg(&arena->mem_count, mem_count + 1) != mem_count) {
      return vk_errorf(dev, VK_ERROR_UNKNOWN,
                       "Raced in arena grow.  This is an internal driver bug "
                       "and things are now in an unknown state.");
   }

   if (addr_out != NULL)
      *addr_out = addr;
   if (new_mem_size_B_out != NULL)
      *new_mem_size_B_out = mem_size_B;

   return VK_SUCCESS;
}

static uint32_t
nvk_mem_arena_find_mem_by_addr(const struct nvk_mem_arena *arena, uint64_t addr)
{
   if (nvk_mem_arena_is_contiguous(arena)) {
      assert(addr >= arena->contig_va->addr);
      assert(addr < arena->contig_va->addr + nvk_mem_arena_size_B(arena));
      const uint64_t arena_offset_B = addr - arena->contig_va->addr;
      return nvk_contiguous_mem_arena_find_mem_by_offset(arena, arena_offset_B);
   } else {
      const uint32_t mem_count = nvk_mem_arena_mem_count(arena);

      /* Start at the top because, given a random address, there's a 50%
       * liklihood that it's in the largest mem.
       */
      for (int32_t mem_idx = mem_count - 1; mem_idx >= 0; mem_idx--) {
         const struct nvk_arena_mem *mem = &arena->mem[mem_idx];
         const uint64_t mem_size_B = nvk_mem_arena_mem_size_B(mem_idx);
         if (addr >= mem->addr && addr < mem->addr + mem_size_B)
            return mem_idx;
      }

      UNREACHABLE("Not an arena address");
   }
}

void *
nvk_mem_arena_map(const struct nvk_mem_arena *arena,
                  uint64_t addr, size_t map_range_B)
{
   uint32_t mem_idx = nvk_mem_arena_find_mem_by_addr(arena, addr);
   const struct nvk_arena_mem *mem = &arena->mem[mem_idx];

   assert(addr >= mem->addr);
   const uint64_t mem_offset_B = addr - mem->addr;

   ASSERTED const uint64_t mem_size_B = nvk_mem_arena_mem_size_B(mem_idx);
   assert(mem_offset_B + map_range_B <= mem_size_B);

   return mem->mem->map + mem_offset_B;
}

static void
nvk_mem_arena_flush_map_locked(struct nvk_device *dev,
                               struct nvk_mem_arena *arena)
{
   if (p_atomic_xchg(&arena->map_dirty, 0) == 0)
      return;

   const uint32_t mem_count = nvk_mem_arena_mem_count(arena);

   for (uint32_t mem_idx = 0; mem_idx < mem_count; mem_idx++) {
      const struct nvk_arena_mem *mem = &arena->mem[mem_idx];
      const uint64_t mem_size_B = nvk_mem_arena_mem_size_B(mem_idx);
      nvkmd_mem_sync_map_to_gpu(mem->mem, 0, mem_size_B);
   }
}

void
nvk_mem_arena_flush_map(struct nvk_device *dev,
                        struct nvk_mem_arena *arena)
{
   simple_mtx_lock(&arena->mutex);
   nvk_mem_arena_flush_map_locked(dev, arena);
   simple_mtx_unlock(&arena->mutex);
}

void
nvk_mem_arena_copy_to_gpu(struct nvk_mem_arena *arena,
                          uint64_t dst_addr, const void *src, size_t size_B)
{
   assert(nvk_mem_arena_is_mapped(arena));

   while (size_B) {
      uint32_t mem_idx = nvk_mem_arena_find_mem_by_addr(arena, dst_addr);
      const struct nvk_arena_mem *mem = &arena->mem[mem_idx];
      const uint64_t mem_size_B = nvk_mem_arena_mem_size_B(mem_idx);

      assert(dst_addr >= mem->addr);
      const uint64_t mem_offset_B = dst_addr - mem->addr;
      assert(mem_offset_B < mem_size_B);

      /* We can't copy past the end of the mem */
      const size_t copy_size_B = MIN2(size_B, mem_size_B - mem_offset_B);

      memcpy(mem->mem->map + mem_offset_B, src, copy_size_B);

      dst_addr += copy_size_B;
      src += copy_size_B;
      size_B -= copy_size_B;
   }

   nvk_mem_arena_set_map_dirty(arena);
}
