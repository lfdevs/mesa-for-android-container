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
#ifndef VK_INSTANCE_H
#define VK_INSTANCE_H

#include "vk_dispatch_table.h"
#include "vk_extensions.h"
#include "vk_object.h"

#include "c11/threads.h"
#include "util/list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_app_info {
   /** VkApplicationInfo::pApplicationName */
   const char*        app_name;

   /** VkApplicationInfo::applicationVersion */
   uint32_t           app_version;

   /** VkApplicationInfo::pEngineName */
   const char*        engine_name;

   /** VkApplicationInfo::engineVersion */
   uint32_t           engine_version;

   /** VkApplicationInfo::apiVersion or `VK_API_VERSION_1_0`
    *
    * If the application does not provide a `pApplicationInfo` or the
    * `apiVersion` field is 0, this is set to `VK_API_VERSION_1_0`.
    */
   uint32_t           api_version;
};

/** Base struct for all `VkInstance` implementations
 *
 * This contains data structures necessary for detecting enabled extensions,
 * handling entrypoint dispatch, and implementing `vkGetInstanceProcAddr()`.
 * It also contains data copied from the `VkInstanceCreateInfo` such as the
 * application information.
 */
struct vk_instance {
   struct vk_object_base base;

   /** Allocator used when creating this instance
    *
    * This is used as a fall-back for when a NULL pAllocator is passed into a
    * device-level create function such as vkCreateImage().
    */
   VkAllocationCallbacks alloc;

   /** VkInstanceCreateInfo::pApplicationInfo */
   struct vk_app_info app_info;

   /** Table of all enabled instance extensions
    *
    * This is generated automatically as part of `vk_instance_init()` from
    * VkInstanceCreateInfo::ppEnabledExtensionNames.
    */
   struct vk_instance_extension_table enabled_extensions;

   /** Instance-level dispatch table */
   struct vk_instance_dispatch_table dispatch_table;

   /* VK_EXT_debug_report debug callbacks */
   struct {
      mtx_t callbacks_mutex;
      struct list_head callbacks;
   } debug_report;

   /* VK_EXT_debug_utils */
   struct {
      /* These callbacks are only used while creating or destroying an
       * instance
       */
      struct list_head instance_callbacks;
      mtx_t callbacks_mutex;
      /* Persistent callbacks */
      struct list_head callbacks;
   } debug_utils;
};

VK_DEFINE_HANDLE_CASTS(vk_instance, base, VkInstance,
                       VK_OBJECT_TYPE_INSTANCE);

/** Initialize a vk_instance
 *
 * Along with initializing the data structures in `vk_instance`, this function
 * validates the Vulkan version number provided by the client and checks that
 * every extension specified by
 * `VkInstanceCreateInfo::ppEnabledExtensionNames` is actually supported by
 * the implementation and returns `VK_ERROR_EXTENSION_NOT_PRESENT` if an
 * unsupported extension is requested.
 *
 * @param[out] instance             The instance to initialize
 * @param[in]  supported_extensions Table of all instance extensions supported
 *                                  by this instance
 * @param[in]  dispatch_table       Instance-level dispatch table
 * @param[in]  pCreateInfo          VkInstanceCreateInfo pointer passed to
 *                                  `vkCreateInstance()`
 * @param[in]  alloc                Allocation callbacks used to create this
 *                                  instance; must not be `NULL`
 */
VkResult MUST_CHECK
vk_instance_init(struct vk_instance *instance,
                 const struct vk_instance_extension_table *supported_extensions,
                 const struct vk_instance_dispatch_table *dispatch_table,
                 const VkInstanceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *alloc);

/** Tears down a vk_instance
 *
 * @param[out] instance             The instance to tear down
 */
void
vk_instance_finish(struct vk_instance *instance);

/** Implementaiton of vkEnumerateInstanceExtensionProperties() */
VkResult
vk_enumerate_instance_extension_properties(
    const struct vk_instance_extension_table *supported_extensions,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties);

/** Implementaiton of vkGetInstanceProcAddr() */
PFN_vkVoidFunction
vk_instance_get_proc_addr(const struct vk_instance *instance,
                          const struct vk_instance_entrypoint_table *entrypoints,
                          const char *name);

/** Unchecked version of vk_instance_get_proc_addr
 *
 * This is identical to `vk_instance_get_proc_addr()` except that it doesn't
 * check whether extensions are enabled before returning function pointers.
 * This is useful in window-system code where we may use extensions without
 * the client explicitly enabling them.
 */
PFN_vkVoidFunction
vk_instance_get_proc_addr_unchecked(const struct vk_instance *instance,
                                    const char *name);

/** Implementaiton of vk_icdGetPhysicalDeviceProcAddr() */
PFN_vkVoidFunction
vk_instance_get_physical_device_proc_addr(const struct vk_instance *instance,
                                          const char *name);

#ifdef __cplusplus
}
#endif

#endif /* VK_INSTANCE_H */
