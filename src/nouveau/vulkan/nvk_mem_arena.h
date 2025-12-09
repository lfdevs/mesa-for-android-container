/*
 * Copyright © 2025 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_MEM_ARENA_H
#define NVK_MEM_ARENA_H

#include "nvk_private.h"

#include "util/simple_mtx.h"
#include "util/u_atomic.h"
#include "nvkmd/nvkmd.h"

struct nvk_device;

#define NVK_MEM_ARENA_MIN_SIZE_LOG2 16
#define NVK_MEM_ARENA_MAX_SIZE_LOG2 32
#define NVK_MEM_ARENA_MIN_SIZE (1ull << NVK_MEM_ARENA_MIN_SIZE_LOG2)
#define NVK_MEM_ARENA_MAX_SIZE (1ull << NVK_MEM_ARENA_MAX_SIZE_LOG2)
#define NVK_MEM_ARENA_MAX_MEM_COUNT (NVK_MEM_ARENA_MAX_SIZE_LOG2 - \
                                     NVK_MEM_ARENA_MIN_SIZE_LOG2 + 1)

static inline uint64_t
nvk_mem_arena_mem_size_B(uint32_t mem_idx)
{
   /* The first two are both NVK_MEM_ARENA_MIN_SIZE and then we double after
    * that.
    */
   return mem_idx == 0 ? NVK_MEM_ARENA_MIN_SIZE
                       : ((NVK_MEM_ARENA_MIN_SIZE >> 1) << mem_idx);
}

static inline uint64_t
nvk_contiguous_mem_arena_mem_offset_B(uint32_t mem_idx)
{
   /* The first one is at offset 0 and offset_B == size_B after that. */
   return mem_idx == 0 ? 0 : ((NVK_MEM_ARENA_MIN_SIZE >> 1) << mem_idx);
}

struct nvk_arena_mem {
   struct nvkmd_mem *mem;
   uint64_t addr;
};

/** A growable pool of GPU memory
 *
 * This data structure does not provide any special allocation or address
 * management.  It just provides the growable memory area.  Users of this
 * struct are expected to wrap it in something which provides the desired
 * allocation structure on top of it.
 */
struct nvk_mem_arena {
   enum nvkmd_mem_flags mem_flags;
   enum nvkmd_mem_map_flags map_flags;

   /** Used to lock this arena
    *
    * This lock MUST be held when calling nvk_mem_arena_grow_locked().
    */
   simple_mtx_t mutex;

   /* VA for contiguous heaps, NULL otherwise */
   struct nvkmd_va *contig_va;

   /* Maximum mem_count for this arena */
   uint32_t max_mem_count;

   /* Number of nvk_arena_mem.  This value is an atomic which is only ever
    * increased and only after the new nvk_arena_mem has been populated so
    * it's always safe to fetch it and then look at mem[i] for i < mem_count
    * without taking the lock.
    */
   uint32_t mem_count;

   /* Non-zero if any maps of this arena are dirty */
   uint32_t map_dirty;

   struct nvk_arena_mem mem[NVK_MEM_ARENA_MAX_MEM_COUNT];
};

VkResult nvk_mem_arena_init(struct nvk_device *dev,
                            struct nvk_mem_arena *arena,
                            enum nvkmd_mem_flags mem_flags,
                            enum nvkmd_mem_map_flags map_flags,
                            bool contiguous, uint64_t max_size_B);

void nvk_mem_arena_finish(struct nvk_device *dev,
                          struct nvk_mem_arena *arena);

static inline uint64_t
nvk_mem_arena_is_contiguous(const struct nvk_mem_arena *arena)
{
   return arena->contig_va != NULL;
}

static inline uint64_t
nvk_mem_arena_is_mapped(const struct nvk_mem_arena *arena)
{
   return arena->map_flags != 0;
}

/* After calling this function, it is safe to look at any arena->mem[i]
 * where i is less than the returned count.
 */
static inline uint32_t
nvk_mem_arena_mem_count(const struct nvk_mem_arena *arena)
{
   return p_atomic_read(&arena->mem_count);
}

static inline uint64_t
nvk_mem_arena_size_B(const struct nvk_mem_arena *arena)
{
   uint32_t mem_count = nvk_mem_arena_mem_count(arena);
   return nvk_contiguous_mem_arena_mem_offset_B(mem_count);
}

static inline uint64_t
nvk_contiguous_mem_arena_base_address(const struct nvk_mem_arena *arena)
{
   assert(nvk_mem_arena_is_contiguous(arena));
   return arena->contig_va->addr;
}

/** Grows the arena by doubling its size
 *
 * arena->mutex MUST be held when calling this function
 */
VkResult nvk_mem_arena_grow_locked(struct nvk_device *dev,
                                   struct nvk_mem_arena *arena,
                                   uint64_t *addr_out,
                                   uint64_t *new_mem_size_B_out);

static inline uint32_t
nvk_contiguous_mem_arena_find_mem_by_offset(const struct nvk_mem_arena *arena,
                                            uint64_t arena_offset_B)
{
   assert(nvk_mem_arena_is_contiguous(arena));

   assert((arena_offset_B >> (NVK_MEM_ARENA_MIN_SIZE_LOG2 - 1)) <= UINT32_MAX);
   const uint32_t mem_idx =
      util_logbase2(arena_offset_B >> (NVK_MEM_ARENA_MIN_SIZE_LOG2 - 1));

   assert(mem_idx < nvk_mem_arena_mem_count(arena));

   assert(arena_offset_B >= nvk_contiguous_mem_arena_mem_offset_B(mem_idx));
   assert(arena_offset_B < nvk_contiguous_mem_arena_mem_offset_B(mem_idx + 1));
   assert(nvk_contiguous_mem_arena_mem_offset_B(mem_idx + 1) ==
            nvk_contiguous_mem_arena_mem_offset_B(mem_idx) +
            nvk_mem_arena_mem_size_B(mem_idx));

   ASSERTED const struct nvk_arena_mem *mem = &arena->mem[mem_idx];
   ASSERTED uint64_t addr = arena->contig_va->addr + arena_offset_B;
   assert(addr >= mem->addr);
   assert(addr < mem->addr + nvk_mem_arena_mem_size_B(mem_idx));

   return mem_idx;
}

/** An optimized version of `nvk_mem_arena_map()` for contiguous arenas
 *
 * See `nvk_mem_arena_map()` for restrictions on the mapped pointer.  Unlike
 * `nvk_mem_arena_map()`, this takes an offset instead of an address.
 */
static inline void *
nvk_contiguous_mem_arena_map_offset(const struct nvk_mem_arena *arena,
                                    uint64_t arena_offset_B,
                                    ASSERTED size_t map_range_B)
{
   assert(nvk_mem_arena_is_mapped(arena));
   const uint32_t mem_idx =
      nvk_contiguous_mem_arena_find_mem_by_offset(arena, arena_offset_B);
   const uint64_t mem_offset_B =
      arena_offset_B - nvk_contiguous_mem_arena_mem_offset_B(mem_idx);

   ASSERTED const uint64_t mem_size_B = nvk_mem_arena_mem_size_B(mem_idx);
   assert(mem_offset_B + map_range_B <= mem_size_B);

   return arena->mem[mem_idx].mem->map + mem_offset_B;
}

/** Returns a pointer to the CPU map of the given GPU address
 *
 * While nvk_mem_arena can ensure contiguous GPU addresses if requested (see
 * nvk_mem_arena_init()), CPU addresses may not be contiguous.  However, if
 * `dst_addr` is aligned to some power of two alignment align_B and
 * `align_B <= NVK_MEM_ARENA_MIN_SIZE`, then the returned pointer will be
 * valid for at least `align_B` bytes.  For larger or unaligned allocations,
 * use nvk_mem_arena_copy_to_gpu() instead.
 */
void *nvk_mem_arena_map(const struct nvk_mem_arena *arena,
                        uint64_t addr, size_t map_range_B);

/** Mark the arena map dirty
 *
 * This should be called after writing data into the arena via a pointer
 * returned by nvk_mem_arena_map().  It must be called after the write, not
 * before, to ensure that the next call to nvk_mem_arena_flush_map() will
 * flush out the new writes.
 */
static inline void
nvk_mem_arena_set_map_dirty(struct nvk_mem_arena *arena)
{
   return p_atomic_set(&arena->map_dirty, 1);
}

void nvk_mem_arena_flush_map(struct nvk_device *dev,
                             struct nvk_mem_arena *arena);

void nvk_mem_arena_copy_to_gpu(struct nvk_mem_arena *arena,
                               uint64_t dst_addr,
                               const void *src, size_t size_B);

#endif /* NVK_MEM_ARENA_H */
