/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_DEVICE_H
#define VN_DEVICE_H

#include "vn_common.h"

#include "vn_cs.h"
#include "vn_device_memory.h"
#include "vn_renderer.h"
#include "vn_ring.h"
#include "vn_wsi.h"

struct vn_instance {
   struct vn_instance_base base;

   struct driOptionCache dri_options;
   struct driOptionCache available_dri_options;

   struct vn_renderer *renderer;
   struct vn_renderer_info renderer_info;
   uint32_t renderer_version;

   /* to synchronize renderer/ring */
   mtx_t roundtrip_mutex;
   uint32_t roundtrip_next;

   struct {
      mtx_t mutex;
      struct vn_renderer_bo *bo;
      struct vn_ring ring;
      uint64_t id;

      struct vn_cs_encoder upload;
      uint32_t command_dropped;
   } ring;

   struct {
      struct vn_renderer_bo *bo;
      size_t size;
      size_t used;
      void *ptr;
   } reply;

   mtx_t physical_device_mutex;
   struct vn_physical_device *physical_devices;
   uint32_t physical_device_count;
};
VK_DEFINE_HANDLE_CASTS(vn_instance,
                       base.base.base,
                       VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)

struct vn_physical_device {
   struct vn_physical_device_base base;

   struct vn_instance *instance;

   uint32_t renderer_version;
   struct vk_device_extension_table renderer_extensions;

   uint32_t *extension_spec_versions;

   VkPhysicalDeviceFeatures2 features;
   VkPhysicalDeviceVulkan11Features vulkan_1_1_features;
   VkPhysicalDeviceVulkan12Features vulkan_1_2_features;
   VkPhysicalDeviceTransformFeedbackFeaturesEXT transform_feedback_features;

   VkPhysicalDeviceProperties2 properties;
   VkPhysicalDeviceVulkan11Properties vulkan_1_1_properties;
   VkPhysicalDeviceVulkan12Properties vulkan_1_2_properties;
   VkPhysicalDeviceTransformFeedbackPropertiesEXT
      transform_feedback_properties;

   VkQueueFamilyProperties2 *queue_family_properties;
   uint32_t *queue_family_sync_queue_bases;
   uint32_t queue_family_count;

   VkPhysicalDeviceMemoryProperties2 memory_properties;

   VkExternalMemoryHandleTypeFlags external_memory_handles;
   VkExternalFenceHandleTypeFlags external_fence_handles;
   VkExternalSemaphoreHandleTypeFlags external_binary_semaphore_handles;
   VkExternalSemaphoreHandleTypeFlags external_timeline_semaphore_handles;

   struct wsi_device wsi_device;
};
VK_DEFINE_HANDLE_CASTS(vn_physical_device,
                       base.base.base,
                       VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

struct vn_device {
   struct vn_device_base base;

   struct vn_instance *instance;
   struct vn_physical_device *physical_device;

   struct vn_queue *queues;
   uint32_t queue_count;

   struct vn_device_memory_pool memory_pools[VK_MAX_MEMORY_TYPES];
};
VK_DEFINE_HANDLE_CASTS(vn_device,
                       base.base.base,
                       VkDevice,
                       VK_OBJECT_TYPE_DEVICE)

VkResult
vn_instance_submit_roundtrip(struct vn_instance *instance,
                             uint32_t *roundtrip_seqno);

void
vn_instance_wait_roundtrip(struct vn_instance *instance,
                           uint32_t roundtrip_seqno);

static inline void
vn_instance_roundtrip(struct vn_instance *instance)
{
   uint32_t roundtrip_seqno;
   if (vn_instance_submit_roundtrip(instance, &roundtrip_seqno) == VK_SUCCESS)
      vn_instance_wait_roundtrip(instance, roundtrip_seqno);
}

VkResult
vn_instance_ring_submit(struct vn_instance *instance,
                        const struct vn_cs_encoder *cs);

static inline void
vn_instance_ring_wait(struct vn_instance *instance)
{
   struct vn_ring *ring = &instance->ring.ring;
   vn_ring_wait_all(ring);
}

struct vn_instance_submit_command {
   /* empty command implies errors */
   struct vn_cs_encoder command;
   /* non-zero implies waiting */
   size_t reply_size;

   /* when reply_size is non-zero, NULL can be returned on errors */
   struct vn_renderer_bo *reply_bo;
   struct vn_cs_decoder reply;
};

void
vn_instance_submit_command(struct vn_instance *instance,
                           struct vn_instance_submit_command *submit);

#endif /* VN_DEVICE_H */
