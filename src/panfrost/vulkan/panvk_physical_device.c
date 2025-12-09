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

#include <sys/stat.h>
#include <sys/sysinfo.h>

#include "util/disk_cache.h"
#include "util/os_misc.h"
#include "git_sha1.h"

#include "vk_android.h"
#include "vk_device.h"
#include "vk_drm_syncobj.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"

#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"
#include "panvk_wsi.h"

#include "pan_afbc.h"
#include "pan_props.h"

#include "genxml/gen_macros.h"

#define PER_ARCH_FUNCS(_ver)                                                   \
   void panvk_v##_ver##_get_physical_device_extensions(                        \
      const struct panvk_physical_device *device,                              \
      struct vk_device_extension_table *ext);                                  \
                                                                               \
   void panvk_v##_ver##_get_physical_device_features(                          \
      const struct panvk_instance *instance,                                   \
      const struct panvk_physical_device *device,                              \
      struct vk_features *features);                                           \
                                                                               \
   void panvk_v##_ver##_get_physical_device_properties(                        \
      const struct panvk_instance *instance,                                   \
      const struct panvk_physical_device *device,                              \
      struct vk_properties *properties);                                       \
                                                                               \
   VkResult panvk_v##_ver##_create_device(                                     \
      struct panvk_physical_device *physical_device,                           \
      const VkDeviceCreateInfo *pCreateInfo,                                   \
      const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);             \
                                                                               \
   void panvk_v##_ver##_destroy_device(                                        \
      struct panvk_device *device, const VkAllocationCallbacks *pAllocator)

PER_ARCH_FUNCS(6);
PER_ARCH_FUNCS(7);
PER_ARCH_FUNCS(10);
PER_ARCH_FUNCS(12);
PER_ARCH_FUNCS(13);

static VkResult
create_kmod_dev(struct panvk_physical_device *device,
                const struct panvk_instance *instance, drmDevicePtr drm_device)
{
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   drmVersionPtr version;
   int fd;

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "failed to open device %s", path);
   }

   version = drmGetVersion(fd);
   if (!version) {
      close(fd);
      return panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "failed to query kernel driver version for device %s",
                          path);
   }

   if (strcmp(version->name, "panfrost") && strcmp(version->name, "panthor")) {
      drmFreeVersion(version);
      close(fd);
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }

   drmFreeVersion(version);

   if (PANVK_DEBUG(STARTUP))
      mesa_logi("Found compatible device '%s'.", path);

   device->kmod.dev = pan_kmod_dev_create(fd, PAN_KMOD_DEV_FLAG_OWNS_FD,
                                          &instance->kmod.allocator);

   if (!device->kmod.dev) {
      close(fd);
      return panvk_errorf(instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "cannot create device");
   }

   return VK_SUCCESS;
}

static VkResult
get_drm_device_ids(struct panvk_physical_device *device,
                   const struct panvk_instance *instance,
                   drmDevicePtr drm_device)
{
   struct stat st;

   if (stat(drm_device->nodes[DRM_NODE_RENDER], &st)) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "failed to query render node stat");
   }

   device->drm.render_rdev = st.st_rdev;

   if (drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) {
      if (stat(drm_device->nodes[DRM_NODE_PRIMARY], &st)) {
         return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                          "failed to query primary node stat");
      }

      device->drm.primary_rdev = st.st_rdev;
   }

   return VK_SUCCESS;
}

static void
init_shader_caches(struct panvk_physical_device *device,
                   const struct panvk_instance *instance)
{
   struct mesa_sha1 sha_ctx;
   _mesa_sha1_init(&sha_ctx);

   _mesa_sha1_update(&sha_ctx, instance->driver_build_sha,
                     sizeof(instance->driver_build_sha));

   _mesa_sha1_update(&sha_ctx, &device->kmod.props.gpu_id,
                     sizeof(device->kmod.props.gpu_id));

   unsigned char sha[SHA1_DIGEST_LENGTH];
   _mesa_sha1_final(&sha_ctx, sha);

   STATIC_ASSERT(VK_UUID_SIZE <= SHA1_DIGEST_LENGTH);
   memcpy(device->cache_uuid, sha, VK_UUID_SIZE);

#ifdef ENABLE_SHADER_CACHE
   char renderer[17];
   ASSERTED int len = snprintf(renderer, sizeof(renderer), "panvk_0x%08x",
                               device->kmod.props.gpu_id);
   assert(len == sizeof(renderer) - 1);

   char timestamp[SHA1_DIGEST_STRING_LENGTH];
   _mesa_sha1_format(timestamp, instance->driver_build_sha);

   const uint64_t driver_flags = 0;
   device->vk.disk_cache = disk_cache_create(renderer, timestamp, driver_flags);
#endif
}

static void
free_disk_cache(struct panvk_physical_device *device)
{
#ifdef ENABLE_SHADER_CACHE
   if (device->vk.disk_cache) {
      disk_cache_destroy(device->vk.disk_cache);
      device->vk.disk_cache = NULL;
   }
#else
   assert(device->vk.disk_cache == NULL);
#endif
}

static VkResult
get_core_mask(struct panvk_physical_device *device,
              const struct panvk_instance *instance, const char *option_name,
              uint64_t *mask)
{
   uint64_t present = device->kmod.props.shader_present;
   *mask = driQueryOptionu64(&instance->dri_options, option_name) & present;

   if (!*mask)
      return panvk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                          "None of the cores specified in %s are present. "
                          "Available shader cores are 0x%" PRIx64 ".\n",
                          option_name, present);

   return VK_SUCCESS;
}

static VkResult
get_core_masks(struct panvk_physical_device *device,
               const struct panvk_instance *instance)
{
   VkResult result;

   result = get_core_mask(device, instance, "pan_compute_core_mask",
                          &device->compute_core_mask);
   if (result != VK_SUCCESS)
      return result;
   result = get_core_mask(device, instance, "pan_fragment_core_mask",
                          &device->fragment_core_mask);

   return result;
}

static VkResult
get_device_sync_types(struct panvk_physical_device *device,
                      const struct panvk_instance *instance)
{
   const unsigned arch = pan_arch(device->kmod.props.gpu_id);
   uint32_t sync_type_count = 0;

   device->drm_syncobj_type = vk_drm_syncobj_get_type(device->kmod.dev->fd);
   if (!device->drm_syncobj_type.features) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "failed to query syncobj features");
   }

   device->sync_types[sync_type_count++] = &device->drm_syncobj_type;

   if (arch >= 10) {
      assert(device->drm_syncobj_type.features & VK_SYNC_FEATURE_TIMELINE);
   } else {
      /* We don't support timelines in the uAPI yet and we don't want it getting
       * suddenly turned on by vk_drm_syncobj_get_type() without us adding panvk
       * code for it first.
       */
      device->drm_syncobj_type.features &= ~VK_SYNC_FEATURE_TIMELINE;

      device->sync_timeline_type =
         vk_sync_timeline_get_type(&device->drm_syncobj_type);
      device->sync_types[sync_type_count++] = &device->sync_timeline_type.sync;
   }

   assert(sync_type_count < ARRAY_SIZE(device->sync_types));
   device->sync_types[sync_type_count] = NULL;

   return VK_SUCCESS;
}

float
panvk_get_gpu_system_timestamp_period(const struct panvk_physical_device *device)
{
   if (!device->kmod.props.gpu_can_query_timestamp ||
       !device->kmod.props.timestamp_frequency)
      return 0;

   const float ns_per_s = 1000000000.0;
   return ns_per_s / (float)device->kmod.props.timestamp_frequency;
}

void
panvk_physical_device_finish(struct panvk_physical_device *device)
{
   panvk_wsi_finish(device);

   free_disk_cache(device);

   pan_kmod_dev_destroy(device->kmod.dev);

   vk_physical_device_finish(&device->vk);
}

VkResult
panvk_physical_device_init(struct panvk_physical_device *device,
                           struct panvk_instance *instance,
                           drmDevicePtr drm_device)
{
   VkResult result;

   result = create_kmod_dev(device, instance, drm_device);
   if (result != VK_SUCCESS)
      return result;

   pan_kmod_dev_query_props(device->kmod.dev, &device->kmod.props);

   device->model = pan_get_model(device->kmod.props.gpu_id,
                                 device->kmod.props.gpu_variant);

   unsigned arch = pan_arch(device->kmod.props.gpu_id);

   if (!device->model) {
      result = panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                            "Unknown gpu_id (%#x) or variant (%#x)",
                            device->kmod.props.gpu_id,
                            device->kmod.props.gpu_variant);
      goto fail;
   }

   switch (arch) {
   case 6:
   case 7:
      if (!os_get_option("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER")) {
         result = panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "WARNING: panvk is not well-tested on v%d, "
                               "pass PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 "
                               "if you know what you're doing.", arch);
         goto fail;
      }
      break;

   case 10:
   case 12:
   case 13:
      break;

   default:
      result = panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                            "%s not supported", device->model->name);
      goto fail;
   }

   result = get_drm_device_ids(device, instance, drm_device);
   if (result != VK_SUCCESS)
      goto fail;

   device->formats.all = pan_format_table(arch);
   device->formats.blendable = pan_blendable_format_table(arch);

   memset(device->name, 0, sizeof(device->name));
   sprintf(device->name, "%s", device->model->name);

   init_shader_caches(device, instance);

   result = get_core_masks(device, instance);
   if (result != VK_SUCCESS)
      goto fail;

   result = get_device_sync_types(device, instance);
   if (result != VK_SUCCESS)
      goto fail;

   if (arch >= 10) {
      /* XXX: Make dri options for thoses */
      device->csf.tiler.chunk_size = 2 * 1024 * 1024;
      device->csf.tiler.initial_chunks = 5;
      device->csf.tiler.max_chunks = 64;
   }

   if (arch != 10)
      vk_warn_non_conformant_implementation("panvk");

   struct vk_device_extension_table supported_extensions;
   panvk_arch_dispatch(arch, get_physical_device_extensions, device,
                       &supported_extensions);

   struct vk_features supported_features;
   panvk_arch_dispatch(arch, get_physical_device_features, instance,
                       device, &supported_features);

   struct vk_properties properties;
   panvk_arch_dispatch(arch, get_physical_device_properties, instance,
                       device, &properties);

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &panvk_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk,
                                    &supported_extensions, &supported_features,
                                    &properties, &dispatch_table);

   if (result != VK_SUCCESS)
      goto fail;

   device->vk.supported_sync_types = device->sync_types;

   result = panvk_wsi_init(device);
   if (result != VK_SUCCESS)
      goto fail;

   return VK_SUCCESS;

fail:
   free_disk_cache(device);

   if (device->vk.instance)
      vk_physical_device_finish(&device->vk);

   pan_kmod_dev_destroy(device->kmod.dev);

   return result;
}

static void
panvk_fill_global_priority(const struct panvk_physical_device *physical_device,
                           uint32_t family_idx,
                           VkQueueFamilyGlobalPriorityPropertiesKHR *prio)
{
   const unsigned arch = pan_arch(physical_device->kmod.props.gpu_id);
   uint32_t prio_idx = 0;

   switch (family_idx) {
   case PANVK_QUEUE_FAMILY_GPU: {
      enum pan_kmod_group_allow_priority_flags prio_mask =
         physical_device->kmod.props.allowed_group_priorities_mask;

      /* Non-medium priority context is not hooked-up in the JM backend, even
       * though the panfrost kmod advertize it. Manually filter non-medium
       * priority for now.
       */
      if (arch < 10)
         prio_mask &= PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM;

      if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_LOW)
         prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR;
      if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM)
         prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;
      if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_HIGH)
         prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR;
      if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_REALTIME)
         prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR;
      break;
   }

   case PANVK_QUEUE_FAMILY_BIND:
      prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM;
      break;

   default:
      UNREACHABLE("Unknown queue family");
   }

   prio->priorityCount = prio_idx;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out, pQueueFamilyProperties,
                          pQueueFamilyPropertyCount);
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_id);

   const VkQueueFamilyProperties qfamily_props[PANVK_QUEUE_FAMILY_COUNT] = {
      [PANVK_QUEUE_FAMILY_GPU] = {
         .queueFlags =
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
         /* On v10+ we can support up to 127 queues but this causes timeout on
            some CTS tests */
         .queueCount = arch >= 10 ? 2 : 1,
         .timestampValidBits =
            arch >= 10 && physical_device->kmod.props.gpu_can_query_timestamp
               ? 64
               : 0,
         .minImageTransferGranularity = {1, 1, 1},
      },
      [PANVK_QUEUE_FAMILY_BIND] = {
         .queueFlags = VK_QUEUE_SPARSE_BINDING_BIT,
         .queueCount = 1,
      },
   };

   for (uint32_t family = 0; family < ARRAY_SIZE(qfamily_props); family++) {
      if (family == PANVK_QUEUE_FAMILY_BIND &&
          !physical_device->vk.supported_features.sparseBinding)
         break;

      vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
         p->queueFamilyProperties = qfamily_props[family];

         VkQueueFamilyGlobalPriorityPropertiesKHR *prio =
            vk_find_struct(p->pNext, QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR);
         if (prio)
            panvk_fill_global_priority(physical_device, family, prio);
      }
   }
}

static uint64_t
get_system_heap_size()
{
   struct sysinfo info;
   sysinfo(&info);

   uint64_t total_ram = (uint64_t)info.totalram * info.mem_unit;

   /* We don't want to burn too much ram with the GPU.  If the user has 4GiB
    * or less, we use at most half.  If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ull * 1024 * 1024 * 1024)
      available_ram = total_ram / 2;
   else
      available_ram = total_ram * 3 / 4;

   return available_ram;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   pMemoryProperties->memoryProperties = (VkPhysicalDeviceMemoryProperties){
      .memoryHeapCount = 1,
      .memoryHeaps[0].size = get_system_heap_size(),
      .memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      .memoryTypeCount = 1,
      .memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      .memoryTypes[0].heapIndex = 0,
   };
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateDevice(VkPhysicalDevice physicalDevice,
                   const VkDeviceCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_id);
   VkResult result = VK_ERROR_INITIALIZATION_FAILED;

   panvk_arch_dispatch_ret(arch, create_device, result, physical_device,
                           pCreateInfo, pAllocator, pDevice);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_physical_device *physical_device =
      to_panvk_physical_device(device->vk.physical);
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_id);

   panvk_arch_dispatch(arch, destroy_device, device, pAllocator);
}

static bool
unsupported_yuv_format(enum pipe_format pfmt)
{
   switch (pfmt) {
   /* 3-plane YUV 444 and 16-bit 3-plane YUV are not supported natively by
    * the HW.
    */
   case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
   case PIPE_FORMAT_Y16_U16_V16_420_UNORM:
   case PIPE_FORMAT_Y16_U16_V16_422_UNORM:
   case PIPE_FORMAT_Y16_U16_V16_444_UNORM:
      return true;
   default:
      return false;
   }
}

static bool
format_is_supported(struct panvk_physical_device *physical_device,
                    const struct pan_format fmt, enum pipe_format pfmt)
{
   if (pfmt == PIPE_FORMAT_NONE)
      return false;

   if (unsupported_yuv_format(pfmt))
      return false;

   /* If the format ID is zero, it's not supported. */
   if (!fmt.hw)
      return false;

   /* Compressed formats (ID < 32) are optional. We need to check against
    * the supported formats reported by the GPU. */
   if (util_format_is_compressed(pfmt)) {
      uint32_t supported_compr_fmts =
         pan_query_compressed_formats(&physical_device->kmod.props);

      if (!(BITFIELD_BIT(fmt.texfeat_bit) & supported_compr_fmts))
         return false;
   }

   return true;
}

static VkFormatFeatureFlags2
get_image_plane_format_features(struct panvk_physical_device *physical_device,
                                VkFormat format)
{
   VkFormatFeatureFlags2 features = 0;
   enum pipe_format pfmt = vk_format_to_pipe_format(format);
   const struct pan_format fmt = physical_device->formats.all[pfmt];
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_id);

   if (!format_is_supported(physical_device, fmt, pfmt))
      return 0;

   if (fmt.bind & PAN_BIND_SAMPLER_VIEW) {
      features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
                  VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
                  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;

      if (arch >= 10)
         features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT;

      /* Integer formats only support nearest filtering */
      if (!util_format_is_scaled(pfmt) && !util_format_is_pure_integer(pfmt))
         features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

      features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;

      if (vk_format_has_depth(format))
         features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
   }

   if (fmt.bind & PAN_BIND_RENDER_TARGET) {
      features |= VK_FORMAT_FEATURE_2_BLIT_DST_BIT;
      features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
      features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
   }

   if (fmt.bind & PAN_BIND_STORAGE_IMAGE) {
      features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
      if (pfmt == PIPE_FORMAT_R32_UINT || pfmt == PIPE_FORMAT_R32_SINT)
         features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;
   }

   if (fmt.bind & PAN_BIND_DEPTH_STENCIL)
      features |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;

   if (features != 0)
      features |= VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT;

   return features;
}

static VkFormatFeatureFlags2
get_image_format_features(struct panvk_physical_device *physical_device,
                          VkFormat format)
{
   const struct vk_format_ycbcr_info *ycbcr_info =
         vk_format_get_ycbcr_info(format);

   if (ycbcr_info == NULL)
      return get_image_plane_format_features(physical_device, format);

   if (unsupported_yuv_format(vk_format_to_pipe_format(format)))
      return 0;

   /* For multi-plane, we get the feature flags of each plane separately,
    * then take their intersection as the overall format feature flags
    */
   VkFormatFeatureFlags2 features = ~0ull;
   bool cosited_chroma = false;
   for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
      const struct vk_format_ycbcr_plane *plane_info =
         &ycbcr_info->planes[plane];
      features &=
         get_image_plane_format_features(physical_device, plane_info->format);
      if (plane_info->denominator_scales[0] > 1 ||
          plane_info->denominator_scales[1] > 1)
         cosited_chroma = true;
   }
   if (features == 0)
      return 0;

   /* Uh... We really should be able to sample from YCbCr */
   assert(features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT);
   assert(features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

   /* Siting is handled in the YCbCr lowering pass. */
   features |= VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;
   if (cosited_chroma)
      features |= VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT;

   /* These aren't allowed for YCbCr formats */
   features &= ~(VK_FORMAT_FEATURE_2_BLIT_SRC_BIT |
                 VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
                 VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                 VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT |
                 VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                 VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                 VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT);

   /* This is supported on all YCbCr formats */
   features |=
      VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

   if (ycbcr_info->n_planes > 1) {
      /* DISJOINT_BIT implies that each plane has its own separate binding,
       * while SEPARATE_RECONSTRUCTION_FILTER_BIT implies that luma and chroma
       * each have their own, separate filters, so these two bits make sense
       * for multi-planar formats only.
       */
      features |= VK_FORMAT_FEATURE_2_DISJOINT_BIT |
                  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT;
   }

   return features;
}

VkSampleCountFlags
panvk_get_sample_counts(unsigned arch, unsigned max_tib_size,
                        unsigned max_cbuf_atts, unsigned format_size)
{
   VkSampleCountFlags sample_counts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;

   unsigned max_msaa =
      pan_get_max_msaa(arch, max_tib_size, max_cbuf_atts, format_size);

   assert(max_msaa >= 4);

   if (arch >= 12)
      sample_counts |= VK_SAMPLE_COUNT_2_BIT;

   if (max_msaa >= 8)
      sample_counts |= VK_SAMPLE_COUNT_8_BIT;

   if (max_msaa >= 16)
      sample_counts |= VK_SAMPLE_COUNT_16_BIT;

   return sample_counts;
}

static VkFormatFeatureFlags2
get_image_format_sample_counts(struct panvk_physical_device *physical_device,
                               VkFormat format)
{
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_id);
   unsigned max_tib_size = pan_query_tib_size(physical_device->model);
   unsigned max_cbuf_atts = pan_get_max_cbufs(arch, max_tib_size);

   assert(!vk_format_is_compressed(format));
   unsigned format_size = vk_format_get_blocksize(format);

   return panvk_get_sample_counts(arch, max_tib_size, max_cbuf_atts,
                                  format_size);
}

static VkFormatFeatureFlags2
get_buffer_format_features(struct panvk_physical_device *physical_device,
                           VkFormat format)
{
   VkFormatFeatureFlags2 features = 0;
   enum pipe_format pfmt = vk_format_to_pipe_format(format);
   const struct pan_format fmt = physical_device->formats.all[pfmt];

   if (!format_is_supported(physical_device, fmt, pfmt))
      return 0;

   /* Reject sRGB formats (see
    * https://github.com/KhronosGroup/Vulkan-Docs/issues/2214).
    */
   if ((fmt.bind & PAN_BIND_VERTEX_BUFFER) && !util_format_is_srgb(pfmt))
      features |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;

   if (fmt.bind & PAN_BIND_TEXEL_BUFFER)
      features |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;

   if (pfmt == PIPE_FORMAT_R32_UINT || pfmt == PIPE_FORMAT_R32_SINT)
      features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;

   return features;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                         VkFormat format,
                                         VkFormatProperties2 *pFormatProperties)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);
   const unsigned arch = pan_arch(physical_device->kmod.props.gpu_id);

   VkFormatFeatureFlags2 tex =
      get_image_format_features(physical_device, format);
   VkFormatFeatureFlags2 buffer =
      get_buffer_format_features(physical_device, format);

   pFormatProperties->formatProperties = (VkFormatProperties){
      .linearTilingFeatures = tex,
      .optimalTilingFeatures = tex,
      .bufferFeatures = buffer,
   };

   VkFormatProperties3 *formatProperties3 =
      vk_find_struct(pFormatProperties->pNext, FORMAT_PROPERTIES_3);
   if (formatProperties3) {
      formatProperties3->linearTilingFeatures = tex;
      formatProperties3->optimalTilingFeatures = tex;
      formatProperties3->bufferFeatures = buffer;
   }

   VkDrmFormatModifierPropertiesListEXT *list = vk_find_struct(
      pFormatProperties->pNext, DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT);
   if (list) {
      VK_OUTARRAY_MAKE_TYPED(VkDrmFormatModifierPropertiesEXT, out,
                             list->pDrmFormatModifierProperties,
                             &list->drmFormatModifierCount);
      if (pFormatProperties->formatProperties.optimalTilingFeatures &&
          PANVK_DEBUG(WSI_AFBC))
      {
         PAN_SUPPORTED_MODIFIERS(supported);
         for (int mi = 0; mi < ARRAY_SIZE(supported); mi++) {
            if (drm_is_afbc(supported[mi]) &&
                pan_afbc_supports_format(arch, vk_format_to_pipe_format(format)))
            {
               vk_outarray_append_typed(VkDrmFormatModifierPropertiesEXT, &out, mod_props)
               {
                  mod_props->drmFormatModifier = supported[mi];
                  mod_props->drmFormatModifierPlaneCount = 1;
                  mod_props->drmFormatModifierTilingFeatures =
                     pFormatProperties->formatProperties.optimalTilingFeatures;
               }
            }
         }
      }

      if (pFormatProperties->formatProperties.linearTilingFeatures) {
         vk_outarray_append_typed(VkDrmFormatModifierPropertiesEXT, &out,
                                  mod_props)
         {
            mod_props->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
            mod_props->drmFormatModifierPlaneCount = 1;
            mod_props->drmFormatModifierTilingFeatures =
               pFormatProperties->formatProperties.linearTilingFeatures;
         }
      }
   }
}

#define MAX_IMAGE_SIZE_PX (1 << 16)

static VkExtent3D
get_max_2d_image_size(struct panvk_physical_device *phys_dev, VkFormat format)
{
   const unsigned arch = pan_arch(phys_dev->kmod.props.gpu_id);
   const uint64_t max_img_size_B =
      arch <= 10 ? u_uintN_max(32) : u_uintN_max(48);
   const enum pipe_format pfmt = vk_format_to_pipe_format(format);
   const uint32_t fmt_blksize = util_format_get_blocksize(pfmt);
   /* Evenly split blocks across all axis. */
   const uint32_t max_size_el = floor(sqrt(max_img_size_B / fmt_blksize));
   const VkExtent3D ret = {
      .width = MIN2(max_size_el * util_format_get_blockwidth(pfmt),
                    MAX_IMAGE_SIZE_PX),
      .height = MIN2(max_size_el * util_format_get_blockheight(pfmt),
                     MAX_IMAGE_SIZE_PX),
      .depth = 1,
   };

   assert(ret.width >= phys_dev->vk.properties.maxImageDimension2D);
   assert(ret.height >= phys_dev->vk.properties.maxImageDimension2D);
   return ret;
}

static VkExtent3D
get_max_3d_image_size(struct panvk_physical_device *phys_dev, VkFormat format)
{
   const unsigned arch = pan_arch(phys_dev->kmod.props.gpu_id);
   const uint64_t max_img_size_B =
      arch <= 10 ? u_uintN_max(32) : u_uintN_max(48);
   enum pipe_format pfmt = vk_format_to_pipe_format(format);
   uint32_t fmt_blksize = util_format_get_blocksize(pfmt);
   /* Evenly split blocks across each axis. */
   const uint32_t max_size_el = floor(cbrt(max_img_size_B / fmt_blksize));
   const VkExtent3D ret = {
      .width = MIN2(max_size_el * util_format_get_blockwidth(pfmt),
                    MAX_IMAGE_SIZE_PX),
      .height = MIN2(max_size_el * util_format_get_blockheight(pfmt),
                     MAX_IMAGE_SIZE_PX),
      .depth = MIN2(max_size_el * util_format_get_blockdepth(pfmt),
                    MAX_IMAGE_SIZE_PX),
   };

   assert(ret.width >= phys_dev->vk.properties.maxImageDimension3D);
   assert(ret.height >= phys_dev->vk.properties.maxImageDimension3D);
   assert(ret.depth >= phys_dev->vk.properties.maxImageDimension3D);
   return ret;
}

static VkResult
get_image_format_properties(struct panvk_physical_device *physical_device,
                            const VkPhysicalDeviceImageFormatInfo2 *info,
                            VkImageFormatProperties *pImageFormatProperties,
                            VkFormatFeatureFlags2 *p_feature_flags)
{
   VkFormatFeatureFlags2 format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   enum pipe_format format = vk_format_to_pipe_format(info->format);

   const VkImageStencilUsageCreateInfo *stencil_usage_info =
      vk_find_struct_const(info->pNext, IMAGE_STENCIL_USAGE_CREATE_INFO);
   VkImageUsageFlags stencil_usage =
      stencil_usage_info ? stencil_usage_info->stencilUsage : info->usage;
   VkImageUsageFlags all_usage = info->usage | stencil_usage;
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(info->format);

   if (info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      if (!physical_device->vk.supported_features.sparseBinding)
         goto unsupported;

      /*
       * Sparse only manipulates device mappings and we implement host copies on
       * host. Purely hypotetically, we could implement host copies for sparse
       * images in one of, but not limited to, the following ways:
       *
       *    * submitting a device copy and immediately waiting on it
       *
       *    * mirror sparse binds' modifications to device mappings on host
       *
       *    * share a single address space and thus mappings between host and device
       *
       * but realistically speaking, the set of people, apps and tests in the
       * CTS that expect a driver to implement host copies on sparse images is
       * exactly empty, so let's just not bother.
       */
      if (info->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT)
         goto unsupported;
   }

   if (info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) {
      if (!((info->type == VK_IMAGE_TYPE_2D &&
             physical_device->vk.supported_features.sparseResidencyImage2D) ||
            (info->type == VK_IMAGE_TYPE_3D &&
             physical_device->vk.supported_features.sparseResidencyImage3D)))
         goto unsupported;

      /* Only single aspect (thus single plane) stuff is supported for now */
      if (util_bitcount(vk_format_aspects(info->format)) != 1)
         goto unsupported;

      if (info->tiling != VK_IMAGE_TILING_OPTIMAL)
         goto unsupported;

      struct panvk_sparse_block_desc sblock_desc = panvk_get_sparse_block_desc(info->type, info->format);
      if (!panvk_sparse_block_is_valid(sblock_desc))
         goto unsupported;

      VkImageUsageFlags allowed_usages =
         VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
         VK_IMAGE_USAGE_TRANSFER_DST_BIT |
         VK_IMAGE_USAGE_SAMPLED_BIT |
         VK_IMAGE_USAGE_STORAGE_BIT |
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
         VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
      if (all_usage & ~allowed_usages)
         goto unsupported;
   }

   switch (info->tiling) {
   case VK_IMAGE_TILING_LINEAR:
   case VK_IMAGE_TILING_OPTIMAL:
      break;
   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT: {
      const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *mod_info =
         vk_find_struct_const(
            info->pNext, PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);

      /* TODO: switch to using a more generic function for checking mod support here
       * when adding new modifiers, so that this case doesn't become too big. */
      const bool can_use_afbc = 
         PANVK_DEBUG(WSI_AFBC) &&
         panvk_image_can_use_afbc(physical_device, info->format, info->usage,
                                  info->type, info->tiling, 0);
      const bool supported = (drm_is_afbc(mod_info->drmFormatModifier) && can_use_afbc) ||
         mod_info->drmFormatModifier == DRM_FORMAT_MOD_LINEAR;
      if (!supported)
         goto unsupported;

      /* The only difference between optimal and linear is currently whether
       * depth/stencil attachments are allowed on depth/stencil formats.
       * There's no reason to allow importing depth/stencil textures, so just
       * disallow it and then this annoying edge case goes away.
       */
      if (util_format_is_depth_or_stencil(format))
         goto unsupported;
      break;
   }
   default:
      /* VK_KHR_maintenance5: Physical-device-level functions can now be called
       * with any value in the valid range for a type beyond the defined
       * enumerants [...] */
      goto unsupported;
   }

   /* For the purposes of these checks, we don't care about all the extra
    * YCbCr features and we just want the intersection of features available
    * to all planes of the given format.
    */
   if (ycbcr_info == NULL) {
      format_feature_flags =
         get_image_format_features(physical_device, info->format);
   } else {
      format_feature_flags = ~0u;
      assert(ycbcr_info->n_planes > 0);
      for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
         const VkFormat plane_format = ycbcr_info->planes[plane].format;
         format_feature_flags &=
            get_image_format_features(physical_device, plane_format);
      }
   }

   if (format_feature_flags == 0)
      goto unsupported;

   if (ycbcr_info && info->type != VK_IMAGE_TYPE_2D)
      goto unsupported;

   switch (info->type) {
   case VK_IMAGE_TYPE_1D:
      maxExtent.width = 1 << 16;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = 17; /* log2(maxWidth) + 1 */
      maxArraySize = 1 << 16;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent = get_max_2d_image_size(physical_device, info->format);
      maxMipLevels = util_logbase2(maxExtent.width) + 1;
      maxArraySize = 1 << 16;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent = get_max_3d_image_size(physical_device, info->format);
      maxMipLevels = util_logbase2(maxExtent.width) + 1;
      maxArraySize = 1;
      break;
   default:
      /* VK_KHR_maintenance5: Physical-device-level functions can now be called
       * with any value in the valid range for a type beyond the defined
       * enumerants [...] */
      goto unsupported;
   }

   if (ycbcr_info)
      maxMipLevels = 1;

   if (info->tiling == VK_IMAGE_TILING_OPTIMAL &&
       info->type == VK_IMAGE_TYPE_2D && ycbcr_info == NULL &&
       (format_feature_flags &
        (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
         VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(all_usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      sampleCounts |=
         get_image_format_sample_counts(physical_device, info->format);
   }

   /* From the Vulkan 1.2.199 spec:
   *
   *    "VK_IMAGE_CREATE_EXTENDED_USAGE_BIT specifies that the image can be
   *    created with usage flags that are not supported for the format the
   *    image is created with but are supported for at least one format a
   *    VkImageView created from the image can have."
   *
   * If VK_IMAGE_CREATE_EXTENDED_USAGE_BIT is set, views can be created with
   * different usage than the image so we can't always filter on usage.
   * There is one exception to this below for storage.
   */
   if (!(info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)) {
      if (all_usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
         if (!(format_feature_flags & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT)) {
            goto unsupported;
         }
      }

      if (all_usage & VK_IMAGE_USAGE_STORAGE_BIT) {
         if (!(format_feature_flags & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT)) {
            goto unsupported;
         }
      }

      if (all_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ||
          ((all_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
           !vk_format_is_depth_or_stencil(info->format))) {
         if (!(format_feature_flags & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT)) {
            goto unsupported;
         }
      }

      if ((all_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
          ((all_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
           vk_format_is_depth_or_stencil(info->format))) {
         if (!(format_feature_flags &
               VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            goto unsupported;
         }
      }
   }

   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* We need to limit images to 32-bit range, because the maximum
       * slice-stride is 32-bit wide, meaning that if we allocate an image
       * with the maximum width and height, we end up overflowing it.
       *
       * We get around this by simply limiting the maximum resource size.
       */
      .maxResourceSize = UINT32_MAX,
   };

   if (p_feature_flags)
      *p_feature_flags = format_feature_flags;

   return VK_SUCCESS;
unsupported:
   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = {0, 0, 0},
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

static VkResult
panvk_get_external_image_format_properties(
   const struct panvk_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkExternalMemoryHandleTypeFlagBits handleType,
   VkExternalMemoryProperties *external_properties)
{
   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   if (!(handleType & supported_handle_types)) {
      return panvk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                          "VkExternalMemoryTypeFlagBits(0x%x) unsupported",
                          handleType);
   }

   /* pan_image_layout_init requires 2D for explicit layout */
   if (pImageFormatInfo->type != VK_IMAGE_TYPE_2D) {
      return panvk_errorf(
         physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
         "VkExternalMemoryTypeFlagBits(0x%x) unsupported for VkImageType(%d)",
         handleType, pImageFormatInfo->type);
   }

   /* There is no restriction on opaque fds.  But for dma-bufs, we want to
    * make sure vkGetImageSubresourceLayout can be used to query the image
    * layout of an exported dma-buf.  We also want to make sure
    * VkImageDrmFormatModifierExplicitCreateInfoEXT can be used to specify the
    * image layout of an imported dma-buf.  These add restrictions on the
    * image tilings.
    */
   VkExternalMemoryFeatureFlags features = 0;
   if (handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
       pImageFormatInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      features |= VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                  VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
   } else if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR) {
      features |= VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;
   }

   if (!features) {
      return panvk_errorf(
         physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
         "VkExternalMemoryTypeFlagBits(0x%x) unsupported for VkImageTiling(%d)",
         handleType, pImageFormatInfo->tiling);
   }

   *external_properties = (VkExternalMemoryProperties){
      .externalMemoryFeatures = features,
      .exportFromImportedHandleTypes = supported_handle_types,
      .compatibleHandleTypes = supported_handle_types,
   };

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *base_info,
   VkImageFormatProperties2 *base_props)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);
   const VkImageStencilUsageCreateInfo *stencil_usage_info = NULL;
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   const VkPhysicalDeviceImageViewImageFormatInfoEXT *image_view_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkFilterCubicImageViewImageFormatPropertiesEXT *cubic_props = NULL;
   VkFormatFeatureFlags2 format_feature_flags;
   VkHostImageCopyDevicePerformanceQuery *hic_props = NULL;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
   VkResult result;

   result = get_image_format_properties(physical_device, base_info,
                                        &base_props->imageFormatProperties,
                                        &format_feature_flags);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_IMAGE_STENCIL_USAGE_CREATE_INFO:
         stencil_usage_info = (const void*)s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *)s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT:
         image_view_info = (const void *)s;
         break;
      default:
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT:
         cubic_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY:
         hic_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (void *)s;
         break;
      default:
         break;
      }
   }

   /* From the Vulkan 1.0.42 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      VkExternalImageFormatProperties fallback_external_props;

      if (!external_props) {
         memset(&fallback_external_props, 0, sizeof(fallback_external_props));
         external_props = &fallback_external_props;
      }

      if (external_info->handleType ==
          VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
         result = vk_android_get_ahb_image_properties(physicalDevice, base_info,
                                                      base_props);
      } else {
         result = panvk_get_external_image_format_properties(
            physical_device, base_info, external_info->handleType,
            &external_props->externalMemoryProperties);
      }
      if (result != VK_SUCCESS)
         goto fail;

      /* pan_image_layout_init requirements for explicit layout */
      base_props->imageFormatProperties.maxMipLevels = 1;
      base_props->imageFormatProperties.maxArrayLayers = 1;
      base_props->imageFormatProperties.sampleCounts = 1;
   }

   if (cubic_props) {
      /* note: blob only allows cubic filtering for 2D and 2D array views
       * its likely we can enable it for 1D and CUBE, needs testing however
       */
      if ((image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D ||
           image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY) &&
          (format_feature_flags &
           VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT)) {
         cubic_props->filterCubic = true;
         cubic_props->filterCubicMinmax = true;
      } else {
         cubic_props->filterCubic = false;
         cubic_props->filterCubicMinmax = false;
      }
   }

   if (hic_props) {
      VkImageUsageFlags stencil_usage = stencil_usage_info ?
         stencil_usage_info->stencilUsage : base_info->usage;

      /* We don't support AFBC for images used for host transfer. So, if an
       * image could have been tiled as AFBC if it weren't for host transfer,
       * report suboptimal access. */
      VkImageUsageFlags usage = base_info->usage | stencil_usage;
      usage &= ~VK_IMAGE_USAGE_HOST_TRANSFER_BIT;
      bool can_use_afbc = panvk_image_can_use_afbc(
         physical_device, base_info->format, usage, base_info->type,
         base_info->tiling, base_info->flags);
      hic_props->optimalDeviceAccess = !can_use_afbc;
      hic_props->identicalMemoryLayout = !can_use_afbc;
   }

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(base_info->format);
   const unsigned plane_count =
      vk_format_get_plane_count(base_info->format);

   /* From the Vulkan 1.3.259 spec, VkImageCreateInfo:
    *
    *    VUID-VkImageCreateInfo-imageCreateFormatFeatures-02260
    *
    *    "If format is a multi-planar format, and if imageCreateFormatFeatures
    *    (as defined in Image Creation Limits) does not contain
    *    VK_FORMAT_FEATURE_2_DISJOINT_BIT, then flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    *
    * This is satisfied trivially because we support DISJOINT on all
    * multi-plane formats.  Also,
    *
    *    VUID-VkImageCreateInfo-format-01577
    *
    *    "If format is not a multi-planar format, and flags does not include
    *    VK_IMAGE_CREATE_ALIAS_BIT, flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    */
   if (plane_count == 1 &&
       !(base_info->flags & VK_IMAGE_CREATE_ALIAS_BIT) &&
       (base_info->flags & VK_IMAGE_CREATE_DISJOINT_BIT))
      goto fail;

   if (ycbcr_info &&
       ((base_info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) ||
       (base_info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)))
      goto fail;

   if ((base_info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) &&
       (base_info->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT))
      goto fail;

   if (ycbcr_props)
      ycbcr_props->combinedImageSamplerDescriptorCount = 1;

   return VK_SUCCESS;

fail:
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      /* From the Vulkan 1.0.42 spec:
       *
       *    If the combination of parameters to
       *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by
       *    the implementation for use in vkCreateImage, then all members of
       *    imageFormatProperties will be filled with zero.
       */
      base_props->imageFormatProperties = (VkImageFormatProperties){};
   }

   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkSparseImageFormatProperties2, out, pProperties, pPropertyCount);

   VkPhysicalDeviceImageFormatInfo2 img_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = pFormatInfo->format,
      .type = pFormatInfo->type,
      .tiling = pFormatInfo->tiling,
      .usage = pFormatInfo->usage,
      .flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
               VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
   };
   VkImageFormatProperties2 img_props = {};
   if (panvk_GetPhysicalDeviceImageFormatProperties2(physicalDevice, &img_info, &img_props) != VK_SUCCESS)
      return;

   if (!(img_props.imageFormatProperties.sampleCounts & pFormatInfo->samples))
      return;

   /*
    * We don't support multisampled sparse partially-resident images for now.
    * Weirdly enough, banning it the obvious way by making
    * get_image_format_properties report sampleCounts of 1 when flags includes
    * SPARSE_RESIDENCY causes "required sample counts not supported" CTS fails,
    * so we ban them here.
    */
   if (pFormatInfo->samples != 1)
      return;

   vk_outarray_append_typed(VkSparseImageFormatProperties2, &out, prop) {
      prop->properties = panvk_get_sparse_image_fmt_props(pFormatInfo->type, pFormatInfo->format);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   if (pExternalBufferInfo->handleType ==
       VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
      vk_android_get_ahb_buffer_properties(physicalDevice, pExternalBufferInfo,
                                           pExternalBufferProperties);
      return;
   }

   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   /* From the Vulkan 1.3.298 spec:
    *
    *    compatibleHandleTypes must include at least handleType.
    */
   VkExternalMemoryHandleTypeFlags handle_types =
      pExternalBufferInfo->handleType;
   VkExternalMemoryFeatureFlags features = 0;
   if (pExternalBufferInfo->handleType & supported_handle_types) {
      handle_types |= supported_handle_types;
      features |= VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                  VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
   }

   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties){
         .externalMemoryFeatures = features,
         .exportFromImportedHandleTypes = handle_types,
         .compatibleHandleTypes = handle_types,
      };
}
