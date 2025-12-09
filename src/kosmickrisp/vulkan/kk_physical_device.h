/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_PHYSICAL_DEVICE_H
#define KK_PHYSICAL_DEVICE_H 1

#include "kk_private.h"

#include "kosmickrisp/bridge/mtl_types.h"

#include "vk_physical_device.h"
#include "vk_sync.h"
#include "vk_sync_binary.h"

#include "wsi_common.h"

#include <sys/types.h>

struct kk_instance;
struct kk_physical_device;

struct kk_queue_family {
   VkQueueFlags queue_flags;
   uint32_t queue_count;
};

struct kk_memory_heap {
   uint64_t size;
   uint64_t used;
   VkMemoryHeapFlags flags;
   uint64_t (*available)(struct kk_physical_device *pdev);
};

struct kk_device_info {
   uint32_t max_workgroup_count[3];
   uint32_t max_workgroup_invocations;
};

struct kk_physical_device {
   struct vk_physical_device vk;
   mtl_device *mtl_dev_handle;
   struct kk_device_info info;

   struct wsi_device wsi_device;

   uint8_t device_uuid[VK_UUID_SIZE];

   // TODO: add mapable VRAM heap if possible
   struct kk_memory_heap mem_heaps[3];
   VkMemoryType mem_types[3];
   uint8_t mem_heap_count;
   uint8_t mem_type_count;

   // Emulated binary sync type
   struct vk_sync_binary_type sync_binary_type;
   const struct vk_sync_type *sync_types[3];

   struct kk_queue_family queue_families[3];
   uint8_t queue_family_count;
};

static inline uint32_t
kk_min_cbuf_alignment()
{
   /* Size of vec4 */
   return 16;
}

VK_DEFINE_HANDLE_CASTS(kk_physical_device, vk.base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

static inline struct kk_instance *
kk_physical_device_instance(struct kk_physical_device *pdev)
{
   return (struct kk_instance *)pdev->vk.instance;
}

VkResult kk_enumerate_physical_devices(struct vk_instance *_instance);
void kk_physical_device_destroy(struct vk_physical_device *vk_device);

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) ||                                    \
   defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_XLIB_KHR) ||    \
   defined(VK_USE_PLATFORM_DISPLAY_KHR) || defined(VK_USE_PLATFORM_METAL_EXT)
#define KK_USE_WSI_PLATFORM
#endif

#endif // KK_PHYSICAL_DEVICE_H
