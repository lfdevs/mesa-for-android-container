/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_HEAP_H
#define NVK_HEAP_H 1

#include "nvk_mem_arena.h"

#include "util/vma.h"

struct nvk_device;

struct nvk_heap {
   struct nvk_mem_arena arena;
   uint32_t overalloc;
   struct util_vma_heap heap;
};

VkResult nvk_heap_init(struct nvk_device *dev, struct nvk_heap *heap,
                       enum nvkmd_mem_flags mem_flags,
                       enum nvkmd_mem_map_flags map_flags,
                       uint32_t overalloc, bool contiguous);

void nvk_heap_finish(struct nvk_device *dev, struct nvk_heap *heap);

VkResult nvk_heap_alloc(struct nvk_device *dev, struct nvk_heap *heap,
                        uint64_t size, uint32_t alignment,
                        uint64_t *addr_out, void **map_out);

VkResult nvk_heap_upload(struct nvk_device *dev, struct nvk_heap *heap,
                         const void *data, size_t size, uint32_t alignment,
                         uint64_t *addr_out);

void nvk_heap_free(struct nvk_device *dev, struct nvk_heap *heap,
                   uint64_t addr, uint64_t size);

static void
nvk_heap_flush_maps(struct nvk_device *dev, struct nvk_heap *heap)
{
   nvk_mem_arena_flush_map(dev, &heap->arena);
}

static inline uint64_t
nvk_heap_contiguous_base_address(struct nvk_heap *heap)
{
   return nvk_contiguous_mem_arena_base_address(&heap->arena);
}

#endif /* define NVK_HEAP_H */
