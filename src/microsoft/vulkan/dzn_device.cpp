/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dzn_private.h"

#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_debug_report.h"
#include "vk_format.h"
#include "vk_sync_dummy.h"
#include "vk_util.h"

#include "util/debug.h"
#include "util/macros.h"

#include "glsl_types.h"

#include "dxil_validator.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>
#include <directx/d3d12sdklayers.h>

#if defined(VK_USE_PLATFORM_WIN32_KHR) || \
    defined(VK_USE_PLATFORM_DISPLAY_KHR)
#define DZN_USE_WSI_PLATFORM
#endif

#define DZN_API_VERSION VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION)

static const vk_instance_extension_table instance_extensions = {
   .KHR_get_physical_device_properties2      = true,
#ifdef DZN_USE_WSI_PLATFORM
   .KHR_surface                              = true,
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .KHR_win32_surface                        = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display                              = true,
   .KHR_get_display_properties2              = true,
   .EXT_direct_mode_display                  = true,
   .EXT_display_surface_counter              = true,
#endif
   .EXT_debug_report                         = true,
   .EXT_debug_utils                          = true,
};

static void
dzn_physical_device_get_extensions(dzn_physical_device *pdev)
{
   pdev->vk.supported_extensions = vk_device_extension_table {
#ifdef DZN_USE_WSI_PLATFORM
      .KHR_swapchain                         = true,
#endif
   };
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                         uint32_t *pPropertyCount,
                                         VkExtensionProperties *pProperties)
{
   /* We don't support any layers  */
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &instance_extensions, pPropertyCount, pProperties);
}

static const struct debug_control dzn_debug_options[] = {
   { "sync", DZN_DEBUG_SYNC },
   { "nir", DZN_DEBUG_NIR },
   { "dxil", DZN_DEBUG_DXIL },
   { "warp", DZN_DEBUG_WARP },
   { "internal", DZN_DEBUG_INTERNAL },
   { "signature", DZN_DEBUG_SIG },
   { "gbv", DZN_DEBUG_GBV },
   { "d3d12", DZN_DEBUG_D3D12 },
   { NULL, 0 }
};

static void
dzn_physical_device_destroy(dzn_physical_device *pdev)
{
   dzn_instance *instance = container_of(pdev->vk.instance, dzn_instance, vk);

   list_del(&pdev->link);

   if (pdev->dev)
      pdev->dev->Release();

   if (pdev->adapter)
      pdev->adapter->Release();

   dzn_wsi_finish(pdev);
   vk_physical_device_finish(&pdev->vk);
   vk_free(&instance->vk.alloc, pdev);
}

static void
dzn_instance_destroy(dzn_instance *instance, const VkAllocationCallbacks *alloc)
{
   if (!instance)
      return;

   if (instance->dxil_validator)
      dxil_destroy_validator(instance->dxil_validator);

   list_for_each_entry_safe(dzn_physical_device, pdev,
                            &instance->physical_devices, link) {
      dzn_physical_device_destroy(pdev);
   }

   vk_instance_finish(&instance->vk);
   vk_free2(vk_default_allocator(), alloc, instance);
}

static VkResult
dzn_instance_create(const VkInstanceCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkInstance *out)
{
   dzn_instance *instance = (dzn_instance *)
      vk_zalloc2(vk_default_allocator(), pAllocator, sizeof(*instance), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &dzn_instance_entrypoints,
                                               true);

   VkResult result =
      vk_instance_init(&instance->vk, &instance_extensions,
                       &dispatch_table, pCreateInfo,
                       pAllocator ? pAllocator : vk_default_allocator());
   if (result != VK_SUCCESS) {
      vk_free2(vk_default_allocator(), pAllocator, instance);
      return result;
   }

   list_inithead(&instance->physical_devices);
   instance->physical_devices_enumerated = false;
   instance->debug_flags =
      parse_debug_string(getenv("DZN_DEBUG"), dzn_debug_options);

   instance->dxil_validator = dxil_create_validator(NULL);
   instance->d3d12.serialize_root_sig = d3d12_get_serialize_root_sig();

   if (!instance->dxil_validator ||
       !instance->d3d12.serialize_root_sig) {
      dzn_instance_destroy(instance, pAllocator);
      return vk_error(NULL, VK_ERROR_INITIALIZATION_FAILED);
   }

   if (instance->debug_flags & DZN_DEBUG_D3D12)
      d3d12_enable_debug_layer();
   if (instance->debug_flags & DZN_DEBUG_GBV)
      d3d12_enable_gpu_validation();

   *out = dzn_instance_to_handle(instance);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkInstance *pInstance)
{
   return dzn_instance_create(pCreateInfo, pAllocator, pInstance);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyInstance(VkInstance instance,
                    const VkAllocationCallbacks *pAllocator)
{
   dzn_instance_destroy(dzn_instance_from_handle(instance), pAllocator);
}

static VkResult
dzn_physical_device_create(dzn_instance *instance,
                           IDXGIAdapter1 *adapter,
                           const DXGI_ADAPTER_DESC1 *adapter_desc)
{
   dzn_physical_device *pdev = (dzn_physical_device *)
      vk_zalloc(&instance->vk.alloc, sizeof(*pdev), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

   if (!pdev)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &dzn_physical_device_entrypoints,
                                                      true);
   vk_physical_device_dispatch_table_from_entrypoints(&dispatch_table,
                                                      &wsi_physical_device_entrypoints,
                                                      false);

   VkResult result =
      vk_physical_device_init(&pdev->vk, &instance->vk,
                              NULL, /* We set up extensions later */
                              &dispatch_table);
   if (result != VK_SUCCESS) {
      vk_free(&instance->vk.alloc, pdev);
      return result;
   }

   mtx_init(&pdev->dev_lock, mtx_plain);
   pdev->adapter_desc = *adapter_desc;
   pdev->adapter = adapter;
   adapter->AddRef();
   list_addtail(&pdev->link, &instance->physical_devices);

   vk_warn_non_conformant_implementation("dzn");

   /* TODO: correct UUIDs */
   memset(pdev->pipeline_cache_uuid, 0, VK_UUID_SIZE);
   memset(pdev->driver_uuid, 0, VK_UUID_SIZE);
   memset(pdev->device_uuid, 0, VK_UUID_SIZE);

   /* TODO: something something queue families */

   result = dzn_wsi_init(pdev);
   if (result != VK_SUCCESS) {
      dzn_physical_device_destroy(pdev);
      return result;
   }

   dzn_physical_device_get_extensions(pdev);

   uint32_t num_sync_types = 0;
   pdev->sync_types[num_sync_types++] = &dzn_sync_type;
   pdev->sync_types[num_sync_types++] = &vk_sync_dummy_type;
   pdev->sync_types[num_sync_types] = NULL;
   assert(num_sync_types <= MAX_SYNC_TYPES);
   pdev->vk.supported_sync_types = pdev->sync_types;

   return VK_SUCCESS;
}

static void
dzn_physical_device_cache_caps(dzn_physical_device *pdev)
{
   D3D_FEATURE_LEVEL checklist[] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_12_1,
      D3D_FEATURE_LEVEL_12_2,
   };

   D3D12_FEATURE_DATA_FEATURE_LEVELS levels = {
      .NumFeatureLevels = ARRAY_SIZE(checklist),
      .pFeatureLevelsRequested = checklist,
   };

   pdev->dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels));
   pdev->feature_level = levels.MaxSupportedFeatureLevel;

   pdev->dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &pdev->architecture, sizeof(pdev->architecture));
   pdev->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &pdev->options, sizeof(pdev->options));

   pdev->queue_families[pdev->queue_family_count++] = {
      .props = {
         .queueFlags = VK_QUEUE_GRAPHICS_BIT |
                       VK_QUEUE_COMPUTE_BIT |
                       VK_QUEUE_TRANSFER_BIT,
         .queueCount = 1,
         .timestampValidBits = 64,
         .minImageTransferGranularity = { 0, 0, 0 },
      },
      .desc = {
         .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      },
   };

   pdev->queue_families[pdev->queue_family_count++] = {
      .props = {
         .queueFlags = VK_QUEUE_COMPUTE_BIT |
                       VK_QUEUE_TRANSFER_BIT,
         .queueCount = 8,
         .timestampValidBits = 64,
         .minImageTransferGranularity = { 0, 0, 0 },
      },
      .desc = {
         .Type = D3D12_COMMAND_LIST_TYPE_COMPUTE,
      },
   };

   pdev->queue_families[pdev->queue_family_count++] = {
      .props = {
         .queueFlags = VK_QUEUE_TRANSFER_BIT,
         .queueCount = 1,
         .timestampValidBits = 0,
         .minImageTransferGranularity = { 0, 0, 0 },
      },
      .desc = {
         .Type = D3D12_COMMAND_LIST_TYPE_COPY,
      },
   };

   assert(pdev->queue_family_count <= ARRAY_SIZE(pdev->queue_families));

   D3D12_COMMAND_QUEUE_DESC queue_desc = {
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
      .NodeMask = 0,
   };

   ComPtr<ID3D12CommandQueue> cmdqueue;

   pdev->dev->CreateCommandQueue(&queue_desc,
                                 IID_PPV_ARGS(&cmdqueue));

   uint64_t ts_freq;
   cmdqueue->GetTimestampFrequency(&ts_freq);
   pdev->timestamp_period = 1000000000.0f / ts_freq;
}

static void
dzn_physical_device_init_memory(dzn_physical_device *pdev)
{
   VkPhysicalDeviceMemoryProperties *mem = &pdev->memory;
   const DXGI_ADAPTER_DESC1 *desc = &pdev->adapter_desc;

   mem->memoryHeapCount = 1;
   mem->memoryHeaps[0] = VkMemoryHeap {
      .size = desc->SharedSystemMemory,
      .flags = 0,
   };

   mem->memoryTypes[mem->memoryTypeCount++] = VkMemoryType {
      .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      .heapIndex = 0,
   };
   mem->memoryTypes[mem->memoryTypeCount++] = VkMemoryType {
      .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
     .heapIndex = 0,
   };

   if (!pdev->architecture.UMA) {
      mem->memoryHeaps[mem->memoryHeapCount++] = VkMemoryHeap {
         .size = desc->DedicatedVideoMemory,
         .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      };
      mem->memoryTypes[mem->memoryTypeCount++] = VkMemoryType {
         .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
         .heapIndex = mem->memoryHeapCount - 1,
      };
   } else {
      mem->memoryHeaps[0].flags |= VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
      mem->memoryTypes[0].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      mem->memoryTypes[1].propertyFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   }

   constexpr unsigned MaxTier2MemoryTypes = 3;
   assert(mem->memoryTypeCount <= MaxTier2MemoryTypes);

   if (pdev->options.ResourceHeapTier == D3D12_RESOURCE_HEAP_TIER_1) {
      unsigned oldMemoryTypeCount = mem->memoryTypeCount;
      VkMemoryType oldMemoryTypes[MaxTier2MemoryTypes];

      memcpy(oldMemoryTypes, mem->memoryTypes, oldMemoryTypeCount * sizeof(VkMemoryType));

      mem->memoryTypeCount = 0;
      for (unsigned oldMemoryTypeIdx = 0; oldMemoryTypeIdx < oldMemoryTypeCount; ++oldMemoryTypeIdx) {
         D3D12_HEAP_FLAGS flags[] = {
            D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS,
            D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES,
            /* Note: Vulkan requires *all* images to come from the same memory type as long as
             * the tiling property (and a few other misc properties) are the same. So, this
             * non-RT/DS texture flag will only be used for TILING_LINEAR textures, which
             * can't be render targets.
             */
            D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
         };
         for (D3D12_HEAP_FLAGS flag : flags) {
            pdev->heap_flags_for_mem_type[mem->memoryTypeCount] = flag;
            mem->memoryTypes[mem->memoryTypeCount] = oldMemoryTypes[oldMemoryTypeIdx];
            mem->memoryTypeCount++;
         }
      }
   }
}

static D3D12_HEAP_FLAGS
dzn_physical_device_get_heap_flags_for_mem_type(const dzn_physical_device *pdev,
                                                uint32_t mem_type)
{
   return pdev->heap_flags_for_mem_type[mem_type];
}

uint32_t
dzn_physical_device_get_mem_type_mask_for_resource(const dzn_physical_device *pdev,
                                                   const D3D12_RESOURCE_DESC *desc)
{
   if (pdev->options.ResourceHeapTier > D3D12_RESOURCE_HEAP_TIER_1)
      return (1u << pdev->memory.memoryTypeCount) - 1;

   D3D12_HEAP_FLAGS deny_flag;
   if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
      deny_flag = D3D12_HEAP_FLAG_DENY_BUFFERS;
   else if (desc->Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
      deny_flag = D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
   else
      deny_flag = D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

   uint32_t mask = 0;
   for (unsigned i = 0; i < pdev->memory.memoryTypeCount; ++i) {
      if ((pdev->heap_flags_for_mem_type[i] & deny_flag) == D3D12_HEAP_FLAG_NONE)
         mask |= (1 << i);
   }
   return mask;
}

static uint32_t
dzn_physical_device_get_max_mip_level(bool is_3d)
{
   return is_3d ? 11 : 14;
}

static uint32_t
dzn_physical_device_get_max_extent(bool is_3d)
{
   uint32_t max_mip = dzn_physical_device_get_max_mip_level(is_3d);

   return 1 << max_mip;
}

static uint32_t
dzn_physical_device_get_max_array_layers()
{
   return dzn_physical_device_get_max_extent(false);
}

static ID3D12Device1 *
dzn_physical_device_get_d3d12_dev(dzn_physical_device *pdev)
{
   dzn_instance *instance = container_of(pdev->vk.instance, dzn_instance, vk);

   mtx_lock(&pdev->dev_lock);
   if (!pdev->dev) {
      pdev->dev = d3d12_create_device(pdev->adapter, !instance->dxil_validator);

      dzn_physical_device_cache_caps(pdev);
      dzn_physical_device_init_memory(pdev);
   }
   mtx_unlock(&pdev->dev_lock);

   return pdev->dev;
}

D3D12_FEATURE_DATA_FORMAT_SUPPORT
dzn_physical_device_get_format_support(dzn_physical_device *pdev,
                                       VkFormat format)
{
   VkImageUsageFlags usage =
      vk_format_is_depth_or_stencil(format) ?
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0;
   VkImageAspectFlags aspects = 0;

   if (vk_format_has_depth(format))
      aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
   if (vk_format_has_stencil(format))
      aspects = VK_IMAGE_ASPECT_STENCIL_BIT;

   D3D12_FEATURE_DATA_FORMAT_SUPPORT dfmt_info = {
     .Format = dzn_image_get_dxgi_format(format, usage, aspects),
   };

   ID3D12Device *dev = dzn_physical_device_get_d3d12_dev(pdev);
   HRESULT hres =
      dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                               &dfmt_info, sizeof(dfmt_info));
   assert(!FAILED(hres));

   if (usage != VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
      return dfmt_info;

   /* Depth/stencil resources have different format when they're accessed
    * as textures, query the capabilities for this format too.
    */
   dzn_foreach_aspect(aspect, aspects) {
      D3D12_FEATURE_DATA_FORMAT_SUPPORT dfmt_info2 = {
        .Format = dzn_image_get_dxgi_format(format, 0, aspect),
      };

      hres = dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                      &dfmt_info2, sizeof(dfmt_info2));
      assert(!FAILED(hres));

#define DS_SRV_FORMAT_SUPPORT1_MASK \
        (D3D12_FORMAT_SUPPORT1_SHADER_LOAD | \
         D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | \
         D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON | \
         D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_MONO_TEXT | \
         D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE | \
         D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD | \
         D3D12_FORMAT_SUPPORT1_SHADER_GATHER | \
         D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW | \
         D3D12_FORMAT_SUPPORT1_SHADER_GATHER_COMPARISON)

      dfmt_info.Support1 |= dfmt_info2.Support1 & DS_SRV_FORMAT_SUPPORT1_MASK;
      dfmt_info.Support2 |= dfmt_info2.Support2;
   }

   return dfmt_info;
}

static void
dzn_physical_device_get_format_properties(dzn_physical_device *pdev,
                                          VkFormat format,
                                          VkFormatProperties2 *properties)
{
   D3D12_FEATURE_DATA_FORMAT_SUPPORT dfmt_info =
      dzn_physical_device_get_format_support(pdev, format);
   VkFormatProperties *base_props = &properties->formatProperties;

   vk_foreach_struct(ext, properties->pNext) {
      dzn_debug_ignored_stype(ext->sType);
   }

   if (dfmt_info.Format == DXGI_FORMAT_UNKNOWN) {
      *base_props = VkFormatProperties { };
      return;
   }

   ID3D12Device *dev = dzn_physical_device_get_d3d12_dev(pdev);

   *base_props = VkFormatProperties {
      .linearTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
      .optimalTilingFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
      .bufferFeatures = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT,
   };

   if (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER)
      base_props->bufferFeatures |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;

#define TEX_FLAGS (D3D12_FORMAT_SUPPORT1_TEXTURE1D | \
                   D3D12_FORMAT_SUPPORT1_TEXTURE2D | \
                   D3D12_FORMAT_SUPPORT1_TEXTURE3D | \
                   D3D12_FORMAT_SUPPORT1_TEXTURECUBE)
   if (dfmt_info.Support1 & TEX_FLAGS) {
      base_props->optimalTilingFeatures |=
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT;
   }

   if (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) {
      base_props->optimalTilingFeatures |=
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
   }

   if ((dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) &&
       (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)) {
      base_props->optimalTilingFeatures |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
      base_props->bufferFeatures |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
   }

#define ATOMIC_FLAGS (D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | \
                      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS | \
                      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE | \
                      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE | \
                      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX | \
                      D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX)
   if ((dfmt_info.Support2 & ATOMIC_FLAGS) == ATOMIC_FLAGS) {
      base_props->optimalTilingFeatures |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
      base_props->bufferFeatures |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   if (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD)
      base_props->bufferFeatures |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;

   /* Color/depth/stencil attachment cap implies input attachement cap, and input
    * attachment loads are lowered to texture loads in dozen, hence the requirement
    * to have shader-load support.
    */
   if (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) {
      if (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
         base_props->optimalTilingFeatures |=
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
      }

      if (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE)
         base_props->optimalTilingFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;

      if (dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) {
         base_props->optimalTilingFeatures |=
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
      }
   }

   /* B4G4R4A4 support is required, but d3d12 doesn't support it. We map this
    * format to R4G4B4A4 and adjust the SRV component-mapping to fake
    * B4G4R4A4, but that forces us to limit the usage to sampling, which,
    * luckily, is exactly what we need to support the required features.
    */
   if (format == VK_FORMAT_B4G4R4A4_UNORM_PACK16) {
      VkFormatFeatureFlags bgra4_req_features =
         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
         VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
         VK_FORMAT_FEATURE_BLIT_SRC_BIT |
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      base_props->optimalTilingFeatures &= bgra4_req_features;
      base_props->bufferFeatures =
         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   }

   /* depth/stencil format shouldn't advertise buffer features */
   if (vk_format_is_depth_or_stencil(format))
      base_props->bufferFeatures = 0;
}

static VkResult
dzn_physical_device_get_image_format_properties(dzn_physical_device *pdev,
                                                const VkPhysicalDeviceImageFormatInfo2 *info,
                                                VkImageFormatProperties2 *properties)
{
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;

   *properties = VkImageFormatProperties2 {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
   };

   /* Extract input structs */
   vk_foreach_struct_const(s, info->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const VkPhysicalDeviceExternalImageFormatInfo *)s;
         break;
      default:
         dzn_debug_ignored_stype(s->sType);
         break;
      }
   }

   assert(info->tiling == VK_IMAGE_TILING_OPTIMAL || info->tiling == VK_IMAGE_TILING_LINEAR);

   /* Extract output structs */
   vk_foreach_struct(s, properties->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (VkExternalImageFormatProperties *)s;
         break;
      default:
         dzn_debug_ignored_stype(s->sType);
         break;
      }
   }

   assert((external_props != NULL) == (external_info != NULL));

   /* TODO: support image import */
   if (external_info && external_info->handleType != 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (info->tiling != VK_IMAGE_TILING_OPTIMAL &&
       (info->usage & ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (info->tiling != VK_IMAGE_TILING_OPTIMAL &&
       vk_format_is_depth_or_stencil(info->format))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   D3D12_FEATURE_DATA_FORMAT_SUPPORT dfmt_info =
      dzn_physical_device_get_format_support(pdev, info->format);
   if (dfmt_info.Format == DXGI_FORMAT_UNKNOWN)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   bool is_bgra4 = info->format == VK_FORMAT_B4G4R4A4_UNORM_PACK16;
   ID3D12Device *dev = dzn_physical_device_get_d3d12_dev(pdev);

   if ((info->type == VK_IMAGE_TYPE_1D && !(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE1D)) ||
       (info->type == VK_IMAGE_TYPE_2D && !(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)) ||
       (info->type == VK_IMAGE_TYPE_3D && !(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE3D)) ||
       ((info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
        !(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURECUBE)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if ((info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) &&
       !(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if ((info->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
       (!(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) || is_bgra4))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if ((info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
       (!(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) || is_bgra4))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if ((info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
       (!(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) || is_bgra4))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if ((info->usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
       (!(dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) || is_bgra4))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (info->type == VK_IMAGE_TYPE_3D && info->tiling != VK_IMAGE_TILING_OPTIMAL)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   bool is_3d = info->type == VK_IMAGE_TYPE_3D;
   uint32_t max_extent = dzn_physical_device_get_max_extent(is_3d);

   if (info->tiling == VK_IMAGE_TILING_OPTIMAL &&
       dfmt_info.Support1 & D3D12_FORMAT_SUPPORT1_MIP)
      properties->imageFormatProperties.maxMipLevels = dzn_physical_device_get_max_mip_level(is_3d) + 1;
   else
      properties->imageFormatProperties.maxMipLevels = 1;

   if (info->tiling == VK_IMAGE_TILING_OPTIMAL && info->type != VK_IMAGE_TYPE_3D)
      properties->imageFormatProperties.maxArrayLayers = dzn_physical_device_get_max_array_layers();
   else
      properties->imageFormatProperties.maxArrayLayers = 1;

   switch (info->type) {
   case VK_IMAGE_TYPE_1D:
      properties->imageFormatProperties.maxExtent.width = max_extent;
      properties->imageFormatProperties.maxExtent.height = 1;
      properties->imageFormatProperties.maxExtent.depth = 1;
      break;
   case VK_IMAGE_TYPE_2D:
      properties->imageFormatProperties.maxExtent.width = max_extent;
      properties->imageFormatProperties.maxExtent.height = max_extent;
      properties->imageFormatProperties.maxExtent.depth = 1;
      break;
   case VK_IMAGE_TYPE_3D:
      properties->imageFormatProperties.maxExtent.width = max_extent;
      properties->imageFormatProperties.maxExtent.height = max_extent;
      properties->imageFormatProperties.maxExtent.depth = max_extent;
      break;
   default:
      unreachable("bad VkImageType");
   }

   /* From the Vulkan 1.0 spec, section 34.1.1. Supported Sample Counts:
    *
    * sampleCounts will be set to VK_SAMPLE_COUNT_1_BIT if at least one of the
    * following conditions is true:
    *
    *   - tiling is VK_IMAGE_TILING_LINEAR
    *   - type is not VK_IMAGE_TYPE_2D
    *   - flags contains VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    *   - neither the VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT flag nor the
    *     VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT flag in
    *     VkFormatProperties::optimalTilingFeatures returned by
    *     vkGetPhysicalDeviceFormatProperties is set.
    *
    * D3D12 has a few more constraints:
    *   - no UAVs on multisample resources
    */
   bool rt_or_ds_cap =
      dfmt_info.Support1 &
      (D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);

   properties->imageFormatProperties.sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   if (info->tiling != VK_IMAGE_TILING_LINEAR &&
       info->type == VK_IMAGE_TYPE_2D &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       rt_or_ds_cap && !is_bgra4 &&
       !(info->usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      for (uint32_t s = VK_SAMPLE_COUNT_2_BIT; s < VK_SAMPLE_COUNT_64_BIT; s <<= 1) {
         D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms_info = {
            .Format = dfmt_info.Format,
            .SampleCount = s,
         };

         HRESULT hres =
            dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                                     &ms_info, sizeof(ms_info));
         if (!FAILED(hres) && ms_info.NumQualityLevels > 0)
            properties->imageFormatProperties.sampleCounts |= s;
      }
   }

   /* TODO: set correct value here */
   properties->imageFormatProperties.maxResourceSize = UINT32_MAX;

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                       VkFormat format,
                                       VkFormatProperties2 *pFormatProperties)
{
   VK_FROM_HANDLE(dzn_physical_device, pdev, physicalDevice);

   dzn_physical_device_get_format_properties(pdev, format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                            const VkPhysicalDeviceImageFormatInfo2 *info,
                                            VkImageFormatProperties2 *props)
{
   VK_FROM_HANDLE(dzn_physical_device, pdev, physicalDevice);

   return dzn_physical_device_get_image_format_properties(pdev, info, props);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_GetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice,
                                           VkFormat format,
                                           VkImageType type,
                                           VkImageTiling tiling,
                                           VkImageUsageFlags usage,
                                           VkImageCreateFlags createFlags,
                                           VkImageFormatProperties *pImageFormatProperties)
{
   const VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = format,
      .type = type,
      .tiling = tiling,
      .usage = usage,
      .flags = createFlags,
   };

   VkImageFormatProperties2 props = {};

   VkResult result =
      dzn_GetPhysicalDeviceImageFormatProperties2(physicalDevice, &info, &props);
   *pImageFormatProperties = props.imageFormatProperties;

   return result;
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceSparseImageFormatProperties(VkPhysicalDevice physicalDevice,
                                                 VkFormat format,
                                                 VkImageType type,
                                                 VkSampleCountFlagBits samples,
                                                 VkImageUsageFlags usage,
                                                 VkImageTiling tiling,
                                                 uint32_t *pPropertyCount,
                                                 VkSparseImageFormatProperties *pProperties)
{
   *pPropertyCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceSparseImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                                  const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
                                                  uint32_t *pPropertyCount,
                                                  VkSparseImageFormatProperties2 *pProperties)
{
   *pPropertyCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice physicalDevice,
                                              const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
                                              VkExternalBufferProperties *pExternalBufferProperties)
{
   pExternalBufferProperties->externalMemoryProperties =
      VkExternalMemoryProperties {
         .compatibleHandleTypes = (VkExternalMemoryHandleTypeFlags)pExternalBufferInfo->handleType,
      };
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_EnumeratePhysicalDevices(VkInstance inst,
                             uint32_t *pPhysicalDeviceCount,
                             VkPhysicalDevice *pPhysicalDevices)
{
   VK_FROM_HANDLE(dzn_instance, instance, inst);

   if (!instance->physical_devices_enumerated) {
      ComPtr<IDXGIFactory4> factory = dxgi_get_factory(false);
      ComPtr<IDXGIAdapter1> adapter(NULL);
      for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)); ++i) {
         DXGI_ADAPTER_DESC1 desc;
         adapter->GetDesc1(&desc);
         if (instance->debug_flags & DZN_DEBUG_WARP) {
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
               continue;
         }

         VkResult result =
            dzn_physical_device_create(instance, adapter.Get(), &desc);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDevice, out, pPhysicalDevices,
                          pPhysicalDeviceCount);

   list_for_each_entry(dzn_physical_device, pdev, &instance->physical_devices, link) {
      vk_outarray_append_typed(VkPhysicalDevice, &out, i)
         *i = dzn_physical_device_to_handle(pdev);
   }

   instance->physical_devices_enumerated = true;
   return vk_outarray_status(&out);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
    *pApiVersion = DZN_API_VERSION;
    return VK_SUCCESS;
}

static bool
dzn_physical_device_supports_compressed_format(dzn_physical_device *pdev,
                                               const VkFormat *formats,
                                               uint32_t format_count)
{
#define REQUIRED_COMPRESSED_CAPS \
        (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | \
         VK_FORMAT_FEATURE_BLIT_SRC_BIT | \
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
   for (uint32_t i = 0; i < format_count; i++) {
      VkFormatProperties2 props = {};
      dzn_physical_device_get_format_properties(pdev, formats[i], &props);
      if ((props.formatProperties.optimalTilingFeatures & REQUIRED_COMPRESSED_CAPS) != REQUIRED_COMPRESSED_CAPS)
         return false;
   }

   return true;
}

static bool
dzn_physical_device_supports_bc(dzn_physical_device *pdev)
{
   static const VkFormat formats[] = {
      VK_FORMAT_BC1_RGB_UNORM_BLOCK,
      VK_FORMAT_BC1_RGB_SRGB_BLOCK,
      VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
      VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
      VK_FORMAT_BC2_UNORM_BLOCK,
      VK_FORMAT_BC2_SRGB_BLOCK,
      VK_FORMAT_BC3_UNORM_BLOCK,
      VK_FORMAT_BC3_SRGB_BLOCK,
      VK_FORMAT_BC4_UNORM_BLOCK,
      VK_FORMAT_BC4_SNORM_BLOCK,
      VK_FORMAT_BC5_UNORM_BLOCK,
      VK_FORMAT_BC5_SNORM_BLOCK,
      VK_FORMAT_BC6H_UFLOAT_BLOCK,
      VK_FORMAT_BC6H_SFLOAT_BLOCK,
      VK_FORMAT_BC7_UNORM_BLOCK,
      VK_FORMAT_BC7_SRGB_BLOCK,
   };

   return dzn_physical_device_supports_compressed_format(pdev, formats, ARRAY_SIZE(formats));
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                               VkPhysicalDeviceFeatures2 *pFeatures)
{
   VK_FROM_HANDLE(dzn_physical_device, pdev, physicalDevice);

   pFeatures->features = VkPhysicalDeviceFeatures {
      .robustBufferAccess = true, /* This feature is mandatory */
      .fullDrawIndexUint32 = false,
      .imageCubeArray = true,
      .independentBlend = false,
      .geometryShader = false,
      .tessellationShader = false,
      .sampleRateShading = false,
      .dualSrcBlend = false,
      .logicOp = false,
      .multiDrawIndirect = false,
      .drawIndirectFirstInstance = false,
      .depthClamp = false,
      .depthBiasClamp = false,
      .fillModeNonSolid = false,
      .depthBounds = false,
      .wideLines = false,
      .largePoints = false,
      .alphaToOne = false,
      .multiViewport = false,
      .samplerAnisotropy = false,
      .textureCompressionETC2 = false,
      .textureCompressionASTC_LDR = false,
      .textureCompressionBC = dzn_physical_device_supports_bc(pdev),
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = true,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = false,
      .shaderImageGatherExtended = false,
      .shaderStorageImageExtendedFormats = false,
      .shaderStorageImageMultisample = false,
      .shaderStorageImageReadWithoutFormat = false,
      .shaderStorageImageWriteWithoutFormat = false,
      .shaderUniformBufferArrayDynamicIndexing = false,
      .shaderSampledImageArrayDynamicIndexing = false,
      .shaderStorageBufferArrayDynamicIndexing = false,
      .shaderStorageImageArrayDynamicIndexing = false,
      .shaderClipDistance = false,
      .shaderCullDistance = false,
      .shaderFloat64 = false,
      .shaderInt64 = false,
      .shaderInt16 = false,
      .shaderResourceResidency = false,
      .shaderResourceMinLod = false,
      .sparseBinding = false,
      .sparseResidencyBuffer = false,
      .sparseResidencyImage2D = false,
      .sparseResidencyImage3D = false,
      .sparseResidency2Samples = false,
      .sparseResidency4Samples = false,
      .sparseResidency8Samples = false,
      .sparseResidency16Samples = false,
      .sparseResidencyAliased = false,
      .variableMultisampleRate = false,
      .inheritedQueries = false,
   };


   vk_foreach_struct(ext, pFeatures->pNext) {
      dzn_debug_ignored_stype(ext->sType);
   }
}


VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
dzn_GetInstanceProcAddr(VkInstance _instance,
                        const char *pName)
{
   VK_FROM_HANDLE(dzn_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk,
                                    &dzn_instance_entrypoints,
                                    pName);
}

/* Windows will use a dll definition file to avoid build errors. */
#ifdef _WIN32
#undef PUBLIC
#define PUBLIC
#endif

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance,
                          const char *pName);

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance,
                          const char *pName)
{
   return dzn_GetInstanceProcAddr(instance, pName);
}

/* With version 4+ of the loader interface the ICD should expose
 * vk_icdGetPhysicalDeviceProcAddr()
 */
PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance  _instance,
                                const char* pName);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance  _instance,
                                const char* pName)
{
   VK_FROM_HANDLE(dzn_instance, instance, _instance);
   return vk_instance_get_physical_device_proc_addr(&instance->vk, pName);
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
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 4u);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                 VkPhysicalDeviceProperties2 *pProperties)
{
   VK_FROM_HANDLE(dzn_physical_device, pdevice, physicalDevice);

   /* minimum from the spec */
   const VkSampleCountFlags supported_sample_counts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;

   /* FIXME: this is mostly bunk for now */
   VkPhysicalDeviceLimits limits = {

      /* TODO: support older feature levels */
      .maxImageDimension1D                      = (1 << 14),
      .maxImageDimension2D                      = (1 << 14),
      .maxImageDimension3D                      = (1 << 11),
      .maxImageDimensionCube                    = (1 << 14),
      .maxImageArrayLayers                      = (1 << 11),

      /* from here on, we simply use the minimum values from the spec for now */
      .maxTexelBufferElements                   = 65536,
      .maxUniformBufferRange                    = 16384,
      .maxStorageBufferRange                    = (1ul << 27),
      .maxPushConstantsSize                     = 128,
      .maxMemoryAllocationCount                 = 4096,
      .maxSamplerAllocationCount                = 4000,
      .bufferImageGranularity                   = 131072,
      .sparseAddressSpaceSize                   = 0,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = 16,
      .maxPerStageDescriptorUniformBuffers      = 12,
      .maxPerStageDescriptorStorageBuffers      = 4,
      .maxPerStageDescriptorSampledImages       = 16,
      .maxPerStageDescriptorStorageImages       = 4,
      .maxPerStageDescriptorInputAttachments    = 4,
      .maxPerStageResources                     = 128,
      .maxDescriptorSetSamplers                 = 96,
      .maxDescriptorSetUniformBuffers           = 72,
      .maxDescriptorSetUniformBuffersDynamic    = MAX_DYNAMIC_UNIFORM_BUFFERS,
      .maxDescriptorSetStorageBuffers           = 24,
      .maxDescriptorSetStorageBuffersDynamic    = MAX_DYNAMIC_STORAGE_BUFFERS,
      .maxDescriptorSetSampledImages            = 96,
      .maxDescriptorSetStorageImages            = 24,
      .maxDescriptorSetInputAttachments         = 4,
      .maxVertexInputAttributes                 = 16,
      .maxVertexInputBindings                   = 16,
      .maxVertexInputAttributeOffset            = 2047,
      .maxVertexInputBindingStride              = 2048,
      .maxVertexOutputComponents                = 64,
      .maxTessellationGenerationLevel           = 0,
      .maxTessellationPatchSize                 = 0,
      .maxTessellationControlPerVertexInputComponents = 0,
      .maxTessellationControlPerVertexOutputComponents = 0,
      .maxTessellationControlPerPatchOutputComponents = 0,
      .maxTessellationControlTotalOutputComponents = 0,
      .maxTessellationEvaluationInputComponents = 0,
      .maxTessellationEvaluationOutputComponents = 0,
      .maxGeometryShaderInvocations             = 0,
      .maxGeometryInputComponents               = 0,
      .maxGeometryOutputComponents              = 0,
      .maxGeometryOutputVertices                = 0,
      .maxGeometryTotalOutputComponents         = 0,
      .maxFragmentInputComponents               = 64,
      .maxFragmentOutputAttachments             = 4,
      .maxFragmentDualSrcAttachments            = 0,
      .maxFragmentCombinedOutputResources       = 4,
      .maxComputeSharedMemorySize               = 16384,
      .maxComputeWorkGroupCount                 = { 65535, 65535, 65535 },
      .maxComputeWorkGroupInvocations           = 128,
      .maxComputeWorkGroupSize                  = { 128, 128, 64 },
      .subPixelPrecisionBits                    = 4,
      .subTexelPrecisionBits                    = 4,
      .mipmapPrecisionBits                      = 4,
      .maxDrawIndexedIndexValue                 = 0x00ffffff,
      .maxDrawIndirectCount                     = 1,
      .maxSamplerLodBias                        = 2.0f,
      .maxSamplerAnisotropy                     = 1.0f,
      .maxViewports                             = 1,
      .maxViewportDimensions                    = { 4096, 4096 },
      .viewportBoundsRange                      = { -8192, 8191 },
      .viewportSubPixelBits                     = 0,
      .minMemoryMapAlignment                    = 64,
      .minTexelBufferOffsetAlignment            = 256,
      .minUniformBufferOffsetAlignment          = 256,
      .minStorageBufferOffsetAlignment          = 256,
      .minTexelOffset                           = -8,
      .maxTexelOffset                           = 7,
      .minTexelGatherOffset                     = 0,
      .maxTexelGatherOffset                     = 0,
      .minInterpolationOffset                   = 0.0f,
      .maxInterpolationOffset                   = 0.0f,
      .subPixelInterpolationOffsetBits          = 0,
      .maxFramebufferWidth                      = 4096,
      .maxFramebufferHeight                     = 4096,
      .maxFramebufferLayers                     = 256,
      .framebufferColorSampleCounts             = supported_sample_counts,
      .framebufferDepthSampleCounts             = supported_sample_counts,
      .framebufferStencilSampleCounts           = supported_sample_counts,
      .framebufferNoAttachmentsSampleCounts     = supported_sample_counts,
      .maxColorAttachments                      = 4,
      .sampledImageColorSampleCounts            = supported_sample_counts,
      .sampledImageIntegerSampleCounts          = VK_SAMPLE_COUNT_1_BIT,
      .sampledImageDepthSampleCounts            = supported_sample_counts,
      .sampledImageStencilSampleCounts          = supported_sample_counts,
      .storageImageSampleCounts                 = VK_SAMPLE_COUNT_1_BIT,
      .maxSampleMaskWords                       = 1,
      .timestampComputeAndGraphics              = true,
      .timestampPeriod                          = pdevice->timestamp_period,
      .maxClipDistances                         = 8,
      .maxCullDistances                         = 8,
      .maxCombinedClipAndCullDistances          = 8,
      .discreteQueuePriorities                  = 2,
      .pointSizeRange                           = { 1.0f, 1.0f },
      .lineWidthRange                           = { 1.0f, 1.0f },
      .pointSizeGranularity                     = 0.0f,
      .lineWidthGranularity                     = 0.0f,
      .strictLines                              = 0,
      .standardSampleLocations                  = false,
      .optimalBufferCopyOffsetAlignment         = 1,
      .optimalBufferCopyRowPitchAlignment       = 1,
      .nonCoherentAtomSize                      = 256,
   };

   const DXGI_ADAPTER_DESC1& desc = pdevice->adapter_desc;

   VkPhysicalDeviceType devtype = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
   if (desc.Flags == DXGI_ADAPTER_FLAG_SOFTWARE)
      devtype = VK_PHYSICAL_DEVICE_TYPE_CPU;
   else if (false) { // TODO: detect discreete GPUs
      /* This is a tad tricky to get right, because we need to have the
       * actual ID3D12Device before we can query the
       * D3D12_FEATURE_DATA_ARCHITECTURE structure... So for now, let's
       * just pretend everything is integrated, because... well, that's
       * what I have at hand right now ;)
       */
      devtype = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
   }

   pProperties->properties = VkPhysicalDeviceProperties {
      .apiVersion = DZN_API_VERSION,
      .driverVersion = vk_get_driver_version(),

      .vendorID = desc.VendorId,
      .deviceID = desc.DeviceId,
      .deviceType = devtype,

      .limits = limits,
      .sparseProperties = { 0 },
   };

   snprintf(pProperties->properties.deviceName,
            sizeof(pProperties->properties.deviceName),
            "Microsoft Direct3D12 (%S)", desc.Description);

   memcpy(pProperties->properties.pipelineCacheUUID,
          pdevice->pipeline_cache_uuid, VK_UUID_SIZE);

   vk_foreach_struct(ext, pProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
         VkPhysicalDeviceIDProperties *id_props =
            (VkPhysicalDeviceIDProperties *)ext;
         memcpy(id_props->deviceUUID, pdevice->device_uuid, VK_UUID_SIZE);
         memcpy(id_props->driverUUID, pdevice->driver_uuid, VK_UUID_SIZE);
         /* The LUID is for Windows. */
         id_props->deviceLUIDValid = false;
         break;
      }
      default:
         dzn_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice physicalDevice,
                                            uint32_t *pQueueFamilyPropertyCount,
                                            VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_FROM_HANDLE(dzn_physical_device, pdev, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out,
                          pQueueFamilyProperties, pQueueFamilyPropertyCount);

   (void)dzn_physical_device_get_d3d12_dev(pdev);

   for (uint32_t i = 0; i < pdev->queue_family_count; i++) {
      vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p) {
         p->queueFamilyProperties = pdev->queue_families[i].props;

         vk_foreach_struct(ext, pQueueFamilyProperties->pNext) {
            dzn_debug_ignored_stype(ext->sType);
         }
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                      VkPhysicalDeviceMemoryProperties *pMemoryProperties)
{
   VK_FROM_HANDLE(dzn_physical_device, pdev, physicalDevice);

   // Ensure memory caps are up-to-date
   (void)dzn_physical_device_get_d3d12_dev(pdev);
   *pMemoryProperties = pdev->memory;
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetPhysicalDeviceMemoryProperties2(VkPhysicalDevice physicalDevice,
                                       VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   dzn_GetPhysicalDeviceMemoryProperties(physicalDevice,
                                         &pMemoryProperties->memoryProperties);

   vk_foreach_struct(ext, pMemoryProperties->pNext) {
      dzn_debug_ignored_stype(ext->sType);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                     VkLayerProperties *pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

static VkResult
dzn_queue_sync_wait(dzn_queue *queue, const struct vk_sync_wait *wait)
{
   if (wait->sync->type == &vk_sync_dummy_type)
      return VK_SUCCESS;

   dzn_device *device = container_of(queue->vk.base.device, dzn_device, vk);
   assert(wait->sync->type == &dzn_sync_type);
   dzn_sync *sync = container_of(wait->sync, dzn_sync, vk);
   uint64_t value =
      (sync->vk.flags & VK_SYNC_IS_TIMELINE) ? wait->wait_value : 1;

   assert(sync->fence != NULL);

   if (value > 0 && FAILED(queue->cmdqueue->Wait(sync->fence, value)))
      return vk_error(device, VK_ERROR_UNKNOWN);

   return VK_SUCCESS;
}

static VkResult
dzn_queue_sync_signal(dzn_queue *queue, const struct vk_sync_signal *signal)
{
   if (signal->sync->type == &vk_sync_dummy_type)
      return VK_SUCCESS;

   dzn_device *device = container_of(queue->vk.base.device, dzn_device, vk);
   assert(signal->sync->type == &dzn_sync_type);
   dzn_sync *sync = container_of(signal->sync, dzn_sync, vk);
   uint64_t value =
      (sync->vk.flags & VK_SYNC_IS_TIMELINE) ? signal->signal_value : 1;
   assert(value > 0);

   assert(sync->fence != NULL);

   if (FAILED(queue->cmdqueue->Signal(sync->fence, value)))
      return vk_error(device, VK_ERROR_UNKNOWN);

   return VK_SUCCESS;
}

static VkResult
dzn_queue_submit(struct vk_queue *q,
                 struct vk_queue_submit *info)
{
   dzn_queue *queue = container_of(q, dzn_queue, vk);
   dzn_device *device = container_of(q->base.device, dzn_device, vk);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < info->wait_count; i++) {
      result = dzn_queue_sync_wait(queue, &info->waits[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   for (uint32_t i = 0; i < info->command_buffer_count; i++) {
      dzn_cmd_buffer *cmd_buffer =
         container_of(info->command_buffers[i], dzn_cmd_buffer, vk);

      ID3D12CommandList *cmdlists[] = { cmd_buffer->cmdlist };

      util_dynarray_foreach(&cmd_buffer->events.wait, dzn_event *, evt) {
         if (FAILED(queue->cmdqueue->Wait((*evt)->fence, 1)))
            return vk_error(device, VK_ERROR_UNKNOWN);
      }

      util_dynarray_foreach(&cmd_buffer->queries.wait, dzn_cmd_buffer_query_range, range) {
         mtx_lock(&range->qpool->queries_lock);
         for (uint32_t q = range->start; q < range->start + range->count; q++) {
            struct dzn_query *query = &range->qpool->queries[q];

            if (query->fence &&
                FAILED(queue->cmdqueue->Wait(query->fence, query->fence_value)))
               return vk_error(device, VK_ERROR_UNKNOWN);
         }
         mtx_unlock(&range->qpool->queries_lock);
      }

      util_dynarray_foreach(&cmd_buffer->queries.reset, dzn_cmd_buffer_query_range, range) {
         mtx_lock(&range->qpool->queries_lock);
         for (uint32_t q = range->start; q < range->start + range->count; q++) {
            struct dzn_query *query = &range->qpool->queries[q];
            if (query->fence) {
               query->fence->Release();
               query->fence = NULL;
            }
            query->fence_value = 0;
         }
         mtx_unlock(&range->qpool->queries_lock);
      }

      queue->cmdqueue->ExecuteCommandLists(1, cmdlists);

      util_dynarray_foreach(&cmd_buffer->events.signal, dzn_cmd_event_signal, evt) {
         if (FAILED(queue->cmdqueue->Signal(evt->event->fence, evt->value ? 1 : 0)))
            return vk_error(device, VK_ERROR_UNKNOWN);
      }

      util_dynarray_foreach(&cmd_buffer->queries.signal, dzn_cmd_buffer_query_range, range) {
         mtx_lock(&range->qpool->queries_lock);
         for (uint32_t q = range->start; q < range->start + range->count; q++) {
            struct dzn_query *query = &range->qpool->queries[q];
            query->fence_value = queue->fence_point + 1;
            query->fence = queue->fence;
            query->fence->AddRef();
         }
         mtx_unlock(&range->qpool->queries_lock);
      }
   }

   for (uint32_t i = 0; i < info->signal_count; i++) {
      result = dzn_queue_sync_signal(queue, &info->signals[i]);
      if (result != VK_SUCCESS)
         return vk_error(device, VK_ERROR_UNKNOWN);
   }

   if (FAILED(queue->cmdqueue->Signal(queue->fence, ++queue->fence_point)))
      return vk_error(device, VK_ERROR_UNKNOWN);

   return VK_SUCCESS;
}

static void
dzn_queue_finish(dzn_queue *queue)
{
   if (queue->cmdqueue)
      queue->cmdqueue->Release();

   if (queue->fence)
      queue->fence->Release();

   vk_queue_finish(&queue->vk);
}

static VkResult
dzn_queue_init(dzn_queue *queue,
               dzn_device *device,
               const VkDeviceQueueCreateInfo *pCreateInfo,
               uint32_t index_in_family)
{
   dzn_physical_device *pdev = container_of(device->vk.physical, dzn_physical_device, vk);

   VkResult result = vk_queue_init(&queue->vk, &device->vk, pCreateInfo, index_in_family);
   if (result != VK_SUCCESS)
      return result;

   queue->vk.driver_submit = dzn_queue_submit;

   assert(pCreateInfo->queueFamilyIndex < pdev->queue_family_count);

   D3D12_COMMAND_QUEUE_DESC queue_desc =
      pdev->queue_families[pCreateInfo->queueFamilyIndex].desc;

   queue_desc.Priority =
      (INT)(pCreateInfo->pQueuePriorities[index_in_family] * (float)D3D12_COMMAND_QUEUE_PRIORITY_HIGH);
   queue_desc.NodeMask = 0;

   if (FAILED(device->dev->CreateCommandQueue(&queue_desc,
                                              IID_PPV_ARGS(&queue->cmdqueue)))) {
      dzn_queue_finish(queue);
      return vk_error(device->vk.physical->instance, VK_ERROR_INITIALIZATION_FAILED);
   }

   if (FAILED(device->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&queue->fence)))) {
      dzn_queue_finish(queue);
      return vk_error(device->vk.physical->instance, VK_ERROR_INITIALIZATION_FAILED);
   }

   return VK_SUCCESS;
}

static VkResult
check_physical_device_features(VkPhysicalDevice physicalDevice,
                               const VkPhysicalDeviceFeatures *features)
{
   VK_FROM_HANDLE(dzn_physical_device, pdev, physicalDevice);

   VkPhysicalDeviceFeatures supported_features;

   pdev->vk.dispatch_table.GetPhysicalDeviceFeatures(physicalDevice, &supported_features);

   VkBool32 *supported_feature = (VkBool32 *)&supported_features;
   VkBool32 *enabled_feature = (VkBool32 *)features;
   unsigned num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
   for (uint32_t i = 0; i < num_features; i++) {
      if (enabled_feature[i] && !supported_feature[i])
         return VK_ERROR_FEATURE_NOT_PRESENT;
   }

   return VK_SUCCESS;
}

static VkResult
dzn_device_create_sync_for_memory(struct vk_device *device,
                                  VkDeviceMemory memory,
                                  bool signal_memory,
                                  struct vk_sync **sync_out)
{
   return vk_sync_create(device, &vk_sync_dummy_type,
                         (enum vk_sync_flags)0, 1, sync_out);
}

static void
dzn_device_ref_pipeline_layout(struct vk_device *dev, VkPipelineLayout layout)
{
   VK_FROM_HANDLE(dzn_pipeline_layout, playout, layout);

   dzn_pipeline_layout_ref(playout);
}

static void
dzn_device_unref_pipeline_layout(struct vk_device *dev, VkPipelineLayout layout)
{
   VK_FROM_HANDLE(dzn_pipeline_layout, playout, layout);

   dzn_pipeline_layout_unref(playout);
}

static VkResult
dzn_device_query_init(dzn_device *device)
{
   /* FIXME: create the resource in the default heap */
   D3D12_HEAP_PROPERTIES hprops =
      device->dev->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);
   D3D12_RESOURCE_DESC rdesc = {
      .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
      .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
      .Width = DZN_QUERY_REFS_RES_SIZE,
      .Height = 1,
      .DepthOrArraySize = 1,
      .MipLevels = 1,
      .Format = DXGI_FORMAT_UNKNOWN,
      .SampleDesc = { .Count = 1, .Quality = 0 },
      .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
      .Flags = D3D12_RESOURCE_FLAG_NONE,
   };

   if (FAILED(device->dev->CreateCommittedResource(&hprops,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &rdesc,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   NULL,
                                                   IID_PPV_ARGS(&device->queries.refs))))
      return vk_error(device->vk.physical, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   uint8_t *queries_ref;
   if (FAILED(device->queries.refs->Map(0, NULL, (void **)&queries_ref)))
      return vk_error(device->vk.physical, VK_ERROR_OUT_OF_HOST_MEMORY);

   memset(queries_ref + DZN_QUERY_REFS_ALL_ONES_OFFSET, 0xff, DZN_QUERY_REFS_SECTION_SIZE);
   memset(queries_ref + DZN_QUERY_REFS_ALL_ZEROS_OFFSET, 0x0, DZN_QUERY_REFS_SECTION_SIZE);
   device->queries.refs->Unmap(0, NULL);

   return VK_SUCCESS;
}

static void
dzn_device_query_finish(dzn_device *device)
{
   if (device->queries.refs)
      device->queries.refs->Release();
}

static void
dzn_device_destroy(dzn_device *device, const VkAllocationCallbacks *pAllocator)
{
   if (!device)
      return;

   dzn_instance *instance =
      container_of(device->vk.physical->instance, dzn_instance, vk);

   vk_foreach_queue_safe(q, &device->vk) {
      dzn_queue *queue = container_of(q, dzn_queue, vk);

      dzn_queue_finish(queue);
   }

   dzn_device_query_finish(device);
   dzn_meta_finish(device);

   if (device->dev)
      device->dev->Release();

   vk_device_finish(&device->vk);
   vk_free2(&instance->vk.alloc, pAllocator, device);
}

static VkResult
dzn_device_create(dzn_physical_device *pdev,
                  const VkDeviceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkDevice *out)
{
   dzn_instance *instance = container_of(pdev->vk.instance, dzn_instance, vk);

   uint32_t queue_count = 0;
   for (uint32_t qf = 0; qf < pCreateInfo->queueCreateInfoCount; qf++) {
      const VkDeviceQueueCreateInfo *qinfo = &pCreateInfo->pQueueCreateInfos[qf];
      queue_count += qinfo->queueCount;
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, dzn_device, device, 1);
   VK_MULTIALLOC_DECL(&ma, dzn_queue, queues, queue_count);

   if (!vk_multialloc_zalloc2(&ma, &instance->vk.alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return vk_error(pdev, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_device_dispatch_table dispatch_table;

   /* For secondary command buffer support, overwrite any command entrypoints
    * in the main device-level dispatch table with
    * vk_cmd_enqueue_unless_primary_Cmd*.
    */
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      &vk_cmd_enqueue_unless_primary_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      &dzn_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      &wsi_device_entrypoints, false);

   /* Populate our primary cmd_dispatch table. */
   vk_device_dispatch_table_from_entrypoints(&device->cmd_dispatch,
      &dzn_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(&device->cmd_dispatch,
                                             &vk_common_device_entrypoints,
                                             false);

   VkResult result =
      vk_device_init(&device->vk, &pdev->vk, &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, device);
      return result;
   }

   /* Must be done after vk_device_init() because this function memset(0) the
    * whole struct.
    */
   device->vk.command_dispatch_table = &device->cmd_dispatch;
   device->vk.ref_pipeline_layout = dzn_device_ref_pipeline_layout;
   device->vk.unref_pipeline_layout = dzn_device_unref_pipeline_layout;
   device->vk.create_sync_for_memory = dzn_device_create_sync_for_memory;

   device->dev = dzn_physical_device_get_d3d12_dev(pdev);
   if (!device->dev) {
      dzn_device_destroy(device, pAllocator);
      return vk_error(pdev, VK_ERROR_INITIALIZATION_FAILED);
   }

   device->dev->AddRef();

   ID3D12InfoQueue *info_queue;
   if (SUCCEEDED(device->dev->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
      D3D12_MESSAGE_SEVERITY severities[] = {
         D3D12_MESSAGE_SEVERITY_INFO,
         D3D12_MESSAGE_SEVERITY_WARNING,
      };

      D3D12_MESSAGE_ID msg_ids[] = {
         D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
      };

      D3D12_INFO_QUEUE_FILTER NewFilter = {};
      NewFilter.DenyList.NumSeverities = ARRAY_SIZE(severities);
      NewFilter.DenyList.pSeverityList = severities;
      NewFilter.DenyList.NumIDs = ARRAY_SIZE(msg_ids);
      NewFilter.DenyList.pIDList = msg_ids;

      info_queue->PushStorageFilter(&NewFilter);
   }

   result = dzn_meta_init(device);
   if (result != VK_SUCCESS) {
      dzn_device_destroy(device, pAllocator);
      return result;
   }

   result = dzn_device_query_init(device);
   if (result != VK_SUCCESS) {
      dzn_device_destroy(device, pAllocator);
      return result;
   }

   uint32_t qindex = 0;
   for (uint32_t qf = 0; qf < pCreateInfo->queueCreateInfoCount; qf++) {
      const VkDeviceQueueCreateInfo *qinfo = &pCreateInfo->pQueueCreateInfos[qf];

      for (uint32_t q = 0; q < qinfo->queueCount; q++) {
         result =
            dzn_queue_init(&queues[qindex++], device, qinfo, q);
	 if (result != VK_SUCCESS) {
            dzn_device_destroy(device, pAllocator);
            return result;
         }
      }
   }

   assert(queue_count == qindex);
   *out = dzn_device_to_handle(device);
   return VK_SUCCESS;
}

ID3D12RootSignature *
dzn_device_create_root_sig(dzn_device *device,
                           const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *desc)
{
   dzn_instance *instance =
      container_of(device->vk.physical->instance, dzn_instance, vk);
   ComPtr<ID3DBlob> sig, error;

   if (FAILED(instance->d3d12.serialize_root_sig(desc,
                                                 &sig, &error))) {
      if (instance->debug_flags & DZN_DEBUG_SIG) {
         const char* error_msg = (const char*)error->GetBufferPointer();
         fprintf(stderr,
                 "== SERIALIZE ROOT SIG ERROR =============================================\n"
                 "%s\n"
                 "== END ==========================================================\n",
                 error_msg);
      }

      return NULL;
   }

   ID3D12RootSignature *root_sig;
   if (FAILED(device->dev->CreateRootSignature(0,
                                               sig->GetBufferPointer(),
                                               sig->GetBufferSize(),
                                               IID_PPV_ARGS(&root_sig))))
      return NULL;

   return root_sig;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateDevice(VkPhysicalDevice physicalDevice,
                 const VkDeviceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkDevice *pDevice)
{
   VK_FROM_HANDLE(dzn_physical_device, physical_device, physicalDevice);
   dzn_instance *instance =
      container_of(physical_device->vk.instance, dzn_instance, vk);
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   /* Check enabled features */
   if (pCreateInfo->pEnabledFeatures) {
      result = check_physical_device_features(physicalDevice,
                                              pCreateInfo->pEnabledFeatures);
      if (result != VK_SUCCESS)
         return vk_error(physical_device, result);
   }

   /* Check requested queues and fail if we are requested to create any
    * queues with flags we don't support.
    */
   assert(pCreateInfo->queueCreateInfoCount > 0);
   for (uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
      if (pCreateInfo->pQueueCreateInfos[i].flags != 0)
         return vk_error(physical_device, VK_ERROR_INITIALIZATION_FAILED);
   }

   return dzn_device_create(physical_device, pCreateInfo, pAllocator, pDevice);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyDevice(VkDevice dev,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(dzn_device, device, dev);

   device->vk.dispatch_table.DeviceWaitIdle(dev);

   dzn_device_destroy(device, pAllocator);
}

static void
dzn_device_memory_destroy(dzn_device_memory *mem,
                          const VkAllocationCallbacks *pAllocator)
{
   if (!mem)
      return;

   dzn_device *device = container_of(mem->base.device, dzn_device, vk);

   if (mem->map)
      mem->map_res->Unmap(0, NULL);

   if (mem->map_res)
      mem->map_res->Release();

   if (mem->heap)
      mem->heap->Release();

   vk_object_base_finish(&mem->base);
   vk_free2(&device->vk.alloc, pAllocator, mem);
}

static VkResult
dzn_device_memory_create(dzn_device *device,
                         const VkMemoryAllocateInfo *pAllocateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkDeviceMemory *out)
{
   dzn_physical_device *pdevice =
      container_of(device->vk.physical, dzn_physical_device, vk);

   dzn_device_memory *mem = (dzn_device_memory *)
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*mem), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!mem)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &mem->base, VK_OBJECT_TYPE_DEVICE_MEMORY);

   /* The Vulkan 1.0.33 spec says "allocationSize must be greater than 0". */
   assert(pAllocateInfo->allocationSize > 0);

   mem->size = pAllocateInfo->allocationSize;

#if 0
   const VkExportMemoryAllocateInfo *export_info = NULL;
   VkMemoryAllocateFlags vk_flags = 0;
#endif

   vk_foreach_struct_const(ext, pAllocateInfo->pNext) {
      dzn_debug_ignored_stype(ext->sType);
   }

   const VkMemoryType *mem_type =
      &pdevice->memory.memoryTypes[pAllocateInfo->memoryTypeIndex];

   D3D12_HEAP_DESC heap_desc = {};
   // TODO: fix all of these:
   heap_desc.SizeInBytes = pAllocateInfo->allocationSize;
   heap_desc.Alignment =
      heap_desc.SizeInBytes >= D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT ?
      D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT :
      D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
   heap_desc.Flags =
      dzn_physical_device_get_heap_flags_for_mem_type(pdevice,
                                                      pAllocateInfo->memoryTypeIndex);

   /* TODO: Unsure about this logic??? */
   mem->initial_state = D3D12_RESOURCE_STATE_COMMON;
   heap_desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
   heap_desc.Properties.MemoryPoolPreference =
      ((mem_type->propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
       !pdevice->architecture.UMA) ?
      D3D12_MEMORY_POOL_L1 : D3D12_MEMORY_POOL_L0;
   if (mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      heap_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
   } else if (mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      heap_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
   } else {
      heap_desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
   }

   if (FAILED(device->dev->CreateHeap(&heap_desc, IID_PPV_ARGS(&mem->heap)))) {
      dzn_device_memory_destroy(mem, pAllocator);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   if ((mem_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
       !(heap_desc.Flags & D3D12_HEAP_FLAG_DENY_BUFFERS)){
      D3D12_RESOURCE_DESC res_desc = {};
      res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      res_desc.Format = DXGI_FORMAT_UNKNOWN;
      res_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
      res_desc.Width = heap_desc.SizeInBytes;
      res_desc.Height = 1;
      res_desc.DepthOrArraySize = 1;
      res_desc.MipLevels = 1;
      res_desc.SampleDesc.Count = 1;
      res_desc.SampleDesc.Quality = 0;
      res_desc.Flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
      res_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      HRESULT hr = device->dev->CreatePlacedResource(mem->heap, 0, &res_desc,
                                                     mem->initial_state,
                                                     NULL, IID_PPV_ARGS(&mem->map_res));
      if (FAILED(hr)) {
         dzn_device_memory_destroy(mem, pAllocator);
         return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }
   }

   *out = dzn_device_memory_to_handle(mem);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_AllocateMemory(VkDevice device,
                   const VkMemoryAllocateInfo *pAllocateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkDeviceMemory *pMem)
{
   return dzn_device_memory_create(dzn_device_from_handle(device),
                                   pAllocateInfo, pAllocator, pMem);
}

VKAPI_ATTR void VKAPI_CALL
dzn_FreeMemory(VkDevice device,
               VkDeviceMemory mem,
               const VkAllocationCallbacks *pAllocator)
{
   dzn_device_memory_destroy(dzn_device_memory_from_handle(mem), pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_MapMemory(VkDevice _device,
              VkDeviceMemory _memory,
              VkDeviceSize offset,
              VkDeviceSize size,
              VkMemoryMapFlags flags,
              void **ppData)
{
   VK_FROM_HANDLE(dzn_device, device, _device);
   VK_FROM_HANDLE(dzn_device_memory, mem, _memory);

   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   if (size == VK_WHOLE_SIZE)
      size = mem->size - offset;

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */
   assert(size > 0);
   assert(offset + size <= mem->size);

   assert(mem->map_res);
   D3D12_RANGE range = {};
   range.Begin = offset;
   range.End = offset + size;
   void *map = NULL;
   if (FAILED(mem->map_res->Map(0, &range, &map)))
      return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);

   mem->map = map;
   mem->map_size = size;

   *ppData = ((uint8_t*) map) + offset;

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
dzn_UnmapMemory(VkDevice _device,
                VkDeviceMemory _memory)
{
   VK_FROM_HANDLE(dzn_device, device, _device);
   VK_FROM_HANDLE(dzn_device_memory, mem, _memory);

   if (mem == NULL)
      return;

   assert(mem->map_res);
   mem->map_res->Unmap(0, NULL);

   mem->map = NULL;
   mem->map_size = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_FlushMappedMemoryRanges(VkDevice _device,
                            uint32_t memoryRangeCount,
                            const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_InvalidateMappedMemoryRanges(VkDevice _device,
                                 uint32_t memoryRangeCount,
                                 const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

static void
dzn_buffer_destroy(dzn_buffer *buf, const VkAllocationCallbacks *pAllocator)
{
   if (!buf)
      return;

   dzn_device *device = container_of(buf->base.device, dzn_device, vk);

   if (buf->res)
      buf->res->Release();

   vk_object_base_finish(&buf->base);
   vk_free2(&device->vk.alloc, pAllocator, buf);
}

static VkResult
dzn_buffer_create(dzn_device *device,
                  const VkBufferCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkBuffer *out)
{
   dzn_buffer *buf = (dzn_buffer *)
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*buf), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!buf)
     return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &buf->base, VK_OBJECT_TYPE_BUFFER);
   buf->create_flags = pCreateInfo->flags;
   buf->size = pCreateInfo->size;
   buf->usage = pCreateInfo->usage;

   if (buf->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
      buf->size = ALIGN_POT(buf->size, 256);

   buf->desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
   buf->desc.Format = DXGI_FORMAT_UNKNOWN;
   buf->desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
   buf->desc.Width = buf->size;
   buf->desc.Height = 1;
   buf->desc.DepthOrArraySize = 1;
   buf->desc.MipLevels = 1;
   buf->desc.SampleDesc.Count = 1;
   buf->desc.SampleDesc.Quality = 0;
   buf->desc.Flags = D3D12_RESOURCE_FLAG_NONE;
   buf->desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

   if (buf->usage &
       (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
      buf->desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

   *out = dzn_buffer_to_handle(buf);
   return VK_SUCCESS;
}

DXGI_FORMAT
dzn_buffer_get_dxgi_format(VkFormat format)
{
   enum pipe_format pfmt = vk_format_to_pipe_format(format);

   return dzn_pipe_to_dxgi_format(pfmt);
}

D3D12_TEXTURE_COPY_LOCATION
dzn_buffer_get_copy_loc(const dzn_buffer *buf,
                        VkFormat format,
                        const VkBufferImageCopy2KHR *region,
                        VkImageAspectFlagBits aspect,
                        uint32_t layer)
{
   const uint32_t buffer_row_length =
      region->bufferRowLength ? region->bufferRowLength : region->imageExtent.width;
   const uint32_t buffer_image_height =
      region->bufferImageHeight ? region->bufferImageHeight : region->imageExtent.height;

   VkFormat plane_format = dzn_image_get_plane_format(format, aspect);

   enum pipe_format pfmt = vk_format_to_pipe_format(plane_format);
   uint32_t blksz = util_format_get_blocksize(pfmt);
   uint32_t blkw = util_format_get_blockwidth(pfmt);
   uint32_t blkh = util_format_get_blockheight(pfmt);

   D3D12_TEXTURE_COPY_LOCATION loc = {
     .pResource = buf->res,
     .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
     .PlacedFootprint = {
        .Footprint = {
           .Format =
              dzn_image_get_placed_footprint_format(format, aspect),
           .Width = region->imageExtent.width,
           .Height = region->imageExtent.height,
           .Depth = region->imageExtent.depth,
           .RowPitch = blksz * DIV_ROUND_UP(buffer_row_length, blkw),
        },
     },
   };

   uint32_t buffer_layer_stride =
      loc.PlacedFootprint.Footprint.RowPitch *
      DIV_ROUND_UP(loc.PlacedFootprint.Footprint.Height, blkh);

   loc.PlacedFootprint.Offset =
      region->bufferOffset + (layer * buffer_layer_stride);

   return loc;
}

D3D12_TEXTURE_COPY_LOCATION
dzn_buffer_get_line_copy_loc(const dzn_buffer *buf, VkFormat format,
                             const VkBufferImageCopy2KHR *region,
                             const D3D12_TEXTURE_COPY_LOCATION *loc,
                             uint32_t y, uint32_t z, uint32_t *start_x)
{
   uint32_t buffer_row_length =
      region->bufferRowLength ? region->bufferRowLength : region->imageExtent.width;
   uint32_t buffer_image_height =
      region->bufferImageHeight ? region->bufferImageHeight : region->imageExtent.height;

   format = dzn_image_get_plane_format(format, region->imageSubresource.aspectMask);

   enum pipe_format pfmt = vk_format_to_pipe_format(format);
   uint32_t blksz = util_format_get_blocksize(pfmt);
   uint32_t blkw = util_format_get_blockwidth(pfmt);
   uint32_t blkh = util_format_get_blockheight(pfmt);
   uint32_t blkd = util_format_get_blockdepth(pfmt);
   D3D12_TEXTURE_COPY_LOCATION new_loc = *loc;
   uint32_t buffer_row_stride =
      DIV_ROUND_UP(buffer_row_length, blkw) * blksz;
   uint32_t buffer_layer_stride =
      buffer_row_stride *
      DIV_ROUND_UP(buffer_image_height, blkh);

   uint64_t tex_offset =
      ((y / blkh) * buffer_row_stride) +
      ((z / blkd) * buffer_layer_stride);
   uint64_t offset = loc->PlacedFootprint.Offset + tex_offset;
   uint32_t offset_alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;

   while (offset_alignment % blksz)
      offset_alignment += D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;

   new_loc.PlacedFootprint.Footprint.Height = blkh;
   new_loc.PlacedFootprint.Footprint.Depth = 1;
   new_loc.PlacedFootprint.Offset = (offset / offset_alignment) * offset_alignment;
   *start_x = ((offset % offset_alignment) / blksz) * blkw;
   new_loc.PlacedFootprint.Footprint.Width = *start_x + region->imageExtent.width;
   new_loc.PlacedFootprint.Footprint.RowPitch =
      ALIGN_POT(DIV_ROUND_UP(new_loc.PlacedFootprint.Footprint.Width, blkw) * blksz,
                D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
   return new_loc;
}

bool
dzn_buffer_supports_region_copy(const D3D12_TEXTURE_COPY_LOCATION *loc)
{
   return !(loc->PlacedFootprint.Offset & (D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)) &&
          !(loc->PlacedFootprint.Footprint.RowPitch & (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateBuffer(VkDevice device,
                 const VkBufferCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkBuffer *pBuffer)
{
   return dzn_buffer_create(dzn_device_from_handle(device),
                            pCreateInfo, pAllocator, pBuffer);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyBuffer(VkDevice device,
                  VkBuffer buffer,
                  const VkAllocationCallbacks *pAllocator)
{
   dzn_buffer_destroy(dzn_buffer_from_handle(buffer), pAllocator);
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetBufferMemoryRequirements2(VkDevice dev,
                                 const VkBufferMemoryRequirementsInfo2 *pInfo,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VK_FROM_HANDLE(dzn_buffer, buffer, pInfo->buffer);
   dzn_physical_device *pdev =
      container_of(device->vk.physical, dzn_physical_device, vk);

   /* uh, this is grossly over-estimating things */
   uint32_t alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
   VkDeviceSize size = buffer->size;

   if (buffer->usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
      alignment = MAX2(alignment, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
      size = ALIGN_POT(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
   }

   pMemoryRequirements->memoryRequirements.size = size;
   pMemoryRequirements->memoryRequirements.alignment = alignment;
   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      dzn_physical_device_get_mem_type_mask_for_resource(pdev, &buffer->desc);

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *requirements =
            (VkMemoryDedicatedRequirements *)ext;
         /* TODO: figure out dedicated allocations */
         requirements->prefersDedicatedAllocation = false;
         requirements->requiresDedicatedAllocation = false;
         break;
      }

      default:
         dzn_debug_ignored_stype(ext->sType);
         break;
      }
   }

#if 0
   D3D12_RESOURCE_ALLOCATION_INFO GetResourceAllocationInfo(
      UINT                      visibleMask,
      UINT                      numResourceDescs,
      const D3D12_RESOURCE_DESC *pResourceDescs);
#endif
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_BindBufferMemory2(VkDevice _device,
                      uint32_t bindInfoCount,
                      const VkBindBufferMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(dzn_device, device, _device);

   for (uint32_t i = 0; i < bindInfoCount; i++) {
      assert(pBindInfos[i].sType == VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO);

      VK_FROM_HANDLE(dzn_device_memory, mem, pBindInfos[i].memory);
      VK_FROM_HANDLE(dzn_buffer, buffer, pBindInfos[i].buffer);

      if (FAILED(device->dev->CreatePlacedResource(mem->heap,
                                                   pBindInfos[i].memoryOffset,
                                                   &buffer->desc,
                                                   mem->initial_state,
                                                   NULL, IID_PPV_ARGS(&buffer->res))))
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   return VK_SUCCESS;
}

static VkResult
dzn_framebuffer_create(dzn_device *device,
                       const VkFramebufferCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkFramebuffer *out)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, dzn_framebuffer, framebuffer, 1);
   VK_MULTIALLOC_DECL(&ma, dzn_image_view *, attachments, pCreateInfo->attachmentCount);

   if (!vk_multialloc_zalloc2(&ma, &device->vk.alloc, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;

   framebuffer->attachments = attachments;
   framebuffer->attachment_count = pCreateInfo->attachmentCount;
   for (uint32_t i = 0; i < framebuffer->attachment_count; i++) {
      VK_FROM_HANDLE(dzn_image_view, iview, pCreateInfo->pAttachments[i]);
      framebuffer->attachments[i] = iview;
   }

   vk_object_base_init(&device->vk, &framebuffer->base, VK_OBJECT_TYPE_FRAMEBUFFER);
   *out = dzn_framebuffer_to_handle(framebuffer);
   return VK_SUCCESS;
}

static void
dzn_framebuffer_destroy(dzn_framebuffer *framebuffer,
                        const VkAllocationCallbacks *pAllocator)
{
   if (!framebuffer)
      return;

   dzn_device *device =
      container_of(framebuffer->base.device, dzn_device, vk);

   vk_object_base_finish(&framebuffer->base);
   vk_free2(&device->vk.alloc, pAllocator, framebuffer);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateFramebuffer(VkDevice device,
                      const VkFramebufferCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkFramebuffer *pFramebuffer)
{
   return dzn_framebuffer_create(dzn_device_from_handle(device),
                                 pCreateInfo, pAllocator, pFramebuffer);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyFramebuffer(VkDevice device,
                       VkFramebuffer fb,
                       const VkAllocationCallbacks *pAllocator)
{
   dzn_framebuffer_destroy(dzn_framebuffer_from_handle(fb), pAllocator);
}

static void
dzn_event_destroy(dzn_event *event,
                  const VkAllocationCallbacks *pAllocator)
{
   if (!event)
      return;

   dzn_device *device =
      container_of(event->base.device, dzn_device, vk);

   if (event->fence)
      event->fence->Release();

   vk_object_base_finish(&event->base);
   vk_free2(&device->vk.alloc, pAllocator, event);
}

static VkResult
dzn_event_create(dzn_device *device,
                 const VkEventCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkEvent *out)
{
   dzn_event *event = (dzn_event *)
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*event), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &event->base, VK_OBJECT_TYPE_EVENT);

   if (FAILED(device->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&event->fence)))) {
      dzn_event_destroy(event, pAllocator);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   *out = dzn_event_to_handle(event);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateEvent(VkDevice device,
                const VkEventCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator,
                VkEvent *pEvent)
{
   return dzn_event_create(dzn_device_from_handle(device),
                           pCreateInfo, pAllocator, pEvent);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroyEvent(VkDevice device,
                 VkEvent event,
                 const VkAllocationCallbacks *pAllocator)
{
   dzn_event_destroy(dzn_event_from_handle(event), pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_ResetEvent(VkDevice dev,
               VkEvent evt)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VK_FROM_HANDLE(dzn_event, event, evt);

   if (FAILED(event->fence->Signal(0)))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_SetEvent(VkDevice dev,
             VkEvent evt)
{
   VK_FROM_HANDLE(dzn_device, device, dev);
   VK_FROM_HANDLE(dzn_event, event, evt);

   if (FAILED(event->fence->Signal(1)))
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_GetEventStatus(VkDevice device,
                   VkEvent evt)
{
   VK_FROM_HANDLE(dzn_event, event, evt);

   return event->fence->GetCompletedValue() == 0 ?
          VK_EVENT_RESET : VK_EVENT_SET;
}

VKAPI_ATTR void VKAPI_CALL
dzn_GetDeviceMemoryCommitment(VkDevice device,
                              VkDeviceMemory memory,
                              VkDeviceSize *pCommittedMemoryInBytes)
{
   VK_FROM_HANDLE(dzn_device_memory, mem, memory);

   // TODO: find if there's a way to query/track actual heap residency
   *pCommittedMemoryInBytes = mem->size;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_QueueBindSparse(VkQueue queue,
                    uint32_t bindInfoCount,
                    const VkBindSparseInfo *pBindInfo,
                    VkFence fence)
{
   // FIXME: add proper implem
   dzn_stub();
   return VK_SUCCESS;
}

static D3D12_TEXTURE_ADDRESS_MODE
dzn_sampler_translate_addr_mode(VkSamplerAddressMode in)
{
   switch (in) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
   default: unreachable("Invalid address mode");
   }
}

static void
dzn_sampler_destroy(dzn_sampler *sampler,
                    const VkAllocationCallbacks *pAllocator)
{
   if (!sampler)
      return;

   dzn_device *device =
      container_of(sampler->base.device, dzn_device, vk);

   vk_object_base_finish(&sampler->base);
   vk_free2(&device->vk.alloc, pAllocator, sampler);
}

static VkResult
dzn_sampler_create(dzn_device *device,
                   const VkSamplerCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSampler *out)
{
   dzn_sampler *sampler = (dzn_sampler *)
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*sampler), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &sampler->base, VK_OBJECT_TYPE_SAMPLER);

   const VkSamplerCustomBorderColorCreateInfoEXT *pBorderColor = (const VkSamplerCustomBorderColorCreateInfoEXT *)
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);

   /* TODO: have a sampler pool to allocate shader-invisible descs which we
    * can copy to the desc_set when UpdateDescriptorSets() is called.
    */
   sampler->desc.Filter = dzn_translate_sampler_filter(pCreateInfo);
   sampler->desc.AddressU = dzn_sampler_translate_addr_mode(pCreateInfo->addressModeU);
   sampler->desc.AddressV = dzn_sampler_translate_addr_mode(pCreateInfo->addressModeV);
   sampler->desc.AddressW = dzn_sampler_translate_addr_mode(pCreateInfo->addressModeW);
   sampler->desc.MipLODBias = pCreateInfo->mipLodBias;
   sampler->desc.MaxAnisotropy = pCreateInfo->maxAnisotropy;
   sampler->desc.MinLOD = pCreateInfo->minLod;
   sampler->desc.MaxLOD = pCreateInfo->maxLod;

   if (pCreateInfo->compareEnable)
      sampler->desc.ComparisonFunc = dzn_translate_compare_op(pCreateInfo->compareOp);

   bool reads_border_color =
      pCreateInfo->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
      pCreateInfo->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
      pCreateInfo->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

   if (reads_border_color) {
      switch (pCreateInfo->borderColor) {
      case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
      case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
         sampler->desc.BorderColor[0] = 0.0f;
         sampler->desc.BorderColor[1] = 0.0f;
         sampler->desc.BorderColor[2] = 0.0f;
         sampler->desc.BorderColor[3] =
            pCreateInfo->borderColor == VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK ? 0.0f : 1.0f;
         sampler->static_border_color =
            pCreateInfo->borderColor == VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK ?
            D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK :
            D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
         break;
      case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
         sampler->desc.BorderColor[0] = sampler->desc.BorderColor[1] = 1.0f;
         sampler->desc.BorderColor[2] = sampler->desc.BorderColor[3] = 1.0f;
         sampler->static_border_color = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
         break;
      case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
         sampler->static_border_color = (D3D12_STATIC_BORDER_COLOR)-1;
         for (unsigned i = 0; i < ARRAY_SIZE(sampler->desc.BorderColor); i++)
            sampler->desc.BorderColor[i] = pBorderColor->customBorderColor.float32[i];
         break;
      case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      case VK_BORDER_COLOR_INT_CUSTOM_EXT:
         /* FIXME: sampling from integer textures is not supported yet. */
         sampler->static_border_color = (D3D12_STATIC_BORDER_COLOR)-1;
         break;
      default:
         unreachable("Unsupported border color");
      }
   }

   *out = dzn_sampler_to_handle(sampler);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
dzn_CreateSampler(VkDevice device,
                  const VkSamplerCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkSampler *pSampler)
{
   return dzn_sampler_create(dzn_device_from_handle(device),
                             pCreateInfo, pAllocator, pSampler);
}

VKAPI_ATTR void VKAPI_CALL
dzn_DestroySampler(VkDevice device,
                   VkSampler sampler,
                   const VkAllocationCallbacks *pAllocator)
{
   dzn_sampler_destroy(dzn_sampler_from_handle(sampler), pAllocator);
}
