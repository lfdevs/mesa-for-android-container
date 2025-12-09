/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_query_table.h"

#include "kk_device.h"
#include "kk_physical_device.h"

static uint32_t query_size = sizeof(uint64_t);

static VkResult
kk_query_table_grow_locked(struct kk_device *dev, struct kk_query_table *table,
                           uint32_t new_alloc)
{
   struct kk_bo *bo;
   BITSET_WORD *new_in_use;
   uint32_t *new_free_table;
   VkResult result;

   assert(new_alloc <= table->max_alloc);

   const uint32_t new_mem_size = new_alloc * query_size;
   result = kk_alloc_bo(dev, &dev->vk.base, new_mem_size, 256, &bo);
   if (result != VK_SUCCESS)
      return result;

   /* We don't allow resize */
   assert(table->bo == NULL);
   table->bo = bo;

   assert((new_alloc % BITSET_WORDBITS) == 0);
   const size_t new_in_use_size = BITSET_BYTES(new_alloc);
   new_in_use =
      vk_realloc(&dev->vk.alloc, table->in_use, new_in_use_size,
                 sizeof(BITSET_WORD), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (new_in_use == NULL) {
      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "Failed to allocate query in-use set");
   }
   memset((char *)new_in_use, 0, new_in_use_size);
   table->in_use = new_in_use;

   const size_t new_free_table_size = new_alloc * sizeof(uint32_t);
   new_free_table =
      vk_realloc(&dev->vk.alloc, table->free_table, new_free_table_size, 4,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (new_free_table == NULL) {
      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "Failed to allocate query free table");
   }
   table->free_table = new_free_table;

   return VK_SUCCESS;
}

VkResult
kk_query_table_init(struct kk_device *dev, struct kk_query_table *table,
                    uint32_t query_count)
{
   memset(table, 0, sizeof(*table));
   VkResult result;

   simple_mtx_init(&table->mutex, mtx_plain);

   assert(util_is_power_of_two_nonzero(query_count));

   table->max_alloc = query_count;
   table->next_query = 0;
   table->free_count = 0;

   result = kk_query_table_grow_locked(dev, table, query_count);
   if (result != VK_SUCCESS) {
      kk_query_table_finish(dev, table);
      return result;
   }

   return VK_SUCCESS;
}

void
kk_query_table_finish(struct kk_device *dev, struct kk_query_table *table)
{
   if (table->bo != NULL)
      kk_destroy_bo(dev, table->bo);
   vk_free(&dev->vk.alloc, table->in_use);
   vk_free(&dev->vk.alloc, table->free_table);
   simple_mtx_destroy(&table->mutex);
}

static VkResult
kk_query_table_alloc_locked(struct kk_device *dev, struct kk_query_table *table,
                            uint32_t *index_out)
{
   while (1) {
      uint32_t index;
      if (table->free_count > 0) {
         index = table->free_table[--table->free_count];
      } else if (table->next_query < table->max_alloc) {
         index = table->next_query++;
      } else {
         return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "Query table not large enough");
      }

      if (!BITSET_TEST(table->in_use, index)) {
         BITSET_SET(table->in_use, index);
         *index_out = index;
         return VK_SUCCESS;
      }
   }
}

static VkResult
kk_query_table_take_locked(struct kk_device *dev, struct kk_query_table *table,
                           uint32_t index)
{
   if (index >= table->max_alloc) {
      return vk_errorf(dev, VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS,
                       "Query %u does not exist", index);
   }

   if (BITSET_TEST(table->in_use, index)) {
      return vk_errorf(dev, VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS,
                       "Query %u is already in use", index);
   } else {
      BITSET_SET(table->in_use, index);
      return VK_SUCCESS;
   }
}

static VkResult
kk_query_table_add_locked(struct kk_device *dev, struct kk_query_table *table,
                          uint64_t value, uint32_t *index_out)
{
   VkResult result = kk_query_table_alloc_locked(dev, table, index_out);
   if (result != VK_SUCCESS)
      return result;

   uint64_t *map = (uint64_t *)table->bo->cpu + *index_out;
   *map = value;

   return VK_SUCCESS;
}

VkResult
kk_query_table_add(struct kk_device *dev, struct kk_query_table *table,
                   uint64_t value, uint32_t *index_out)
{
   simple_mtx_lock(&table->mutex);
   VkResult result = kk_query_table_add_locked(dev, table, value, index_out);
   simple_mtx_unlock(&table->mutex);

   return result;
}

static VkResult
kk_query_table_insert_locked(struct kk_device *dev,
                             struct kk_query_table *table, uint32_t index,
                             uint64_t value)
{
   VkResult result = kk_query_table_take_locked(dev, table, index);
   if (result != VK_SUCCESS)
      return result;

   uint64_t *map = (uint64_t *)table->bo->cpu + index;
   *map = value;

   return result;
}

VkResult
kk_query_table_insert(struct kk_device *dev, struct kk_query_table *table,
                      uint32_t index, uint64_t value)
{
   simple_mtx_lock(&table->mutex);
   VkResult result = kk_query_table_insert_locked(dev, table, index, value);
   simple_mtx_unlock(&table->mutex);

   return result;
}

static int
compar_u32(const void *_a, const void *_b)
{
   const uint32_t *a = _a, *b = _b;
   return *a - *b;
}

static void
kk_query_table_compact_free_table(struct kk_query_table *table)
{
   if (table->free_count <= 1)
      return;

   qsort(table->free_table, table->free_count, sizeof(*table->free_table),
         compar_u32);

   uint32_t j = 1;
   for (uint32_t i = 1; i < table->free_count; i++) {
      if (table->free_table[i] == table->free_table[j - 1])
         continue;

      assert(table->free_table[i] > table->free_table[j - 1]);
      table->free_table[j++] = table->free_table[i];
   }

   table->free_count = j;
}

void
kk_query_table_remove(struct kk_device *dev, struct kk_query_table *table,
                      uint32_t index)
{
   simple_mtx_lock(&table->mutex);

   uint64_t *map = (uint64_t *)table->bo->cpu + index;
   *map = 0u;

   assert(BITSET_TEST(table->in_use, index));

   /* There may be duplicate entries in the free table.  For most operations,
    * this is fine as we always consult kk_query_table::in_use when
    * allocating.  However, it does mean that there's nothing preventing our
    * free table from growing larger than the memory we allocated for it.  In
    * the unlikely event that we end up with more entries than we can fit in
    * the allocated space, compact the table to ensure that the new entry
    * we're about to add fits.
    */
   if (table->free_count >= table->max_alloc)
      kk_query_table_compact_free_table(table);
   assert(table->free_count < table->max_alloc);

   BITSET_CLEAR(table->in_use, index);
   table->free_table[table->free_count++] = index;

   simple_mtx_unlock(&table->mutex);
}
