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

#include <dlfcn.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"
#include "util/strtod.h"
#include "util/debug.h"

#include "genxml/gen7_pack.h"

struct anv_dispatch_table dtable;

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

static bool
anv_get_function_timestamp(void *ptr, uint32_t* timestamp)
{
   Dl_info info;
   struct stat st;
   if (!dladdr(ptr, &info) || !info.dli_fname)
      return false;

   if (stat(info.dli_fname, &st))
      return false;

   *timestamp = st.st_mtim.tv_sec;
   return true;
}

static bool
anv_device_get_cache_uuid(void *uuid)
{
   uint32_t timestamp;

   memset(uuid, 0, VK_UUID_SIZE);
   if (!anv_get_function_timestamp(anv_device_get_cache_uuid, &timestamp))
      return false;

   snprintf(uuid, VK_UUID_SIZE, "anv-%d", timestamp);
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

   if (anv_gem_get_aperture(fd, &device->aperture_size) == -1) {
      result = vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
                         "failed to get aperture size: %m");
      goto fail;
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

   if (!anv_device_get_cache_uuid(device->uuid)) {
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

   close(fd);
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
};

static const VkExtensionProperties device_extensions[] = {
   {
      .extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      .specVersion = 68,
   },
   {
      .extensionName = VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
      .specVersion = 1,
   }
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

VkResult anv_EnumeratePhysicalDevices(
    VkInstance                                  _instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices)
{
   ANV_FROM_HANDLE(anv_instance, instance, _instance);
   VkResult result;

   if (instance->physicalDeviceCount < 0) {
      char path[20];
      for (unsigned i = 0; i < 8; i++) {
         snprintf(path, sizeof(path), "/dev/dri/renderD%d", 128 + i);
         result = anv_physical_device_init(&instance->physicalDevice,
                                           instance, path);
         if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
            break;
      }

      if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
         instance->physicalDeviceCount = 0;
      } else if (result == VK_SUCCESS) {
         instance->physicalDeviceCount = 1;
      } else {
         return result;
      }
   }

   /* pPhysicalDeviceCount is an out parameter if pPhysicalDevices is NULL;
    * otherwise it's an inout parameter.
    *
    * The Vulkan spec (git aaed022) says:
    *
    *    pPhysicalDeviceCount is a pointer to an unsigned integer variable
    *    that is initialized with the number of devices the application is
    *    prepared to receive handles to. pname:pPhysicalDevices is pointer to
    *    an array of at least this many VkPhysicalDevice handles [...].
    *
    *    Upon success, if pPhysicalDevices is NULL, vkEnumeratePhysicalDevices
    *    overwrites the contents of the variable pointed to by
    *    pPhysicalDeviceCount with the number of physical devices in in the
    *    instance; otherwise, vkEnumeratePhysicalDevices overwrites
    *    pPhysicalDeviceCount with the number of physical handles written to
    *    pPhysicalDevices.
    */
   if (!pPhysicalDevices) {
      *pPhysicalDeviceCount = instance->physicalDeviceCount;
   } else if (*pPhysicalDeviceCount >= 1) {
      pPhysicalDevices[0] = anv_physical_device_to_handle(&instance->physicalDevice);
      *pPhysicalDeviceCount = 1;
   } else if (*pPhysicalDeviceCount < instance->physicalDeviceCount) {
      return VK_INCOMPLETE;
   } else {
      *pPhysicalDeviceCount = 0;
   }

   return VK_SUCCESS;
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
      .pipelineStatisticsQuery                  = false,
      .fragmentStoresAndAtomics                 = true,
      .shaderTessellationAndGeometryPointSize   = true,
      .shaderImageGatherExtended                = true,
      .shaderStorageImageExtendedFormats        = true,
      .shaderStorageImageMultisample            = false,
      .shaderStorageImageReadWithoutFormat      = false,
      .shaderStorageImageWriteWithoutFormat     = false,
      .shaderUniformBufferArrayDynamicIndexing  = true,
      .shaderSampledImageArrayDynamicIndexing   = true,
      .shaderStorageBufferArrayDynamicIndexing  = true,
      .shaderStorageImageArrayDynamicIndexing   = true,
      .shaderClipDistance                       = true,
      .shaderCullDistance                       = true,
      .shaderFloat64                            = pdevice->info.gen >= 8,
      .shaderInt64                              = false,
      .shaderInt16                              = false,
      .shaderResourceMinLod                     = false,
      .variableMultisampleRate                  = false,
      .inheritedQueries                         = false,
   };

   /* We can't do image stores in vec4 shaders */
   pFeatures->vertexPipelineStoresAndAtomics =
      pdevice->compiler->scalar_stage[MESA_SHADER_VERTEX] &&
      pdevice->compiler->scalar_stage[MESA_SHADER_GEOMETRY];
}

void anv_GetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, pdevice, physicalDevice);
   const struct gen_device_info *devinfo = &pdevice->info;

   const float time_stamp_base = devinfo->gen >= 9 ? 83.333 : 80.0;

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
      .maxVertexInputAttributes                 = 32,
      .maxVertexInputBindings                   = 32,
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
      .timestampPeriod                          = time_stamp_base,
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
      .apiVersion = VK_MAKE_VERSION(1, 0, 5),
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

void anv_GetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
   if (pQueueFamilyProperties == NULL) {
      *pCount = 1;
      return;
   }

   /* The spec implicitly allows the incoming count to be 0. From the Vulkan
    * 1.0.38 spec, Section 4.1 Physical Devices:
    *
    *     If the value referenced by pQueueFamilyPropertyCount is not 0 [then
    *     do stuff].
    */
   if (*pCount == 0)
      return;

   *pQueueFamilyProperties = (VkQueueFamilyProperties) {
      .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                    VK_QUEUE_COMPUTE_BIT |
                    VK_QUEUE_TRANSFER_BIT,
      .queueCount = 1,
      .timestampValidBits = 36, /* XXX: Real value here */
      .minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
   };

   *pCount = 1;
}

void anv_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties)
{
   ANV_FROM_HANDLE(anv_physical_device, physical_device, physicalDevice);
   VkDeviceSize heap_size;

   /* Reserve some wiggle room for the driver by exposing only 75% of the
    * aperture to the heap.
    */
   heap_size = 3 * physical_device->aperture_size / 4;

   if (physical_device->info.has_llc) {
      /* Big core GPUs share LLC with the CPU and thus one memory type can be
       * both cached and coherent at the same time.
       */
      pMemoryProperties->memoryTypeCount = 1;
      pMemoryProperties->memoryTypes[0] = (VkMemoryType) {
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                          VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
         .heapIndex = 0,
      };
   } else {
      /* The spec requires that we expose a host-visible, coherent memory
       * type, but Atom GPUs don't share LLC. Thus we offer two memory types
       * to give the application a choice between cached, but not coherent and
       * coherent but uncached (WC though).
       */
      pMemoryProperties->memoryTypeCount = 2;
      pMemoryProperties->memoryTypes[0] = (VkMemoryType) {
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         .heapIndex = 0,
      };
      pMemoryProperties->memoryTypes[1] = (VkMemoryType) {
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
         .heapIndex = 0,
      };
   }

   pMemoryProperties->memoryHeapCount = 1;
   pMemoryProperties->memoryHeaps[0] = (VkMemoryHeap) {
      .size = heap_size,
      .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
   };
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

   if (!pool->block_pool->device->info.has_llc)
      anv_state_clflush(state);

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
   int64_t timeout;
   int ret;

   /* Kernel driver requires 8 byte aligned batch length */
   size = align_u32(batch->next - batch->start, 8);
   result = anv_bo_pool_alloc(&device->batch_bo_pool, &bo, size);
   if (result != VK_SUCCESS)
      return result;

   memcpy(bo.map, batch->start, size);
   if (!device->info.has_llc)
      anv_clflush_range(bo.map, size);

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

   timeout = INT64_MAX;
   ret = anv_gem_wait(device, bo.gem_handle, &timeout);
   if (ret != 0) {
      /* We don't know the real error. */
      result = vk_errorf(VK_ERROR_DEVICE_LOST, "execbuf2 failed: %m");
      goto fail;
   }

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

   pthread_mutex_init(&device->mutex, NULL);

   pthread_condattr_t condattr;
   pthread_condattr_init(&condattr);
   pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
   pthread_cond_init(&device->queue_submit, NULL);
   pthread_condattr_destroy(&condattr);

   anv_bo_pool_init(&device->batch_bo_pool, device);

   anv_block_pool_init(&device->dynamic_state_block_pool, device, 16384);

   anv_state_pool_init(&device->dynamic_state_pool,
                       &device->dynamic_state_block_pool);

   anv_block_pool_init(&device->instruction_block_pool, device, 1024 * 1024);
   anv_state_pool_init(&device->instruction_state_pool,
                       &device->instruction_block_pool);

   anv_block_pool_init(&device->surface_state_block_pool, device, 4096);

   anv_state_pool_init(&device->surface_state_pool,
                       &device->surface_state_block_pool);

   anv_bo_init_new(&device->workaround_bo, device, 1024);

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
      goto fail_fd;

   anv_device_init_blorp(device);

   anv_device_init_border_colors(device);

   *pDevice = anv_device_to_handle(device);

   return VK_SUCCESS;

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
      return vk_errorf(VK_ERROR_DEVICE_LOST, "execbuf2 failed: %m");
   }

   struct drm_i915_gem_exec_object2 *objects =
      (void *)(uintptr_t)execbuf->buffers_ptr;
   for (uint32_t k = 0; k < execbuf->buffer_count; k++)
      execbuf_bos[k]->offset = objects[k].offset;

   return VK_SUCCESS;
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
   VkResult result = VK_SUCCESS;

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
   struct anv_device_memory *mem;
   VkResult result;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   /* The Vulkan 1.0.33 spec says "allocationSize must be greater than 0". */
   assert(pAllocateInfo->allocationSize > 0);

   /* We support exactly one memory heap. */
   assert(pAllocateInfo->memoryTypeIndex == 0 ||
          (!device->info.has_llc && pAllocateInfo->memoryTypeIndex < 2));

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

   mem->type_index = pAllocateInfo->memoryTypeIndex;

   mem->map = NULL;
   mem->map_size = 0;

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
   if (!device->info.has_llc && mem->type_index == 0)
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
      void *p = mem->map + (ranges[i].offset & ~CACHELINE_MASK);
      void *end;

      if (ranges[i].offset + ranges[i].size > mem->map_size)
         end = mem->map + mem->map_size;
      else
         end = mem->map + ranges[i].offset + ranges[i].size;

      while (p < end) {
         __builtin_ia32_clflush(p);
         p += CACHELINE_SIZE;
      }
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

   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    *
    * We support exactly one memory type on LLC, two on non-LLC.
    */
   pMemoryRequirements->memoryTypeBits = device->info.has_llc ? 1 : 3;

   pMemoryRequirements->size = buffer->size;
   pMemoryRequirements->alignment = 16;
}

void anv_GetImageMemoryRequirements(
    VkDevice                                    _device,
    VkImage                                     _image,
    VkMemoryRequirements*                       pMemoryRequirements)
{
   ANV_FROM_HANDLE(anv_image, image, _image);
   ANV_FROM_HANDLE(anv_device, device, _device);

   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    *
    * We support exactly one memory type on LLC, two on non-LLC.
    */
   pMemoryRequirements->memoryTypeBits = device->info.has_llc ? 1 : 3;

   pMemoryRequirements->size = image->size;
   pMemoryRequirements->alignment = image->alignment;
}

void anv_GetImageSparseMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
   stub();
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
      buffer->bo = &mem->bo;
      buffer->offset = memoryOffset;
   } else {
      buffer->bo = NULL;
      buffer->offset = 0;
   }

   return VK_SUCCESS;
}

VkResult anv_QueueBindSparse(
    VkQueue                                     queue,
    uint32_t                                    bindInfoCount,
    const VkBindSparseInfo*                     pBindInfo,
    VkFence                                     fence)
{
   stub_return(VK_ERROR_INCOMPATIBLE_DRIVER);
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
   int64_t t = 0;
   int ret;

   switch (fence->state) {
   case ANV_FENCE_STATE_RESET:
      /* If it hasn't even been sent off to the GPU yet, it's not ready */
      return VK_NOT_READY;

   case ANV_FENCE_STATE_SIGNALED:
      /* It's been signaled, return success */
      return VK_SUCCESS;

   case ANV_FENCE_STATE_SUBMITTED:
      /* It's been submitted to the GPU but we don't know if it's done yet. */
      ret = anv_gem_wait(device, fence->bo.gem_handle, &t);
      if (ret == 0) {
         fence->state = ANV_FENCE_STATE_SIGNALED;
         return VK_SUCCESS;
      } else {
         return VK_NOT_READY;
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

   /* DRM_IOCTL_I915_GEM_WAIT uses a signed 64 bit timeout and is supposed
    * to block indefinitely timeouts <= 0.  Unfortunately, this was broken
    * for a couple of kernel releases.  Since there's no way to know
    * whether or not the kernel we're using is one of the broken ones, the
    * best we can do is to clamp the timeout to INT64_MAX.  This limits the
    * maximum timeout from 584 years to 292 years - likely not a big deal.
    */
   int64_t timeout = MIN2(_timeout, INT64_MAX);

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
            if (!waitAll)
               return VK_SUCCESS;
            continue;

         case ANV_FENCE_STATE_SUBMITTED:
            /* These are the fences we really care about.  Go ahead and wait
             * on it until we hit a timeout.
             */
            ret = anv_gem_wait(device, fence->bo.gem_handle, &timeout);
            if (ret == -1 && errno == ETIME) {
               return VK_TIMEOUT;
            } else if (ret == -1) {
               /* We don't know the real error. */
               return vk_errorf(VK_ERROR_DEVICE_LOST, "gem wait failed: %m");
            } else {
               fence->state = ANV_FENCE_STATE_SIGNALED;
               signaled_fences = true;
               if (!waitAll)
                  return VK_SUCCESS;
               continue;
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
               return VK_TIMEOUT;
            }

            timeout -= time_elapsed;
         }

         pthread_mutex_unlock(&device->mutex);
      }
   }

   return VK_SUCCESS;
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

   if (!device->info.has_llc)
      anv_state_clflush(state);
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
