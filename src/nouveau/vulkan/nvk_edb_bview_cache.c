/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nvk_edb_bview_cache.h"

#include "nil.h"
#include "nvk_device.h"
#include "nvk_descriptor_types.h"
#include "nvk_physical_device.h"

#include "util/format/u_format.h"
#include "util/hash_table.h"

PRAGMA_DIAGNOSTIC_PUSH
PRAGMA_DIAGNOSTIC_ERROR(-Wpadded)
struct bvdesc_key {
   uint16_t format;
   uint16_t chunk : 12;
   uint16_t rgb_offset : 4;
};
PRAGMA_DIAGNOSTIC_POP
static_assert(sizeof(struct bvdesc_key) == 4, "bvdesc_key has no holes");

static uint64_t
view_size_B(enum pipe_format format)
{
   const uint8_t el_size_B = util_format_get_blocksize(format);
   if (util_is_power_of_two_nonzero(el_size_B)) {
      return 4ull << 30;
   } else {
      /* On Ampere (but not Turing or Maxwell for some reason), we're limited
       * to 3GB for RGB32 buffers.
       */
      assert(util_format_get_nr_components(format) == 3);
      return 3ull << 30;
   }
}

/* Stride in VA between views */
static uint64_t
view_stride_B(enum pipe_format format)
{
   return view_size_B(format) / 2;
}

static uint32_t
view_size_el(enum pipe_format format)
{
   /* If someone uses the last element of this chunk, then they're a max-sized
    * client view which starts at the middle of this chunk and therefore
    * should be in the next chunk.
    */
   return (view_size_B(format) / util_format_get_blocksize(format)) - 1;
}

static uint64_t
base_addr_for_chunk(struct nvk_device *dev, uint16_t chunk,
                    enum pipe_format format)
{
   return dev->nvkmd->va_start + chunk * view_stride_B(format);
}

static uint64_t
chunk_for_addr(struct nvk_device *dev, uint64_t addr, enum pipe_format format)
{
   assert(addr >= dev->nvkmd->va_start);
   return (addr - dev->nvkmd->va_start) / view_stride_B(format);
}

static VkResult
nvk_edb_bview_cache_add_bview(struct nvk_device *dev,
                              struct nvk_edb_bview_cache *cache,
                              struct bvdesc_key key)
{
   void *void_key = NULL;
   STATIC_ASSERT(sizeof(key) <= sizeof(void_key));
   memcpy(&void_key, &key, sizeof(key));

   const uint64_t base_addr =
      base_addr_for_chunk(dev, key.chunk, key.format) + key.rgb_offset;

   uint32_t size_el = view_size_el(key.format);

   const uint8_t el_size_B = util_format_get_blocksize(key.format);
   if (base_addr + (uint64_t)size_el * el_size_B > dev->nvkmd->va_end) {
      const uint64_t size_B = dev->nvkmd->va_end - base_addr;
      size_el = size_B / el_size_B;
   }

   uint32_t desc[8];
   nil_buffer_fill_tic(&nvk_device_physical(dev)->info, base_addr,
                       nil_format(key.format), size_el, &desc);

   uint32_t index;
   VkResult result = nvk_descriptor_table_add(dev, &dev->images,
                                              desc, sizeof(desc), &index);
   if (result != VK_SUCCESS)
      return result;

   _mesa_hash_table_insert(cache->cache, void_key, (void *)(uintptr_t)index);

   return VK_SUCCESS;
}

static uint32_t
nvk_edb_bview_cache_lookup_bview(struct nvk_device *dev,
                                 struct nvk_edb_bview_cache *cache,
                                 struct bvdesc_key key)
{
   void *void_key = NULL;
   STATIC_ASSERT(sizeof(key) <= sizeof(void_key));
   memcpy(&void_key, &key, sizeof(key));

   struct hash_entry *entry = _mesa_hash_table_search(cache->cache, void_key);
   if (entry != NULL) {
      return (uintptr_t)entry->data;
   } else {
      return 0;
   }
}

VkResult
nvk_edb_bview_cache_init(struct nvk_device *dev,
                         struct nvk_edb_bview_cache *cache)
{
   struct nvk_physical_device *pdev = nvk_device_physical(dev);
   VkResult result;

   cache->cache = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                          _mesa_key_pointer_equal);
   if (cache->cache == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t format = 0; format < PIPE_FORMAT_COUNT; format++) {
      if (!nil_format_supports_buffer(&pdev->info, format))
         continue;

      const uint8_t el_size_B = util_format_get_blocksize(format);

      for (uint16_t chunk = 0;; chunk++) {
         if (base_addr_for_chunk(dev, chunk, format) >= dev->nvkmd->va_end)
            break;

         assert(format <= UINT16_MAX);
         assert(chunk < (1u << 12));

         if (!util_is_power_of_two_nonzero(el_size_B)) {
            assert(util_format_get_nr_components(format) == 3);
            assert(el_size_B % 3 == 0);
            const uint8_t chan_size_B = el_size_B / 3;
            for (uint8_t chan = 0; chan < 3; chan++) {
               struct bvdesc_key key = {
                  .format = format,
                  .chunk = chunk,
                  .rgb_offset = chan * chan_size_B,
               };
               result = nvk_edb_bview_cache_add_bview(dev, cache, key);
               if (result != VK_SUCCESS)
                  goto fail;
            }
         } else {
            struct bvdesc_key key = {
               .format = format,
               .chunk = chunk,
            };
            result = nvk_edb_bview_cache_add_bview(dev, cache, key);
            if (result != VK_SUCCESS)
               goto fail;
         }
      }
   }

   return VK_SUCCESS;

fail:
   _mesa_hash_table_destroy(cache->cache, NULL);
   return result;
}

void
nvk_edb_bview_cache_finish(struct nvk_device *dev,
                           struct nvk_edb_bview_cache *cache)
{
   /* We don't bother freeing the descriptors as those will be cleaned up
    * automatically when the device is destroyed.
    */
   if (cache->cache)
      _mesa_hash_table_destroy(cache->cache, NULL);
}

struct nvk_edb_buffer_view_descriptor
nvk_edb_bview_cache_get_descriptor(struct nvk_device *dev,
                                   struct nvk_edb_bview_cache *cache,
                                   uint64_t base_addr, uint64_t size_B,
                                   enum pipe_format format)
{
   /* The actual hardware limit for buffer image/texture descriptors is 4GB
    * regardless of format.  This cache works by covering the address space
    * with 4GB buffer descriptors at 2GB offsets.  In order for this to work
    * properly, the size if the client's buffer view must be at most 2 GB.
    */
   assert(size_B <= view_stride_B(format));

   const uint8_t el_size_B = util_format_get_blocksize(format);
   const uint64_t size_el = size_B / el_size_B;

   const uint64_t chunk = chunk_for_addr(dev, base_addr, format);
   const uint64_t desc_base_addr = base_addr_for_chunk(dev, chunk, format);
   const uint32_t offset_B = base_addr - desc_base_addr;

   const uint32_t offset_el = offset_B / el_size_B;

   uint16_t rgb_offset = 0;
   if (!util_is_power_of_two_nonzero(el_size_B)) {
      assert(util_format_get_nr_components(format) == 3);
      assert(el_size_B % 3 == 0);
      rgb_offset = offset_B % el_size_B;
   } else {
      assert(offset_B % el_size_B == 0);
   }

   assert(offset_el + size_el > offset_el);
   assert(offset_el + size_el <= view_size_el(format));

   assert(format <= UINT16_MAX);
   assert(chunk < (1u << 12));
   assert(rgb_offset < (1u << 4));
   const struct bvdesc_key key = {
      .format = format,
      .chunk = chunk,
      .rgb_offset = rgb_offset,
   };
   uint32_t index = nvk_edb_bview_cache_lookup_bview(dev, cache, key);

   uint32_t oob_alpha;
   if (util_format_has_alpha(format)) {
      /* OOB reads as if it read 0 texture data so an RGBA format reads
       * (0, 0, 0, 0) out-of-bounds.
       */
      oob_alpha = 0;
   } else if (util_format_is_pure_integer(format)) {
      /* OOB reads 0 texture data but then gets extended by (0, 0, 0, 1) */
      oob_alpha = 1;
   } else {
      /* OOB reads 0 texture data but then gets extended by
       * (0.0, 0.0, 0.0, 1.0)
       */
      oob_alpha = 0x3f800000;
   }

   return (struct nvk_edb_buffer_view_descriptor) {
      .index = index,
      .offset_el = offset_el,
      .size_el = size_el,
      .oob_alpha = oob_alpha,
   };
}
