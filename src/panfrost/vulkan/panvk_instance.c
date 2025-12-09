/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_device.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "util/build_id.h"
#include "util/driconf.h"
#include "util/mesa-sha1.h"
#include "util/os_misc.h"
#include "util/u_call_once.h"

#include "vk_alloc.h"
#include "vk_log.h"

#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_macros.h"
#include "panvk_physical_device.h"

#ifdef HAVE_VALGRIND
#include <memcheck.h>
#include <valgrind.h>
#define VG(x) x
#else
#define VG(x)
#endif

static const struct debug_control panvk_debug_options[] = {
   {"startup", PANVK_DEBUG_STARTUP},
   {"nir", PANVK_DEBUG_NIR},
   {"trace", PANVK_DEBUG_TRACE},
   {"sync", PANVK_DEBUG_SYNC},
   {"noafbc", PANVK_DEBUG_NO_AFBC},
   {"linear", PANVK_DEBUG_LINEAR},
   {"dump", PANVK_DEBUG_DUMP},
   {"no_known_warn", PANVK_DEBUG_NO_KNOWN_WARN},
   {"cs", PANVK_DEBUG_CS},
   {"copy_gfx", PANVK_DEBUG_COPY_GFX},
   {"force_simultaneous", PANVK_DEBUG_FORCE_SIMULTANEOUS},
   {"implicit_others_inv", PANVK_DEBUG_IMPLICIT_OTHERS_INV},
   {"force_blackhole", PANVK_DEBUG_FORCE_BLACKHOLE},
   {"wsi_afbc", PANVK_DEBUG_WSI_AFBC},
   {NULL, 0}};

uint64_t panvk_debug;

static void
panvk_debug_init_once(void)
{
   panvk_debug =
      parse_debug_string(os_get_option("PANVK_DEBUG"), panvk_debug_options);
}

static void
panvk_debug_init(void)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, panvk_debug_init_once);

   /* log per VkInstance creation */
   if (PANVK_DEBUG(STARTUP)) {
      char debug_string[256];
      dump_debug_control_string(debug_string, sizeof(debug_string),
                                panvk_debug_options, panvk_debug);
      mesa_logi("panvk_debug: %s", debug_string);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   uint32_t version_override = vk_get_version_override();
   *pApiVersion = version_override ? version_override :
      VK_MAKE_API_VERSION(0, 1, 4, VK_HEADER_VERSION);

   return VK_SUCCESS;
}

static const struct vk_instance_extension_table panvk_instance_extensions = {
   .KHR_device_group_creation = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_external_fence_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
#ifdef PANVK_USE_WSI_PLATFORM
   .KHR_surface = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display = true,
   .EXT_direct_mode_display = true,
   .EXT_display_surface_counter = true,
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
   .EXT_debug_report = true,
   .EXT_debug_utils = true,
#ifndef VK_USE_PLATFORM_WIN32_KHR
   .EXT_headless_surface = true,
#endif
};

static VkResult
panvk_physical_device_try_create(struct vk_instance *vk_instance,
                                 struct _drmDevice *drm_device,
                                 struct vk_physical_device **out)
{
   struct panvk_instance *instance =
      container_of(vk_instance, struct panvk_instance, vk);

   if (!(drm_device->available_nodes & (1 << DRM_NODE_RENDER)) ||
       drm_device->bustype != DRM_BUS_PLATFORM)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   struct panvk_physical_device *device =
      vk_zalloc(&instance->vk.alloc, sizeof(*device), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device)
      return panvk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = panvk_physical_device_init(device, instance, drm_device);
   if (result != VK_SUCCESS) {
      vk_free(&instance->vk.alloc, device);
      return result;
   }

   *out = &device->vk;
   return VK_SUCCESS;
}

static void
panvk_destroy_physical_device(struct vk_physical_device *device)
{
   panvk_physical_device_finish((struct panvk_physical_device *)device);
   vk_free(&device->instance->alloc, device);
}

static void *
panvk_kmod_zalloc(const struct pan_kmod_allocator *allocator, size_t size,
                  bool transient)
{
   const VkAllocationCallbacks *vkalloc = allocator->priv;

   void *obj = vk_zalloc(vkalloc, size, 8,
                         transient ? VK_SYSTEM_ALLOCATION_SCOPE_COMMAND
                                   : VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   /* We force errno to -ENOMEM on host allocation failures so we can properly
    * report it back as VK_ERROR_OUT_OF_HOST_MEMORY. */
   if (!obj)
      errno = -ENOMEM;

   return obj;
}

static void
panvk_kmod_free(const struct pan_kmod_allocator *allocator, void *data)
{
   const VkAllocationCallbacks *vkalloc = allocator->priv;

   return vk_free(vkalloc, data);
}

static const driOptionDescription panvk_dri_options[] = {
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_ADAPTIVE_SYNC(true)
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
      DRI_CONF_VK_X11_ENSURE_MIN_IMAGE_COUNT(false)
      DRI_CONF_VK_XWAYLAND_WAIT_READY(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_DEBUG
      DRI_CONF_FORCE_VK_VENDOR()
      DRI_CONF_VK_WSI_FORCE_SWAPCHAIN_TO_CURRENT_EXTENT(false)
      DRI_CONF_VK_X11_IGNORE_SUBOPTIMAL(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_MISCELLANEOUS
      DRI_CONF_PAN_COMPUTE_CORE_MASK(~0ull)
      DRI_CONF_PAN_FRAGMENT_CORE_MASK(~0ull)
      DRI_CONF_PAN_ENABLE_VERTEX_PIPELINE_STORES_ATOMICS(false)
      DRI_CONF_PAN_FORCE_ENABLE_SHADER_ATOMICS(false)
   DRI_CONF_SECTION_END
};

static void
panvk_init_dri_options(struct panvk_instance *instance)
{
   driParseOptionInfo(&instance->available_dri_options, panvk_dri_options, ARRAY_SIZE(panvk_dri_options));
   driParseConfigFiles(&instance->dri_options, &instance->available_dri_options, 0, "panvk", NULL, NULL,
                       instance->vk.app_info.app_name, instance->vk.app_info.app_version,
                       instance->vk.app_info.engine_name, instance->vk.app_info.engine_version);

   instance->force_vk_vendor =
      driQueryOptioni(&instance->dri_options, "force_vk_vendor");

   instance->enable_vertex_pipeline_stores_atomics = driQueryOptionb(
      &instance->dri_options, "pan_enable_vertex_pipeline_stores_atomics");
   instance->force_enable_shader_atomics = driQueryOptionb(
      &instance->dri_options, "pan_force_enable_shader_atomics");
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkInstance *pInstance)
{
   struct panvk_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   panvk_debug_init();

   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(panvk_CreateInstance);
   if (!note) {
      return panvk_errorf(NULL, VK_ERROR_INITIALIZATION_FAILED,
                          "Failed to find build-id");
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < SHA1_DIGEST_LENGTH) {
      return panvk_errorf(NULL, VK_ERROR_INITIALIZATION_FAILED,
                          "build-id too short.  It needs to be a SHA");
   }

   pAllocator = pAllocator ?: vk_default_allocator();
   instance = vk_zalloc(pAllocator, sizeof(*instance), 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return panvk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;

   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &panvk_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);
   result = vk_instance_init(&instance->vk, &panvk_instance_extensions,
                             &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return panvk_error(NULL, result);
   }

   panvk_init_dri_options(instance);

   instance->kmod.allocator = (struct pan_kmod_allocator){
      .zalloc = panvk_kmod_zalloc,
      .free = panvk_kmod_free,
      .priv = &instance->vk.alloc,
   };

   instance->vk.physical_devices.try_create_for_drm =
      panvk_physical_device_try_create;
   instance->vk.physical_devices.destroy = panvk_destroy_physical_device;

   if (PANVK_DEBUG(STARTUP))
      mesa_logi("Created an instance");

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   STATIC_ASSERT(sizeof(instance->driver_build_sha) == SHA1_DIGEST_LENGTH);
   memcpy(instance->driver_build_sha, build_id_data(note), SHA1_DIGEST_LENGTH);

   *pInstance = panvk_instance_to_handle(instance);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_DestroyInstance(VkInstance _instance,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_instance, instance, _instance);

   if (!instance)
      return;

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                       VkLayerProperties *pProperties)
{
   *pPropertyCount = 0;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                           uint32_t *pPropertyCount,
                                           VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return panvk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &panvk_instance_extensions, pPropertyCount, pProperties);
}

PFN_vkVoidFunction
panvk_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(panvk_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk, &panvk_instance_entrypoints,
                                    pName);
}

/* The loader wants us to expose a second GetInstanceProcAddr function
 * to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return panvk_GetInstanceProcAddr(instance, pName);
}
