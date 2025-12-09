/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"

static unsigned
embedded_sampler_key_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct anv_embedded_sampler_key));
}

static bool
embedded_sampler_key_equal(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct anv_embedded_sampler_key)) == 0;
}

static void
anv_embedded_sampler_free(struct anv_device *device,
                          struct anv_embedded_sampler *sampler)
{
   anv_state_pool_free(&device->dynamic_state_pool, sampler->sampler_state);
   anv_state_pool_free(&device->dynamic_state_pool, sampler->border_color_state);
   vk_free(&device->vk.alloc, sampler);
}

void
anv_embedded_sampler_unref(struct anv_device *device,
                           struct anv_embedded_sampler *sampler)
{
   simple_mtx_lock(&device->embedded_samplers.mutex);
   if (--sampler->ref_cnt == 0) {
      _mesa_hash_table_remove_key(device->embedded_samplers.map,
                                  &sampler->key);
      anv_embedded_sampler_free(device, sampler);
   }
   simple_mtx_unlock(&device->embedded_samplers.mutex);
}

void
anv_device_init_embedded_samplers(struct anv_device *device)
{
   simple_mtx_init(&device->embedded_samplers.mutex, mtx_plain);
   device->embedded_samplers.map =
      _mesa_hash_table_create(NULL,
                              embedded_sampler_key_hash,
                              embedded_sampler_key_equal);
}

void
anv_device_finish_embedded_samplers(struct anv_device *device)
{
   hash_table_foreach(device->embedded_samplers.map, entry) {
      anv_embedded_sampler_free(device, entry->data);
   }
   ralloc_free(device->embedded_samplers.map);
   simple_mtx_destroy(&device->embedded_samplers.mutex);
}

VkResult
anv_device_get_embedded_samplers(struct anv_device *device,
                                 struct anv_embedded_sampler **out_samplers,
                                 const struct anv_pipeline_bind_map *bind_map)
{
   VkResult result = VK_SUCCESS;

   simple_mtx_lock(&device->embedded_samplers.mutex);

   for (uint32_t i = 0; i < bind_map->embedded_sampler_count; i++) {
      struct hash_entry *entry =
         _mesa_hash_table_search(device->embedded_samplers.map,
                                 &bind_map->embedded_sampler_to_binding[i].key);
      if (entry == NULL) {
         out_samplers[i] =
            vk_zalloc(&device->vk.alloc,
                      sizeof(struct anv_embedded_sampler), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
         if (out_samplers[i] == NULL) {
            result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
            for (uint32_t j = 0; j < i; j++)
               anv_embedded_sampler_unref(device, out_samplers[i]);
            goto err;
         }

         anv_genX(device->info, emit_embedded_sampler)(
            device, out_samplers[i],
            &bind_map->embedded_sampler_to_binding[i]);
         _mesa_hash_table_insert(device->embedded_samplers.map,
                                 &out_samplers[i]->key,
                                 out_samplers[i]);
      } else {
         out_samplers[i] = anv_embedded_sampler_ref(entry->data);
      }
   }

 err:
   simple_mtx_unlock(&device->embedded_samplers.mutex);
   return result;
}
