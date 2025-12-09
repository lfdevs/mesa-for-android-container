/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv driver which is:
 * Copyright © 2019 Raspberry Pi
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_instance.h"

#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "vk_alloc.h"
#include "vk_log.h"

#include "wsi_common.h"

#include "util/build_id.h"

#include "pvr_debug.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"
#include "pvr_wsi.h"

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
#   define PVR_USE_WSI_PLATFORM_DISPLAY true
#else
#   define PVR_USE_WSI_PLATFORM_DISPLAY false
#endif

struct pvr_drm_device_config {
   struct pvr_drm_device_info {
      const char *name;
      size_t len;
   } render;
};

#define DEF_CONFIG(render_)                                      \
   {                                                             \
      .render = { .name = render_, .len = sizeof(render_) - 1 }, \
   }

/* This is the list of supported DRM render driver configs. */
static const struct pvr_drm_device_config pvr_drm_configs[] = {
   DEF_CONFIG("mediatek,mt8173-gpu"),
   DEF_CONFIG("ti,am62-gpu"),
   DEF_CONFIG("ti,j721s2-gpu"),
};
#undef DEF_CONFIG

static const struct vk_instance_extension_table pvr_instance_extensions = {
   .KHR_device_group_creation = true,
   .KHR_display = PVR_USE_WSI_PLATFORM_DISPLAY,
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_display_properties2 = PVR_USE_WSI_PLATFORM_DISPLAY,
   .KHR_get_physical_device_properties2 = true,
   .KHR_get_surface_capabilities2 = PVR_USE_WSI_PLATFORM,
   .KHR_surface = PVR_USE_WSI_PLATFORM,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
   .EXT_debug_report = true,
   .EXT_debug_utils = true,
#ifndef VK_USE_PLATFORM_WIN32_KHR
   .EXT_headless_surface = PVR_USE_WSI_PLATFORM && false,
#endif
};

static VkResult pvr_get_drm_devices(void *const obj,
                                    drmDevicePtr *const devices,
                                    const int max_devices,
                                    int *const num_devices_out)
{
   int ret = drmGetDevices2(0, devices, max_devices);
   if (ret < 0) {
      return vk_errorf(obj,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to enumerate drm devices (errno %d: %s)",
                       -ret,
                       strerror(-ret));
   }

   if (num_devices_out)
      *num_devices_out = ret;

   return VK_SUCCESS;
}

static bool
pvr_drm_device_compatible(const struct pvr_drm_device_info *const info,
                          drmDevice *const drm_dev)
{
   char **const compatible = drm_dev->deviceinfo.platform->compatible;

   for (char **compat = compatible; *compat; compat++) {
      if (strncmp(*compat, info->name, info->len) == 0)
         return true;
   }

   return false;
}

static const struct pvr_drm_device_config *
pvr_drm_device_get_config(drmDevice *const drm_dev)
{
   for (size_t i = 0U; i < ARRAY_SIZE(pvr_drm_configs); i++) {
      if (pvr_drm_device_compatible(&pvr_drm_configs[i].render, drm_dev))
         return &pvr_drm_configs[i];
   }

   return NULL;
}

static bool pvr_drm_device_is_compatible_display(drmDevicePtr drm_dev)
{
   uint64_t has_dumb_buffer = 0;
   uint64_t prime_caps = 0;
   bool ret = false;
   int32_t fd;

   mesa_logd("Checking DRM primary node for compatibility: %s",
             drm_dev->nodes[DRM_NODE_PRIMARY]);
   fd = open(drm_dev->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      mesa_logd("Failed to open display node: %s\n",
                drm_dev->nodes[DRM_NODE_PRIMARY]);
      return ret;
   }

   /* Must support KMS */
   if (!drmIsKMS(fd)) {
      mesa_logd("DRM device does not support KMS");
      goto out;
   }

   /* Must support dumb buffers, as these are used by the PVR winsys to
    * allocate device memory for PVR_WINSYS_BO_TYPE_DISPLAY buffer objects.
    */
   if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb_buffer) ||
       !has_dumb_buffer) {
      mesa_logd("DRM device does not support dumb buffers");
      goto out;
   }

   /* Must support PRIME export (so GPU can import dumb buffers) */
   if (drmGetCap(fd, DRM_CAP_PRIME, &prime_caps)) {
      mesa_loge("Failed to query DRM_CAP_PRIME: %s", strerror(errno));
      goto out;
   }

   if (!(prime_caps & DRM_PRIME_CAP_EXPORT)) {
      mesa_logd("DRM device lacks PRIME export support (caps: 0x%" PRIx64 ")",
                prime_caps);
      goto out;
   }

   ret = true;

out:
   close(fd);
   return ret;
}

static VkResult
pvr_physical_device_enumerate(struct vk_instance *const vk_instance)
{
   struct pvr_instance *const instance =
      container_of(vk_instance, struct pvr_instance, vk);

   const struct pvr_drm_device_config *config = NULL;

   drmDevicePtr drm_display_device = NULL;
   drmDevicePtr drm_render_device = NULL;
   struct pvr_physical_device *pdevice;
   drmDevicePtr *drm_devices;
   int num_drm_devices = 0;
   VkResult result;

   result = pvr_get_drm_devices(instance, NULL, 0, &num_drm_devices);
   if (result != VK_SUCCESS)
      goto out;

   if (num_drm_devices == 0) {
      result = VK_SUCCESS;
      goto out;
   }

   drm_devices = vk_alloc(&vk_instance->alloc,
                          sizeof(*drm_devices) * num_drm_devices,
                          8,
                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!drm_devices) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto out;
   }

   result = pvr_get_drm_devices(instance, drm_devices, num_drm_devices, NULL);
   if (result != VK_SUCCESS)
      goto out_free_drm_device_ptrs;

   /* First search for our render node... */
   for (int i = 0; i < num_drm_devices; i++) {
      drmDevice *const drm_dev = drm_devices[i];

      if (drm_dev->bustype != DRM_BUS_PLATFORM)
         continue;

      if (!(drm_dev->available_nodes & BITFIELD_BIT(DRM_NODE_RENDER)))
         continue;

      config = pvr_drm_device_get_config(drm_dev);
      if (config) {
         drm_render_device = drm_dev;
         break;
      }
   }

   if (!config) {
      result = VK_SUCCESS;
      goto out_free_drm_devices;
   }

   mesa_logd("Found compatible render device '%s'.",
             drm_render_device->nodes[DRM_NODE_RENDER]);

   /* ...then find a compatible display node, if available. */
   for (int i = 0; i < num_drm_devices; i++) {
      drmDevice *const drm_dev = drm_devices[i];

      if (drm_dev->bustype != DRM_BUS_PLATFORM)
         continue;

      if (!(drm_dev->available_nodes & BITFIELD_BIT(DRM_NODE_PRIMARY)))
         continue;

      if (!pvr_drm_device_is_compatible_display(drm_devices[i]))
         continue;

      drm_display_device = drm_dev;
      mesa_logd("Found a compatible display device: '%s'.",
                drm_display_device->nodes[DRM_NODE_PRIMARY]);
      break;
   }

   pdevice = vk_alloc(&vk_instance->alloc,
                      sizeof(*pdevice),
                      8,
                      VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!pdevice) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto out_free_drm_devices;
   }

   result = pvr_physical_device_init(pdevice,
                                     instance,
                                     drm_render_device,
                                     drm_display_device);
   if (result != VK_SUCCESS) {
      if (result == VK_ERROR_INCOMPATIBLE_DRIVER)
         result = VK_SUCCESS;

      goto err_free_pdevice;
   }

   if (PVR_IS_DEBUG_SET(INFO)) {
      pvr_physical_device_dump_info(
         pdevice,
         drm_display_device
            ? drm_display_device->deviceinfo.platform->compatible
            : NULL,
         drm_render_device->deviceinfo.platform->compatible);
   }

   list_add(&pdevice->vk.link, &vk_instance->physical_devices.list);

   result = VK_SUCCESS;
   goto out_free_drm_devices;

err_free_pdevice:
   vk_free(&vk_instance->alloc, pdevice);

out_free_drm_devices:
   drmFreeDevices(drm_devices, num_drm_devices);

out_free_drm_device_ptrs:
   vk_free(&vk_instance->alloc, drm_devices);

out:
   return result;
}

static bool
pvr_get_driver_build_sha(uint8_t sha_out[const static SHA1_DIGEST_LENGTH])
{
   const struct build_id_note *note;
   unsigned build_id_len;

   note = build_id_find_nhdr_for_addr(pvr_get_driver_build_sha);
   if (!note) {
      mesa_loge("Failed to find build-id.");
      return false;
   }

   build_id_len = build_id_length(note);
   if (build_id_len < SHA1_DIGEST_LENGTH) {
      mesa_loge("Build-id too short. It needs to be a SHA.");
      return false;
   }

   memcpy(sha_out, build_id_data(note), SHA1_DIGEST_LENGTH);

   return true;
}

VkResult pvr_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkInstance *pInstance)
{
   struct vk_instance_dispatch_table dispatch_table;
   struct pvr_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   if (!pAllocator)
      pAllocator = vk_default_allocator();

   instance = vk_alloc(pAllocator,
                       sizeof(*instance),
                       8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &pvr_instance_entrypoints,
                                               true);

   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &wsi_instance_entrypoints,
                                               false);

   result = vk_instance_init(&instance->vk,
                             &pvr_instance_extensions,
                             &dispatch_table,
                             pCreateInfo,
                             pAllocator);
   if (result != VK_SUCCESS)
      goto err_free_instance;

   pvr_process_debug_variable();

   instance->active_device_count = 0;

   instance->vk.physical_devices.enumerate = pvr_physical_device_enumerate;
   instance->vk.physical_devices.destroy = pvr_physical_device_destroy;

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   if (!pvr_get_driver_build_sha(instance->driver_build_sha)) {
      result = vk_errorf(NULL,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to get driver build sha.");
      goto err_free_instance;
   }

   *pInstance = pvr_instance_to_handle(instance);

   return VK_SUCCESS;

err_free_instance:
   vk_free(pAllocator, instance);
   return result;
}

void pvr_DestroyInstance(VkInstance _instance,
                         const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_instance, instance, _instance);

   if (!instance)
      return;

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

VkResult pvr_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   *pApiVersion = VK_MAKE_API_VERSION(0, 1, 4, VK_HEADER_VERSION);
   return VK_SUCCESS;
}

VkResult
pvr_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                         uint32_t *pPropertyCount,
                                         VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(&pvr_instance_extensions,
                                                     pPropertyCount,
                                                     pProperties);
}

PFN_vkVoidFunction pvr_GetInstanceProcAddr(VkInstance _instance,
                                           const char *pName)
{
   const struct vk_instance *vk_instance = NULL;

   if (_instance != NULL) {
      VK_FROM_HANDLE(pvr_instance, instance, _instance);
      vk_instance = &instance->vk;
   }

   return vk_instance_get_proc_addr(vk_instance,
                                    &pvr_instance_entrypoints,
                                    pName);
}

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in
 * apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return pvr_GetInstanceProcAddr(instance, pName);
}

VkResult pvr_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                              VkLayerProperties *pProperties)
{
   if (!pProperties) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}
