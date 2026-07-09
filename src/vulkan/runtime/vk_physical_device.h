/*
 * Copyright © 2021 Intel Corporation
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
#ifndef VK_PHYSICAL_DEVICE_H
#define VK_PHYSICAL_DEVICE_H

#include <stdbool.h>

#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_physical_device_features.h"
#include "vk_physical_device_properties.h"
#include "vk_util.h"

#include "compiler/spirv/spirv_info.h"

#include "util/list.h"
#include "util/os_misc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct disk_cache;
struct wsi_device;
struct vk_sync_type;
struct vk_pipeline_cache_object_ops;

/** Base struct for all VkPhysicalDevice implementations
 */
struct vk_physical_device {
   struct vk_object_base base;

   /* See vk_instance::pdevices::list */
   struct list_head link;

   /** Instance which is the parent of this physical device */
   struct vk_instance *instance;

   /** Table of all supported device extensions
    *
    * This table is initialized from the `supported_extensions` parameter
    * passed to `vk_physical_device_init()` if not `NULL`.  If a `NULL`
    * extension table is passed, all extensions are initialized to false and
    * it's the responsibility of the driver to populate the table.  This may
    * be useful if the driver's physical device initialization order is such
    * that extension support cannot be determined until significant physical
    * device setup work has already been done.
    */
   struct vk_device_extension_table supported_extensions;

   /** Table of all supported features
    *
    * This table is initialized from the `supported_features` parameter
    * passed to `vk_physical_device_init()` if not `NULL`.  If a `NULL`
    * features table is passed, all features are initialized to false and
    * it's the responsibility of the driver to populate the table.  This may
    * be useful if the driver's physical device initialization order is such
    * that feature support cannot be determined until significant physical
    * device setup work has already been done.
    */
   struct vk_features supported_features;

   /** Table of all physical device properties which is initialized similarly
    * to supported_features
    */
   struct vk_properties properties;

   /** Physical-device-level dispatch table */
   struct vk_physical_device_dispatch_table dispatch_table;

   /** Disk cache, or NULL */
   struct disk_cache *disk_cache;

   /** WSI device, or NULL */
   struct wsi_device *wsi_device;

   /** A null-terminated array of supported sync types, in priority order
    *
    * The common implementations of VkFence and VkSemaphore use this list to
    * determine what vk_sync_type to use for each scenario.  The list is
    * walked and the first vk_sync_type matching their criterion is taken.
    * For instance, VkFence requires that it not be a timeline and support
    * reset and CPU wait.  If an external handle type is requested, that is
    * considered just one more criterion.
    */
   const struct vk_sync_type *const *supported_sync_types;

   /** A null-terminated array of supported pipeline cache object types
    *
    * The common implementation of VkPipelineCache uses this to remember the
    * type of objects stored in the cache and deserialize them immediately
    * when importing the cache. If an object type isn't in this list, then it
    * will be loaded as a raw data object and then deserialized when we first
    * look it up. Deserializing immediately avoids a copy but may be more
    * expensive for objects that aren't hit.
    */
   const struct vk_pipeline_cache_object_ops *const *pipeline_cache_import_ops;
};

VK_DEFINE_HANDLE_CASTS(vk_physical_device, base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE);

/** Initialize a vk_physical_device
 *
 * :param physical_device:      |out| The physical device to initialize
 * :param instance:             |in|  The instance which is the parent of this
 *                                    physical device
 * :param supported_extensions: |in|  Table of all device extensions supported
 *                                    by this physical device
 * :param supported_features:   |in|  Table of all features supported by this
 *                                    physical device
 * :param dispatch_table:       |in|  Physical-device-level dispatch table
 */
VkResult MUST_CHECK
vk_physical_device_init(struct vk_physical_device *physical_device,
                        struct vk_instance *instance,
                        const struct vk_device_extension_table *supported_extensions,
                        const struct vk_features *supported_features,
                        const struct vk_properties *properties,
                        const struct vk_physical_device_dispatch_table *dispatch_table);

/** Tears down a vk_physical_device
 *
 * :param physical_device:      |out| The physical device to tear down
 */
void
vk_physical_device_finish(struct vk_physical_device *physical_device);

VkResult
vk_physical_device_check_device_features(struct vk_physical_device *physical_device,
                                         const VkDeviceCreateInfo *pCreateInfo);

struct spirv_capabilities
vk_physical_device_get_spirv_capabilities(const struct vk_physical_device *pdev);

/** Calculate GPU heap budget based on the provided available memory
 *
 * :param available_memory:  |in| Total available memory
 * :param available_percent: |in| Percentage to apply to the available memory
 * :param heap_size:         |in| Size of the system memory exposed as a GPU
 *                                heap
 * :param used:              |in| Heap memory used up. Can be `0` if the driver
 *                                doesn't track allocations and relies on just
 *                                the available system memory
 */
static inline uint64_t
vk_physical_device_heap_budget(uint64_t available_memory,
                               float available_percent,
                               uint64_t heap_size,
                               uint64_t used)
{
   available_memory *= available_percent;

   /* From the Vulkan 1.3.278 spec:
    *
    *    "heapBudget is an array of VK_MAX_MEMORY_HEAPS VkDeviceSize
    *    values in which memory budgets are returned, with one
    *    element for each memory heap. A heap’s budget is a rough
    *    estimate of how much memory the process can allocate from
    *    that heap before allocations may fail or cause performance
    *    degradation. The budget includes any currently allocated
    *    device memory."
    *
    * and
    *
    *    "The heapBudget value must be less than or equal to
    *    VkMemoryHeap::size for each heap."
    *
    * available (queried above) is the total amount free memory
    * system-wide and does not include our allocations so we need
    * to add that in.
    */
   available_memory += used;
   available_memory = MIN2(heap_size, available_memory);

   return ROUND_DOWN_TO(available_memory, 1 << 20);
}

static inline uint64_t
vk_physical_device_heap_budget_from_system(struct vk_physical_device *physical_device,
                                           float available_percent,
                                           uint64_t heap_size,
                                           uint64_t used)
{
   uint64_t available_memory;

   const bool success = os_get_available_system_memory(&available_memory);
   if (!success) {
      const void *log_objs[] = { (const void *)physical_device };
      __vk_log(VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
               VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
               1, log_objs, __FILE__, __LINE__,
               "Failed to query available system memory");
      return 0;
   }

   return vk_physical_device_heap_budget(available_memory, available_percent,
                                         heap_size, used);
}

static inline VkImageCreateFlags2KHR
vk_image_format_info_2_flags(const VkPhysicalDeviceImageFormatInfo2 *info)
{
   const VkImageCreateFlags2CreateInfoKHR *flags_create_info =
      vk_find_struct_const(info->pNext, IMAGE_CREATE_FLAGS_2_CREATE_INFO_KHR);
   if (flags_create_info) {
      return flags_create_info->flags;
   } else {
      return info->flags;
   }
}

static inline VkImageUsageFlags2KHR
vk_image_format_info_2_usage(const VkPhysicalDeviceImageFormatInfo2 *info)
{
   const VkImageUsageFlags2CreateInfoKHR *usage_create_info =
      vk_find_struct_const(info->pNext, IMAGE_USAGE_FLAGS_2_CREATE_INFO_KHR);
   if (usage_create_info) {
      return usage_create_info->usage;
   } else {
      return info->usage;
   }
}

static inline VkImageUsageFlags2KHR
vk_sparse_image_format_info_2_usage(const VkPhysicalDeviceSparseImageFormatInfo2 *info)
{
   const VkImageUsageFlags2CreateInfoKHR *usage_create_info =
      vk_find_struct_const(info->pNext, IMAGE_USAGE_FLAGS_2_CREATE_INFO_KHR);
   if (usage_create_info) {
      return usage_create_info->usage;
   } else {
      return info->usage;
   }
}

#ifdef __cplusplus
}
#endif

#endif /* VK_PHYSICAL_DEVICE_H */
