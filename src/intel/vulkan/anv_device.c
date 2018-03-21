/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

#include "anv_private.h"
#include "util/strtod.h"
#include "util/debug.h"
#include "util/build_id.h"
#include "util/mesa-sha1.h"
#include "vk_util.h"

#include "genxml/gen7_pack.h"

static void
compiler_debug_log(void *data, const char *fmt, ...)
{ }

static void
compiler_perf_log(void *data, const char *fmt, ...)
{
   va_list args;
   va_start(args, fmt);

   if (unlikely(INTEL_DEBUG & DEBUG_PERF))
      intel_logd_v(fmt, args);

   va_end(args);
}

static VkResult
anv_compute_heap_size(int fd, uint64_t *heap_size)
{
   uint64_t gtt_size;
   if (anv_gem_get_context_param(fd, 0, I915_CONTEXT_PARAM_GTT_SIZE,
                                 &gtt_size) == -1) {
      /* If, for whatever reason, we can't actually get the GTT size from the
       * kernel (too old?) fall back to the aperture size.
       */
      anv_perf_warn(NULL, NULL,
                    "Failed to get I915_CONTEXT_PARAM_GTT_SIZE: %m");

      if (anv_gem_get_aperture(fd, &gtt_size) == -1) {
         return vk_errorf(NULL, NULL, VK_ERROR_INITIALIZATION_FAILED,
                          "failed to get aperture size: %m");
      }
   }

   /* Query the total ram from the system */
   struct sysinfo info;
   sysinfo(&info);

   uint64_t total_ram = (uint64_t)info.totalram * (uint64_t)info.mem_unit;

   /* We don't want to burn too much ram with the GPU.  If the user has 4GiB
    * or less, we use at most half.  If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ull * 1024ull * 1024ull * 1024ull)
      available_ram = total_ram / 2;
   else
      available_ram = total_ram * 3 / 4;

   /* We also want to leave some padding for things we allocate in the driver,
    * so don't go over 3/4 of the GTT either.
    */
   uint64_t available_gtt = gtt_size * 3 / 4;

   *heap_size = MIN2(available_ram, available_gtt);

   return VK_SUCCESS;
}

static VkResult
anv_physical_device_init_heaps(struct anv_physical_device *device, int fd)
{
   /* The kernel query only tells us whether or not the kernel supports the
    * EXEC_OBJECT_SUPPORTS_48B_ADDRESS flag and not whether or not the
    * hardware has actual 48bit address support.
    */
   device->supports_48bit_addresses =
      (device->info.gen >= 8) && anv_gem_supports_48b_addresses(fd);

   uint64_t heap_size;
   VkResult result = anv_compute_heap_size(fd, &heap_size);
   if (result != VK_SUCCESS)
      return result;

   if (heap_size > (2ull << 30) && !device->supports_48bit_addresses) {
      /* When running with an overridden PCI ID, we may get a GTT size from
       * the kernel that is greater than 2 GiB but the execbuf check for 48bit
       * address support can still fail.  Just clamp the address space size to
       * 2 GiB if we don't have 48-bit support.
       */
      intel_logw("%s:%d: The kernel reported a GTT size larger than 2 GiB but "
                        "not support for 48-bit addresses",
                        __FILE__, __LINE__);
      heap_size = 2ull << 30;
   }

   if (heap_size <= 3ull * (1ull << 30)) {
      /* In this case, everything fits nicely into the 32-bit address space,
       * so there's no need for supporting 48bit addresses on client-allocated
       * memory objects.
       */
      device->memory.heap_count = 1;
      device->memory.heaps[0] = (struct anv_memory_heap) {
         .size = heap_size,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
         .supports_48bit_addresses = false,
      };
   } else {
      /* Not everything will fit nicely into a 32-bit address space.  In this
       * case we need a 64-bit heap.  Advertise a small 32-bit heap and a
       * larger 48-bit heap.  If we're in this case, then we have a total heap
       * size larger than 3GiB which most likely means they have 8 GiB of
       * video memory and so carving off 1 GiB for the 32-bit heap should be
       * reasonable.
       */
      const uint64_t heap_size_32bit = 1ull << 30;
      const uint64_t heap_size_48bit = heap_size - heap_size_32bit;

      assert(device->supports_48bit_addresses);

      device->memory.heap_count = 2;
      device->memory.heaps[0] = (struct anv_memory_heap) {
         .size = heap_size_48bit,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
         .supports_48bit_addresses = true,
      };
      device->memory.heaps[1] = (struct anv_memory_heap) {
         .size = heap_size_32bit,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
         .supports_48bit_addresses = false,
      };
   }

   uint32_t type_count = 0;
   for (uint32_t heap = 0; heap < device->memory.heap_count; heap++) {
      uint32_t valid_buffer_usage = ~0;

      /* There appears to be a hardware issue in the VF cache where it only
       * considers the bottom 32 bits of memory addresses.  If you happen to
       * have two vertex buffers which get placed exactly 4 GiB apart and use
       * them in back-to-back draw calls, you can get collisions.  In order to
       * solve this problem, we require vertex and index buffers be bound to
       * memory allocated out of the 32-bit heap.
       */
      if (device->memory.heaps[heap].supports_48bit_addresses) {
         valid_buffer_usage &= ~(VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
      }

      if (device->info.has_llc) {
         /* Big core GPUs share LLC with the CPU and thus one memory type can be
          * both cached and coherent at the same time.
          */
         device->memory.types[type_count++] = (struct anv_memory_type) {
            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            .heapIndex = heap,
            .valid_buffer_usage = valid_buffer_usage,
         };
      } else {
         /* The spec requires that we expose a host-visible, coherent memory
          * type, but Atom GPUs don't share LLC. Thus we offer two memory types
          * to give the application a choice between cached, but not coherent and
          * coherent but uncached (WC though).
          */
         device->memory.types[type_count++] = (struct anv_memory_type) {
            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            .heapIndex = heap,
            .valid_buffer_usage = valid_buffer_usage,
         };
         device->memory.types[type_count++] = (struct anv_memory_type) {
            .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            .heapIndex = heap,
            .valid_buffer_usage = valid_buffer_usage,
         };
      }
   }
   device->memory.type_count = type_count;

   return VK_SUCCESS;
}

static VkResult
anv_physical_device_init_uuids(struct anv_physical_device *device)
{
   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(anv_physical_device_init_uuids);
   if (!note) {
      return vk_errorf(device->instance, device,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to find build-id");
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < 20) {
      return vk_errorf(device->instance, device,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "build-id too short.  It needs to be a SHA");
   }

   struct mesa_sha1 sha1_ctx;
   uint8_t sha1[20];
   STATIC_ASSERT(VK_UUID_SIZE <= sizeof(sha1));

   /* The pipeline cache UUID is used for determining when a pipeline cache is
    * invalid.  It needs both a driver build and the PCI ID of the device.
    */
   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, build_id_data(note), build_id_len);
   _mesa_sha1_update(&sha1_ctx, &device->chipset_id,
                     sizeof(device->chipset_id));
   _mesa_sha1_final(&sha1_ctx, sha1);
   memcpy(device->pipeline_cache_uuid, sha1, VK_UUID_SIZE);

   /* The driver UUID is used for determining sharability of images and memory
    * between two Vulkan instances in separate processes.  People who want to
    * share memory need to also check the device UUID (below) so all this
    * needs to be is the build-id.
    */
   memcpy(device->driver_uuid, build_id_data(note), VK_UUID_SIZE);

   /* The device UUID uniquely identifies the given device within the machine.
    * Since we never have more than one device, this doesn't need to be a real
    * UUID.  However, on the off-chance that someone tries to use this to
    * cache pre-tiled images or something of the like, we use the PCI ID and
    * some bits of ISL info to ensure that this is safe.
    */
   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, &device->chipset_id,
                     sizeof(device->chipset_id));
   _mesa_sha1_update(&sha1_ctx, &device->isl_dev.has_bit6_swizzling,
                     sizeof(device->isl_dev.has_bit6_swizzling));
   _mesa_sha1_final(&sha1_ctx, sha1);
   memcpy(device->device_uuid, sha1, VK_UUID_SIZE);

   return VK_SUCCESS;
}

static VkResult
anv_physical_device_init(struct anv_physical_device *device,
                         struct anv_instance *instance,
                         const char *path)
{
   VkResult result;
   int fd;

   brw_process_intel_debug_variable();

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0)
      return vk_error(VK_ERROR_INCOMPATIBLE_DRIVER);

   device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   device->instance = instance;

   assert(strlen(path) < ARRAY_SIZE(device->path));
   strncpy(device->path, path, ARRAY_SIZE(device->path));

   device->chipset_id = anv_gem_get_param(fd, I915_PARAM_CHIPSET_ID);
   if (!device->chipset_id) {
      result = vk_error(VK_ERROR_INCOMPATIBLE_DRIVER);
      goto fail;
   }

   device->name = gen_get_device_name(device->chipset_id);
   if (!gen_get_device_info(device->chipset_id, &device->info)) {
      result = vk_error(VK_ERROR_INCOMPATIBLE_DRIVER);
      goto fail;
   }

   if (device->info.is_haswell) {
      intel_logw("Haswell Vulkan support is incomplete");
   } else if (device->info.gen == 7 && !device->info.is_baytrail) {
      intel_logw("Ivy Bridge Vulkan support is incomplete");
   } else if (device->info.gen == 7 && device->info.is_baytrail) {
      intel_logw("Bay Trail Vulkan support is incomplete");
   } else if (device->info.gen >= 8 && device->info.gen <= 10) {
      /* Gen8-10 fully supported */
   } else {
      result = vk_errorf(device->instance, device,
                         VK_ERROR_INCOMPATIBLE_DRIVER,
                         "Vulkan not yet supported on %s", device->name);
      goto fail;
   }

   device->cmd_parser_version = -1;
   if (device->info.gen == 7) {
      device->cmd_parser_version =
         anv_gem_get_param(fd, I915_PARAM_CMD_PARSER_VERSION);
      if (device->cmd_parser_version == -1) {
         result = vk_errorf(device->instance, device,
                            VK_ERROR_INITIALIZATION_FAILED,
                            "failed to get command parser version");
         goto fail;
      }
   }

   if (!anv_gem_get_param(fd, I915_PARAM_HAS_WAIT_TIMEOUT)) {
      result = vk_errorf(device->instance, device,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "kernel missing gem wait");
      goto fail;
   }

   if (!anv_gem_get_param(fd, I915_PARAM_HAS_EXECBUF2)) {
      result = vk_errorf(device->instance, device,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "kernel missing execbuf2");
      goto fail;
   }

   if (!device->info.has_llc &&
       anv_gem_get_param(fd, I915_PARAM_MMAP_VERSION) < 1) {
      result = vk_errorf(device->instance, device,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "kernel missing wc mmap");
      goto fail;
   }

   result = anv_physical_device_init_heaps(device, fd);
   if (result != VK_SUCCESS)
      goto fail;

   device->has_exec_async = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_ASYNC);
   device->has_exec_capture = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_CAPTURE);
   device->has_exec_fence = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_FENCE);
   device->has_syncobj = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_FENCE_ARRAY);
   device->has_syncobj_wait = device->has_syncobj &&
                              anv_gem_supports_syncobj_wait(fd);

   bool swizzled = anv_gem_get_bit6_swizzle(fd, I915_TILING_X);

   /* Starting with Gen10, the timestamp frequency of the command streamer may
    * vary from one part to another. We can query the value from the kernel.
    */
   if (device->info.gen >= 10) {
      int timestamp_frequency =
         anv_gem_get_param(fd, I915_PARAM_CS_TIMESTAMP_FREQUENCY);

      if (timestamp_frequency < 0)
         intel_logw("Kernel 4.16-rc1+ required to properly query CS timestamp frequency");
      else
         device->info.timestamp_frequency = timestamp_frequency;
   }

   /* GENs prior to 8 do not support EU/Subslice info */
   if (device->info.gen >= 8) {
      device->subslice_total = anv_gem_get_param(fd, I915_PARAM_SUBSLICE_TOTAL);
      device->eu_total = anv_gem_get_param(fd, I915_PARAM_EU_TOTAL);

      /* Without this information, we cannot get the right Braswell
       * brandstrings, and we have to use conservative numbers for GPGPU on
       * many platforms, but otherwise, things will just work.
       */
      if (device->subslice_total < 1 || device->eu_total < 1) {
         intel_logw("Kernel 4.1 required to properly query GPU properties");
      }
   } else if (device->info.gen == 7) {
      device->subslice_total = 1 << (device->info.gt - 1);
   }

   if (device->info.is_cherryview &&
       device->subslice_total > 0 && device->eu_total > 0) {
      /* Logical CS threads = EUs per subslice * num threads per EU */
      uint32_t max_cs_threads =
         device->eu_total / device->subslice_total * device->info.num_thread_per_eu;

      /* Fuse configurations may give more threads than expected, never less. */
      if (max_cs_threads > device->info.max_cs_threads)
         device->info.max_cs_threads = max_cs_threads;
   }

   device->compiler = brw_compiler_create(NULL, &device->info);
   if (device->compiler == NULL) {
      result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail;
   }
   device->compiler->shader_debug_log = compiler_debug_log;
   device->compiler->shader_perf_log = compiler_perf_log;
   device->compiler->supports_pull_constants = false;
   device->compiler->constant_buffer_0_is_relative = true;

   isl_device_init(&device->isl_dev, &device->info, swizzled);

   result = anv_physical_device_init_uuids(device);
   if (result != VK_SUCCESS)
      goto fail;

   result = anv_init_wsi(device);
   if (result != VK_SUCCESS) {
      ralloc_free(device->compiler);
      goto fail;
   }

   anv_physical_device_get_supported_extensions(device,
                                                &device->supported_extensions);

   device->local_fd = fd;
   return VK_SUCCESS;

fail:
   close(fd);
   return result;
}

static void
anv_physical_device_finish(struct anv_physical_device *device)
{
   anv_finish_wsi(device);
   ralloc_free(device->compiler);
   close(device->local_fd);
}

static void *
default_alloc_func(void *pUserData, size_t size, size_t align,
                   VkSystemAllocationScope allocationScope)
{
   return malloc(size);
}

static void *
default_realloc_func(void *pUserData, void *pOriginal, size_t size,
                     size_t align, VkSystemAllocationScope allocationScope)
{
   return realloc(pOriginal, size);
}

static void
default_free_func(void *pUserData, void *pMemory)
{
   free(pMemory);
}

static const VkAllocationCallbacks default_alloc = {
   .pUserData = NULL,
   .pfnAllocation = default_alloc_func,
   .pfnReallocation = default_realloc_func,
   .pfnFree = default_free_func,
};

VkResult anv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < ANV_INSTANCE_EXTENSION_COUNT; i++) {
      if (anv_instance_extensions_supported.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = anv_instance_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

VkResult anv_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
   struct anv_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   /* Check if user passed a debug report callback to be used during
    * Create/Destroy of instance.
    */
   const VkDebugReportCallbackCreateInfoEXT *ctor_cb =
      vk_find_struct_const(pCreateInfo->pNext,
                           DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT);

   uint32_t client_version;
   if (pCreateInfo->pApplicationInfo &&
       pCreateInfo->pApplicationInfo->apiVersion != 0) {
      client_version = pCreateInfo->pApplicationInfo->apiVersion;
   } else {
      client_version = VK_MAKE_VERSION(1, 0, 0);
   }

   if (VK_MAKE_VERSION(1, 0, 0) > client_version ||
       client_version > VK_MAKE_VERSION(1, 0, 0xfff)) {

      if (ctor_cb && ctor_cb->flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
         ctor_cb->pfnCallback(VK_DEBUG_REPORT_ERROR_BIT_EXT,
                              VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT,
                              VK_NULL_HANDLE, /* No handle available yet. */
                              __LINE__,
                              0,
                              "anv",
                              "incompatible driver version",
                              ctor_cb->pUserData);

      return vk_errorf(NULL, NULL, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "Client requested version %d.%d.%d",
                       VK_VERSION_MAJOR(client_version),
                       VK_VERSION_MINOR(client_version),
                       VK_VERSION_PATCH(client_version));
   }

   struct anv_instance_extension_table enabled_extensions = {};
   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < ANV_INSTANCE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    anv_instance_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= ANV_INSTANCE_EXTENSION_COUNT)
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      if (!anv_instance_extensions_supported.extensions[idx])
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      enabled_extensions.extensions[idx] = true;
   }

   instance = vk_alloc2(&default_alloc, pAllocator, sizeof(*instance), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   instance->_loader_data.loaderMagic = ICD_LOADER_MAGIC;

   if (pAllocator)
      instance->alloc = *pAllocator;
   else
      instance->alloc = default_alloc;

   instance->apiVersion = client_version;
   instance->enabled_extensions = enabled_extensions;

   for (unsigned i = 0; i < ARRAY_SIZE(instance->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!anv_entrypoint_is_enabled(i, instance->apiVersion,
                                     &instance->enabled_extensions, NULL)) {
         instance->dispatch.entrypoints[i] = NULL;
      } else if (anv_dispatch_table.entrypoints[i] != NULL) {
         instance->dispatch.entrypoints[i] = anv_dispatch_table.entrypoints[i];
      } else {
         instance->dispatch.entrypoints[i] =
            anv_tramp_dispatch_table.entrypoints[i];
      }
   }

   instance->physicalDeviceCount = -1;

   result = vk_debug_report_instance_init(&instance->debug_report_callbacks);
   if (result != VK_SUCCESS) {
      vk_free2(&default_alloc, pAllocator, instance);
      return vk_error(result);
   }

   _mesa_locale_init();

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   *pInstance = anv_instance_to_handle(instance);

   return VK_SUCCESS;
}

void anv_DestroyInstance(
    VkInstance                                  _instance,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   if (!instance)
      return;

   if (instance->physicalDeviceCount > 0) {
      /* We support at most one physical device. */
      assert(instance->physicalDeviceCount == 1);
      anv_physical_device_finish(&instance->physicalDevice);
   }

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   vk_debug_report_instance_destroy(&instance->debug_report_callbacks);

   _mesa_locale_fini();

   vk_free(&instance->alloc, instance);
}

static VkResult
anv_enumerate_devices(struct anv_instance *instance)
{
   /* TODO: Check for more devices ? */
   drmDevicePtr devices[8];
   VkResult result = VK_ERROR_INCOMPATIBLE_DRIVER;
   int max_devices;

   instance->physicalDeviceCount = 0;

   max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));
   if (max_devices < 1)
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   for (unsigned i = 0; i < (unsigned)max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PCI &&
          devices[i]->deviceinfo.pci->vendor_id == 0x8086) {

         result = anv_physical_device_init(&instance->physicalDevice,
                        instance,
                        devices[i]->nodes[DRM_NODE_RENDER]);
         if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
            break;
      }
   }
   drmFreeDevices(devices, max_devices);

   if (result == VK_SUCCESS)
      instance->physicalDeviceCount = 1;

   return result;
}


VkResult anv_EnumeratePhysicalDevices(
    VkInstance                                  _instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   VK_OUTARRAY_MAKE(out, pPhysicalDevices, pPhysicalDeviceCount);
   VkResult result;

   if (instance->physicalDeviceCount < 0) {
      result = anv_enumerate_devices(instance);
      if (result != VK_SUCCESS &&
          result != VK_ERROR_INCOMPATIBLE_DRIVER)
         return result;
   }

   if (instance->physicalDeviceCount > 0) {
      assert(instance->physicalDeviceCount == 1);
      vk_outarray_append(&out, i) {
         *i = anv_physical_device_to_handle(&instance->physicalDevice);
      }
   }

   return vk_outarray_status(&out);
}

void anv_GetPhysicalDeviceFeatures(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);

   *pFeatures = (VkPhysicalDeviceFeatures) {
      .robustBufferAccess                       = true,
      .fullDrawIndexUint32                      = true,
      .imageCubeArray                           = true,
      .independentBlend                         = true,
      .geometryShader                           = true,
      .tessellationShader                       = true,
      .sampleRateShading                        = true,
      .dualSrcBlend                             = true,
      .logicOp                                  = true,
      .multiDrawIndirect                        = true,
      .drawIndirectFirstInstance                = true,
      .depthClamp                               = true,
      .depthBiasClamp                           = true,
      .fillModeNonSolid                         = true,
      .depthBounds                              = false,
      .wideLines                                = true,
      .largePoints                              = true,
      .alphaToOne                               = true,
      .multiViewport                            = true,
      .samplerAnisotropy                        = true,
      .textureCompressionETC2                   = pdevice->info.gen >= 8 ||
                                                  pdevice->info.is_baytrail,
      .textureCompressionASTC_LDR               = pdevice->info.gen >= 9, /* FINISHME CHV */
      .textureCompressionBC                     = true,
      .occlusionQueryPrecise                    = true,
      .pipelineStatisticsQuery                  = true,
      .fragmentStoresAndAtomics                 = true,
      .shaderTessellationAndGeometryPointSize   = true,
      .shaderImageGatherExtended                = true,
      .shaderStorageImageExtendedFormats        = true,
      .shaderStorageImageMultisample            = false,
      .shaderStorageImageReadWithoutFormat      = false,
      .shaderStorageImageWriteWithoutFormat     = true,
      .shaderUniformBufferArrayDynamicIndexing  = true,
      .shaderSampledImageArrayDynamicIndexing   = true,
      .shaderStorageBufferArrayDynamicIndexing  = true,
      .shaderStorageImageArrayDynamicIndexing   = true,
      .shaderClipDistance                       = true,
      .shaderCullDistance                       = true,
      .shaderFloat64                            = pdevice->info.gen >= 8,
      .shaderInt64                              = pdevice->info.gen >= 8,
      .shaderInt16                              = false,
      .shaderResourceMinLod                     = false,
      .variableMultisampleRate                  = false,
      .inheritedQueries                         = true,
   };

   /* We can't do image stores in vec4 shaders */
   pFeatures->vertexPipelineStoresAndAtomics =
      pdevice->compiler->scalar_stage[MESA_SHADER_VERTEX] &&
      pdevice->compiler->scalar_stage[MESA_SHADER_GEOMETRY];
}

void anv_GetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures2KHR*               pFeatures)
{
   anv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   vk_foreach_struct(ext, pFeatures->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHX: {
         VkPhysicalDeviceMultiviewFeaturesKHX *features =
            (VkPhysicalDeviceMultiviewFeaturesKHX *)ext;
         features->multiview = true;
         features->multiviewGeometryShader = true;
         features->multiviewTessellationShader = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTER_FEATURES_KHR: {
         VkPhysicalDeviceVariablePointerFeaturesKHR *features = (void *)ext;
         features->variablePointersStorageBuffer = true;
         features->variablePointers = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES_KHR: {
         VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR *features =
            (VkPhysicalDeviceSamplerYcbcrConversionFeaturesKHR *) ext;
         features->samplerYcbcrConversion = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR: {
         VkPhysicalDevice16BitStorageFeaturesKHR *features =
            (VkPhysicalDevice16BitStorageFeaturesKHR *)ext;

         features->storageBuffer16BitAccess = false;
         features->uniformAndStorageBuffer16BitAccess = false;
         features->storagePushConstant16 = false;
         features->storageInputOutput16 = false;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void anv_GetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);
   const struct gen_device_info *devinfo = &pdevice->info;

   /* See assertions made when programming the buffer surface state. */
   const uint32_t max_raw_buffer_sz = devinfo->gen >= 7 ?
                                      (1ul << 30) : (1ul << 27);

   const uint32_t max_samplers = (devinfo->gen >= 8 || devinfo->is_haswell) ?
                                 128 : 16;

   VkSampleCountFlags sample_counts =
      isl_device_get_sample_counts(&pdevice->isl_dev);

   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D                      = (1 << 14),
      .maxImageDimension2D                      = (1 << 14),
      .maxImageDimension3D                      = (1 << 11),
      .maxImageDimensionCube                    = (1 << 14),
      .maxImageArrayLayers                      = (1 << 11),
      .maxTexelBufferElements                   = 128 * 1024 * 1024,
      .maxUniformBufferRange                    = (1ul << 27),
      .maxStorageBufferRange                    = max_raw_buffer_sz,
      .maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount                 = UINT32_MAX,
      .maxSamplerAllocationCount                = 64 * 1024,
      .bufferImageGranularity                   = 64, /* A cache line */
      .sparseAddressSpaceSize                   = 0,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = max_samplers,
      .maxPerStageDescriptorUniformBuffers      = 64,
      .maxPerStageDescriptorStorageBuffers      = 64,
      .maxPerStageDescriptorSampledImages       = max_samplers,
      .maxPerStageDescriptorStorageImages       = 64,
      .maxPerStageDescriptorInputAttachments    = 64,
      .maxPerStageResources                     = 250,
      .maxDescriptorSetSamplers                 = 6 * max_samplers, /* number of stages * maxPerStageDescriptorSamplers */
      .maxDescriptorSetUniformBuffers           = 6 * 64,           /* number of stages * maxPerStageDescriptorUniformBuffers */
      .maxDescriptorSetUniformBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetStorageBuffers           = 6 * 64,           /* number of stages * maxPerStageDescriptorStorageBuffers */
      .maxDescriptorSetStorageBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetSampledImages            = 6 * max_samplers, /* number of stages * maxPerStageDescriptorSampledImages */
      .maxDescriptorSetStorageImages            = 6 * 64,           /* number of stages * maxPerStageDescriptorStorageImages */
      .maxDescriptorSetInputAttachments         = 256,
      .maxVertexInputAttributes                 = MAX_VBS,
      .maxVertexInputBindings                   = MAX_VBS,
      .maxVertexInputAttributeOffset            = 2047,
      .maxVertexInputBindingStride              = 2048,
      .maxVertexOutputComponents                = 128,
      .maxTessellationGenerationLevel           = 64,
      .maxTessellationPatchSize                 = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 128,
      .maxTessellationControlTotalOutputComponents = 2048,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations             = 32,
      .maxGeometryInputComponents               = 64,
      .maxGeometryOutputComponents              = 128,
      .maxGeometryOutputVertices                = 256,
      .maxGeometryTotalOutputComponents         = 1024,
      .maxFragmentInputComponents               = 128,
      .maxFragmentOutputAttachments             = 8,
      .maxFragmentDualSrcAttachments            = 1,
      .maxFragmentCombinedOutputResources       = 8,
      .maxComputeSharedMemorySize               = 32768,
      .maxComputeWorkGroupCount                 = { 65535, 65535, 65535 },
      .maxComputeWorkGroupInvocations           = 16 * devinfo->max_cs_threads,
      .maxComputeWorkGroupSize = {
         16 * devinfo->max_cs_threads,
         16 * devinfo->max_cs_threads,
         16 * devinfo->max_cs_threads,
      },
      .subPixelPrecisionBits                    = 4 /* FIXME */,
      .subTexelPrecisionBits                    = 4 /* FIXME */,
      .mipmapPrecisionBits                      = 4 /* FIXME */,
      .maxDrawIndexedIndexValue                 = UINT32_MAX,
      .maxDrawIndirectCount                     = UINT32_MAX,
      .maxSamplerLodBias                        = 16,
      .maxSamplerAnisotropy                     = 16,
      .maxViewports                             = MAX_VIEWPORTS,
      .maxViewportDimensions                    = { (1 << 14), (1 << 14) },
      .viewportBoundsRange                      = { INT16_MIN, INT16_MAX },
      .viewportSubPixelBits                     = 13, /* We take a float? */
      .minMemoryMapAlignment                    = 4096, /* A page */
      .minTexelBufferOffsetAlignment            = 1,
      /* We need 16 for UBO block reads to work and 32 for push UBOs */
      .minUniformBufferOffsetAlignment          = 32,
      .minStorageBufferOffsetAlignment          = 4,
      .minTexelOffset                           = -8,
      .maxTexelOffset                           = 7,
      .minTexelGatherOffset                     = -32,
      .maxTexelGatherOffset                     = 31,
      .minInterpolationOffset                   = -0.5,
      .maxInterpolationOffset                   = 0.4375,
      .subPixelInterpolationOffsetBits          = 4,
      .maxFramebufferWidth                      = (1 << 14),
      .maxFramebufferHeight                     = (1 << 14),
      .maxFramebufferLayers                     = (1 << 11),
      .framebufferColorSampleCounts             = sample_counts,
      .framebufferDepthSampleCounts             = sample_counts,
      .framebufferStencilSampleCounts           = sample_counts,
      .framebufferNoAttachmentsSampleCounts     = sample_counts,
      .maxColorAttachments                      = MAX_RTS,
      .sampledImageColorSampleCounts            = sample_counts,
      .sampledImageIntegerSampleCounts          = VK_SAMPLE_COUNT_1_BIT,
      .sampledImageDepthSampleCounts            = sample_counts,
      .sampledImageStencilSampleCounts          = sample_counts,
      .storageImageSampleCounts                 = VK_SAMPLE_COUNT_1_BIT,
      .maxSampleMaskWords                       = 1,
      .timestampComputeAndGraphics              = false,
      .timestampPeriod                          = 1000000000.0 / devinfo->timestamp_frequency,
      .maxClipDistances                         = 8,
      .maxCullDistances                         = 8,
      .maxCombinedClipAndCullDistances          = 8,
      .discreteQueuePriorities                  = 1,
      .pointSizeRange                           = { 0.125, 255.875 },
      .lineWidthRange                           = { 0.0, 7.9921875 },
      .pointSizeGranularity                     = (1.0 / 8.0),
      .lineWidthGranularity                     = (1.0 / 128.0),
      .strictLines                              = false, /* FINISHME */
      .standardSampleLocations                  = true,
      .optimalBufferCopyOffsetAlignment         = 128,
      .optimalBufferCopyRowPitchAlignment       = 128,
      .nonCoherentAtomSize                      = 64,
   };

   *pProperties = (VkPhysicalDeviceProperties) {
      .apiVersion = anv_physical_device_api_version(pdevice),
      .driverVersion = vk_get_driver_version(),
      .vendorID = 0x8086,
      .deviceID = pdevice->chipset_id,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
      .limits = limits,
      .sparseProperties = {0}, /* Broadwell doesn't do sparse. */
   };

   snprintf(pProperties->deviceName, sizeof(pProperties->deviceName),
            "%s", pdevice->name);
   memcpy(pProperties->pipelineCacheUUID,
          pdevice->pipeline_cache_uuid, VK_UUID_SIZE);
}

void anv_GetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2KHR*             pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);

   anv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   vk_foreach_struct(ext, pProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *) ext;

         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR: {
         VkPhysicalDeviceIDPropertiesKHR *id_props =
            (VkPhysicalDeviceIDPropertiesKHR *)ext;
         memcpy(id_props->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
         memcpy(id_props->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
         /* The LUID is for Windows. */
         id_props->deviceLUIDValid = false;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES_KHX: {
         VkPhysicalDeviceMultiviewPropertiesKHX *properties =
            (VkPhysicalDeviceMultiviewPropertiesKHX *)ext;
         properties->maxMultiviewViewCount = 16;
         properties->maxMultiviewInstanceIndex = UINT32_MAX / 16;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES_KHR: {
         VkPhysicalDevicePointClippingPropertiesKHR *properties =
            (VkPhysicalDevicePointClippingPropertiesKHR *) ext;
         properties->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES_KHR;
         anv_finishme("Implement pop-free point clipping");
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

/* We support exactly one queue family. */
static const VkQueueFamilyProperties
anv_queue_family_properties = {
   .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                 VK_QUEUE_COMPUTE_BIT |
                 VK_QUEUE_TRANSFER_BIT,
   .queueCount = 1,
   .timestampValidBits = 36, /* XXX: Real value here */
   .minImageTransferGranularity = { 1, 1, 1 },
};

void anv_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE(out, pQueueFamilyProperties, pCount);

   vk_outarray_append(&out, p) {
      *p = anv_queue_family_properties;
   }
}

void anv_GetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2KHR*                pQueueFamilyProperties)
{

   VK_OUTARRAY_MAKE(out, pQueueFamilyProperties, pQueueFamilyPropertyCount);

   vk_outarray_append(&out, p) {
      p->queueFamilyProperties = anv_queue_family_properties;

      vk_foreach_struct(s, p->pNext) {
         anv_debug_ignored_stype(s->sType);
      }
   }
}

void anv_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);

   pMemoryProperties->memoryTypeCount = physical_device->memory.type_count;
   for (uint32_t i = 0; i < physical_device->memory.type_count; i++) {
      pMemoryProperties->memoryTypes[i] = (VkMemoryType) {
         .propertyFlags = physical_device->memory.types[i].propertyFlags,
         .heapIndex     = physical_device->memory.types[i].heapIndex,
      };
   }

   pMemoryProperties->memoryHeapCount = physical_device->memory.heap_count;
   for (uint32_t i = 0; i < physical_device->memory.heap_count; i++) {
      pMemoryProperties->memoryHeaps[i] = (VkMemoryHeap) {
         .size    = physical_device->memory.heaps[i].size,
         .flags   = physical_device->memory.heaps[i].flags,
      };
   }
}

void anv_GetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties2KHR*       pMemoryProperties)
{
   anv_GetPhysicalDeviceMemoryProperties(physicalDevice,
                                         &pMemoryProperties->memoryProperties);

   vk_foreach_struct(ext, pMemoryProperties->pNext) {
      switch (ext->sType) {
      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

PFN_vkVoidFunction anv_GetInstanceProcAddr(
    VkInstance                                  _instance,
    const char*                                 pName)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (pName == NULL)
      return NULL;

#define LOOKUP_ANV_ENTRYPOINT(entrypoint) \
   if (strcmp(pName, "vk" #entrypoint) == 0) \
      return (PFN_vkVoidFunction)anv_##entrypoint

   LOOKUP_ANV_ENTRYPOINT(EnumerateInstanceExtensionProperties);
   LOOKUP_ANV_ENTRYPOINT(EnumerateInstanceLayerProperties);
   LOOKUP_ANV_ENTRYPOINT(CreateInstance);

#undef LOOKUP_ANV_ENTRYPOINT

   if (instance == NULL)
      return NULL;

   int idx = anv_get_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return instance->dispatch.entrypoints[idx];
}

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance                                  instance,
    const char*                                 pName)
{
   return anv_GetInstanceProcAddr(instance, pName);
}

PFN_vkVoidFunction anv_GetDeviceProcAddr(
    VkDevice                                    _device,
    const char*                                 pName)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (!device || !pName)
      return NULL;

   int idx = anv_get_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return device->dispatch.entrypoints[idx];
}

VkResult
anv_CreateDebugReportCallbackEXT(VkInstance _instance,
                                 const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkDebugReportCallbackEXT* pCallback)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   return vk_create_debug_report_callback(&instance->debug_report_callbacks,
                                          pCreateInfo, pAllocator, &instance->alloc,
                                          pCallback);
}

void
anv_DestroyDebugReportCallbackEXT(VkInstance _instance,
                                  VkDebugReportCallbackEXT _callback,
                                  const VkAllocationCallbacks* pAllocator)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   vk_destroy_debug_report_callback(&instance->debug_report_callbacks,
                                    _callback, pAllocator, &instance->alloc);
}

void
anv_DebugReportMessageEXT(VkInstance _instance,
                          VkDebugReportFlagsEXT flags,
                          VkDebugReportObjectTypeEXT objectType,
                          uint64_t object,
                          size_t location,
                          int32_t messageCode,
                          const char* pLayerPrefix,
                          const char* pMessage)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   vk_debug_report(&instance->debug_report_callbacks, flags, objectType,
                   object, location, messageCode, pLayerPrefix, pMessage);
}

static void
anv_queue_init(struct anv_device *device, struct anv_queue *queue)
{
   queue->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   queue->device = device;
   queue->pool = &device->surface_state_pool;
}

static void
anv_queue_finish(struct anv_queue *queue)
{
}

static struct anv_state
anv_state_pool_emit_data(struct anv_state_pool *pool, size_t size, size_t align, const void *p)
{
   struct anv_state state;

   state = anv_state_pool_alloc(pool, size, align);
   memcpy(state.map, p, size);

   anv_state_flush(pool->block_pool.device, state);

   return state;
}

struct gen8_border_color {
   union {
      float float32[4];
      uint32_t uint32[4];
   };
   /* Pad out to 64 bytes */
   uint32_t _pad[12];
};

static void
anv_device_init_border_colors(struct anv_device *device)
{
   static const struct gen8_border_color border_colors[] = {
      [VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK] =  { .float32 = { 0.0, 0.0, 0.0, 0.0 } },
      [VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK] =       { .float32 = { 0.0, 0.0, 0.0, 1.0 } },
      [VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE] =       { .float32 = { 1.0, 1.0, 1.0, 1.0 } },
      [VK_BORDER_COLOR_INT_TRANSPARENT_BLACK] =    { .uint32 = { 0, 0, 0, 0 } },
      [VK_BORDER_COLOR_INT_OPAQUE_BLACK] =         { .uint32 = { 0, 0, 0, 1 } },
      [VK_BORDER_COLOR_INT_OPAQUE_WHITE] =         { .uint32 = { 1, 1, 1, 1 } },
   };

   device->border_colors = anv_state_pool_emit_data(&device->dynamic_state_pool,
                                                    sizeof(border_colors), 64,
                                                    border_colors);
}

static void
anv_device_init_trivial_batch(struct anv_device *device)
{
   anv_bo_init_new(&device->trivial_batch_bo, device, 4096);

   if (device->instance->physicalDevice.has_exec_async)
      device->trivial_batch_bo.flags |= EXEC_OBJECT_ASYNC;

   void *map = anv_gem_mmap(device, device->trivial_batch_bo.gem_handle,
                            0, 4096, 0);

   struct anv_batch batch = {
      .start = map,
      .next = map,
      .end = map + 4096,
   };

   anv_batch_emit(&batch, GEN7_MI_BATCH_BUFFER_END, bbe);
   anv_batch_emit(&batch, GEN7_MI_NOOP, noop);

   if (!device->info.has_llc)
      gen_clflush_range(map, batch.next - map);

   anv_gem_munmap(map, device->trivial_batch_bo.size);
}

VkResult anv_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, device, physicalDevice);
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);
   (void)device;

   for (int i = 0; i < ANV_DEVICE_EXTENSION_COUNT; i++) {
      if (device->supported_extensions.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = anv_device_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

static void
anv_device_init_dispatch(struct anv_device *device)
{
   const struct anv_dispatch_table *genX_table;
   switch (device->info.gen) {
   case 10:
      genX_table = &gen10_dispatch_table;
      break;
   case 9:
      genX_table = &gen9_dispatch_table;
      break;
   case 8:
      genX_table = &gen8_dispatch_table;
      break;
   case 7:
      if (device->info.is_haswell)
         genX_table = &gen75_dispatch_table;
      else
         genX_table = &gen7_dispatch_table;
      break;
   default:
      unreachable("unsupported gen\n");
   }

   for (unsigned i = 0; i < ARRAY_SIZE(device->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!anv_entrypoint_is_enabled(i, device->instance->apiVersion,
                                     &device->instance->enabled_extensions,
                                     &device->enabled_extensions)) {
         device->dispatch.entrypoints[i] = NULL;
      } else if (genX_table->entrypoints[i]) {
         device->dispatch.entrypoints[i] = genX_table->entrypoints[i];
      } else {
         device->dispatch.entrypoints[i] = anv_dispatch_table.entrypoints[i];
      }
   }
}

VkResult anv_CreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   VkResult result;
   struct anv_device *device;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   struct anv_device_extension_table enabled_extensions = { };
   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < ANV_DEVICE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    anv_device_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= ANV_DEVICE_EXTENSION_COUNT)
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      if (!physical_device->supported_extensions.extensions[idx])
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);

      enabled_extensions.extensions[idx] = true;
   }

   /* Check enabled features */
   if (pCreateInfo->pEnabledFeatures) {
      VkPhysicalDeviceFeatures supported_features;
      anv_GetPhysicalDeviceFeatures(physicalDevice, &supported_features);
      VkBool32 *supported_feature = (VkBool32 *)&supported_features;
      VkBool32 *enabled_feature = (VkBool32 *)pCreateInfo->pEnabledFeatures;
      unsigned num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
      for (uint32_t i = 0; i < num_features; i++) {
         if (enabled_feature[i] && !supported_feature[i])
            return vk_error(VK_ERROR_FEATURE_NOT_PRESENT);
      }
   }

   device = vk_alloc2(&physical_device->instance->alloc, pAllocator,
                       sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   device->instance = physical_device->instance;
   device->chipset_id = physical_device->chipset_id;
   device->lost = false;

   if (pAllocator)
      device->alloc = *pAllocator;
   else
      device->alloc = physical_device->instance->alloc;

   /* XXX(chadv): Can we dup() physicalDevice->fd here? */
   device->fd = open(physical_device->path, O_RDWR | O_CLOEXEC);
   if (device->fd == -1) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_device;
   }

   device->context_id = anv_gem_create_context(device);
   if (device->context_id == -1) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_fd;
   }

   device->info = physical_device->info;
   device->isl_dev = physical_device->isl_dev;

   /* On Broadwell and later, we can use batch chaining to more efficiently
    * implement growing command buffers.  Prior to Haswell, the kernel
    * command parser gets in the way and we have to fall back to growing
    * the batch.
    */
   device->can_chain_batches = device->info.gen >= 8;

   device->robust_buffer_access = pCreateInfo->pEnabledFeatures &&
      pCreateInfo->pEnabledFeatures->robustBufferAccess;
   device->enabled_extensions = enabled_extensions;

   anv_device_init_dispatch(device);

   if (pthread_mutex_init(&device->mutex, NULL) != 0) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_context_id;
   }

   pthread_condattr_t condattr;
   if (pthread_condattr_init(&condattr) != 0) {
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   if (pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   if (pthread_cond_init(&device->queue_submit, NULL) != 0) {
      pthread_condattr_destroy(&condattr);
      result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
      goto fail_mutex;
   }
   pthread_condattr_destroy(&condattr);

   uint64_t bo_flags =
      (physical_device->supports_48bit_addresses ? EXEC_OBJECT_SUPPORTS_48B_ADDRESS : 0) |
      (physical_device->has_exec_async ? EXEC_OBJECT_ASYNC : 0) |
      (physical_device->has_exec_capture ? EXEC_OBJECT_CAPTURE : 0);

   anv_bo_pool_init(&device->batch_bo_pool, device, bo_flags);

   result = anv_bo_cache_init(&device->bo_cache);
   if (result != VK_SUCCESS)
      goto fail_batch_bo_pool;

   /* For the state pools we explicitly disable 48bit. */
   bo_flags = (physical_device->has_exec_async ? EXEC_OBJECT_ASYNC : 0) |
              (physical_device->has_exec_capture ? EXEC_OBJECT_CAPTURE : 0);

   result = anv_state_pool_init(&device->dynamic_state_pool, device, 16384,
                                bo_flags);
   if (result != VK_SUCCESS)
      goto fail_bo_cache;

   result = anv_state_pool_init(&device->instruction_state_pool, device, 16384,
                                bo_flags);
   if (result != VK_SUCCESS)
      goto fail_dynamic_state_pool;

   result = anv_state_pool_init(&device->surface_state_pool, device, 4096,
                                bo_flags);
   if (result != VK_SUCCESS)
      goto fail_instruction_state_pool;

   result = anv_bo_init_new(&device->workaround_bo, device, 1024);
   if (result != VK_SUCCESS)
      goto fail_surface_state_pool;

   anv_device_init_trivial_batch(device);

   anv_scratch_pool_init(device, &device->scratch_pool);

   anv_queue_init(device, &device->queue);

   switch (device->info.gen) {
   case 7:
      if (!device->info.is_haswell)
         result = gen7_init_device_state(device);
      else
         result = gen75_init_device_state(device);
      break;
   case 8:
      result = gen8_init_device_state(device);
      break;
   case 9:
      result = gen9_init_device_state(device);
      break;
   case 10:
      result = gen10_init_device_state(device);
      break;
   default:
      /* Shouldn't get here as we don't create physical devices for any other
       * gens. */
      unreachable("unhandled gen");
   }
   if (result != VK_SUCCESS)
      goto fail_workaround_bo;

   anv_device_init_blorp(device);

   anv_device_init_border_colors(device);

   *pDevice = anv_device_to_handle(device);

   return VK_SUCCESS;

 fail_workaround_bo:
   anv_queue_finish(&device->queue);
   anv_scratch_pool_finish(device, &device->scratch_pool);
   anv_gem_munmap(device->workaround_bo.map, device->workaround_bo.size);
   anv_gem_close(device, device->workaround_bo.gem_handle);
 fail_surface_state_pool:
   anv_state_pool_finish(&device->surface_state_pool);
 fail_instruction_state_pool:
   anv_state_pool_finish(&device->instruction_state_pool);
 fail_dynamic_state_pool:
   anv_state_pool_finish(&device->dynamic_state_pool);
 fail_bo_cache:
   anv_bo_cache_finish(&device->bo_cache);
 fail_batch_bo_pool:
   anv_bo_pool_finish(&device->batch_bo_pool);
   pthread_cond_destroy(&device->queue_submit);
 fail_mutex:
   pthread_mutex_destroy(&device->mutex);
 fail_context_id:
   anv_gem_destroy_context(device, device->context_id);
 fail_fd:
   close(device->fd);
 fail_device:
   vk_free(&device->alloc, device);

   return result;
}

void anv_DestroyDevice(
    VkDevice                                    _device,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (!device)
      return;

   anv_device_finish_blorp(device);

   anv_queue_finish(&device->queue);

#ifdef HAVE_VALGRIND
   /* We only need to free these to prevent valgrind errors.  The backing
    * BO will go away in a couple of lines so we don't actually leak.
    */
   anv_state_pool_free(&device->dynamic_state_pool, device->border_colors);
#endif

   anv_scratch_pool_finish(device, &device->scratch_pool);

   anv_gem_munmap(device->workaround_bo.map, device->workaround_bo.size);
   anv_gem_close(device, device->workaround_bo.gem_handle);

   anv_gem_close(device, device->trivial_batch_bo.gem_handle);

   anv_state_pool_finish(&device->surface_state_pool);
   anv_state_pool_finish(&device->instruction_state_pool);
   anv_state_pool_finish(&device->dynamic_state_pool);

   anv_bo_cache_finish(&device->bo_cache);

   anv_bo_pool_finish(&device->batch_bo_pool);

   pthread_cond_destroy(&device->queue_submit);
   pthread_mutex_destroy(&device->mutex);

   anv_gem_destroy_context(device, device->context_id);

   close(device->fd);

   vk_free(&device->alloc, device);
}

VkResult anv_EnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult anv_EnumerateDeviceLayerProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(VK_ERROR_LAYER_NOT_PRESENT);
}

void anv_GetDeviceQueue(
    VkDevice                                    _device,
    uint32_t                                    queueNodeIndex,
    uint32_t                                    queueIndex,
    VkQueue*                                    pQueue)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   assert(queueIndex == 0);

   *pQueue = anv_queue_to_handle(&device->queue);
}

VkResult
anv_device_query_status(struct anv_device *device)
{
   /* This isn't likely as most of the callers of this function already check
    * for it.  However, it doesn't hurt to check and it potentially lets us
    * avoid an ioctl.
    */
   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   uint32_t active, pending;
   int ret = anv_gem_gpu_get_reset_stats(device, &active, &pending);
   if (ret == -1) {
      /* We don't know the real error. */
      device->lost = true;
      return vk_errorf(device->instance, device, VK_ERROR_DEVICE_LOST,
                       "get_reset_stats failed: %m");
   }

   if (active) {
      device->lost = true;
      return vk_errorf(device->instance, device, VK_ERROR_DEVICE_LOST,
                       "GPU hung on one of our command buffers");
   } else if (pending) {
      device->lost = true;
      return vk_errorf(device->instance, device, VK_ERROR_DEVICE_LOST,
                       "GPU hung with commands in-flight");
   }

   return VK_SUCCESS;
}

VkResult
anv_device_bo_busy(struct anv_device *device, struct anv_bo *bo)
{
   /* Note:  This only returns whether or not the BO is in use by an i915 GPU.
    * Other usages of the BO (such as on different hardware) will not be
    * flagged as "busy" by this ioctl.  Use with care.
    */
   int ret = anv_gem_busy(device, bo->gem_handle);
   if (ret == 1) {
      return VK_NOT_READY;
   } else if (ret == -1) {
      /* We don't know the real error. */
      device->lost = true;
      return vk_errorf(device->instance, device, VK_ERROR_DEVICE_LOST,
                       "gem wait failed: %m");
   }

   /* Query for device status after the busy call.  If the BO we're checking
    * got caught in a GPU hang we don't want to return VK_SUCCESS to the
    * client because it clearly doesn't have valid data.  Yes, this most
    * likely means an ioctl, but we just did an ioctl to query the busy status
    * so it's no great loss.
    */
   return anv_device_query_status(device);
}

VkResult
anv_device_wait(struct anv_device *device, struct anv_bo *bo,
                int64_t timeout)
{
   int ret = anv_gem_wait(device, bo->gem_handle, &timeout);
   if (ret == -1 && errno == ETIME) {
      return VK_TIMEOUT;
   } else if (ret == -1) {
      /* We don't know the real error. */
      device->lost = true;
      return vk_errorf(device->instance, device, VK_ERROR_DEVICE_LOST,
                       "gem wait failed: %m");
   }

   /* Query for device status after the wait.  If the BO we're waiting on got
    * caught in a GPU hang we don't want to return VK_SUCCESS to the client
    * because it clearly doesn't have valid data.  Yes, this most likely means
    * an ioctl, but we just did an ioctl to wait so it's no great loss.
    */
   return anv_device_query_status(device);
}

VkResult anv_DeviceWaitIdle(
    VkDevice                                    _device)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   struct anv_batch batch;

   uint32_t cmds[8];
   batch.start = batch.next = cmds;
   batch.end = (void *) cmds + sizeof(cmds);

   anv_batch_emit(&batch, GEN7_MI_BATCH_BUFFER_END, bbe);
   anv_batch_emit(&batch, GEN7_MI_NOOP, noop);

   return anv_device_submit_simple_batch(device, &batch);
}

VkResult
anv_bo_init_new(struct anv_bo *bo, struct anv_device *device, uint64_t size)
{
   uint32_t gem_handle = anv_gem_create(device, size);
   if (!gem_handle)
      return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);

   anv_bo_init(bo, gem_handle, size);

   return VK_SUCCESS;
}

VkResult anv_AllocateMemory(
    VkDevice                                    _device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMem)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_physical_device *pdevice = &device->instance->physicalDevice;
   struct anv_device_memory *mem;
   VkResult result = VK_SUCCESS;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   /* The Vulkan 1.0.33 spec says "allocationSize must be greater than 0". */
   assert(pAllocateInfo->allocationSize > 0);

   /* The kernel relocation API has a limitation of a 32-bit delta value
    * applied to the address before it is written which, in spite of it being
    * unsigned, is treated as signed .  Because of the way that this maps to
    * the Vulkan API, we cannot handle an offset into a buffer that does not
    * fit into a signed 32 bits.  The only mechanism we have for dealing with
    * this at the moment is to limit all VkDeviceMemory objects to a maximum
    * of 2GB each.  The Vulkan spec allows us to do this:
    *
    *    "Some platforms may have a limit on the maximum size of a single
    *    allocation. For example, certain systems may fail to create
    *    allocations with a size greater than or equal to 4GB. Such a limit is
    *    implementation-dependent, and if such a failure occurs then the error
    *    VK_ERROR_OUT_OF_DEVICE_MEMORY should be returned."
    *
    * We don't use vk_error here because it's not an error so much as an
    * indication to the application that the allocation is too large.
    */
   if (pAllocateInfo->allocationSize > (1ull << 31))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   /* FINISHME: Fail if allocation request exceeds heap size. */

   mem = vk_alloc2(&device->alloc, pAllocator, sizeof(*mem), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(pAllocateInfo->memoryTypeIndex < pdevice->memory.type_count);
   mem->type = &pdevice->memory.types[pAllocateInfo->memoryTypeIndex];
   mem->map = NULL;
   mem->map_size = 0;

   const VkImportMemoryFdInfoKHR *fd_info =
      vk_find_struct_const(pAllocateInfo->pNext, IMPORT_MEMORY_FD_INFO_KHR);

   /* The Vulkan spec permits handleType to be 0, in which case the struct is
    * ignored.
    */
   if (fd_info && fd_info->handleType) {
      /* At the moment, we support only the below handle types. */
      assert(fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR ||
             fd_info->handleType ==
               VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      result = anv_bo_cache_import(device, &device->bo_cache,
                                   fd_info->fd, &mem->bo);
      if (result != VK_SUCCESS)
         goto fail;

      VkDeviceSize aligned_alloc_size =
         align_u64(pAllocateInfo->allocationSize, 4096);

      /* For security purposes, we reject importing the bo if it's smaller
       * than the requested allocation size.  This prevents a malicious client
       * from passing a buffer to a trusted client, lying about the size, and
       * telling the trusted client to try and texture from an image that goes
       * out-of-bounds.  This sort of thing could lead to GPU hangs or worse
       * in the trusted client.  The trusted client can protect itself against
       * this sort of attack but only if it can trust the buffer size.
       */
      if (mem->bo->size < aligned_alloc_size) {
         result = vk_errorf(device->instance, device,
                            VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR,
                            "aligned allocationSize too large for "
                            "VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR: "
                            "%"PRIu64"B > %"PRIu64"B",
                            aligned_alloc_size, mem->bo->size);
         anv_bo_cache_release(device, &device->bo_cache, mem->bo);
         goto fail;
      }

      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
   } else {
      result = anv_bo_cache_alloc(device, &device->bo_cache,
                                  pAllocateInfo->allocationSize,
                                  &mem->bo);
      if (result != VK_SUCCESS)
         goto fail;

      const VkMemoryDedicatedAllocateInfoKHR *dedicated_info =
         vk_find_struct_const(pAllocateInfo->pNext, MEMORY_DEDICATED_ALLOCATE_INFO_KHR);
      if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
         ANV_FROM_HANDLE(anv_image, image, dedicated_info->image);

         /* For images using modifiers, we require a dedicated allocation
          * and we set the BO tiling to match the tiling of the underlying
          * modifier.  This is a bit unfortunate as this is completely
          * pointless for Vulkan.  However, GL needs to be able to map things
          * so it needs the tiling to be set.  The only way to do this in a
          * non-racy way is to set the tiling in the creator of the BO so that
          * makes it our job.
          *
          * One of these days, once the GL driver learns to not map things
          * through the GTT in random places, we can drop this and start
          * allowing multiple modified images in the same BO.
          */
         if (image->drm_format_mod != DRM_FORMAT_MOD_INVALID) {
            assert(isl_drm_modifier_get_info(image->drm_format_mod)->tiling ==
                   image->planes[0].surface.isl.tiling);
            const uint32_t i915_tiling =
               isl_tiling_to_i915_tiling(image->planes[0].surface.isl.tiling);
            int ret = anv_gem_set_tiling(device, mem->bo->gem_handle,
                                         image->planes[0].surface.isl.row_pitch,
                                         i915_tiling);
            if (ret) {
               anv_bo_cache_release(device, &device->bo_cache, mem->bo);
               return vk_errorf(device->instance, NULL,
                                VK_ERROR_OUT_OF_DEVICE_MEMORY,
                                "failed to set BO tiling: %m");
            }
         }
      }
   }

   assert(mem->type->heapIndex < pdevice->memory.heap_count);
   if (pdevice->memory.heaps[mem->type->heapIndex].supports_48bit_addresses)
      mem->bo->flags |= EXEC_OBJECT_SUPPORTS_48B_ADDRESS;

   const struct wsi_memory_allocate_info *wsi_info =
      vk_find_struct_const(pAllocateInfo->pNext, WSI_MEMORY_ALLOCATE_INFO_MESA);
   if (wsi_info && wsi_info->implicit_sync) {
      /* We need to set the WRITE flag on window system buffers so that GEM
       * will know we're writing to them and synchronize uses on other rings
       * (eg if the display server uses the blitter ring).
       */
      mem->bo->flags |= EXEC_OBJECT_WRITE;
   } else if (pdevice->has_exec_async) {
      mem->bo->flags |= EXEC_OBJECT_ASYNC;
   }

   *pMem = anv_device_memory_to_handle(mem);

   return VK_SUCCESS;

 fail:
   vk_free2(&device->alloc, pAllocator, mem);

   return result;
}

VkResult anv_GetMemoryFdKHR(
    VkDevice                                    device_h,
    const VkMemoryGetFdInfoKHR*                 pGetFdInfo,
    int*                                        pFd)
{
   ANV_FROM_HANDLE(anv_device, dev, device_h);
   ANV_FROM_HANDLE(anv_device_memory, mem, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   assert(pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR ||
          pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   return anv_bo_cache_export(dev, &dev->bo_cache, mem->bo, pFd);
}

VkResult anv_GetMemoryFdPropertiesKHR(
    VkDevice                                    _device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    int                                         fd,
    VkMemoryFdPropertiesKHR*                    pMemoryFdProperties)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_physical_device *pdevice = &device->instance->physicalDevice;

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      /* dma-buf can be imported as any memory type */
      pMemoryFdProperties->memoryTypeBits =
         (1 << pdevice->memory.type_count) - 1;
      return VK_SUCCESS;

   default:
      /* The valid usage section for this function says:
       *
       *    "handleType must not be one of the handle types defined as
       *    opaque."
       *
       * So opaque handle types fall into the default "unsupported" case.
       */
      return vk_error(VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR);
   }
}

void anv_FreeMemory(
    VkDevice                                    _device,
    VkDeviceMemory                              _mem,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   if (mem->map)
      anv_UnmapMemory(_device, _mem);

   anv_bo_cache_release(device, &device->bo_cache, mem->bo);

   vk_free2(&device->alloc, pAllocator, mem);
}

VkResult anv_MapMemory(
    VkDevice                                    _device,
    VkDeviceMemory                              _memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_device_memory, mem, _memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   if (size == VK_WHOLE_SIZE)
      size = mem->bo->size - offset;

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */
   assert(size > 0);
   assert(offset + size <= mem->bo->size);

   /* FIXME: Is this supposed to be thread safe? Since vkUnmapMemory() only
    * takes a VkDeviceMemory pointer, it seems like only one map of the memory
    * at a time is valid. We could just mmap up front and return an offset
    * pointer here, but that may exhaust virtual memory on 32 bit
    * userspace. */

   uint32_t gem_flags = 0;

   if (!device->info.has_llc &&
       (mem->type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
      gem_flags |= I915_MMAP_WC;

   /* GEM will fail to map if the offset isn't 4k-aligned.  Round down. */
   uint64_t map_offset = offset & ~4095ull;
   assert(offset >= map_offset);
   uint64_t map_size = (offset + size) - map_offset;

   /* Let's map whole pages */
   map_size = align_u64(map_size, 4096);

   void *map = anv_gem_mmap(device, mem->bo->gem_handle,
                            map_offset, map_size, gem_flags);
   if (map == MAP_FAILED)
      return vk_error(VK_ERROR_MEMORY_MAP_FAILED);

   mem->map = map;
   mem->map_size = map_size;

   *ppData = mem->map + (offset - map_offset);

   return VK_SUCCESS;
}

void anv_UnmapMemory(
    VkDevice                                    _device,
    VkDeviceMemory                              _memory)
{
   ANV_FROM_HANDLE(anv_device_memory, mem, _memory);

   if (mem == NULL)
      return;

   anv_gem_munmap(mem->map, mem->map_size);

   mem->map = NULL;
   mem->map_size = 0;
}

static void
clflush_mapped_ranges(struct anv_device         *device,
                      uint32_t                   count,
                      const VkMappedMemoryRange *ranges)
{
   for (uint32_t i = 0; i < count; i++) {
      ANV_FROM_HANDLE(anv_device_memory, mem, ranges[i].memory);
      if (ranges[i].offset >= mem->map_size)
         continue;

      gen_clflush_range(mem->map + ranges[i].offset,
                        MIN2(ranges[i].size, mem->map_size - ranges[i].offset));
   }
}

VkResult anv_FlushMappedMemoryRanges(
    VkDevice                                    _device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (device->info.has_llc)
      return VK_SUCCESS;

   /* Make sure the writes we're flushing have landed. */
   __builtin_ia32_mfence();

   clflush_mapped_ranges(device, memoryRangeCount, pMemoryRanges);

   return VK_SUCCESS;
}

VkResult anv_InvalidateMappedMemoryRanges(
    VkDevice                                    _device,
    uint32_t                                    memoryRangeCount,
    const VkMappedMemoryRange*                  pMemoryRanges)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   if (device->info.has_llc)
      return VK_SUCCESS;

   clflush_mapped_ranges(device, memoryRangeCount, pMemoryRanges);

   /* Make sure no reads get moved up above the invalidate. */
   __builtin_ia32_mfence();

   return VK_SUCCESS;
}

void anv_GetBufferMemoryRequirements(
    VkDevice                                    _device,
    VkBuffer                                    _buffer,
    VkMemoryRequirements*                       pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_physical_device *pdevice = &device->instance->physicalDevice;

   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    */
   uint32_t memory_types = 0;
   for (uint32_t i = 0; i < pdevice->memory.type_count; i++) {
      uint32_t valid_usage = pdevice->memory.types[i].valid_buffer_usage;
      if ((valid_usage & buffer->usage) == buffer->usage)
         memory_types |= (1u << i);
   }

   /* Base alignment requirement of a cache line */
   uint32_t alignment = 16;

   /* We need an alignment of 32 for pushing UBOs */
   if (buffer->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
      alignment = MAX2(alignment, 32);

   pMemoryRequirements->size = buffer->size;
   pMemoryRequirements->alignment = alignment;
   pMemoryRequirements->memoryTypeBits = memory_types;
}

void anv_GetBufferMemoryRequirements2KHR(
    VkDevice                                    _device,
    const VkBufferMemoryRequirementsInfo2KHR*   pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements)
{
   anv_GetBufferMemoryRequirements(_device, pInfo->buffer,
                                   &pMemoryRequirements->memoryRequirements);

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR: {
         VkMemoryDedicatedRequirementsKHR *requirements = (void *)ext;
         requirements->prefersDedicatedAllocation = VK_FALSE;
         requirements->requiresDedicatedAllocation = VK_FALSE;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void anv_GetImageMemoryRequirements(
    VkDevice                                    _device,
    VkImage                                     _image,
    VkMemoryRequirements*                       pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_image, image, _image);
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_physical_device *pdevice = &device->instance->physicalDevice;

   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    *
    * All types are currently supported for images.
    */
   uint32_t memory_types = (1ull << pdevice->memory.type_count) - 1;

   pMemoryRequirements->size = image->size;
   pMemoryRequirements->alignment = image->alignment;
   pMemoryRequirements->memoryTypeBits = memory_types;
}

void anv_GetImageMemoryRequirements2KHR(
    VkDevice                                    _device,
    const VkImageMemoryRequirementsInfo2KHR*    pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_image, image, pInfo->image);

   anv_GetImageMemoryRequirements(_device, pInfo->image,
                                  &pMemoryRequirements->memoryRequirements);

   vk_foreach_struct_const(ext, pInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO_KHR: {
         struct anv_physical_device *pdevice = &device->instance->physicalDevice;
         const VkImagePlaneMemoryRequirementsInfoKHR *plane_reqs =
            (const VkImagePlaneMemoryRequirementsInfoKHR *) ext;
         uint32_t plane = anv_image_aspect_to_plane(image->aspects,
                                                    plane_reqs->planeAspect);

         assert(image->planes[plane].offset == 0);

         /* The Vulkan spec (git aaed022) says:
          *
          *    memoryTypeBits is a bitfield and contains one bit set for every
          *    supported memory type for the resource. The bit `1<<i` is set
          *    if and only if the memory type `i` in the
          *    VkPhysicalDeviceMemoryProperties structure for the physical
          *    device is supported.
          *
          * All types are currently supported for images.
          */
         pMemoryRequirements->memoryRequirements.memoryTypeBits =
               (1ull << pdevice->memory.type_count) - 1;

         pMemoryRequirements->memoryRequirements.size = image->planes[plane].size;
         pMemoryRequirements->memoryRequirements.alignment =
            image->planes[plane].alignment;
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR: {
         VkMemoryDedicatedRequirementsKHR *requirements = (void *)ext;
         if (image->drm_format_mod != DRM_FORMAT_MOD_INVALID) {
            /* Require a dedicated allocation for images with modifiers.
             *
             * See also anv_AllocateMemory.
             */
            requirements->prefersDedicatedAllocation = VK_TRUE;
            requirements->requiresDedicatedAllocation = VK_TRUE;
         } else {
            requirements->prefersDedicatedAllocation = VK_FALSE;
            requirements->requiresDedicatedAllocation = VK_FALSE;
         }
         break;
      }

      default:
         anv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

void anv_GetImageSparseMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void anv_GetImageSparseMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2KHR* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2KHR*        pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void anv_GetDeviceMemoryCommitment(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize*                               pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

static void
anv_bind_buffer_memory(const VkBindBufferMemoryInfoKHR *pBindInfo)
{
   ANV_FROM_HANDLE(anv_device_memory, mem, pBindInfo->memory);
   ANV_FROM_HANDLE(anv_buffer, buffer, pBindInfo->buffer);

   assert(pBindInfo->sType == VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO_KHR);

   if (mem) {
      assert((buffer->usage & mem->type->valid_buffer_usage) == buffer->usage);
      buffer->bo = mem->bo;
      buffer->offset = pBindInfo->memoryOffset;
   } else {
      buffer->bo = NULL;
      buffer->offset = 0;
   }
}

VkResult anv_BindBufferMemory(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset)
{
   anv_bind_buffer_memory(
      &(VkBindBufferMemoryInfoKHR) {
         .sType         = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO_KHR,
         .buffer        = buffer,
         .memory        = memory,
         .memoryOffset  = memoryOffset,
      });

   return VK_SUCCESS;
}

VkResult anv_BindBufferMemory2KHR(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfoKHR*            pBindInfos)
{
   for (uint32_t i = 0; i < bindInfoCount; i++)
      anv_bind_buffer_memory(&pBindInfos[i]);

   return VK_SUCCESS;
}

VkResult anv_QueueBindSparse(
    VkQueue                                     _queue,
    uint32_t                                    bindInfoCount,
    const VkBindSparseInfo*                     pBindInfo,
    VkFence                                     fence)
{
   ANV_FROM_HANDLE(anv_queue, queue, _queue);
   if (unlikely(queue->device->lost))
      return VK_ERROR_DEVICE_LOST;

   return vk_error(VK_ERROR_FEATURE_NOT_PRESENT);
}

// Event functions

VkResult anv_CreateEvent(
    VkDevice                                    _device,
    const VkEventCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkEvent*                                    pEvent)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_state state;
   struct anv_event *event;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_EVENT_CREATE_INFO);

   state = anv_state_pool_alloc(&device->dynamic_state_pool,
                                sizeof(*event), 8);
   event = state.map;
   event->state = state;
   event->semaphore = VK_EVENT_RESET;

   if (!device->info.has_llc) {
      /* Make sure the writes we're flushing have landed. */
      __builtin_ia32_mfence();
      __builtin_ia32_clflush(event);
   }

   *pEvent = anv_event_to_handle(event);

   return VK_SUCCESS;
}

void anv_DestroyEvent(
    VkDevice                                    _device,
    VkEvent                                     _event,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_event, event, _event);

   if (!event)
      return;

   anv_state_pool_free(&device->dynamic_state_pool, event->state);
}

VkResult anv_GetEventStatus(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_event, event, _event);

   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   if (!device->info.has_llc) {
      /* Invalidate read cache before reading event written by GPU. */
      __builtin_ia32_clflush(event);
      __builtin_ia32_mfence();

   }

   return event->semaphore;
}

VkResult anv_SetEvent(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_event, event, _event);

   event->semaphore = VK_EVENT_SET;

   if (!device->info.has_llc) {
      /* Make sure the writes we're flushing have landed. */
      __builtin_ia32_mfence();
      __builtin_ia32_clflush(event);
   }

   return VK_SUCCESS;
}

VkResult anv_ResetEvent(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_event, event, _event);

   event->semaphore = VK_EVENT_RESET;

   if (!device->info.has_llc) {
      /* Make sure the writes we're flushing have landed. */
      __builtin_ia32_mfence();
      __builtin_ia32_clflush(event);
   }

   return VK_SUCCESS;
}

// Buffer functions

VkResult anv_CreateBuffer(
    VkDevice                                    _device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

   buffer = vk_alloc2(&device->alloc, pAllocator, sizeof(*buffer), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (buffer == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   buffer->size = pCreateInfo->size;
   buffer->usage = pCreateInfo->usage;
   buffer->bo = NULL;
   buffer->offset = 0;

   *pBuffer = anv_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

void anv_DestroyBuffer(
    VkDevice                                    _device,
    VkBuffer                                    _buffer,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   if (!buffer)
      return;

   vk_free2(&device->alloc, pAllocator, buffer);
}

void
anv_fill_buffer_surface_state(struct anv_device *device, struct anv_state state,
                              enum isl_format format,
                              uint32_t offset, uint32_t range, uint32_t stride)
{
   isl_buffer_fill_state(&device->isl_dev, state.map,
                         .address = offset,
                         .mocs = device->default_mocs,
                         .size = range,
                         .format = format,
                         .stride = stride);

   anv_state_flush(device, state);
}

void anv_DestroySampler(
    VkDevice                                    _device,
    VkSampler                                   _sampler,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_sampler, sampler, _sampler);

   if (!sampler)
      return;

   vk_free2(&device->alloc, pAllocator, sampler);
}

VkResult anv_CreateFramebuffer(
    VkDevice                                    _device,
    const VkFramebufferCreateInfo*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFramebuffer*                              pFramebuffer)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_framebuffer *framebuffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   size_t size = sizeof(*framebuffer) +
                 sizeof(struct anv_image_view *) * pCreateInfo->attachmentCount;
   framebuffer = vk_alloc2(&device->alloc, pAllocator, size, 8,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (framebuffer == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   framebuffer->attachment_count = pCreateInfo->attachmentCount;
   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      VkImageView _iview = pCreateInfo->pAttachments[i];
      framebuffer->attachments[i] = anv_image_view_from_handle(_iview);
   }

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;

   *pFramebuffer = anv_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;
}

void anv_DestroyFramebuffer(
    VkDevice                                    _device,
    VkFramebuffer                               _fb,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_framebuffer, fb, _fb);

   if (!fb)
      return;

   vk_free2(&device->alloc, pAllocator, fb);
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
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 3u);
   return VK_SUCCESS;
}
