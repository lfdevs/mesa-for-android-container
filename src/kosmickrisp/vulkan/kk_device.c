/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_device.h"

#include "kk_cmd_buffer.h"
#include "kk_entrypoints.h"
#include "kk_instance.h"
#include "kk_physical_device.h"
#include "kk_shader.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_common_entrypoints.h"

#include "vulkan/wsi/wsi_common.h"
#include "vk_pipeline_cache.h"

#include <time.h>

DERIVE_HASH_TABLE(mtl_sampler_packed);

static VkResult
kk_init_sampler_heap(struct kk_device *dev, struct kk_sampler_heap *h)
{
   h->ht = mtl_sampler_packed_table_create(NULL);
   if (!h->ht)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result = kk_query_table_init(dev, &h->table, 1024);

   if (result != VK_SUCCESS) {
      ralloc_free(h->ht);
      return result;
   }

   simple_mtx_init(&h->lock, mtx_plain);
   return VK_SUCCESS;
}

static void
kk_destroy_sampler_heap(struct kk_device *dev, struct kk_sampler_heap *h)
{
   struct hash_entry *entry = _mesa_hash_table_next_entry(h->ht, NULL);
   while (entry) {
      struct kk_rc_sampler *sampler = (struct kk_rc_sampler *)entry->data;
      mtl_release(sampler->handle);
      entry = _mesa_hash_table_next_entry(h->ht, entry);
   }
   kk_query_table_finish(dev, &h->table);
   ralloc_free(h->ht);
   simple_mtx_destroy(&h->lock);
}

static VkResult
kk_sampler_heap_add_locked(struct kk_device *dev, struct kk_sampler_heap *h,
                           struct mtl_sampler_packed desc,
                           struct kk_rc_sampler **out)
{
   struct hash_entry *ent = _mesa_hash_table_search(h->ht, &desc);
   if (ent != NULL) {
      *out = ent->data;

      assert((*out)->refcount != 0);
      (*out)->refcount++;

      return VK_SUCCESS;
   }

   struct kk_rc_sampler *rc = ralloc(h->ht, struct kk_rc_sampler);
   if (!rc)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   mtl_sampler *handle = kk_sampler_create(dev, &desc);
   uint64_t gpu_id = mtl_sampler_get_gpu_resource_id(handle);

   uint32_t index;
   VkResult result = kk_query_table_add(dev, &h->table, gpu_id, &index);
   if (result != VK_SUCCESS) {
      mtl_release(handle);
      ralloc_free(rc);
      return result;
   }

   *rc = (struct kk_rc_sampler){
      .key = desc,
      .handle = handle,
      .refcount = 1,
      .index = index,
   };

   _mesa_hash_table_insert(h->ht, &rc->key, rc);
   *out = rc;

   return VK_SUCCESS;
}

VkResult
kk_sampler_heap_add(struct kk_device *dev, struct mtl_sampler_packed desc,
                    struct kk_rc_sampler **out)
{
   struct kk_sampler_heap *h = &dev->samplers;

   simple_mtx_lock(&h->lock);
   VkResult result = kk_sampler_heap_add_locked(dev, h, desc, out);
   simple_mtx_unlock(&h->lock);

   return result;
}

static void
kk_sampler_heap_remove_locked(struct kk_device *dev, struct kk_sampler_heap *h,
                              struct kk_rc_sampler *rc)
{
   assert(rc->refcount != 0);
   rc->refcount--;

   if (rc->refcount == 0) {
      mtl_release(rc->handle);
      kk_query_table_remove(dev, &h->table, rc->index);
      _mesa_hash_table_remove_key(h->ht, &rc->key);
      ralloc_free(rc);
   }
}

void
kk_sampler_heap_remove(struct kk_device *dev, struct kk_rc_sampler *rc)
{
   struct kk_sampler_heap *h = &dev->samplers;

   simple_mtx_lock(&h->lock);
   kk_sampler_heap_remove_locked(dev, h, rc);
   simple_mtx_unlock(&h->lock);
}

static void
kk_parse_device_environment_options(struct kk_device *dev)
{
   dev->gpu_capture_enabled =
      debug_get_bool_option("MESA_KK_GPU_CAPTURE", false);
   if (dev->gpu_capture_enabled) {
      const char *capture_directory =
         debug_get_option("MESA_KK_GPU_CAPTURE_DIRECTORY", NULL);
      mtl_start_gpu_capture(dev->mtl_handle, capture_directory);
   }

   const char *list = debug_get_option("MESA_KK_DISABLE_WORKAROUNDS", "");
   const char *all_workarounds = "all";
   const size_t all_len = strlen(all_workarounds);
   for (unsigned n; n = strcspn(list, ","), *list; list += MAX2(1, n)) {
      if (n == all_len && !strncmp(list, all_workarounds, n)) {
         dev->disabled_workarounds = UINT64_MAX;
         break;
      }

      int index = atoi(list);
      dev->disabled_workarounds |= BITFIELD64_BIT(index);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateDevice(VkPhysicalDevice physicalDevice,
                const VkDeviceCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VK_FROM_HANDLE(kk_physical_device, pdev, physicalDevice);
   VkResult result = VK_ERROR_OUT_OF_HOST_MEMORY;
   struct kk_device *dev;

   dev = vk_zalloc2(&pdev->vk.instance->alloc, pAllocator, sizeof(*dev), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!dev)
      return vk_error(pdev, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Fill the dispatch table we will expose to the users */
   vk_device_dispatch_table_from_entrypoints(
      &dev->exposed_dispatch_table, &vk_cmd_enqueue_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(&dev->exposed_dispatch_table,
                                             &kk_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(&dev->exposed_dispatch_table,
                                             &wsi_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(
      &dev->exposed_dispatch_table, &vk_common_device_entrypoints, false);

   struct vk_device_dispatch_table dispatch_table;
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &kk_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &vk_common_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints, false);

   result = vk_device_init(&dev->vk, &pdev->vk, &dispatch_table, pCreateInfo,
                           pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   dev->vk.shader_ops = &kk_device_shader_ops;
   dev->mtl_handle = pdev->mtl_dev_handle;
   dev->vk.command_buffer_ops = &kk_cmd_buffer_ops;
   dev->vk.command_dispatch_table = &dev->vk.dispatch_table;

   /* We need to initialize the device residency set before any bo is created. */
   simple_mtx_init(&dev->residency_set.mutex, mtx_plain);
   dev->residency_set.handle = mtl_new_residency_set(dev->mtl_handle);
   if (dev->residency_set.handle == NULL)
      goto fail_init;

   result =
      kk_queue_init(dev, &dev->queue, &pCreateInfo->pQueueCreateInfos[0], 0);
   if (result != VK_SUCCESS)
      goto fail_vab_memory;

   result = kk_device_init_meta(dev);
   if (result != VK_SUCCESS)
      goto fail_mem_cache;

   result = kk_query_table_init(dev, &dev->occlusion_queries,
                                KK_MAX_OCCLUSION_QUERIES);
   if (result != VK_SUCCESS)
      goto fail_meta;

   result = kk_init_sampler_heap(dev, &dev->samplers);
   if (result != VK_SUCCESS)
      goto fail_query_table;

   result = kk_device_init_lib(dev);
   if (result != VK_SUCCESS)
      goto fail_sampler_heap;

   kk_parse_device_environment_options(dev);

   *pDevice = kk_device_to_handle(dev);

   return VK_SUCCESS;

fail_sampler_heap:
   kk_destroy_sampler_heap(dev, &dev->samplers);
fail_query_table:
   kk_query_table_finish(dev, &dev->occlusion_queries);
fail_meta:
   kk_device_finish_meta(dev);
fail_mem_cache:
   kk_queue_finish(dev, &dev->queue);
fail_vab_memory:
   mtl_release(dev->residency_set.handle);
   simple_mtx_destroy(&dev->residency_set.mutex);
fail_init:
   vk_device_finish(&dev->vk);
fail_alloc:
   vk_free(&dev->vk.alloc, dev);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_device, dev, _device);

   if (!dev)
      return;

   /* End capture before we start releasing resources. Otherwise, Metal capture
    * may run into issues. */
   if (dev->gpu_capture_enabled) {
      mtl_stop_gpu_capture();
   }

   /* Meta first since it may destroy Vulkan objects */
   kk_device_finish_meta(dev);
   kk_device_finish_lib(dev);
   kk_query_table_finish(dev, &dev->occlusion_queries);
   kk_destroy_sampler_heap(dev, &dev->samplers);

   /* Release the residency set last once all BOs are released. */
   mtl_release(dev->residency_set.handle);
   simple_mtx_destroy(&dev->residency_set.mutex);

   kk_queue_finish(dev, &dev->queue);
   vk_device_finish(&dev->vk);

   vk_free(&dev->vk.alloc, dev);
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_GetCalibratedTimestampsKHR(
   VkDevice _device, uint32_t timestampCount,
   const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
   uint64_t *pMaxDeviation)
{
   uint64_t max_clock_period = 0;
   uint64_t begin, end;
   int d;

#ifdef CLOCK_MONOTONIC_RAW
   begin = vk_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   begin = vk_clock_gettime(CLOCK_MONOTONIC);
#endif

   for (d = 0; d < timestampCount; d++) {
      switch (pTimestampInfos[d].timeDomain) {
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR:
         pTimestamps[d] = vk_clock_gettime(CLOCK_MONOTONIC);
         max_clock_period = MAX2(max_clock_period, 1);
         break;

#ifdef CLOCK_MONOTONIC_RAW
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR:
         pTimestamps[d] = begin;
         break;
#endif
      default:
         pTimestamps[d] = 0;
         break;
      }
   }

#ifdef CLOCK_MONOTONIC_RAW
   end = vk_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   end = vk_clock_gettime(CLOCK_MONOTONIC);
#endif

   *pMaxDeviation = vk_time_max_deviation(begin, end, max_clock_period);

   return VK_SUCCESS;
}

/* We need to implement this ourselves so we give the fake ones for vk_common_*
 * to work when executing actual commands */
static PFN_vkVoidFunction
kk_device_get_proc_addr(const struct kk_device *device, const char *name)
{
   if (device == NULL || name == NULL)
      return NULL;

   struct vk_instance *instance = device->vk.physical->instance;
   return vk_device_dispatch_table_get_if_supported(
      &device->exposed_dispatch_table, name, instance->app_info.api_version,
      &instance->enabled_extensions, &device->vk.enabled_extensions);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
kk_GetDeviceProcAddr(VkDevice _device, const char *pName)
{
   VK_FROM_HANDLE(kk_device, device, _device);
   return kk_device_get_proc_addr(device, pName);
}

void
kk_device_add_heap_to_residency_set(struct kk_device *dev, mtl_heap *heap)
{
   simple_mtx_lock(&dev->residency_set.mutex);
   mtl_residency_set_add_allocation(dev->residency_set.handle, heap);
   simple_mtx_unlock(&dev->residency_set.mutex);
}

void
kk_device_remove_heap_from_residency_set(struct kk_device *dev, mtl_heap *heap)
{
   simple_mtx_lock(&dev->residency_set.mutex);
   mtl_residency_set_remove_allocation(dev->residency_set.handle, heap);
   simple_mtx_unlock(&dev->residency_set.mutex);
}

void
kk_device_make_resources_resident(struct kk_device *dev)
{
   simple_mtx_lock(&dev->residency_set.mutex);
   mtl_residency_set_commit(dev->residency_set.handle);
   mtl_residency_set_request_residency(dev->residency_set.handle);
   simple_mtx_unlock(&dev->residency_set.mutex);
}
