/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DESCRIPTOR_TABLE_H
#define NVK_DESCRIPTOR_TABLE_H 1

#include "nvk_private.h"

#include "util/simple_mtx.h"
#include "nvkmd/nvkmd.h"

struct nvk_device;

struct nvk_descriptor_table {
   simple_mtx_t mutex;

   uint32_t desc_size; /**< Size of a descriptor */
   uint32_t alloc; /**< Number of descriptors allocated */
   uint32_t max_alloc; /**< Maximum possible number of descriptors */
   uint32_t next_desc; /**< Next unallocated descriptor */
   uint32_t free_count; /**< Size of free_table */

   struct nvkmd_mem *mem;

   /* Stack for free descriptor elements */
   uint32_t *free_table;
};

VkResult nvk_descriptor_table_init(struct nvk_device *dev,
                                   struct nvk_descriptor_table *table,
                                   uint32_t descriptor_size,
                                   uint32_t min_descriptor_count,
                                   uint32_t max_descriptor_count);

void nvk_descriptor_table_finish(struct nvk_device *dev,
                                 struct nvk_descriptor_table *table);

VkResult nvk_descriptor_table_add(struct nvk_device *dev,
                                  struct nvk_descriptor_table *table,
                                  const void *desc_data, size_t desc_size,
                                  uint32_t *index_out);

void nvk_descriptor_table_remove(struct nvk_device *dev,
                                 struct nvk_descriptor_table *table,
                                 uint32_t index);

static inline struct nvkmd_mem *
nvk_descriptor_table_get_mem_ref(struct nvk_descriptor_table *table,
                                 uint32_t *alloc_count_out)
{
   simple_mtx_lock(&table->mutex);
   struct nvkmd_mem *mem = table->mem;
   if (mem)
      nvkmd_mem_ref(mem);
   *alloc_count_out = table->alloc;
   simple_mtx_unlock(&table->mutex);

   return mem;
}

#endif
