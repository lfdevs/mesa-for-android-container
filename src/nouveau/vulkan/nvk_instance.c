/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_instance.h"

#include "nvk_entrypoints.h"
#include "nvk_physical_device.h"

#include "vulkan/wsi/wsi_common.h"

#include "util/build_id.h"
#include "util/mesa-sha1.h"

VKAPI_ATTR VkResult VKAPI_CALL
nvk_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   *pApiVersion = VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION);
   return VK_SUCCESS;
}

/* vk_icd.h does not declare this function, so we declare it here to
 * suppress Wmissing-prototypes.
 */
PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion)
{
   /* For the full details on loader interface versioning, see
    * <https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/loader/LoaderAndLayerInterface.md>.
    * What follows is a condensed summary, to help you navigate the large and
    * confusing official doc.
    *
    *   - Loader interface v0 is incompatible with later versions. We don't
    *     support it.
    *
    *   - In loader interface v1:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdGetInstanceProcAddr(). The ICD must statically expose this
    *         entrypoint.
    *       - The ICD must statically expose no other Vulkan symbol unless it is
    *         linked with -Bsymbolic.
    *       - Each dispatchable Vulkan handle created by the ICD must be
    *         a pointer to a struct whose first member is VK_LOADER_DATA. The
    *         ICD must initialize VK_LOADER_DATA.loadMagic to ICD_LOADER_MAGIC.
    *       - The loader implements vkCreate{PLATFORM}SurfaceKHR() and
    *         vkDestroySurfaceKHR(). The ICD must be capable of working with
    *         such loader-managed surfaces.
    *
    *    - Loader interface v2 differs from v1 in:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdNegotiateLoaderICDInterfaceVersion(). The ICD must
    *         statically expose this entrypoint.
    *
    *    - Loader interface v3 differs from v2 in:
    *        - The ICD must implement vkCreate{PLATFORM}SurfaceKHR(),
    *          vkDestroySurfaceKHR(), and other API which uses VKSurfaceKHR,
    *          because the loader no longer does so.
    *
    *    - Loader interface v4 differs from v3 in:
    *        - The ICD must implement vk_icdGetPhysicalDeviceProcAddr().
    *
    *    - Loader interface v5 differs from v4 in:
    *        - The ICD must support Vulkan API version 1.1 and must not return
    *          VK_ERROR_INCOMPATIBLE_DRIVER from vkCreateInstance() unless a
    *          Vulkan Loader with interface v4 or smaller is being used and the
    *          application provides an API version that is greater than 1.0.
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 4u);
   return VK_SUCCESS;
}

static const struct vk_instance_extension_table instance_extensions = {
#ifdef NVK_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2 = true,
   .KHR_surface = true,
   .KHR_surface_protected_capabilities = true,
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
   .KHR_device_group_creation = true,
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
   .EXT_debug_report = true,
   .EXT_debug_utils = true,
};

VKAPI_ATTR VkResult VKAPI_CALL
nvk_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                         uint32_t *pPropertyCount,
                                         VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &instance_extensions, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
nvk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkInstance *pInstance)
{
   struct nvk_instance *instance;
   VkResult result;

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = vk_alloc(pAllocator, sizeof(*instance), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &nvk_instance_entrypoints,
                                               true);
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &wsi_instance_entrypoints,
                                               false);

   result = vk_instance_init(&instance->vk, &instance_extensions,
                             &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   instance->vk.physical_devices.try_create_for_drm =
      nvk_create_drm_physical_device;
   instance->vk.physical_devices.destroy = nvk_physical_device_destroy;

   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(nvk_CreateInstance);
   if (!note) {
      result = vk_errorf(NULL, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to find build-id");
      goto fail_init;
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < SHA1_DIGEST_LENGTH) {
      result = vk_errorf(NULL, VK_ERROR_INITIALIZATION_FAILED,
                        "build-id too short.  It needs to be a SHA");
      goto fail_init;
   }

   STATIC_ASSERT(sizeof(instance->driver_build_sha) == SHA1_DIGEST_LENGTH);
   memcpy(instance->driver_build_sha, build_id_data(note), SHA1_DIGEST_LENGTH);

   *pInstance = nvk_instance_to_handle(instance);
   return VK_SUCCESS;

fail_init:
   vk_instance_finish(&instance->vk);
fail_alloc:
   vk_free(pAllocator, instance);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
nvk_DestroyInstance(VkInstance _instance,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(nvk_instance, instance, _instance);

   if (!instance)
      return;

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
nvk_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(nvk_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk,
                                    &nvk_instance_entrypoints,
                                    pName);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return nvk_GetInstanceProcAddr(instance, pName);
}
