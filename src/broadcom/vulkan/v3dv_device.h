/*
 * Copyright © 2026 Raspberry Pi Ltd
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
#ifndef V3DV_DEVICE_H
#define V3DV_DEVICE_H

#include "v3dv_common.h"
#include "v3dv_bo.h"
#include "v3dv_limits.h"
#include "v3dv_pipeline.h"
#include "vk_device.h"
#include "vk_device_memory.h"
#include "vk_instance.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_sync.h"
#include "vk_sync_timeline.h"
#include "common/v3d_device_info.h"
#include "wsi_common.h"
#include "util/sparse_array.h"
#include "util/xmlconfig.h"

struct v3dv_event;
struct v3dv_format;
struct v3dv_format_plane;
struct v3dv_image;
struct v3dv_image_view;
struct v3dv_job;
struct v3d_compiler;
struct v3d_perfcntrs;
struct v3d_simulator_file;

struct v3dv_physical_device {
   struct vk_physical_device vk;

   char *name;

   /* primary node (cardN) of the render device */
   int32_t primary_fd;
   /* render node (renderN) of the render device */
   int32_t render_fd;
   /* primary node (cardN) of the display device, if available */
   int32_t display_fd;

   /* We need these because it is not clear how to detect
    * valid devids in a portable way
     */
   bool has_primary;
   bool has_render;

   dev_t primary_devid;
   dev_t render_devid;

   uint8_t driver_build_sha1[BLAKE3_KEY_LEN];
   uint8_t pipeline_cache_uuid[VK_UUID_SIZE];
   uint8_t device_uuid[VK_UUID_SIZE];
   uint8_t driver_uuid[VK_UUID_SIZE];

   struct vk_sync_type drm_syncobj_type;
   struct vk_sync_timeline_type sync_timeline_type;
   const struct vk_sync_type *sync_types[3];

   struct disk_cache *disk_cache;

   mtx_t mutex;

   struct wsi_device wsi_device;

   VkPhysicalDeviceMemoryProperties memory;

   struct v3d_device_info devinfo;
   struct v3d_perfcntrs *perfcntr;

#if USE_V3D_SIMULATOR
   struct v3d_simulator_file *sim_file;
#endif

   const struct v3d_compiler *compiler;
   uint32_t next_program_id;

   alignas(8) uint64_t heap_used;

   /* This array holds all our 'struct v3dv_bo' allocations. We use this
    * so we can add a refcount to our BOs and check if a particular BO
    * was already allocated in this device using its GEM handle. This is
    * necessary to properly manage BO imports, because the kernel doesn't
    * refcount the underlying BO memory.
    *
    * Specifically, when self-importing (i.e. importing a BO into the same
    * device that created it), the kernel will give us the same BO handle
    * for both BOs and we must only free it once when  both references are
    * freed. Otherwise, if we are not self-importing, we get two different BO
    * handles, and we want to free each one individually.
    *
    * The BOs in this map all have a refcnt with the reference counter and
    * only self-imported BOs will ever have a refcnt > 1.
    */
   struct util_sparse_array bo_map;

   struct {
      bool merge_jobs;
   } options;

   struct {
      bool cpu_queue;
      bool multisync;
      bool perfmon;
   } caps;
};

static inline struct v3dv_bo *
v3dv_device_lookup_bo(struct v3dv_physical_device *device, uint32_t handle)
{
   return (struct v3dv_bo *) util_sparse_array_get(&device->bo_map, handle);
}

VkResult v3dv_wsi_init(struct v3dv_physical_device *physical_device);
void v3dv_wsi_finish(struct v3dv_physical_device *physical_device);

void v3dv_meta_clear_init(struct v3dv_device *device);
void v3dv_meta_clear_finish(struct v3dv_device *device);

void v3dv_meta_blit_init(struct v3dv_device *device);
void v3dv_meta_blit_finish(struct v3dv_device *device);

void v3dv_meta_texel_buffer_copy_init(struct v3dv_device *device);
void v3dv_meta_texel_buffer_copy_finish(struct v3dv_device *device);

bool v3dv_meta_can_use_tlb(struct v3dv_image *image,
                           uint8_t plane,
                           uint8_t miplevel,
                           const VkOffset3D *offset,
                           const VkExtent3D *extent,
                           VkFormat *compat_format);

struct v3dv_instance {
   struct vk_instance vk;

   struct driOptionCache dri_options;
   struct driOptionCache available_dri_options;

   bool pipeline_cache_enabled;
   bool default_pipeline_cache_enabled;
   bool meta_cache_enabled;
};

/* FIXME: In addition to tracking the last job submitted by GPU queue (cl, csd,
 * tfu), we still need a syncobj to track the last overall job submitted
 * (V3DV_QUEUE_ANY) for the case we don't support multisync. Someday we can
 * start expecting multisync to be present and drop the legacy implementation
 * together with this V3DV_QUEUE_ANY tracker.
 */
enum v3dv_queue_type {
   V3DV_QUEUE_CL = 0,
   V3DV_QUEUE_CSD,
   V3DV_QUEUE_TFU,
   V3DV_QUEUE_CPU,
   V3DV_QUEUE_ANY,
   V3DV_QUEUE_COUNT,
};

/* For each GPU queue, we use a syncobj to track the last job submitted. We
 * set the flag `first` to determine when we are starting a new cmd buffer
 * batch and therefore a job submitted to a given queue will be the first in a
 * cmd buf batch.
 */
struct v3dv_last_job_sync {
   /* If the job is the first submitted to a GPU queue in a cmd buffer batch.
    *
    * We use V3DV_QUEUE_{CL,CSD,TFU} both with and without multisync.
    */
   bool first[V3DV_QUEUE_COUNT];
   /* Array of syncobj to track the last job submitted to a GPU queue.
    *
    * With multisync we use V3DV_QUEUE_{CL,CSD,TFU} to track syncobjs for each
    * queue, but without multisync we only track the last job submitted to any
    * queue in V3DV_QUEUE_ANY.
    */
   uint32_t syncs[V3DV_QUEUE_COUNT];
};

struct v3dv_queue {
   struct vk_queue vk;

   struct v3dv_device *device;

   struct v3dv_last_job_sync last_job_syncs;
};

VkResult v3dv_queue_driver_submit(struct vk_queue *vk_queue,
                                  struct vk_queue_submit *submit);

VkResult v3dv_device_create_noop_job(struct v3dv_device *device);

#define V3DV_META_BLIT_CACHE_KEY_SIZE              (4 * sizeof(uint32_t))
#define V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE (3 * sizeof(uint32_t) + \
                                                    sizeof(VkComponentMapping))

struct v3dv_meta_color_clear_pipeline {
   VkPipeline pipeline;
   VkRenderPass pass;
   bool cached;
   uint64_t key;
};

struct v3dv_meta_depth_clear_pipeline {
   VkPipeline pipeline;
   uint64_t key;
};

struct v3dv_meta_blit_pipeline {
   VkPipeline pipeline;
   VkRenderPass pass;
   VkRenderPass pass_no_load;
   uint8_t key[V3DV_META_BLIT_CACHE_KEY_SIZE];
};

struct v3dv_meta_texel_buffer_copy_pipeline {
   VkPipeline pipeline;
   VkRenderPass pass;
   VkRenderPass pass_no_load;
   uint8_t key[V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE];
};

struct v3dv_device {
   struct vk_device vk;

   struct v3dv_instance *instance;
   struct v3dv_physical_device *pdevice;

   struct v3d_device_info devinfo;
   struct v3dv_queue *queues;
   uint32_t queue_count;

   /* In cases where we instantiate more than one queue (Android), this protects
    * against concurrent access from multiple queues.
    */
   mtx_t queue_mutex;

   struct v3dv_job *noop_job;

   /* The last active perfmon ID to prevent mixing of counter results when a
    * job is submitted with a different perfmon id.
    */
   uint32_t last_perfmon_id;

   /* Guards query->maybe_available and value for timestamps */
   mtx_t query_mutex;

   /* Signaled whenever a query is ended */
   cnd_t query_ended;

   /* Resources used for meta operations */
   struct {
      mtx_t mtx;
      struct {
         VkPipelineLayout p_layout;
         struct hash_table *cache; /* v3dv_meta_color_clear_pipeline */
      } color_clear;
      struct {
         VkPipelineLayout p_layout;
         struct hash_table *cache; /* v3dv_meta_depth_clear_pipeline */
      } depth_clear;
      struct {
         VkDescriptorSetLayout ds_layout;
         VkPipelineLayout p_layout;
         struct hash_table *cache[3]; /* v3dv_meta_blit_pipeline for 1d, 2d, 3d */
      } blit;
      struct {
         VkDescriptorSetLayout ds_layout;
         VkPipelineLayout p_layout;
         struct hash_table *cache[3]; /* v3dv_meta_texel_buffer_copy_pipeline for 1d, 2d, 3d */
      } texel_buffer_copy;
   } meta;

   struct v3dv_bo_cache {
      /** List of struct v3d_bo freed, by age. */
      struct list_head time_list;
      /** List of struct v3d_bo freed, per size, by age. */
      struct list_head *size_list;
      uint32_t size_list_size;

      mtx_t lock;

      uint32_t cache_size;
      uint32_t cache_count;
      uint32_t max_cache_size;
   } bo_cache;

   uint32_t bo_size;
   uint32_t bo_count;

   /* Event handling resources.
    *
    * Our implementation of events uses a BO to store event state (signaled vs
    * reset) and dispatches compute shaders to handle GPU event functions
    * (signal, reset, wait). This struct holds all the resources required
    * by the implementation.
    */
   struct {
      mtx_t lock;

      /* BO for the event states: signaled (1) or reset (0) */
      struct v3dv_bo *bo;

      /* We pre-allocate all the events we can fit for the size of the BO we
       * create to track their states, where each event has an index which is
       * basically the offset of its state in that BO. We keep a free list with
       * the pre-allocated events that are available.
       */
      uint32_t event_count;
      struct v3dv_event *events;
      struct list_head free_list;

      /* Vulkan resources to access the event BO from shaders. We have a
       * pipeline that sets the state of an event and another that waits on
       * a single event. Both pipelines require access to the event state BO,
       * for which we need to allocate a single descripot set.
       */
      VkBuffer buffer;
      VkDeviceMemory mem;
      VkDescriptorSetLayout descriptor_set_layout;
      VkPipelineLayout pipeline_layout;
      VkDescriptorPool descriptor_pool;
      VkDescriptorSet descriptor_set;
      VkPipeline set_event_pipeline;
      VkPipeline wait_event_pipeline;
   } events;

   /* Query handling resources.
    *
    * Our implementation of occlusion queries uses a BO per pool to keep track
    * of the per-query availability state and dispatches compute shaders to
    * handle GPU query functions that read and write that state. This struct
    * holds Vulkan resources that can be shared across all query pools to
    * implement this. This framework may be extended in the future to handle
    * more query types.
    */
   struct {
      VkDescriptorSetLayout buf_descriptor_set_layout;

      /* Set query availability */
      VkPipelineLayout avail_pipeline_layout;
      VkPipeline avail_pipeline;

      /* Reset query availability and clear occlusion counters */
      VkPipelineLayout reset_occlusion_pipeline_layout;
      VkPipeline reset_occlusion_pipeline;

      /* Copy query results */
      VkPipelineLayout copy_pipeline_layout;
      VkPipeline copy_pipeline[8];
   } queries;

   struct v3dv_pipeline_cache default_pipeline_cache;

   /* GL_SHADER_STATE_RECORD needs to specify default attribute values. The
    * following covers the most common case, that is all attributes format
    * being float being float, allowing us to reuse the same BO for all
    * pipelines matching this requirement. Pipelines that need integer
    * attributes will create their own BO.
    *
    * Note that since v71 the default attribute values are not needed, so this
    * can be NULL.
    */
   struct v3dv_bo *default_attribute_float;

   void *device_address_mem_ctx;
   struct util_dynarray device_address_bo_list; /* Array of struct v3dv_bo * */
};

struct v3dv_device_memory {
   struct vk_device_memory vk;

   struct v3dv_bo *bo;
   const VkMemoryType *type;
   bool is_for_wsi;
   bool is_for_device_address;
};

uint32_t v3dv_physical_device_vendor_id(const struct v3dv_physical_device *dev);
uint32_t v3dv_physical_device_device_id(const struct v3dv_physical_device *dev);

static inline bool
v3dv_texture_shader_state_has_rb_swap_reverse_bits(const struct v3dv_device *device)
{
   return device->devinfo.ver > 71 ||
          (device->devinfo.ver == 71 && device->devinfo.rev >= 5);
}

VK_DEFINE_HANDLE_CASTS(v3dv_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)
VK_DEFINE_HANDLE_CASTS(v3dv_instance, vk.base, VkInstance,
                       VK_OBJECT_TYPE_INSTANCE)
VK_DEFINE_HANDLE_CASTS(v3dv_physical_device, vk.base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)
VK_DEFINE_HANDLE_CASTS(v3dv_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_device_memory, vk.base, VkDeviceMemory,
                               VK_OBJECT_TYPE_DEVICE_MEMORY)

#endif /* V3DV_DEVICE_H */
