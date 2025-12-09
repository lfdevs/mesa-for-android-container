/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_descriptor_table.h"

#include "nvk_device.h"
#include "nvk_physical_device.h"

#include <sys/mman.h>

static VkResult
nvk_descriptor_table_grow_locked(struct nvk_device *dev,
                                 struct nvk_descriptor_table *table,
                                 uint32_t new_alloc)
{
   BITSET_WORD *new_in_use;
   uint32_t *new_free_table;

   uint32_t new_arena_size_B = new_alloc * table->desc_size;
   while (nvk_mem_arena_size_B(&table->arena) < new_arena_size_B) {
      VkResult result = nvk_mem_arena_grow_locked(dev, &table->arena,
                                                  NULL, NULL);
      if (result != VK_SUCCESS)
         return result;
   }

   assert((table->alloc % BITSET_WORDBITS) == 0);
   assert((new_alloc % BITSET_WORDBITS) == 0);
   const size_t old_in_use_size = BITSET_BYTES(table->alloc);
   const size_t new_in_use_size = BITSET_BYTES(new_alloc);
   new_in_use = vk_realloc(&dev->vk.alloc, table->in_use,
                           new_in_use_size, sizeof(BITSET_WORD),
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (new_in_use == NULL) {
      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "Failed to allocate image descriptor in-use set");
   }
   memset((char *)new_in_use + old_in_use_size, 0,
          new_in_use_size - old_in_use_size);
   table->in_use = new_in_use;

   const size_t new_free_table_size = new_alloc * sizeof(uint32_t);
   new_free_table = vk_realloc(&dev->vk.alloc, table->free_table,
                               new_free_table_size, 4,
                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (new_free_table == NULL) {
      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "Failed to allocate image descriptor free table");
   }
   table->free_table = new_free_table;

   table->alloc = new_alloc;

   return VK_SUCCESS;
}

VkResult
nvk_descriptor_table_init(struct nvk_device *dev,
                          struct nvk_descriptor_table *table,
                          uint32_t descriptor_size,
                          uint32_t min_descriptor_count,
                          uint32_t max_descriptor_count)
{
   memset(table, 0, sizeof(*table));
   VkResult result;

   assert(util_is_power_of_two_nonzero(min_descriptor_count));
   assert(util_is_power_of_two_nonzero(max_descriptor_count));

   result = nvk_mem_arena_init(dev, &table->arena, NVKMD_MEM_LOCAL,
                               NVKMD_MEM_MAP_WR, true /* contiguous */,
                               max_descriptor_count * descriptor_size);
   if (result != VK_SUCCESS)
      return result;

   table->desc_size = descriptor_size;
   table->alloc = 0;
   table->max_alloc = max_descriptor_count;
   table->next_desc = 0;
   table->free_count = 0;

   result = nvk_descriptor_table_grow_locked(dev, table, min_descriptor_count);
   if (result != VK_SUCCESS) {
      nvk_descriptor_table_finish(dev, table);
      return result;
   }

   return VK_SUCCESS;
}

void
nvk_descriptor_table_finish(struct nvk_device *dev,
                            struct nvk_descriptor_table *table)
{
   nvk_mem_arena_finish(dev, &table->arena);
   vk_free(&dev->vk.alloc, table->in_use);
   vk_free(&dev->vk.alloc, table->free_table);
}

static void *
nvk_descriptor_table_map_locked(struct nvk_descriptor_table *table,
                                uint32_t index)
{
   assert(index < table->alloc);
   assert(BITSET_TEST(table->in_use, index));

   uint32_t offset_B = index * table->desc_size;
   return nvk_contiguous_mem_arena_map_offset(&table->arena, offset_B,
                                              table->desc_size);
}

static void
nvk_descriptor_table_write_locked(struct nvk_descriptor_table *table,
                                  uint32_t index,
                                  const void *desc_data, size_t desc_size)
{
   void *map = nvk_descriptor_table_map_locked(table, index);

   assert(desc_size == table->desc_size);
   memcpy(map, desc_data, table->desc_size);
   nvk_mem_arena_set_map_dirty(&table->arena);
}

static void
nvk_descriptor_table_clear_locked(struct nvk_descriptor_table *table,
                                  uint32_t index)
{
   void *map = nvk_descriptor_table_map_locked(table, index);

   memset(map, 0, table->desc_size);
   nvk_mem_arena_set_map_dirty(&table->arena);
}

static VkResult
nvk_descriptor_table_alloc_locked(struct nvk_device *dev,
                                  struct nvk_descriptor_table *table,
                                  uint32_t *index_out)
{
   VkResult result;

   while (1) {
      uint32_t index;
      if (table->free_count > 0) {
         index = table->free_table[--table->free_count];
      } else if (table->next_desc < table->alloc) {
         index = table->next_desc++;
      } else {
         if (table->next_desc >= table->max_alloc) {
            return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                             "Descriptor table not large enough");
         }

         result = nvk_descriptor_table_grow_locked(dev, table,
                                                   table->alloc * 2);
         if (result != VK_SUCCESS)
            return result;

         assert(table->next_desc < table->alloc);
         index = table->next_desc++;
      }

      if (!BITSET_TEST(table->in_use, index)) {
         BITSET_SET(table->in_use, index);
         *index_out = index;
         return VK_SUCCESS;
      }
   }
}

static VkResult
nvk_descriptor_table_take_locked(struct nvk_device *dev,
                                 struct nvk_descriptor_table *table,
                                 uint32_t index)
{
   VkResult result;

   while (index >= table->alloc) {
      result = nvk_descriptor_table_grow_locked(dev, table, table->alloc * 2);
      if (result != VK_SUCCESS)
         return result;
   }

   if (BITSET_TEST(table->in_use, index)) {
      return vk_errorf(dev, VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS,
                       "Descriptor %u is already in use", index);
   } else {
      BITSET_SET(table->in_use, index);
      return VK_SUCCESS;
   }
}

static VkResult
nvk_descriptor_table_add_locked(struct nvk_device *dev,
                                struct nvk_descriptor_table *table,
                                const void *desc_data, size_t desc_size,
                                uint32_t *index_out)
{
   VkResult result = nvk_descriptor_table_alloc_locked(dev, table, index_out);
   if (result != VK_SUCCESS)
      return result;

   nvk_descriptor_table_write_locked(table, *index_out, desc_data, desc_size);

   return VK_SUCCESS;
}


VkResult
nvk_descriptor_table_add(struct nvk_device *dev,
                         struct nvk_descriptor_table *table,
                         const void *desc_data, size_t desc_size,
                         uint32_t *index_out)
{
   simple_mtx_lock(&table->arena.mutex);
   VkResult result = nvk_descriptor_table_add_locked(dev, table, desc_data,
                                                     desc_size, index_out);
   simple_mtx_unlock(&table->arena.mutex);

   return result;
}

static VkResult
nvk_descriptor_table_insert_locked(struct nvk_device *dev,
                                   struct nvk_descriptor_table *table,
                                   uint32_t index,
                                   const void *desc_data, size_t desc_size)
{
   VkResult result = nvk_descriptor_table_take_locked(dev, table, index);
   if (result != VK_SUCCESS)
      return result;

   nvk_descriptor_table_write_locked(table, index, desc_data, desc_size);

   return result;
}

VkResult
nvk_descriptor_table_insert(struct nvk_device *dev,
                            struct nvk_descriptor_table *table,
                            uint32_t index,
                            const void *desc_data, size_t desc_size)
{
   simple_mtx_lock(&table->arena.mutex);
   VkResult result = nvk_descriptor_table_insert_locked(dev, table, index,
                                                        desc_data, desc_size);
   simple_mtx_unlock(&table->arena.mutex);

   return result;
}

static int
compar_u32(const void *_a, const void *_b)
{
   const uint32_t *a = _a, *b = _b;
   return *a - *b;
}

static void
nvk_descriptor_table_compact_free_table(struct nvk_descriptor_table *table)
{
   if (table->free_count <= 1)
      return;

   qsort(table->free_table, table->free_count,
         sizeof(*table->free_table), compar_u32);

   uint32_t j = 1;
   for (uint32_t i = 1; i < table->free_count; i++) {
      if (table->free_table[i] == table->free_table[j - 1])
         continue;

      assert(table->free_table[i] > table->free_table[j - 1]);
      table->free_table[j++] = table->free_table[i];
   }

   table->free_count = j;
}

static void
nvk_descriptor_table_remove_locked(struct nvk_device *dev,
                                   struct nvk_descriptor_table *table,
                                   uint32_t index)
{
   assert(BITSET_TEST(table->in_use, index));

   nvk_descriptor_table_clear_locked(table, index);

   /* There may be duplicate entries in the free table.  For most operations,
    * this is fine as we always consult nvk_descriptor_table::in_use when
    * allocating.  However, it does mean that there's nothing preventing our
    * free table from growing larger than the memory we allocated for it.  In
    * the unlikely event that we end up with more entries than we can fit in
    * the allocated space, compact the table to ensure that the new entry
    * we're about to add fits.
    */
   if (table->free_count >= table->alloc)
      nvk_descriptor_table_compact_free_table(table);
   assert(table->free_count < table->alloc);

   BITSET_CLEAR(table->in_use, index);
   table->free_table[table->free_count++] = index;
}

void
nvk_descriptor_table_remove(struct nvk_device *dev,
                            struct nvk_descriptor_table *table,
                            uint32_t index)
{
   simple_mtx_lock(&table->arena.mutex);
   nvk_descriptor_table_remove_locked(dev, table, index);
   simple_mtx_unlock(&table->arena.mutex);
}
