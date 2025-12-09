/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#ifndef NVK_DEVICE_H
#define NVK_DEVICE_H 1

#include "nvk_private.h"

#include "nvk_edb_bview_cache.h"
#include "nvk_descriptor_table.h"
#include "nvk_heap.h"
#include "nvk_queue.h"
#include "nvk_upload_queue.h"
#include "vk_device.h"
#include "vk_meta.h"
#include "vk_queue.h"

struct nvk_physical_device;
struct nvkmd_dev;
struct nvkmd_mem;
struct vk_pipeline_cache;

struct nvk_slm_area {
   simple_mtx_t mutex;
   struct nvkmd_mem *mem;
   uint32_t bytes_per_warp;
   uint32_t bytes_per_tpc;
};

struct nvkmd_mem *
nvk_slm_area_get_mem_ref(struct nvk_slm_area *area,
                         uint32_t *bytes_per_warp_out,
                         uint32_t *bytes_per_mp_out);

struct nvk_device {
   struct vk_device vk;

   struct nvkmd_dev *nvkmd;

   struct nvk_upload_queue upload;

   struct nvkmd_mem *zero_page;
   struct nvk_descriptor_table images;
   struct nvk_descriptor_table samplers;
   struct nvk_edb_bview_cache edb_bview_cache;
   struct nvk_heap shader_heap;
   struct nvk_heap event_heap;
   struct nvk_heap qmd_heap;
   struct nvk_slm_area slm;
   struct nvkmd_mem *vab_memory;

   struct vk_meta_device meta;

   struct nvk_shader *copy_queries;
};

VK_DEFINE_HANDLE_CASTS(nvk_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)

VkResult nvk_device_ensure_slm(struct nvk_device *dev,
                               uint32_t slm_bytes_per_lane,
                               uint32_t crs_bytes_per_warp);

static inline const struct nvk_physical_device *
nvk_device_physical(const struct nvk_device *dev)
{
   return (struct nvk_physical_device *)dev->vk.physical;
}

static inline struct nvk_physical_device *
nvk_device_physical_mut(struct nvk_device *dev)
{
   return (struct nvk_physical_device *)dev->vk.physical;
}

VkResult nvk_device_init_meta(struct nvk_device *dev);
void nvk_device_finish_meta(struct nvk_device *dev);

#endif
