/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "test_common.h"

void shader_heap_small_allocs_lo(void);
void shader_heap_small_allocs_hi(void);

static void shader_heap_small_allocs(bool high)
{
   struct anv_instance instance = {};
   struct anv_physical_device physical_device = { .instance = &instance };
   struct anv_device device = {};
   struct anv_shader_heap heap;

   test_device_info_init(&physical_device.info);
   device.vk.base.device = &device.vk;
   anv_device_set_physical(&device, &physical_device);
   device.kmd_backend = anv_kmd_backend_get(INTEL_KMD_TYPE_STUB);
   pthread_mutex_init(&device.mutex, NULL);
   anv_bo_cache_init(&device.bo_cache, &device);
   anv_shader_heap_init(&heap, &device,
                        (struct anv_va_range) {
                           .addr = 3ull * 1024 * 1024 * 1024,
                           .size = 1ull * 1024 * 1024 * 1024,
                        }, 21, 27);

   uint32_t sizes[] = {
      64,
      3 * 64,
      12 * 64,
      16 * 64,
      233 * 64,
      1025 * 64,
      6 * 4096 + 64,
      2 * 1024 * 1024,
      4 * 1024 * 1024,
      2 * 1024 * 1024 + 2048,
      16 * 1024 * 1024 + 1024,
   };
   struct anv_shader_alloc allocs[ARRAY_SIZE(sizes)];

   for (uint32_t i = 0; i < ARRAY_SIZE(sizes); i++) {
      allocs[i] = anv_shader_heap_alloc(&heap, sizes[i], 64, high, 0);
      assert(allocs[i].alloc_size != 0);
   }

   anv_shader_heap_finish(&heap);
   anv_bo_cache_finish(&device.bo_cache);
   pthread_mutex_destroy(&device.mutex);
}

void shader_heap_small_allocs_hi()
{
   shader_heap_small_allocs(true);
}

void shader_heap_small_allocs_lo()
{
   shader_heap_small_allocs(false);
}
