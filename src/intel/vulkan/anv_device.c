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

#include "anv_private.h"
#include "util/strtod.h"
#include "util/debug.h"
#include "util/build_id.h"
#include "util/mesa-sha1.h"
#include "util/vk_util.h"

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
      vfprintf(stderr, fmt, args);

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
      anv_perf_warn("Failed to get I915_CONTEXT_PARAM_GTT_SIZE: %m");

      if (anv_gem_get_aperture(fd, &gtt_size) == -1) {
         return vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
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

static bool
anv_device_get_cache_uuid(void *uuid, uint16_t pci_id)
{
   const struct build_id_note *note = build_id_find_nhdr("libvulkan_intel.so");
   if (!note)
      return false;

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < 20) /* It should be a SHA-1 */
      return false;

   struct mesa_sha1 sha1_ctx;
   uint8_t sha1[20];
   STATIC_ASSERT(VK_UUID_SIZE <= sizeof(sha1));

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, build_id_data(note), build_id_len);
   _mesa_sha1_update(&sha1_ctx, &pci_id, sizeof(pci_id));
   _mesa_sha1_final(&sha1_ctx, sha1);

   memcpy(uuid, sha1, VK_UUID_SIZE);
   return true;
}

static VkResult
anv_physical_device_init(struct anv_physical_device *device,
                         struct anv_instance *instance,
                         const char *path)
{
   VkResult result;
   int fd;

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
      fprintf(stderr, "WARNING: Haswell Vulkan support is incomplete\n");
   } else if (device->info.gen == 7 && !device->info.is_baytrail) {
      fprintf(stderr, "WARNING: Ivy Bridge Vulkan support is incomplete\n");
   } else if (device->info.gen == 7 && device->info.is_baytrail) {
      fprintf(stderr, "WARNING: Bay Trail Vulkan support is incomplete\n");
   } else if (device->info.gen >= 8) {
      /* Broadwell, Cherryview, Skylake, Broxton, Kabylake is as fully
       * supported as anything */
   } else {
      result = vk_errorf(VK_ERROR_INCOMPATIBLE_DRIVER,
                         "Vulkan not yet supported on %s", device->name);
      goto fail;
   }

   device->cmd_parser_version = -1;
   if (device->info.gen == 7) {
      device->cmd_parser_version =
         anv_gem_get_param(fd, I915_PARAM_CMD_PARSER_VERSION);
      if (device->cmd_parser_version == -1) {
         result = vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
                            "failed to get command parser version");
         goto fail;
      }
   }

   if (!anv_gem_get_param(fd, I915_PARAM_HAS_WAIT_TIMEOUT)) {
      result = vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
                         "kernel missing gem wait");
      goto fail;
   }

   if (!anv_gem_get_param(fd, I915_PARAM_HAS_EXECBUF2)) {
      result = vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
                         "kernel missing execbuf2");
      goto fail;
   }

   if (!device->info.has_llc &&
       anv_gem_get_param(fd, I915_PARAM_MMAP_VERSION) < 1) {
      result = vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
                         "kernel missing wc mmap");
      goto fail;
   }

   result = anv_physical_device_init_heaps(device, fd);
   if (result != VK_SUCCESS)
      goto fail;

   device->has_exec_async = anv_gem_get_param(fd, I915_PARAM_HAS_EXEC_ASYNC);

   if (!anv_device_get_cache_uuid(device->uuid, device->chipset_id)) {
      result = vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
                         "cannot generate UUID");
      goto fail;
   }
   bool swizzled = anv_gem_get_bit6_swizzle(fd, I915_TILING_X);

   /* GENs prior to 8 do not support EU/Subslice info */
   if (device->info.gen >= 8) {
      device->subslice_total = anv_gem_get_param(fd, I915_PARAM_SUBSLICE_TOTAL);
      device->eu_total = anv_gem_get_param(fd, I915_PARAM_EU_TOTAL);

      /* Without this information, we cannot get the right Braswell
       * brandstrings, and we have to use conservative numbers for GPGPU on
       * many platforms, but otherwise, things will just work.
       */
      if (device->subslice_total < 1 || device->eu_total < 1) {
         fprintf(stderr, "WARNING: Kernel 4.1 required to properly"
                         " query GPU properties.\n");
      }
   } else if (device->info.gen == 7) {
      device->subslice_total = 1 << (device->info.gt - 1);
   }

   if (device->info.is_cherryview &&
       device->subslice_total > 0 && device->eu_total > 0) {
      /* Logical CS threads = EUs per subslice * 7 threads per EU */
      uint32_t max_cs_threads = device->eu_total / device->subslice_total * 7;

      /* Fuse configurations may give more threads than expected, never less. */
      if (max_cs_threads > device->info.max_cs_threads)
         device->info.max_cs_threads = max_cs_threads;
   }

   brw_process_intel_debug_variable();

   device->compiler = brw_compiler_create(NULL, &device->info);
   if (device->compiler == NULL) {
      result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail;
   }
   device->compiler->shader_debug_log = compiler_debug_log;
   device->compiler->shader_perf_log = compiler_perf_log;

   result = anv_init_wsi(device);
   if (result != VK_SUCCESS) {
      ralloc_free(device->compiler);
      goto fail;
   }

   isl_device_init(&device->isl_dev, &device->info, swizzled);

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

static const VkExtensionProperties global_extensions[] = {
   {
      .extensionName = VK_KHR_SURFACE_EXTENSION_NAME,
      .specVersion = 25,
   },
#ifdef VK_USE_PLATFORM_XCB_KHR
   {
      .extensionName = VK_KHR_XCB_SURFACE_EXTENSION_NAME,
      .specVersion = 6,
   },
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   {
      .extensionName = VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
      .specVersion = 6,
   },
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   {
      .extensionName = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
      .specVersion = 5,
   },
#endif
   {
      .extensionName = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      .specVersion = 1,
   },
};

static const VkExtensionProperties device_extensions[] = {
   {
      .extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      .specVersion = 68,
   },
   {
      .extensionName = VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
      .specVersion = 1,
   },
   {
      .extensionName = VK_KHR_MAINTENANCE1_EXTENSION_NAME,
      .specVersion = 1,
   },
   {
      .extensionName = VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
      .specVersion = 1,
   },
   {
      .extensionName = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
      .specVersion = 1,
   },
   {
      .extensionName = VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
      .specVersion = 1,
   },
   {
      .extensionName = VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME,
      .specVersion = 1,
   },
};

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

VkResult anv_CreateInstance(
    const VkInstanceCreateInfo*                 pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkInstance*                                 pInstance)
{
   struct anv_instance *instance;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   uint32_t client_version;
   if (pCreateInfo->pApplicationInfo &&
       pCreateInfo->pApplicationInfo->apiVersion != 0) {
      client_version = pCreateInfo->pApplicationInfo->apiVersion;
   } else {
      client_version = VK_MAKE_VERSION(1, 0, 0);
   }

   if (VK_MAKE_VERSION(1, 0, 0) > client_version ||
       client_version > VK_MAKE_VERSION(1, 0, 0xfff)) {
      return vk_errorf(VK_ERROR_INCOMPATIBLE_DRIVER,
                       "Client requested version %d.%d.%d",
                       VK_VERSION_MAJOR(client_version),
                       VK_VERSION_MINOR(client_version),
                       VK_VERSION_PATCH(client_version));
   }

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      bool found = false;
      for (uint32_t j = 0; j < ARRAY_SIZE(global_extensions); j++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    global_extensions[j].extensionName) == 0) {
            found = true;
            break;
         }
      }
      if (!found)
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);
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
   instance->physicalDeviceCount = -1;

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
      .multiDrawIndirect                        = false,
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
      .maxPerStageDescriptorSamplers            = 64,
      .maxPerStageDescriptorUniformBuffers      = 64,
      .maxPerStageDescriptorStorageBuffers      = 64,
      .maxPerStageDescriptorSampledImages       = 64,
      .maxPerStageDescriptorStorageImages       = 64,
      .maxPerStageDescriptorInputAttachments    = 64,
      .maxPerStageResources                     = 128,
      .maxDescriptorSetSamplers                 = 256,
      .maxDescriptorSetUniformBuffers           = 256,
      .maxDescriptorSetUniformBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetStorageBuffers           = 256,
      .maxDescriptorSetStorageBuffersDynamic    = MAX_DYNAMIC_BUFFERS / 2,
      .maxDescriptorSetSampledImages            = 256,
      .maxDescriptorSetStorageImages            = 256,
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
      .minUniformBufferOffsetAlignment          = 16,
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
      .timestampPeriod                          = devinfo->timebase_scale,
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
      .apiVersion = VK_MAKE_VERSION(1, 0, 42),
      .driverVersion = 1,
      .vendorID = 0x8086,
      .deviceID = pdevice->chipset_id,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
      .limits = limits,
      .sparseProperties = {0}, /* Broadwell doesn't do sparse. */
   };

   strcpy(pProperties->deviceName, pdevice->name);
   memcpy(pProperties->pipelineCacheUUID, pdevice->uuid, VK_UUID_SIZE);
}

void anv_GetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties2KHR*             pProperties)
{
   anv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   vk_foreach_struct(ext, pProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *) ext;

         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
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
    VkInstance                                  instance,
    const char*                                 pName)
{
   return anv_lookup_entrypoint(NULL, pName);
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
   return anv_lookup_entrypoint(&device->info, pName);
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

   anv_state_flush(pool->block_pool->device, state);

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

VkResult
anv_device_submit_simple_batch(struct anv_device *device,
                               struct anv_batch *batch)
{
   struct drm_i915_gem_execbuffer2 execbuf;
   struct drm_i915_gem_exec_object2 exec2_objects[1];
   struct anv_bo bo, *exec_bos[1];
   VkResult result = VK_SUCCESS;
   uint32_t size;

   /* Kernel driver requires 8 byte aligned batch length */
   size = align_u32(batch->next - batch->start, 8);
   result = anv_bo_pool_alloc(&device->batch_bo_pool, &bo, size);
   if (result != VK_SUCCESS)
      return result;

   memcpy(bo.map, batch->start, size);
   if (!device->info.has_llc)
      anv_flush_range(bo.map, size);

   exec_bos[0] = &bo;
   exec2_objects[0].handle = bo.gem_handle;
   exec2_objects[0].relocation_count = 0;
   exec2_objects[0].relocs_ptr = 0;
   exec2_objects[0].alignment = 0;
   exec2_objects[0].offset = bo.offset;
   exec2_objects[0].flags = 0;
   exec2_objects[0].rsvd1 = 0;
   exec2_objects[0].rsvd2 = 0;

   execbuf.buffers_ptr = (uintptr_t) exec2_objects;
   execbuf.buffer_count = 1;
   execbuf.batch_start_offset = 0;
   execbuf.batch_len = size;
   execbuf.cliprects_ptr = 0;
   execbuf.num_cliprects = 0;
   execbuf.DR1 = 0;
   execbuf.DR4 = 0;

   execbuf.flags =
      I915_EXEC_HANDLE_LUT | I915_EXEC_NO_RELOC | I915_EXEC_RENDER;
   execbuf.rsvd1 = device->context_id;
   execbuf.rsvd2 = 0;

   result = anv_device_execbuf(device, &execbuf, exec_bos);
   if (result != VK_SUCCESS)
      goto fail;

   result = anv_device_wait(device, &bo, INT64_MAX);

 fail:
   anv_bo_pool_free(&device->batch_bo_pool, &bo);

   return result;
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

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      bool found = false;
      for (uint32_t j = 0; j < ARRAY_SIZE(device_extensions); j++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    device_extensions[j].extensionName) == 0) {
            found = true;
            break;
         }
      }
      if (!found)
         return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);
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

   anv_bo_pool_init(&device->batch_bo_pool, device);

   result = anv_block_pool_init(&device->dynamic_state_block_pool, device,
                                16384);
   if (result != VK_SUCCESS)
      goto fail_batch_bo_pool;

   anv_state_pool_init(&device->dynamic_state_pool,
                       &device->dynamic_state_block_pool);

   result = anv_block_pool_init(&device->instruction_block_pool, device,
                                1024 * 1024);
   if (result != VK_SUCCESS)
      goto fail_dynamic_state_pool;

   anv_state_pool_init(&device->instruction_state_pool,
                       &device->instruction_block_pool);

   result = anv_block_pool_init(&device->surface_state_block_pool, device,
                                4096);
   if (result != VK_SUCCESS)
      goto fail_instruction_state_pool;

   anv_state_pool_init(&device->surface_state_pool,
                       &device->surface_state_block_pool);

   result = anv_bo_init_new(&device->workaround_bo, device, 1024);
   if (result != VK_SUCCESS)
      goto fail_surface_state_pool;

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
   anv_block_pool_finish(&device->surface_state_block_pool);
 fail_instruction_state_pool:
   anv_state_pool_finish(&device->instruction_state_pool);
   anv_block_pool_finish(&device->instruction_block_pool);
 fail_dynamic_state_pool:
   anv_state_pool_finish(&device->dynamic_state_pool);
   anv_block_pool_finish(&device->dynamic_state_block_pool);
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

   anv_state_pool_finish(&device->surface_state_pool);
   anv_block_pool_finish(&device->surface_state_block_pool);
   anv_state_pool_finish(&device->instruction_state_pool);
   anv_block_pool_finish(&device->instruction_block_pool);
   anv_state_pool_finish(&device->dynamic_state_pool);
   anv_block_pool_finish(&device->dynamic_state_block_pool);

   anv_bo_pool_finish(&device->batch_bo_pool);

   pthread_cond_destroy(&device->queue_submit);
   pthread_mutex_destroy(&device->mutex);

   anv_gem_destroy_context(device, device->context_id);

   close(device->fd);

   vk_free(&device->alloc, device);
}

VkResult anv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = ARRAY_SIZE(global_extensions);
      return VK_SUCCESS;
   }

   *pPropertyCount = MIN2(*pPropertyCount, ARRAY_SIZE(global_extensions));
   typed_memcpy(pProperties, global_extensions, *pPropertyCount);

   if (*pPropertyCount < ARRAY_SIZE(global_extensions))
      return VK_INCOMPLETE;

   return VK_SUCCESS;
}

VkResult anv_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = ARRAY_SIZE(device_extensions);
      return VK_SUCCESS;
   }

   *pPropertyCount = MIN2(*pPropertyCount, ARRAY_SIZE(device_extensions));
   typed_memcpy(pProperties, device_extensions, *pPropertyCount);

   if (*pPropertyCount < ARRAY_SIZE(device_extensions))
      return VK_INCOMPLETE;

   return VK_SUCCESS;
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
anv_device_execbuf(struct anv_device *device,
                   struct drm_i915_gem_execbuffer2 *execbuf,
                   struct anv_bo **execbuf_bos)
{
   int ret = anv_gem_execbuffer(device, execbuf);
   if (ret != 0) {
      /* We don't know the real error. */
      device->lost = true;
      return vk_errorf(VK_ERROR_DEVICE_LOST, "execbuf2 failed: %m");
   }

   struct drm_i915_gem_exec_object2 *objects =
      (void *)(uintptr_t)execbuf->buffers_ptr;
   for (uint32_t k = 0; k < execbuf->buffer_count; k++)
      execbuf_bos[k]->offset = objects[k].offset;

   return VK_SUCCESS;
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
      return vk_errorf(VK_ERROR_DEVICE_LOST, "get_reset_stats failed: %m");
   }

   if (active) {
      device->lost = true;
      return vk_errorf(VK_ERROR_DEVICE_LOST,
                       "GPU hung on one of our command buffers");
   } else if (pending) {
      device->lost = true;
      return vk_errorf(VK_ERROR_DEVICE_LOST,
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
      return vk_errorf(VK_ERROR_DEVICE_LOST, "gem wait failed: %m");
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
      return vk_errorf(VK_ERROR_DEVICE_LOST, "gem wait failed: %m");
   }

   /* Query for device status after the wait.  If the BO we're waiting on got
    * caught in a GPU hang we don't want to return VK_SUCCESS to the client
    * because it clearly doesn't have valid data.  Yes, this most likely means
    * an ioctl, but we just did an ioctl to wait so it's no great loss.
    */
   return anv_device_query_status(device);
}

VkResult anv_QueueSubmit(
    VkQueue                                     _queue,
    uint32_t                                    submitCount,
    const VkSubmitInfo*                         pSubmits,
    VkFence                                     _fence)
{
   ANV_FROM_HANDLE(anv_queue, queue, _queue);
   ANV_FROM_HANDLE(anv_fence, fence, _fence);
   struct anv_device *device = queue->device;

   /* Query for device status prior to submitting.  Technically, we don't need
    * to do this.  However, if we have a client that's submitting piles of
    * garbage, we would rather break as early as possible to keep the GPU
    * hanging contained.  If we don't check here, we'll either be waiting for
    * the kernel to kick us or we'll have to wait until the client waits on a
    * fence before we actually know whether or not we've hung.
    */
   VkResult result = anv_device_query_status(device);
   if (result != VK_SUCCESS)
      return result;

   /* We lock around QueueSubmit for three main reasons:
    *
    *  1) When a block pool is resized, we create a new gem handle with a
    *     different size and, in the case of surface states, possibly a
    *     different center offset but we re-use the same anv_bo struct when
    *     we do so.  If this happens in the middle of setting up an execbuf,
    *     we could end up with our list of BOs out of sync with our list of
    *     gem handles.
    *
    *  2) The algorithm we use for building the list of unique buffers isn't
    *     thread-safe.  While the client is supposed to syncronize around
    *     QueueSubmit, this would be extremely difficult to debug if it ever
    *     came up in the wild due to a broken app.  It's better to play it
    *     safe and just lock around QueueSubmit.
    *
    *  3)  The anv_cmd_buffer_execbuf function may perform relocations in
    *      userspace.  Due to the fact that the surface state buffer is shared
    *      between batches, we can't afford to have that happen from multiple
    *      threads at the same time.  Even though the user is supposed to
    *      ensure this doesn't happen, we play it safe as in (2) above.
    *
    * Since the only other things that ever take the device lock such as block
    * pool resize only rarely happen, this will almost never be contended so
    * taking a lock isn't really an expensive operation in this case.
    */
   pthread_mutex_lock(&device->mutex);

   for (uint32_t i = 0; i < submitCount; i++) {
      for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j++) {
         ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer,
                         pSubmits[i].pCommandBuffers[j]);
         assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);
         assert(!anv_batch_has_error(&cmd_buffer->batch));

         result = anv_cmd_buffer_execbuf(device, cmd_buffer);
         if (result != VK_SUCCESS)
            goto out;
      }
   }

   if (fence) {
      struct anv_bo *fence_bo = &fence->bo;
      result = anv_device_execbuf(device, &fence->execbuf, &fence_bo);
      if (result != VK_SUCCESS)
         goto out;

      /* Update the fence and wake up any waiters */
      assert(fence->state == ANV_FENCE_STATE_RESET);
      fence->state = ANV_FENCE_STATE_SUBMITTED;
      pthread_cond_broadcast(&device->queue_submit);
   }

out:
   if (result != VK_SUCCESS) {
      /* In the case that something has gone wrong we may end up with an
       * inconsistent state from which it may not be trivial to recover.
       * For example, we might have computed address relocations and
       * any future attempt to re-submit this job will need to know about
       * this and avoid computing relocation addresses again.
       *
       * To avoid this sort of issues, we assume that if something was
       * wrong during submission we must already be in a really bad situation
       * anyway (such us being out of memory) and return
       * VK_ERROR_DEVICE_LOST to ensure that clients do not attempt to
       * submit the same job again to this device.
       */
      result = VK_ERROR_DEVICE_LOST;
      device->lost = true;

      /* If we return VK_ERROR_DEVICE LOST here, we need to ensure that
       * vkWaitForFences() and vkGetFenceStatus() return a valid result
       * (VK_SUCCESS or VK_ERROR_DEVICE_LOST) in a finite amount of time.
       * Setting the fence status to SIGNALED ensures this will happen in
       * any case.
       */
      if (fence)
         fence->state = ANV_FENCE_STATE_SIGNALED;
   }

   pthread_mutex_unlock(&device->mutex);

   return result;
}

VkResult anv_QueueWaitIdle(
    VkQueue                                     _queue)
{
   ANV_FROM_HANDLE(anv_queue, queue, _queue);

   return anv_DeviceWaitIdle(anv_device_to_handle(queue->device));
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
   VkResult result;

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

   /* The kernel is going to give us whole pages anyway */
   uint64_t alloc_size = align_u64(pAllocateInfo->allocationSize, 4096);

   result = anv_bo_init_new(&mem->bo, device, alloc_size);
   if (result != VK_SUCCESS)
      goto fail;

   assert(pAllocateInfo->memoryTypeIndex < pdevice->memory.type_count);
   mem->type = &pdevice->memory.types[pAllocateInfo->memoryTypeIndex];

   mem->map = NULL;
   mem->map_size = 0;

   assert(mem->type->heapIndex < pdevice->memory.heap_count);
   if (pdevice->memory.heaps[mem->type->heapIndex].supports_48bit_addresses)
      mem->bo.flags |= EXEC_OBJECT_SUPPORTS_48B_ADDRESS;

   if (pdevice->has_exec_async)
      mem->bo.flags |= EXEC_OBJECT_ASYNC;

   *pMem = anv_device_memory_to_handle(mem);

   return VK_SUCCESS;

 fail:
   vk_free2(&device->alloc, pAllocator, mem);

   return result;
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

   if (mem->bo.map)
      anv_gem_munmap(mem->bo.map, mem->bo.size);

   if (mem->bo.gem_handle != 0)
      anv_gem_close(device, mem->bo.gem_handle);

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
      size = mem->bo.size - offset;

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */
   assert(size > 0);
   assert(offset + size <= mem->bo.size);

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

   void *map = anv_gem_mmap(device, mem->bo.gem_handle,
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

      anv_clflush_range(mem->map + ranges[i].offset,
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

   pMemoryRequirements->size = buffer->size;
   pMemoryRequirements->alignment = 16;
   pMemoryRequirements->memoryTypeBits = memory_types;
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

void anv_GetImageSparseMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
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

VkResult anv_BindBufferMemory(
    VkDevice                                    device,
    VkBuffer                                    _buffer,
    VkDeviceMemory                              _memory,
    VkDeviceSize                                memoryOffset)
{
   ANV_FROM_HANDLE(anv_device_memory, mem, _memory);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   if (mem) {
      assert((buffer->usage & mem->type->valid_buffer_usage) == buffer->usage);
      buffer->bo = &mem->bo;
      buffer->offset = memoryOffset;
   } else {
      buffer->bo = NULL;
      buffer->offset = 0;
   }

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

VkResult anv_CreateFence(
    VkDevice                                    _device,
    const VkFenceCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_bo fence_bo;
   struct anv_fence *fence;
   struct anv_batch batch;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);

   result = anv_bo_pool_alloc(&device->batch_bo_pool, &fence_bo, 4096);
   if (result != VK_SUCCESS)
      return result;

   /* Fences are small.  Just store the CPU data structure in the BO. */
   fence = fence_bo.map;
   fence->bo = fence_bo;

   /* Place the batch after the CPU data but on its own cache line. */
   const uint32_t batch_offset = align_u32(sizeof(*fence), CACHELINE_SIZE);
   batch.next = batch.start = fence->bo.map + batch_offset;
   batch.end = fence->bo.map + fence->bo.size;
   anv_batch_emit(&batch, GEN7_MI_BATCH_BUFFER_END, bbe);
   anv_batch_emit(&batch, GEN7_MI_NOOP, noop);

   if (!device->info.has_llc) {
      assert(((uintptr_t) batch.start & CACHELINE_MASK) == 0);
      assert(batch.next - batch.start <= CACHELINE_SIZE);
      __builtin_ia32_mfence();
      __builtin_ia32_clflush(batch.start);
   }

   fence->exec2_objects[0].handle = fence->bo.gem_handle;
   fence->exec2_objects[0].relocation_count = 0;
   fence->exec2_objects[0].relocs_ptr = 0;
   fence->exec2_objects[0].alignment = 0;
   fence->exec2_objects[0].offset = fence->bo.offset;
   fence->exec2_objects[0].flags = 0;
   fence->exec2_objects[0].rsvd1 = 0;
   fence->exec2_objects[0].rsvd2 = 0;

   fence->execbuf.buffers_ptr = (uintptr_t) fence->exec2_objects;
   fence->execbuf.buffer_count = 1;
   fence->execbuf.batch_start_offset = batch.start - fence->bo.map;
   fence->execbuf.batch_len = batch.next - batch.start;
   fence->execbuf.cliprects_ptr = 0;
   fence->execbuf.num_cliprects = 0;
   fence->execbuf.DR1 = 0;
   fence->execbuf.DR4 = 0;

   fence->execbuf.flags =
      I915_EXEC_HANDLE_LUT | I915_EXEC_NO_RELOC | I915_EXEC_RENDER;
   fence->execbuf.rsvd1 = device->context_id;
   fence->execbuf.rsvd2 = 0;

   if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) {
      fence->state = ANV_FENCE_STATE_SIGNALED;
   } else {
      fence->state = ANV_FENCE_STATE_RESET;
   }

   *pFence = anv_fence_to_handle(fence);

   return VK_SUCCESS;
}

void anv_DestroyFence(
    VkDevice                                    _device,
    VkFence                                     _fence,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_fence, fence, _fence);

   if (!fence)
      return;

   assert(fence->bo.map == fence);
   anv_bo_pool_free(&device->batch_bo_pool, &fence->bo);
}

VkResult anv_ResetFences(
    VkDevice                                    _device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences)
{
   for (uint32_t i = 0; i < fenceCount; i++) {
      ANV_FROM_HANDLE(anv_fence, fence, pFences[i]);
      fence->state = ANV_FENCE_STATE_RESET;
   }

   return VK_SUCCESS;
}

VkResult anv_GetFenceStatus(
    VkDevice                                    _device,
    VkFence                                     _fence)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_fence, fence, _fence);

   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   switch (fence->state) {
   case ANV_FENCE_STATE_RESET:
      /* If it hasn't even been sent off to the GPU yet, it's not ready */
      return VK_NOT_READY;

   case ANV_FENCE_STATE_SIGNALED:
      /* It's been signaled, return success */
      return VK_SUCCESS;

   case ANV_FENCE_STATE_SUBMITTED: {
      VkResult result = anv_device_bo_busy(device, &fence->bo);
      if (result == VK_SUCCESS) {
         fence->state = ANV_FENCE_STATE_SIGNALED;
         return VK_SUCCESS;
      } else {
         return result;
      }
   }
   default:
      unreachable("Invalid fence status");
   }
}

#define NSEC_PER_SEC 1000000000
#define INT_TYPE_MAX(type) ((1ull << (sizeof(type) * 8 - 1)) - 1)

VkResult anv_WaitForFences(
    VkDevice                                    _device,
    uint32_t                                    fenceCount,
    const VkFence*                              pFences,
    VkBool32                                    waitAll,
    uint64_t                                    _timeout)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   int ret;

   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   /* DRM_IOCTL_I915_GEM_WAIT uses a signed 64 bit timeout and is supposed
    * to block indefinitely timeouts <= 0.  Unfortunately, this was broken
    * for a couple of kernel releases.  Since there's no way to know
    * whether or not the kernel we're using is one of the broken ones, the
    * best we can do is to clamp the timeout to INT64_MAX.  This limits the
    * maximum timeout from 584 years to 292 years - likely not a big deal.
    */
   int64_t timeout = MIN2(_timeout, INT64_MAX);

   VkResult result = VK_SUCCESS;
   uint32_t pending_fences = fenceCount;
   while (pending_fences) {
      pending_fences = 0;
      bool signaled_fences = false;
      for (uint32_t i = 0; i < fenceCount; i++) {
         ANV_FROM_HANDLE(anv_fence, fence, pFences[i]);
         switch (fence->state) {
         case ANV_FENCE_STATE_RESET:
            /* This fence hasn't been submitted yet, we'll catch it the next
             * time around.  Yes, this may mean we dead-loop but, short of
             * lots of locking and a condition variable, there's not much that
             * we can do about that.
             */
            pending_fences++;
            continue;

         case ANV_FENCE_STATE_SIGNALED:
            /* This fence is not pending.  If waitAll isn't set, we can return
             * early.  Otherwise, we have to keep going.
             */
            if (!waitAll) {
               result = VK_SUCCESS;
               goto done;
            }
            continue;

         case ANV_FENCE_STATE_SUBMITTED:
            /* These are the fences we really care about.  Go ahead and wait
             * on it until we hit a timeout.
             */
            result = anv_device_wait(device, &fence->bo, timeout);
            switch (result) {
            case VK_SUCCESS:
               fence->state = ANV_FENCE_STATE_SIGNALED;
               signaled_fences = true;
               if (!waitAll)
                  goto done;
               break;

            case VK_TIMEOUT:
               goto done;

            default:
               return result;
            }
         }
      }

      if (pending_fences && !signaled_fences) {
         /* If we've hit this then someone decided to vkWaitForFences before
          * they've actually submitted any of them to a queue.  This is a
          * fairly pessimal case, so it's ok to lock here and use a standard
          * pthreads condition variable.
          */
         pthread_mutex_lock(&device->mutex);

         /* It's possible that some of the fences have changed state since the
          * last time we checked.  Now that we have the lock, check for
          * pending fences again and don't wait if it's changed.
          */
         uint32_t now_pending_fences = 0;
         for (uint32_t i = 0; i < fenceCount; i++) {
            ANV_FROM_HANDLE(anv_fence, fence, pFences[i]);
            if (fence->state == ANV_FENCE_STATE_RESET)
               now_pending_fences++;
         }
         assert(now_pending_fences <= pending_fences);

         if (now_pending_fences == pending_fences) {
            struct timespec before;
            clock_gettime(CLOCK_MONOTONIC, &before);

            uint32_t abs_nsec = before.tv_nsec + timeout % NSEC_PER_SEC;
            uint64_t abs_sec = before.tv_sec + (abs_nsec / NSEC_PER_SEC) +
                               (timeout / NSEC_PER_SEC);
            abs_nsec %= NSEC_PER_SEC;

            /* Avoid roll-over in tv_sec on 32-bit systems if the user
             * provided timeout is UINT64_MAX
             */
            struct timespec abstime;
            abstime.tv_nsec = abs_nsec;
            abstime.tv_sec = MIN2(abs_sec, INT_TYPE_MAX(abstime.tv_sec));

            ret = pthread_cond_timedwait(&device->queue_submit,
                                         &device->mutex, &abstime);
            assert(ret != EINVAL);

            struct timespec after;
            clock_gettime(CLOCK_MONOTONIC, &after);
            uint64_t time_elapsed =
               ((uint64_t)after.tv_sec * NSEC_PER_SEC + after.tv_nsec) -
               ((uint64_t)before.tv_sec * NSEC_PER_SEC + before.tv_nsec);

            if (time_elapsed >= timeout) {
               pthread_mutex_unlock(&device->mutex);
               result = VK_TIMEOUT;
               goto done;
            }

            timeout -= time_elapsed;
         }

         pthread_mutex_unlock(&device->mutex);
      }
   }

done:
   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   return result;
}

// Queue semaphore functions

VkResult anv_CreateSemaphore(
    VkDevice                                    device,
    const VkSemaphoreCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSemaphore*                                pSemaphore)
{
   /* The DRM execbuffer ioctl always execute in-oder, even between different
    * rings. As such, there's nothing to do for the user space semaphore.
    */

   *pSemaphore = (VkSemaphore)1;

   return VK_SUCCESS;
}

void anv_DestroySemaphore(
    VkDevice                                    device,
    VkSemaphore                                 semaphore,
    const VkAllocationCallbacks*                pAllocator)
{
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
