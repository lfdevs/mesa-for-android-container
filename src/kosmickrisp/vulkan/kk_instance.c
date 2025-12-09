/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_instance.h"

#include "kk_debug.h"
#include "kk_entrypoints.h"
#include "kk_physical_device.h"

#include "kk_dispatch_trampolines.h"

#include "vulkan/wsi/wsi_common.h"

#include "util/build_id.h"
#include "util/driconf.h"
#include "util/mesa-sha1.h"
#include "util/u_debug.h"

VKAPI_ATTR VkResult VKAPI_CALL
kk_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   uint32_t version_override = vk_get_version_override();
   *pApiVersion = version_override ? version_override
                                   : VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION);

   return VK_SUCCESS;
}

static const struct vk_instance_extension_table instance_extensions = {
#ifdef KK_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2 = true,
   .KHR_surface = true,
   .KHR_surface_maintenance1 = true,
   .KHR_surface_protected_capabilities = true,
   .EXT_surface_maintenance1 = true,
   .EXT_swapchain_colorspace = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display = true,
   .KHR_get_display_properties2 = true,
   .EXT_direct_mode_display = true,
   .EXT_display_surface_counter = true,
   .EXT_acquire_drm_display = true,
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
   .EXT_metal_surface = true,
#endif
#ifndef VK_USE_PLATFORM_METAL_EXT
   .EXT_headless_surface = true,
#endif
   .KHR_device_group_creation = true,
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
   .EXT_debug_report = true,
   .EXT_debug_utils = true,
};

VKAPI_ATTR VkResult VKAPI_CALL
kk_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                        uint32_t *pPropertyCount,
                                        VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &instance_extensions, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
kk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkInstance *pInstance)
{
   struct kk_instance *instance;
   VkResult result;

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = vk_alloc(pAllocator, sizeof(*instance), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &kk_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk, &instance_extensions,
                             &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   instance->vk.physical_devices.enumerate = kk_enumerate_physical_devices;
   instance->vk.physical_devices.destroy = kk_physical_device_destroy;

   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(kk_CreateInstance);
   if (!note) {
      result = vk_errorf(NULL, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to find build-id");
      goto fail_init;
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len != VK_UUID_SIZE) {
      result = vk_errorf(
         NULL, VK_ERROR_INITIALIZATION_FAILED,
         "build-id size does not match expected size (VK_UUID_SIZE which is 16 bytes)");
      goto fail_init;
   }

   static_assert(sizeof(instance->driver_build_sha) == VK_UUID_SIZE,
                 "UUID size mismatch");
   memcpy(instance->driver_build_sha, build_id_data(note), VK_UUID_SIZE);

   kk_process_debug_variable();

   *pInstance = kk_instance_to_handle(instance);
   return VK_SUCCESS;

fail_init:
   vk_instance_finish(&instance->vk);
fail_alloc:
   vk_free(pAllocator, instance);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
kk_DestroyInstance(VkInstance _instance,
                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(kk_instance, instance, _instance);

   if (!instance)
      return;

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

/* We need this to return our own trampoline functions */
static PFN_vkVoidFunction
kk_instance_get_proc_addr(const struct vk_instance *instance,
                          const struct vk_instance_entrypoint_table *entrypoints,
                          const char *name)
{
   PFN_vkVoidFunction func;

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (name == NULL)
      return NULL;

#define LOOKUP_VK_ENTRYPOINT(entrypoint)                                       \
   if (strcmp(name, "vk" #entrypoint) == 0)                                    \
   return (PFN_vkVoidFunction)entrypoints->entrypoint

   LOOKUP_VK_ENTRYPOINT(EnumerateInstanceExtensionProperties);
   LOOKUP_VK_ENTRYPOINT(EnumerateInstanceLayerProperties);
   LOOKUP_VK_ENTRYPOINT(EnumerateInstanceVersion);
   LOOKUP_VK_ENTRYPOINT(CreateInstance);

   /* GetInstanceProcAddr() can also be called with a NULL instance.
    * See https://gitlab.khronos.org/vulkan/vulkan/issues/2057
    */
   LOOKUP_VK_ENTRYPOINT(GetInstanceProcAddr);

#undef LOOKUP_VK_ENTRYPOINT

   /* Beginning with ICD interface v7, the following functions can also be
    * retrieved via vk_icdGetInstanceProcAddr.
    */

   if (strcmp(name, "vk_icdNegotiateLoaderICDInterfaceVersion") == 0)
      return (PFN_vkVoidFunction)vk_icdNegotiateLoaderICDInterfaceVersion;
   if (strcmp(name, "vk_icdGetPhysicalDeviceProcAddr") == 0)
      return (PFN_vkVoidFunction)vk_icdGetPhysicalDeviceProcAddr;
#ifdef _WIN32
   if (strcmp(name, "vk_icdEnumerateAdapterPhysicalDevices") == 0)
      return (PFN_vkVoidFunction)vk_icdEnumerateAdapterPhysicalDevices;
#endif

   if (instance == NULL)
      return NULL;

   func = vk_instance_dispatch_table_get_if_supported(
      &instance->dispatch_table, name, instance->app_info.api_version,
      &instance->enabled_extensions);
   if (func != NULL)
      return func;

   func = vk_physical_device_dispatch_table_get_if_supported(
      &kk_physical_device_trampolines, name, instance->app_info.api_version,
      &instance->enabled_extensions);
   if (func != NULL)
      return func;

   func = vk_device_dispatch_table_get_if_supported(
      &kk_device_trampolines, name, instance->app_info.api_version,
      &instance->enabled_extensions, NULL);
   if (func != NULL)
      return func;

   return NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
kk_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(kk_instance, instance, _instance);
   return kk_instance_get_proc_addr(&instance->vk, &kk_instance_entrypoints,
                                    pName);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return kk_GetInstanceProcAddr(instance, pName);
}
