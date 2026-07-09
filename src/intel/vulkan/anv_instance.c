/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_api_version.h"

static const struct debug_control debug_control[] = {
   { "bindless",                  ANV_DEBUG_BINDLESS},
   { "desc-dirty",                ANV_DEBUG_DESCRIPTOR_DIRTY},
   { "dgc-dump",                  ANV_DEBUG_DGC_DUMP},
   { "experimental",              ANV_DEBUG_EXPERIMENTAL},
   { "no-gpl",                    ANV_DEBUG_NO_GPL},
   { "no-alloc-oversubscription", ANV_DEBUG_NO_ALLOC_OVER_SUBSCRIPTION},
   { "no-slab",                   ANV_DEBUG_NO_SLAB},
   { "no-sparse",                 ANV_DEBUG_NO_SPARSE},
   { "sparse-trtt",               ANV_DEBUG_SPARSE_TRTT},
   { "video-decode",              ANV_DEBUG_VIDEO_DECODE},
   { "video-encode",              ANV_DEBUG_VIDEO_ENCODE},
   { "shader-dump",               ANV_DEBUG_SHADER_DUMP},
   { "shader-hash",               ANV_DEBUG_SHADER_HASH},
   { "shader-print",              ANV_DEBUG_SHADER_PRINT},
   { "skip-disk-cache",           ANV_DEBUG_SKIP_DISK_CACHE},
   { NULL,    0 }
};

enum anv_debug anv_debug;

static void
process_anv_debug_variable_once(void)
{
   anv_debug = parse_debug_string(os_get_option("ANV_DEBUG"), debug_control);
}

VkResult anv_EnumerateInstanceVersion(
    uint32_t*                                   pApiVersion)
{
    *pApiVersion = ANV_API_VERSION;
    return VK_SUCCESS;
}

static const struct vk_instance_extension_table instance_extensions = {
   .KHR_device_group_creation                = true,
   .KHR_external_fence_capabilities          = true,
   .KHR_external_memory_capabilities         = true,
   .KHR_external_semaphore_capabilities      = true,
   .KHR_get_physical_device_properties2      = true,
   .EXT_debug_report                         = true,
   .EXT_debug_utils                          = true,

#ifdef ANV_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2            = true,
   .KHR_surface                              = true,
   .KHR_surface_maintenance1                 = true,
   .KHR_surface_protected_capabilities       = true,
   .EXT_surface_maintenance1                 = true,
   .EXT_swapchain_colorspace                 = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface                      = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface                          = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface                         = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display                 = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display                              = true,
   .KHR_get_display_properties2              = true,
   .EXT_direct_mode_display                  = true,
   .EXT_display_surface_counter              = true,
   .EXT_acquire_drm_display                  = true,
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
   .EXT_headless_surface                     = true,
#endif
};

VkResult anv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &instance_extensions, pPropertyCount, pProperties);
}

static void
anv_drirc_shader_cb(const void *hash_data,
                    uint32_t hash_size,
                    const driOptionInfo *option,
                    const driOptionValue *value,
                    void *shaderOptionCallbackData)
{
   /* Should always be 8 bytes or more. Our compiler prog_data only holds the
    * first 64bits, so just use that for the hash table.
    */
   assert(hash_size >= 8);
   uint64_t shader_hash = ((uint64_t *)hash_data)[0];

   struct anv_instance *instance = shaderOptionCallbackData;

   if (instance->shader_workarounds == NULL)
      instance->shader_workarounds = _mesa_hash_table_u64_create(NULL);

   struct anv_shader_workaround *workaround =
      _mesa_hash_table_u64_search(instance->shader_workarounds, shader_hash);
   if (workaround == NULL) {
      workaround = vk_zalloc(&instance->vk.alloc,
                             sizeof(*workaround), 8,
                             VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      if (workaround == NULL) {
         instance->drirc_status = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
         return;
      }
      _mesa_hash_table_u64_insert(instance->shader_workarounds,
                                  shader_hash, workaround);
   }

   assert(workaround != NULL);

   if (strcmp(option->name, "force_vk_typed_barrier_after_dispatch_to_compute") == 0)
      workaround->force_typed_barrier_after_dispatch_to_compute = true;
   else if (strcmp(option->name, "force_vk_untyped_barrier_after_dispatch_to_compute") == 0)
      workaround->force_untyped_barrier_after_dispatch_to_compute = true;
   else if (strcmp(option->name, "force_vk_typed_barrier_after_dispatch_to_top") == 0)
      workaround->force_typed_barrier_after_dispatch_to_top = true;
   else if (strcmp(option->name, "force_vk_untyped_barrier_after_dispatch_to_top") == 0)
      workaround->force_untyped_barrier_after_dispatch_to_top = true;
   else
      UNREACHABLE("invalid shader option");
}

static VkResult
anv_init_dri_options(struct anv_instance *instance)
{
   instance->drirc_status = VK_SUCCESS;

   anv_parse_dri_options(&instance->drirc,
                         &(driConfigFileParseParams) {
                            .driverName = "anv",
                            .applicationName = instance->vk.app_info.app_name,
                            .applicationVersion = instance->vk.app_info.app_version,
                            .engineName = instance->vk.app_info.engine_name,
                            .engineVersion = instance->vk.app_info.engine_version,
                            .shaderOptionCallback = anv_drirc_shader_cb,
                            .shaderOptionCallbackData = instance,
                         });

   if (instance->drirc_status != VK_SUCCESS)
      return instance->drirc_status;

    if (instance->vk.app_info.engine_name &&
        !strcmp(instance->vk.app_info.engine_name, "DXVK")) {
       /* Since 2.3.1+, DXVK uses the application version to signal D3D9. */
       const bool is_d3d9 = instance->vk.app_info.app_version & 0x1;

       /* This driconf bit enables D3D10+ behaviour for texture coordinate
        * rounding. As D3D9 wants the Vulkan behaviour instead, apply the
        * workaround only to D3D10+.
        */
       instance->drirc.debug.force_filter_addr_rounding &= !is_d3d9;
    }

    switch (instance->drirc.perf.stack_ids) {
    case 256:
    case 512:
    case 1024:
    case 2048:
       break;
    default:
       mesa_logw("Invalid value provided for drirc anv_stack_id=%u, reverting to 512.",
                 instance->drirc.perf.stack_ids);
       instance->drirc.perf.stack_ids = 512;
       break;
    }

   switch(instance->drirc.perf.rt_dispatch_timeout) {
   case 64:
   case 128:
   case 192:
   case 256:
   case 384:
   case 512:
   case 640:
   case 768:
   case 896:
   case 1024:
   case 1152:
   case 1280:
   case 1408:
   case 1536:
   case 1664:
   case 1792:
   case 1920:
   case 2048:
   case 4096:
      break;
   default:
       mesa_logw("Invalid value provided for drirc anv_rt_dispatch_timeout=%u, reverting to 512.",
                 instance->drirc.perf.rt_dispatch_timeout);
       instance->drirc.perf.rt_dispatch_timeout = 512;
       break;
   }

   return VK_SUCCESS;
}

VkResult anv_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
   struct anv_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = vk_zalloc(pAllocator, sizeof(*instance), 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &anv_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk, &instance_extensions,
                             &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(NULL, result);
   }

   result = anv_init_dri_options(instance);
   if (result != VK_SUCCESS)
      goto fail_init;

   instance->vk.physical_devices.try_create_for_drm = anv_physical_device_try_create;
   instance->vk.physical_devices.destroy = anv_physical_device_destroy;

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   static once_flag process_anv_debug_variable_flag = ONCE_FLAG_INIT;
   call_once(&process_anv_debug_variable_flag,
             process_anv_debug_variable_once);

   process_intel_debug_variable();
   instance->vk.enable_debug_logging = INTEL_DEBUG(DEBUG_PERF);

   intel_driver_ds_init();

   *pInstance = anv_instance_to_handle(instance);

   return VK_SUCCESS;

 fail_init:
   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);

   return result;
}

void anv_DestroyInstance(
    VkInstance                                  _instance,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   if (!instance)
      return;

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   driDestroyOptionCache(&instance->drirc.options);
   driDestroyOptionInfo(&instance->drirc.available_options);

   if (instance->shader_workarounds) {
      hash_table_u64_foreach(instance->shader_workarounds, entry)
         vk_free(&instance->vk.alloc, entry.data);
      _mesa_hash_table_u64_destroy(instance->shader_workarounds);
   }

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

PFN_vkVoidFunction anv_GetInstanceProcAddr(
    VkInstance                                  _instance,
    const char*                                 pName)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   return vk_instance_get_proc_addr(instance ? &instance->vk : NULL,
                                    &anv_instance_entrypoints,
                                    pName);
}

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName)
{
   return anv_GetInstanceProcAddr(instance, pName);
}
