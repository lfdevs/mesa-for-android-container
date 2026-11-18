/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"
#include "panvk_mempool.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"

#include "kmod/pan_kmod.h"

#include "vk_log.h"

static bool
panvk_device_uses_kbase(const struct panvk_device *dev)
{
   return to_panvk_physical_device(dev->vk.physical)->kbase_node_path[0] !=
          '\0';
}

#ifdef HAVE_PAN_KMOD_KBASE
/* panvk_priv_mem_write only flushes WB_MMAP BOs. CSF event memory is not one,
 * and kbase leaves it CPU-cached, so a host write has to be cleaned by hand
 * or the GPU keeps seeing the stale line. */
static void
panvk_event_cache_op(const void *start, size_t size, bool invalidate)
{
#if defined(__aarch64__)
   uintptr_t ptr = (uintptr_t)start & ~(uintptr_t)63;
   uintptr_t end = ((uintptr_t)start + size + 63) & ~(uintptr_t)63;

   for (; ptr < end; ptr += 64) {
      if (invalidate)
         __asm__ volatile("dc civac, %0" : : "r"(ptr) : "memory");
      else
         __asm__ volatile("dc cvac, %0" : : "r"(ptr) : "memory");
   }
   __asm__ volatile("dsb sy" ::: "memory");
#else
   (void)start;
   (void)size;
   (void)invalidate;
   __sync_synchronize();
#endif
}
#endif

/* Push the CPU write out of the cache, then on kbase ask the kernel to
 * re-evaluate CSF event waits. The signal is a no-op on other backends
 * (cs_event_signal is NULL). SetEvent/ResetEvent may only fail with
 * OUT_OF_HOST/DEVICE_MEMORY, so a failed notification is not reported: the
 * event word is already written and the CSF scheduler re-evaluates waits on
 * its next tick or GPU event, so only the wake-up latency suffers.
 */
static void
panvk_event_publish(struct panvk_device *dev, struct panvk_event *event)
{
#ifdef HAVE_PAN_KMOD_KBASE
   if (panvk_device_uses_kbase(dev)) {
      void *cpu = panvk_priv_mem_host_addr(event->syncobjs);

      if (cpu)
         panvk_event_cache_op(
            cpu, sizeof(struct panvk_cs_sync32) * PANVK_SUBQUEUE_COUNT, false);
   }
#endif

   /* kbase_kmod logs the failure. */
   (void)pan_kmod_cs_event_signal(dev->kmod.dev);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateEvent)(VkDevice _device,
                            const VkEventCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkEvent *pEvent)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_event *event = vk_object_zalloc(
      &device->vk, pAllocator, sizeof(*event), VK_OBJECT_TYPE_EVENT);
   if (!event)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct panvk_pool_alloc_info info = {
      .size = sizeof(struct panvk_cs_sync32) * PANVK_SUBQUEUE_COUNT,
      .alignment = 64,
   };

#ifdef HAVE_PAN_KMOD_KBASE
   if (panvk_device_uses_kbase(device)) {
      struct panvk_priv_bo *sync_bo;
      VkResult result = panvk_priv_bo_create(
         device, info.size, PAN_KMOD_BO_FLAG_CSF_EVENT,
         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, &sync_bo);
      if (result != VK_SUCCESS) {
         vk_object_free(&device->vk, pAllocator, event);
         return result;
      }

      event->syncobjs = (struct panvk_priv_mem){
         .bo = (uintptr_t)sync_bo,
         .offset = 0,
      };
   } else
#endif
   {
      event->syncobjs = panvk_pool_alloc_mem(&device->mempools.rw_nc, info);
   }
   if (!panvk_priv_mem_check_alloc(event->syncobjs)) {
      vk_object_free(&device->vk, pAllocator, event);
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   panvk_priv_mem_write_array(event->syncobjs, 0, struct panvk_cs_sync32,
                              PANVK_SUBQUEUE_COUNT, sobjs) {
      memset(sobjs, 0, sizeof(struct panvk_cs_sync32) * PANVK_SUBQUEUE_COUNT);
   }
   /* Reset state. */
   panvk_event_publish(device, event);

   *pEvent = panvk_event_to_handle(event);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyEvent)(VkDevice _device, VkEvent _event,
                             const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_event, event, _event);

   if (!event)
      return;

   panvk_pool_free_mem(&event->syncobjs);

   vk_object_free(&device->vk, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(GetEventStatus)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_event, event, _event);

#ifdef HAVE_PAN_KMOD_KBASE
   /* CSF event memory is not WB_MMAP, so the readback macro would neither
    * invalidate nor see a fresh GPU write. Drop the lines first. */
   if (panvk_device_uses_kbase(device)) {
      const struct panvk_cs_sync32 *syncobjs =
         panvk_priv_mem_host_addr(event->syncobjs);
      if (!syncobjs)
         return VK_EVENT_RESET;

      panvk_event_cache_op(syncobjs,
                           sizeof(*syncobjs) * PANVK_SUBQUEUE_COUNT, true);
      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
         if (!syncobjs[i].seqno)
            return VK_EVENT_RESET;
      }
      return VK_EVENT_SET;
   }
#else
   (void)device;
#endif

   panvk_priv_mem_readback_array(event->syncobjs, 0, struct panvk_cs_sync32,
                                 PANVK_SUBQUEUE_COUNT, syncobjs) {
      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
         if (!syncobjs[i].seqno)
            return VK_EVENT_RESET;
      }
   }

   return VK_EVENT_SET;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(SetEvent)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_event, event, _event);

   panvk_priv_mem_write_array(event->syncobjs, 0, struct panvk_cs_sync32,
                              PANVK_SUBQUEUE_COUNT, syncobjs) {
      for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
         syncobjs[i].seqno = 1;
   }
   panvk_event_publish(device, event);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(ResetEvent)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_event, event, _event);

   panvk_priv_mem_write_array(event->syncobjs, 0, struct panvk_cs_sync32,
                              PANVK_SUBQUEUE_COUNT, syncobjs) {
      memset(syncobjs, 0, sizeof(*syncobjs) * PANVK_SUBQUEUE_COUNT);
   }
   panvk_event_publish(device, event);

   return VK_SUCCESS;
}
