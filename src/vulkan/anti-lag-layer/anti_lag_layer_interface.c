/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "util/simple_mtx.h"
#include "vulkan/vk_layer.h"
#include "vulkan/vulkan_core.h"
#include "anti_lag_layer.h"
#include "vk_alloc.h"
#include "vk_util.h"

static uintptr_t
object_to_key(const void *object)
{
   return (uintptr_t)*(uintptr_t *)object;
}

typedef struct instance_data {
   struct InstanceDispatchTable {
#define DECLARE_HOOK(fn) PFN_vk##fn fn
      DECLARE_HOOK(GetInstanceProcAddr);
      DECLARE_HOOK(CreateInstance);
      DECLARE_HOOK(DestroyInstance);
      DECLARE_HOOK(CreateDevice);
      DECLARE_HOOK(EnumerateDeviceExtensionProperties);
      DECLARE_HOOK(GetPhysicalDeviceFeatures2KHR);
      DECLARE_HOOK(GetPhysicalDeviceFeatures2);
      DECLARE_HOOK(GetPhysicalDeviceProperties);
      DECLARE_HOOK(GetPhysicalDeviceCalibrateableTimeDomainsEXT);
      DECLARE_HOOK(GetPhysicalDeviceCalibrateableTimeDomainsKHR);
      DECLARE_HOOK(GetPhysicalDeviceQueueFamilyProperties);
#undef DECLARE_HOOK
   } vtable;

   VkInstance instance;
   uint32_t apiVersion;
   VkAllocationCallbacks alloc;
   struct instance_data *next;
} instance_data;

static void
init_instance_vtable(instance_data *ctx, PFN_vkGetInstanceProcAddr gpa)
{
   ctx->vtable.GetInstanceProcAddr = gpa;
#define INIT_HOOK(fn) ctx->vtable.fn = (PFN_vk##fn)gpa(ctx->instance, "vk" #fn)
   INIT_HOOK(CreateInstance);
   INIT_HOOK(DestroyInstance);
   INIT_HOOK(CreateDevice);
   INIT_HOOK(EnumerateDeviceExtensionProperties);
   INIT_HOOK(GetPhysicalDeviceFeatures2KHR);
   INIT_HOOK(GetPhysicalDeviceFeatures2);
   INIT_HOOK(GetPhysicalDeviceProperties);
   INIT_HOOK(GetPhysicalDeviceCalibrateableTimeDomainsEXT);
   INIT_HOOK(GetPhysicalDeviceCalibrateableTimeDomainsKHR);
   INIT_HOOK(GetPhysicalDeviceQueueFamilyProperties);
#undef INIT_HOOK
}

static simple_mtx_t instance_mtx = SIMPLE_MTX_INITIALIZER;
static instance_data *instance_list = NULL;

static void
add_instance(instance_data *instance)
{
   simple_mtx_lock(&instance_mtx);
   instance_data **ptr = &instance_list;
   while (*ptr != NULL)
      ptr = &(*ptr)->next;
   *ptr = instance;
   simple_mtx_unlock(&instance_mtx);
}

static instance_data *
remove_instance(const void *object)
{
   uintptr_t key = object_to_key(object);
   simple_mtx_lock(&instance_mtx);
   instance_data **ptr = &instance_list;
   while (*ptr && key != object_to_key((*ptr)->instance))
      ptr = &(*ptr)->next;

   instance_data *ctx = *ptr;
   *ptr = ctx ? ctx->next : NULL;
   simple_mtx_unlock(&instance_mtx);
   return ctx;
}

static instance_data *
get_instance_data(const void *object)
{
   uintptr_t key = object_to_key(object);
   simple_mtx_lock(&instance_mtx);
   instance_data *ctx = instance_list;
   while (ctx && key != object_to_key(ctx->instance))
      ctx = ctx->next;
   simple_mtx_unlock(&instance_mtx);
   return ctx;
}

static VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
   VkLayerInstanceCreateInfo *chain_info = (VkLayerInstanceCreateInfo *)(pCreateInfo->pNext);
   while (chain_info && !(chain_info->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                          chain_info->function == VK_LAYER_LINK_INFO)) {
      chain_info = (VkLayerInstanceCreateInfo *)(chain_info->pNext);
   }

   assert(chain_info && chain_info->u.pLayerInfo);
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
      chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkCreateInstance fpCreateInstance =
      (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
   if (fpCreateInstance == NULL)
      return VK_ERROR_INITIALIZATION_FAILED;

   /* Advance the link info for the next element on the chain. */
   chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

   /* Create Instance. */
   VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
   if (result != VK_SUCCESS)
      return result;

   /* Create Instance context. */
   const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : vk_default_allocator();
   void *buf = vk_alloc(alloc, sizeof(instance_data), alignof(instance_data),
                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!buf) {
      PFN_vkDestroyInstance fpDestroyInstance =
         (PFN_vkDestroyInstance)fpGetInstanceProcAddr(*pInstance, "vkDestroyInstance");
      fpDestroyInstance(*pInstance, alloc);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   instance_data *ctx = (instance_data *)buf;
   ctx->apiVersion = pCreateInfo->pApplicationInfo && pCreateInfo->pApplicationInfo->apiVersion
                        ? pCreateInfo->pApplicationInfo->apiVersion
                        : VK_API_VERSION_1_0;
   ctx->instance = *pInstance;
   ctx->alloc = *alloc;
   ctx->next = NULL;
   init_instance_vtable(ctx, fpGetInstanceProcAddr);
   add_instance(ctx);

   return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL
anti_lag_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator)
{
   instance_data *ctx = remove_instance(instance);
   if (ctx) {
      ctx->vtable.DestroyInstance(instance, pAllocator);
      vk_free(&ctx->alloc, ctx);
   }
}

typedef struct device_data {
   VkDevice device;
   PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
   device_context *ctx; /* NULL if anti-lag ext is not enabled. */
   struct device_data *next;
} device_data;

static void
init_device_vtable(device_context *ctx, PFN_vkGetDeviceProcAddr gpa, PFN_vkSetDeviceLoaderData sld,
                   bool calibrated_timestamps_khr, bool host_query_reset_ext,
                   bool timeline_semaphore_khr)
{
   ctx->vtable.GetDeviceProcAddr = gpa;
   ctx->vtable.SetDeviceLoaderData = sld;
#define INIT_HOOK(fn) ctx->vtable.fn = (PFN_vk##fn)gpa(ctx->device, "vk" #fn)
#define INIT_HOOK_ALIAS(fn, alias, cond)                                                           \
   ctx->vtable.fn = (PFN_vk##fn)gpa(ctx->device, cond ? "vk" #alias : "vk" #fn)
   INIT_HOOK(DestroyDevice);
   INIT_HOOK(QueueSubmit);
   INIT_HOOK(QueueSubmit2);
   INIT_HOOK(QueueSubmit2KHR);
   INIT_HOOK(GetDeviceQueue);
   INIT_HOOK(CreateCommandPool);
   INIT_HOOK(DestroyCommandPool);
   INIT_HOOK(CreateQueryPool);
   INIT_HOOK_ALIAS(ResetQueryPool, ResetQueryPoolEXT, host_query_reset_ext);
   INIT_HOOK(DestroyQueryPool);
   INIT_HOOK(GetQueryPoolResults);
   INIT_HOOK(AllocateCommandBuffers);
   INIT_HOOK(FreeCommandBuffers);
   INIT_HOOK(BeginCommandBuffer);
   INIT_HOOK(EndCommandBuffer);
   INIT_HOOK_ALIAS(GetCalibratedTimestampsKHR, GetCalibratedTimestampsEXT, !calibrated_timestamps_khr);
   INIT_HOOK(CmdWriteTimestamp);
   INIT_HOOK(CreateSemaphore);
   INIT_HOOK(DestroySemaphore);
   INIT_HOOK(QueuePresentKHR);
   INIT_HOOK_ALIAS(GetSemaphoreCounterValue, GetSemaphoreCounterValueKHR, timeline_semaphore_khr);
   INIT_HOOK_ALIAS(WaitSemaphores, WaitSemaphoresKHR, timeline_semaphore_khr);
#undef INIT_HOOK
#undef INIT_HOOK_ALIAS
}

static simple_mtx_t device_mtx = SIMPLE_MTX_INITIALIZER;
static device_data *device_list = NULL;

static void
add_device(device_data *device)
{
   simple_mtx_lock(&device_mtx);
   device_data **ptr = &device_list;
   while (*ptr != NULL)
      ptr = &(*ptr)->next;
   *ptr = device;
   simple_mtx_unlock(&device_mtx);
}

static device_data *
remove_device(const void *object)
{
   uintptr_t key = object_to_key(object);
   simple_mtx_lock(&device_mtx);
   device_data **ptr = &device_list;
   while (*ptr && key != object_to_key((*ptr)->device))
      ptr = &(*ptr)->next;

   device_data *ctx = *ptr;
   *ptr = ctx ? ctx->next : NULL;
   simple_mtx_unlock(&device_mtx);
   return ctx;
}

static device_data *
get_device_data(const void *object)
{
   uintptr_t key = object_to_key(object);
   simple_mtx_lock(&device_mtx);
   device_data *ctx = device_list;
   while (ctx && key != object_to_key(ctx->device))
      ctx = ctx->next;
   simple_mtx_unlock(&device_mtx);
   return ctx;
}

device_context *
get_device_context(const void *object)
{
   device_data *data = get_device_data(object);
   assert(data && data->ctx);
   return data->ctx;
}

static VkLayerDeviceCreateInfo *
get_device_chain_info(const VkDeviceCreateInfo *pCreateInfo, VkLayerFunction func)
{
   vk_foreach_struct_const (item, pCreateInfo->pNext) {
      if (item->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
          ((VkLayerDeviceCreateInfo *)item)->function == func)
         return (VkLayerDeviceCreateInfo *)item;
   }
   return NULL;
}

static bool
should_enable_layer(instance_data *ctx, VkPhysicalDevice physicalDevice,
                    VkPhysicalDeviceAntiLagFeaturesAMD ext_feature)
{
   /* The extension is not requested by the application. */
   if (!ext_feature.antiLag)
      return false;

   /* Ensure that the underlying implementation does not expose VK_AMD_anti_lag itself. */
   ext_feature.antiLag = false;

   /* Don't clobber unrelated pNext chains passed in at device creation time. */
   ext_feature.pNext = NULL;

   VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &ext_feature,
   };

   if (ctx->vtable.GetPhysicalDeviceFeatures2KHR) {
      ctx->vtable.GetPhysicalDeviceFeatures2KHR(physicalDevice, &features);
      return !ext_feature.antiLag;
   }

   if (ctx->vtable.GetPhysicalDeviceFeatures2) {
      ctx->vtable.GetPhysicalDeviceFeatures2(physicalDevice, &features);
      return !ext_feature.antiLag;
   }

   return false;
}

static bool
check_calibrated_timestamps(instance_data *data, VkPhysicalDevice physicalDevice, bool *has_khr)
{
   VkResult res;
   uint32_t count = 0;
   res = data->vtable.EnumerateDeviceExtensionProperties(physicalDevice, NULL, &count, NULL);
   VkExtensionProperties *extensions =
      vk_alloc(&data->alloc, count * sizeof(VkExtensionProperties), alignof(VkExtensionProperties),
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!extensions)
      return false;

   res |= data->vtable.EnumerateDeviceExtensionProperties(physicalDevice, NULL, &count, extensions);

   *has_khr = false;
   bool has_ext = false;
   if (res == VK_SUCCESS) {
      for (unsigned i = 0; i < count; i++) {
         if (strcmp(extensions[i].extensionName, VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0)
            *has_khr = true;
         if (strcmp(extensions[i].extensionName, VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0)
            has_ext = true;
      }
   }

   vk_free(&data->alloc, extensions);
   return *has_khr || has_ext;
}

/* Initialize per-queue context:
 *
 * This includes creating one CommandPool and one QueryPool per Queue as well as
 * recording one CommandBuffer per timestamp query.
 */
static VkResult
init_queue_context(device_context *ctx, queue_context *queue_ctx)
{
#define CHECK_RESULT(res, label)                                                                   \
   if (res != VK_SUCCESS) {                                                                        \
      goto label;                                                                                  \
   }

   VkResult result;

   /* Create command pool */
   struct VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndex = queue_ctx->queue_family_idx,
   };
   result =
      ctx->vtable.CreateCommandPool(ctx->device, &pool_info, &ctx->alloc, &queue_ctx->cmdPool);
   CHECK_RESULT(result, fail_cmdpool)

   /* Create query pool */
   VkQueryPoolCreateInfo query_pool_info = {
      .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP,
      .queryCount = MAX_QUERIES,
   };
   result = ctx->vtable.CreateQueryPool(ctx->device, &query_pool_info, &ctx->alloc,
                                        &queue_ctx->queryPool);
   CHECK_RESULT(result, fail_querypool)
   ctx->vtable.ResetQueryPool(ctx->device, queue_ctx->queryPool, 0, MAX_QUERIES);
   ringbuffer_init(queue_ctx->queries);

   /* Create timeline semaphore */
   VkSemaphoreTypeCreateInfo timelineCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .pNext = NULL,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = 0,
   };
   VkSemaphoreCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &timelineCreateInfo,
      .flags = 0,
   };
   result =
      ctx->vtable.CreateSemaphore(ctx->device, &createInfo, &ctx->alloc, &queue_ctx->semaphore);
   CHECK_RESULT(result, fail_semaphore);

   for (unsigned j = 0; j < MAX_QUERIES; j++) {
      struct query *query = &queue_ctx->queries.data[j];

      /* Allocate commandBuffer for timestamp. */
      VkCommandBufferAllocateInfo buffer_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .commandPool = queue_ctx->cmdPool,
         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount = 1,
      };
      result = ctx->vtable.AllocateCommandBuffers(ctx->device, &buffer_info, &query->cmdbuffer);
      CHECK_RESULT(result, fail)
      result = ctx->vtable.SetDeviceLoaderData(ctx->device, query->cmdbuffer);
      CHECK_RESULT(result, fail)

      /* Record commandbuffer. */
      VkCommandBufferBeginInfo beginInfo = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      };

      result = ctx->vtable.BeginCommandBuffer(query->cmdbuffer, &beginInfo);
      CHECK_RESULT(result, fail)
      ctx->vtable.CmdWriteTimestamp(query->cmdbuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                    queue_ctx->queryPool, j);
      result = ctx->vtable.EndCommandBuffer(query->cmdbuffer);
      CHECK_RESULT(result, fail)
   }

#undef CHECK_RESULT
   return result;

fail:
   ctx->vtable.DestroySemaphore(ctx->device, queue_ctx->semaphore, &ctx->alloc);
fail_semaphore:
   ctx->vtable.DestroyQueryPool(ctx->device, queue_ctx->queryPool, &ctx->alloc);
fail_querypool:
   ctx->vtable.DestroyCommandPool(ctx->device, queue_ctx->cmdPool, &ctx->alloc);
fail_cmdpool:
   for (queue_context *qctx = ctx->queues; qctx != queue_ctx; qctx++) {
      ctx->vtable.DestroyQueryPool(ctx->device, qctx->queryPool, &ctx->alloc);
      ctx->vtable.DestroyCommandPool(ctx->device, qctx->cmdPool, &ctx->alloc);
   }

   return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   instance_data *instance_ctx = get_instance_data(physicalDevice);
   VkLayerDeviceCreateInfo *chain_info = get_device_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
   PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
   PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr =
      chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
   PFN_vkCreateDevice fpCreateDevice =
      (PFN_vkCreateDevice)fpGetInstanceProcAddr(instance_ctx->instance, "vkCreateDevice");
   if (fpCreateDevice == NULL)
      return VK_ERROR_INITIALIZATION_FAILED;

   /* Advance the link info for the next element on the chain. */
   chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

   const VkAllocationCallbacks *alloc = pAllocator ? pAllocator : &instance_ctx->alloc;
   device_data *data;
   VkResult result;

   /*  Only allocate a context and add to dispatch if the extension is enabled. */
   const VkPhysicalDeviceAntiLagFeaturesAMD *ext_features =
      vk_find_struct_const(pCreateInfo->pNext, PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD);
   bool enable = ext_features && should_enable_layer(instance_ctx, physicalDevice, *ext_features);
   if (enable) {
      /* Count queues with sufficient timestamp valid bits. */
      // TODO: make it work with less than 64 valid bits
      unsigned num_queue_families = 0;
      unsigned num_queues = 0;
      for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++)
         num_queue_families =
            MAX2(num_queue_families, pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex + 1);
      VkQueueFamilyProperties *queue_family_props =
         vk_alloc(alloc, num_queue_families * sizeof(VkQueueFamilyProperties),
                  alignof(VkQueueFamilyProperties), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!queue_family_props)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      instance_ctx->vtable.GetPhysicalDeviceQueueFamilyProperties(
         physicalDevice, &num_queue_families, queue_family_props);
      for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
         uint32_t queue_family_idx = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
         if (queue_family_props[queue_family_idx].timestampValidBits == 64 &&
             (queue_family_props[queue_family_idx].queueFlags &
              (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) {
            num_queues += pCreateInfo->pQueueCreateInfos[i].queueCount;
         }
      }

      /* Allocate the context. */
      device_context *ctx;
      queue_context *queues;
      VK_MULTIALLOC(ma);
      vk_multialloc_add(&ma, &data, device_data, 1);
      vk_multialloc_add(&ma, &ctx, struct device_context, 1);
      vk_multialloc_add(&ma, &queues, queue_context, num_queues);
      void *buf = vk_multialloc_zalloc(&ma, alloc, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!buf) {
         vk_free(alloc, queue_family_props);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      VkPhysicalDeviceProperties properties;
      instance_ctx->vtable.GetPhysicalDeviceProperties(physicalDevice, &properties);

      /* Ensure that calibrated timestamps and host query reset extensions are enabled. */
      bool has_calibrated_timestamps = false;
      bool has_calibrated_timestamps_khr = false;
      bool has_vk12 = instance_ctx->apiVersion >= VK_API_VERSION_1_2 &&
                      properties.apiVersion >= VK_API_VERSION_1_2;
      bool has_host_query_reset = has_vk12;
      bool has_host_query_reset_ext = false;
      bool has_timeline_semaphore = has_vk12;
      bool has_timeline_semaphore_khr = false;
      for (unsigned i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0)
            has_calibrated_timestamps = has_calibrated_timestamps_khr = true;
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0)
            has_calibrated_timestamps = true;
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME) == 0)
            has_host_query_reset = has_host_query_reset_ext = true;
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0)
            has_timeline_semaphore = has_timeline_semaphore_khr = true;
      }

      /* Add missing extensions. */
      VkDeviceCreateInfo create_info = *pCreateInfo;
      const char **ext_names = NULL;
      uint32_t num_extra_extensions =
         !has_calibrated_timestamps + !has_host_query_reset + !has_timeline_semaphore;
      if (num_extra_extensions) {
         ext_names = vk_alloc(
            alloc, (pCreateInfo->enabledExtensionCount + num_extra_extensions) * sizeof(char *),
            alignof(char *), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         if (!ext_names) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto fail;
         }

         memcpy(ext_names, pCreateInfo->ppEnabledExtensionNames,
                sizeof(char *) * pCreateInfo->enabledExtensionCount);

         if (!has_timeline_semaphore) {
            has_timeline_semaphore_khr = true;
            ext_names[create_info.enabledExtensionCount++] =
               VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
         }
         if (!has_host_query_reset) {
            has_host_query_reset_ext = true;
            ext_names[create_info.enabledExtensionCount++] = VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME;
         }
         if (!has_calibrated_timestamps) {
            check_calibrated_timestamps(instance_ctx, physicalDevice,
                                        &has_calibrated_timestamps_khr);
            ext_names[create_info.enabledExtensionCount++] =
               has_calibrated_timestamps_khr ? VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME
                                             : VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME;
         }
         create_info.ppEnabledExtensionNames = ext_names;
      }

      /* Ensure that hostQueryReset feature is enabled. */
      const VkPhysicalDeviceVulkan12Features *vk12 =
         vk_find_struct_const(pCreateInfo->pNext, PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
      const VkPhysicalDeviceHostQueryResetFeatures *query_reset =
         vk_find_struct_const(pCreateInfo->pNext, PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES);
      const VkPhysicalDeviceTimelineSemaphoreFeatures *timeline_semaphore =
         vk_find_struct_const(pCreateInfo->pNext, PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
      uint32_t prev_hostQueryReset;
      uint32_t prev_timelineSemaphore;
      if (vk12) {
         prev_hostQueryReset = vk12->hostQueryReset;
         prev_timelineSemaphore = vk12->timelineSemaphore;
         ((VkPhysicalDeviceVulkan12Features *)vk12)->hostQueryReset = VK_TRUE;
         ((VkPhysicalDeviceVulkan12Features *)vk12)->timelineSemaphore = VK_TRUE;
      } else {
         if (query_reset) {
            prev_hostQueryReset = query_reset->hostQueryReset;
            ((VkPhysicalDeviceHostQueryResetFeatures *)query_reset)->hostQueryReset = VK_TRUE;
         } else {
            VkPhysicalDeviceHostQueryResetFeatures *feat =
               alloca(sizeof(VkPhysicalDeviceHostQueryResetFeatures));
            *feat = (VkPhysicalDeviceHostQueryResetFeatures){
               .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
               .pNext = (void *)create_info.pNext,
               .hostQueryReset = VK_TRUE,
            };
            create_info.pNext = feat;
         }
         if (timeline_semaphore) {
            prev_timelineSemaphore = timeline_semaphore->timelineSemaphore;
            ((VkPhysicalDeviceTimelineSemaphoreFeatures *)timeline_semaphore)->timelineSemaphore =
               VK_TRUE;
         } else {
            VkPhysicalDeviceTimelineSemaphoreFeatures *feat =
               alloca(sizeof(VkPhysicalDeviceTimelineSemaphoreFeatures));
            *feat = (VkPhysicalDeviceTimelineSemaphoreFeatures){
               .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
               .pNext = (void *)create_info.pNext,
               .timelineSemaphore = VK_TRUE,
            };
            create_info.pNext = feat;
         }
      }

      /* Create Device. */
      result = fpCreateDevice(physicalDevice, &create_info, pAllocator, pDevice);

      if (vk12) {
         ((VkPhysicalDeviceVulkan12Features *)vk12)->hostQueryReset = prev_hostQueryReset;
         ((VkPhysicalDeviceVulkan12Features *)vk12)->timelineSemaphore = prev_timelineSemaphore;
      } else {
         if (query_reset)
            ((VkPhysicalDeviceHostQueryResetFeatures *)query_reset)->hostQueryReset =
               prev_hostQueryReset;
         if (timeline_semaphore)
            ((VkPhysicalDeviceTimelineSemaphoreFeatures *)timeline_semaphore)->timelineSemaphore =
               prev_timelineSemaphore;
      }
      if (ext_names)
         vk_free(alloc, ext_names);

      if (result != VK_SUCCESS)
         goto fail;

      /* Initialize Context. */
      data->ctx = ctx;
      ctx->device = *pDevice;
      chain_info = get_device_chain_info(pCreateInfo, VK_LOADER_DATA_CALLBACK);
      PFN_vkSetDeviceLoaderData fpSetDeviceLoaderData =
         (PFN_vkSetDeviceLoaderData)chain_info->u.pfnSetDeviceLoaderData;
      init_device_vtable(ctx, fpGetDeviceProcAddr, fpSetDeviceLoaderData,
                         has_calibrated_timestamps_khr, has_host_query_reset_ext,
                         has_timeline_semaphore_khr);
      simple_mtx_init(&ctx->mtx, mtx_plain);
      ctx->num_queues = num_queues;
      ctx->alloc = *alloc;
      ctx->calibration.timestamp_period = properties.limits.timestampPeriod;
      ringbuffer_init(ctx->frames);

      /* Initialize Queue contexts. */
      unsigned idx = 0;
      for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
         /* Skip queue families without sufficient timestamp valid bits.
          * Also skip queue families which cannot do GRAPHICS or COMPUTE since they
          * always heavily async in nature (DMA transfers and sparse for example).
          * Video is also irrelvant here since it should never be a critical path
          * in a game that wants anti-lag. */
         uint32_t queue_family_idx = pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex;
         if (queue_family_props[queue_family_idx].timestampValidBits != 64 ||
             !(queue_family_props[queue_family_idx].queueFlags &
               (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)))
            continue;

         for (unsigned j = 0; j < pCreateInfo->pQueueCreateInfos[i].queueCount; j++) {
            VkQueue queue;
            ctx->vtable.GetDeviceQueue(*pDevice, queue_family_idx, j, &queue);
            ctx->queues[idx].queue = queue;
            ctx->queues[idx].queue_family_idx = queue_family_idx;
            result = init_queue_context(ctx, &ctx->queues[idx]);
            idx++;
            if (result != VK_SUCCESS)
               goto fail;
         }
      }
      assert(idx == num_queues);
   fail:
      vk_free(alloc, queue_family_props);
   } else {
      data = (device_data *)vk_alloc(alloc, sizeof(device_data), alignof(device_data),
                                     VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!data)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      result = fpCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
      data->ctx = NULL;
   }

   if (result == VK_SUCCESS) {
      data->device = *pDevice;
      data->GetDeviceProcAddr = fpGetDeviceProcAddr;
      data->next = NULL;
      add_device(data);
   } else {
      vk_free(alloc, data);
   }

   return result;
}

static VKAPI_ATTR void VKAPI_CALL
anti_lag_DestroyDevice(VkDevice pDevice, const VkAllocationCallbacks *pAllocator)
{
   device_data *data = remove_device(pDevice);
   assert(data && data->ctx);
   device_context *ctx = data->ctx;

   /* Destroy per-queue context.
    * The application must ensure that no work is active on the device.
    */
   for (unsigned i = 0; i < ctx->num_queues; i++) {
      queue_context *queue_ctx = &ctx->queues[i];
      ctx->vtable.DestroyQueryPool(ctx->device, queue_ctx->queryPool, &ctx->alloc);
      ctx->vtable.DestroyCommandPool(ctx->device, queue_ctx->cmdPool, &ctx->alloc);
      ctx->vtable.DestroySemaphore(ctx->device, queue_ctx->semaphore, &ctx->alloc);
   }

   ctx->vtable.DestroyDevice(pDevice, pAllocator);
   vk_free(&ctx->alloc, data);
}

static bool
is_anti_lag_supported(VkPhysicalDevice physicalDevice)
{
   instance_data *data = get_instance_data(physicalDevice);
   VkPhysicalDeviceProperties properties;
   data->vtable.GetPhysicalDeviceProperties(physicalDevice, &properties);
   if (properties.limits.timestampPeriod == 0.0 || !properties.limits.timestampComputeAndGraphics)
      return false;

   /* Check whether calibrated timestamps are supported. */
   bool has_khr;
   if (!check_calibrated_timestamps(data, physicalDevice, &has_khr))
      return false;

   /* Check whether timeline semaphores and host query reset are supported. */
   VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
      .timelineSemaphore = VK_FALSE,
   };
   VkPhysicalDeviceHostQueryResetFeatures query_reset = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES,
      .pNext = &timeline_semaphore,
      .hostQueryReset = VK_FALSE,
   };
   VkPhysicalDeviceFeatures2 features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &query_reset,
   };
   if (data->vtable.GetPhysicalDeviceFeatures2KHR)
      data->vtable.GetPhysicalDeviceFeatures2KHR(physicalDevice, &features);
   else if (data->vtable.GetPhysicalDeviceFeatures2)
      data->vtable.GetPhysicalDeviceFeatures2(physicalDevice, &features);
   if (!timeline_semaphore.timelineSemaphore || !query_reset.hostQueryReset)
      return false;

   /* Check that DEVICE and CLOCK_MONOTONIC time domains are available. */
   VkResult res;
   uint32_t count = 0;
   PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsKHR ctd =
      has_khr ? data->vtable.GetPhysicalDeviceCalibrateableTimeDomainsKHR
              : data->vtable.GetPhysicalDeviceCalibrateableTimeDomainsEXT;
   res = ctd(physicalDevice, &count, NULL);
   VkTimeDomainKHR *time_domains = alloca(count * sizeof(VkTimeDomainKHR));
   res |= ctd(physicalDevice, &count, time_domains);
   if (res != VK_SUCCESS)
      return false;

   bool has_device_domain = false;
   bool has_host_domain = false;
   for (unsigned i = 0; i < count; i++) {
      has_device_domain |= time_domains[i] == VK_TIME_DOMAIN_DEVICE_KHR;
      has_host_domain |= time_domains[i] == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
   }

   return has_device_domain && has_host_domain;
}

static VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char *pLayerName,
                                            uint32_t *pPropertyCount,
                                            VkExtensionProperties *pProperties)
{
   instance_data *instance_data = get_instance_data(physicalDevice);

   if (pLayerName && strcmp(pLayerName, "VK_LAYER_MESA_anti_lag") == 0) {
      if (!is_anti_lag_supported(physicalDevice)) {
         *pPropertyCount = 0;
         return VK_SUCCESS;
      }

      VK_OUTARRAY_MAKE_TYPED(VkExtensionProperties, out, pProperties, pPropertyCount);
      vk_outarray_append_typed(VkExtensionProperties, &out, prop)
      {
         *prop =
            (VkExtensionProperties){VK_AMD_ANTI_LAG_EXTENSION_NAME, VK_AMD_ANTI_LAG_SPEC_VERSION};
      }
      return vk_outarray_status(&out);
   }

   return instance_data->vtable.EnumerateDeviceExtensionProperties(physicalDevice, pLayerName,
                                                                   pPropertyCount, pProperties);
}

static VKAPI_ATTR void VKAPI_CALL
anti_lag_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceFeatures2 *pFeatures)
{
   instance_data *ctx = get_instance_data(physicalDevice);
   ctx->vtable.GetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
   VkPhysicalDeviceAntiLagFeaturesAMD *anti_lag_features =
      vk_find_struct(pFeatures->pNext, PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD);

   if (anti_lag_features) {
      anti_lag_features->antiLag |= is_anti_lag_supported(physicalDevice);
   }
}

static VKAPI_ATTR void VKAPI_CALL
anti_lag_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice,
                                       VkPhysicalDeviceFeatures2 *pFeatures)
{
   instance_data *ctx = get_instance_data(physicalDevice);
   ctx->vtable.GetPhysicalDeviceFeatures2KHR(physicalDevice, pFeatures);
   VkPhysicalDeviceAntiLagFeaturesAMD *anti_lag_features =
      vk_find_struct(pFeatures->pNext, PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD);

   if (anti_lag_features) {
      anti_lag_features->antiLag |= is_anti_lag_supported(physicalDevice);
   }
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
anti_lag_GetInstanceProcAddr(VkInstance instance, const char *pName);

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
anti_lag_GetDeviceProcAddr(VkDevice device, const char *pName);

#define ADD_HOOK(fn) {"vk" #fn, (PFN_vkVoidFunction)anti_lag_##fn}
static const struct {
   const char *name;
   PFN_vkVoidFunction ptr;
} instance_funcptr_map[] = {
   ADD_HOOK(GetInstanceProcAddr),
   ADD_HOOK(CreateInstance),
   ADD_HOOK(DestroyInstance),
   ADD_HOOK(EnumerateDeviceExtensionProperties),
   ADD_HOOK(CreateDevice),
   ADD_HOOK(GetPhysicalDeviceFeatures2),
   ADD_HOOK(GetPhysicalDeviceFeatures2KHR),
};

static const struct {
   const char *name;
   PFN_vkVoidFunction ptr;
} device_funcptr_map[] = {
   ADD_HOOK(GetDeviceProcAddr),
   ADD_HOOK(DestroyDevice),
   ADD_HOOK(AntiLagUpdateAMD),
   ADD_HOOK(QueueSubmit),
   ADD_HOOK(QueueSubmit2),
   ADD_HOOK(QueueSubmit2KHR),
   ADD_HOOK(QueuePresentKHR),
};
#undef ADD_HOOK

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
anti_lag_GetInstanceProcAddr(VkInstance instance, const char *pName)
{
   if (!pName)
      return NULL;

   PFN_vkVoidFunction result = NULL;
   if (instance) {
      instance_data *ctx = get_instance_data(instance);
      if (ctx)
         result = ctx->vtable.GetInstanceProcAddr(instance, pName);
   }

   /* Only hook instance functions which are exposed by the underlying impl.
    * Ignore instance parameter for vkCreateInstance and vkCreateDevice.
    */
   if (result || strcmp(pName, "vkCreateInstance") == 0 || strcmp(pName, "vkCreateDevice") == 0) {
      for (uint32_t i = 0; i < ARRAY_SIZE(instance_funcptr_map); i++) {
         if (strcmp(pName, instance_funcptr_map[i].name) == 0)
            return instance_funcptr_map[i].ptr;
      }
   }

   return result;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
anti_lag_GetDeviceProcAddr(VkDevice device, const char *pName)
{
   if (!pName || !device)
      return NULL;

   device_data *data = get_device_data(device);
   PFN_vkVoidFunction result = data->GetDeviceProcAddr(device, pName);

   /* Only hook device functions if the Layer extension is enabled. */
   if (data->ctx && (result || strcmp(pName, "vkAntiLagUpdateAMD") == 0)) {
      for (uint32_t i = 0; i < ARRAY_SIZE(device_funcptr_map); i++) {
         if (strcmp(pName, device_funcptr_map[i].name) == 0)
            return device_funcptr_map[i].ptr;
      }
   }

   return result;
}

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
anti_lag_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct)
{
   assert(pVersionStruct != NULL);
   assert(pVersionStruct->sType == LAYER_NEGOTIATE_INTERFACE_STRUCT);

   if (pVersionStruct->loaderLayerInterfaceVersion >= 2) {
      pVersionStruct->loaderLayerInterfaceVersion = 2;
      pVersionStruct->pfnGetInstanceProcAddr = anti_lag_GetInstanceProcAddr;
      pVersionStruct->pfnGetDeviceProcAddr = anti_lag_GetDeviceProcAddr;
      pVersionStruct->pfnGetPhysicalDeviceProcAddr = NULL;
   }

   return VK_SUCCESS;
}
