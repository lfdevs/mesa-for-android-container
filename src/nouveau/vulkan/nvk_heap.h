/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_HEAP_H
#define NVK_HEAP_H 1

#include "nvk_private.h"

#include "util/simple_mtx.h"
#include "util/vma.h"
#include "nvkmd/nvkmd.h"

struct nvk_device;

#define NVK_HEAP_MIN_SIZE_LOG2 16
#define NVK_HEAP_MAX_SIZE_LOG2 32
#define NVK_HEAP_MIN_SIZE (1ull << NVK_HEAP_MIN_SIZE_LOG2)
#define NVK_HEAP_MAX_SIZE (1ull << NVK_HEAP_MAX_SIZE_LOG2)
#define NVK_HEAP_MAX_BO_COUNT (NVK_HEAP_MAX_SIZE_LOG2 - \
                               NVK_HEAP_MIN_SIZE_LOG2 + 1)

struct nvk_heap_mem {
   struct nvkmd_mem *mem;
   uint64_t addr;
};

struct nvk_heap {
   enum nvkmd_mem_flags mem_flags;
   enum nvkmd_mem_map_flags map_flags;
   uint32_t overalloc;

   simple_mtx_t mutex;
   struct util_vma_heap heap;

   /* VA for contiguous heaps, NULL otherwise */
   struct nvkmd_va *contig_va;

   uint64_t total_size;

   uint32_t mem_count;
   struct nvk_heap_mem mem[NVK_HEAP_MAX_BO_COUNT];
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

static inline uint64_t
nvk_heap_contiguous_base_address(struct nvk_heap *heap)
{
   assert(heap->contig_va != NULL);
   return heap->contig_va->addr;
}

#endif /* define NVK_HEAP_H */
