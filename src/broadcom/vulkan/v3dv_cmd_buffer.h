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
#ifndef V3DV_CMD_BUFFER_H
#define V3DV_CMD_BUFFER_H

#include "v3dv_common.h"
#include "v3dv_limits.h"
#include "v3dv_pipeline.h"
#include "v3dv_pass.h"
#include "v3dv_query.h"
#include "v3dv_cl.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_sync.h"
#include "util/set.h"

struct v3dv_buffer;
struct v3dv_descriptor_set;
struct v3dv_device;
struct v3dv_image;
struct v3dv_image_view;

enum v3dv_cmd_buffer_status {
   V3DV_CMD_BUFFER_STATUS_NEW           = 0,
   V3DV_CMD_BUFFER_STATUS_INITIALIZED   = 1,
   V3DV_CMD_BUFFER_STATUS_RECORDING     = 2,
   V3DV_CMD_BUFFER_STATUS_EXECUTABLE    = 3
};

union v3dv_clear_value {
   uint32_t color[4];
   struct {
      float z;
      uint8_t s;
   };
};

struct v3dv_cmd_buffer_attachment_state {
   /* The original clear value as provided by the Vulkan API */
   VkClearValue vk_clear_value;

   /* The hardware clear value */
   union v3dv_clear_value clear_value;

   /* The underlying image view (from the framebuffer or, if imageless
    * framebuffer is used, from VkRenderPassAttachmentBeginInfo.
    */
   struct v3dv_image_view *image_view;

   /* If this is a multisampled attachment with a resolve operation. */
   bool has_resolve;

   /* If this is a multisampled attachment with a resolve operation,
    * whether we can use the TLB for the resolve.
    */
   bool use_tlb_resolve;
};

enum v3dv_job_type {
   V3DV_JOB_TYPE_GPU_CL = 0,
   V3DV_JOB_TYPE_GPU_CL_INCOMPLETE,
   V3DV_JOB_TYPE_GPU_TFU,
   V3DV_JOB_TYPE_GPU_CSD,
   V3DV_JOB_TYPE_CPU_RESET_QUERIES,
   V3DV_JOB_TYPE_CPU_END_QUERY,
   V3DV_JOB_TYPE_CPU_COPY_QUERY_RESULTS,
   V3DV_JOB_TYPE_CPU_CSD_INDIRECT,
   V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY,
};

struct v3dv_reset_query_cpu_job_info {
   struct v3dv_query_pool *pool;
   uint32_t first;
   uint32_t count;
};

struct v3dv_end_query_info {
   struct v3dv_query_pool *pool;
   uint32_t query;

   /* This is one unless multiview is used */
   uint32_t count;
};

struct v3dv_copy_query_results_cpu_job_info {
   struct v3dv_query_pool *pool;
   uint32_t first;
   uint32_t count;
   struct v3dv_buffer *dst;
   uint32_t offset;
   uint32_t stride;
   VkQueryResultFlags flags;
};

struct v3dv_submit_sync_info {
   /* List of syncs to wait before running a job */
   uint32_t wait_count;
   struct vk_sync_wait *waits;

   /* List of syncs to signal when all jobs complete */
   uint32_t signal_count;
   struct vk_sync_signal *signals;
};

struct v3dv_csd_indirect_cpu_job_info {
   struct v3dv_buffer *buffer;
   uint32_t offset;
   struct v3dv_job *csd_job;
   uint32_t wg_size;
   uint32_t *wg_uniform_offsets[3];
   bool needs_wg_uniform_rewrite;
};

struct v3dv_timestamp_query_cpu_job_info {
   struct v3dv_query_pool *pool;
   uint32_t query;

   /* This is one unless multiview is used */
   uint32_t count;
};

struct v3dv_job {
   struct list_head list_link;

   /* We only create job clones when executing secondary command buffers into
    * primaries. These clones don't make deep copies of the original object
    * so we want to flag them to avoid freeing resources they don't own.
    */
   bool is_clone;

   /* If this is a cloned job, if it has its own BCL resource. This happens
    * when we suspend jobs with in command buffers with the
    * VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT flag.
    */
   bool clone_owns_bcl;

   /* VK_KHR_dynamic_rendering */
   bool suspending;
   bool resuming;
   struct v3dv_cl_out *suspend_branch_inst_ptr;
   uint32_t suspended_bcl_end;

   /* If the job executes on the transfer stage of the pipeline */
   bool is_transfer;

   /* VK_KHR_buffer_device_address allows shaders to use pointers that can
    * dereference memory in any buffer that has been flagged with
    * VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT. These buffers may not
    * be bound via descriptor sets, so we need to make sure that a job that
    * uses this functionality includes all these buffers in its kernel
    * submission.
    */
   bool uses_buffer_device_address;

   /* True if we have not identified anything that would be incompatible
    * with double-buffer (like MSAA) or that would make double-buffer mode
    * not efficient (like tile loads or not having any stores).
    */
   bool can_use_double_buffer;

   /* This structure keeps track of various scores to inform a heuristic
    * for double-buffer mode.
    */
   struct v3d_double_buffer_score double_buffer_score;

   /* We only need to allocate tile state for all layers if the binner
    * writes primitives to layers other than the first. This can only be
    * done using layered rendering (writing gl_Layer from a geometry shader),
    * so for other cases of multilayered framebuffers (typically with
    * meta copy/clear operations) that won't use layered rendering, we only
    * need one layer worth of of tile state for the binner.
    */
   bool allocate_tile_state_for_all_layers;

   /* A pointer to the location of the TILE_BINNING_MODE_CFG packet so we can
    * rewrite it to enable double-buffer mode by the time we have enough info
    * about the job to make that decision.
    */
   struct v3dv_cl_out *bcl_tile_binning_mode_ptr;

   enum v3dv_job_type type;

   struct v3dv_device *device;

   struct v3dv_cmd_buffer *cmd_buffer;

   struct v3dv_cl bcl;
   struct v3dv_cl rcl;
   struct v3dv_cl indirect;

   /* Set of all BOs referenced by the job. This will be used for making
    * the list of BOs that the kernel will need to have paged in to
    * execute our job.
    */
   struct set *bos;
   uint32_t bo_count;
   uint64_t bo_handle_mask;

   struct v3dv_bo *tile_alloc;
   struct v3dv_bo *tile_state;

   bool tmu_dirty_rcl;

   uint32_t first_subpass;

   /* When the current subpass is split into multiple jobs, this flag is set
    * to true for any jobs after the first in the same subpass.
    */
   bool is_subpass_continue;

   /* If this job is the last job emitted for a subpass. */
   bool is_subpass_finish;

   struct v3dv_frame_tiling frame_tiling;

   enum v3dv_ez_state ez_state;
   enum v3dv_ez_state first_ez_state;

   /* If we have already decided if we need to disable Early Z/S completely
    * for this job.
    */
   bool decided_global_ez_enable;

   /* If the job emitted any draw calls with Early Z/S enabled */
   bool has_ez_draws;

   /* If this job has been configured to use early Z/S clear */
   bool early_zs_clear;

   /* Number of draw calls recorded into the job */
   uint32_t draw_count;

   /* A flag indicating whether we want to flush every draw separately. This
    * can be used for debugging, or for cases where special circumstances
    * require this behavior.
    */
   bool always_flush;

   /* A mask of V3DV_BARRIER_* indicating the source(s) of the barrier. We
    * can use this to select the hw queues where we need to serialize the job.
    */
   uint8_t serialize;

   /* If this is a CL job, whether we should sync before binning */
   bool needs_bcl_sync;

   /* If we have emitted a (default) point size packet in this job */
   bool emitted_default_point_size;

   /* Job specs for CPU jobs */
   union {
      struct v3dv_reset_query_cpu_job_info          query_reset;
      struct v3dv_end_query_info                    query_end;
      struct v3dv_copy_query_results_cpu_job_info   query_copy_results;
      struct v3dv_csd_indirect_cpu_job_info         csd_indirect;
      struct v3dv_timestamp_query_cpu_job_info      query_timestamp;
   } cpu;

   /* Job specs for TFU jobs */
   struct drm_v3d_submit_tfu tfu;

   /* Job specs for CSD jobs */
   struct {
      struct v3dv_bo *shared_memory;
      uint32_t wg_count[3];
      uint32_t wg_base[3];
      struct drm_v3d_submit_csd submit;
   } csd;

   /* Perfmons with last job sync for CSD and CL jobs */
   struct v3dv_perf_query *perf;
};

void v3dv_job_init(struct v3dv_job *job,
                   enum v3dv_job_type type,
                   struct v3dv_device *device,
                   struct v3dv_cmd_buffer *cmd_buffer,
                   int32_t subpass_idx);
void v3dv_job_destroy(struct v3dv_job *job);

void v3dv_job_add_bo(struct v3dv_job *job, struct v3dv_bo *bo);
void v3dv_job_add_bo_unchecked(struct v3dv_job *job, struct v3dv_bo *bo);

void v3dv_job_start_frame(struct v3dv_job *job,
                          uint32_t width,
                          uint32_t height,
                          uint32_t layers,
                          bool allocate_tile_state_for_all_layers,
                          uint32_t render_target_count,
                          uint8_t max_internal_bpp,
                          uint8_t total_color_bpp,
                          bool msaa);

bool v3dv_job_type_is_gpu(struct v3dv_job *job);

struct v3dv_job *
v3dv_job_clone(struct v3dv_job *job, bool skip_bcl);

struct v3dv_job *
v3dv_job_clone_in_cmd_buffer(struct v3dv_job *job,
                             struct v3dv_cmd_buffer *cmd_buffer);

struct v3dv_job *v3dv_cmd_buffer_create_cpu_job(struct v3dv_device *device,
                                                enum v3dv_job_type type,
                                                struct v3dv_cmd_buffer *cmd_buffer,
                                                uint32_t subpass_idx);

void
v3dv_cmd_buffer_ensure_array_state(struct v3dv_cmd_buffer *cmd_buffer,
                                   uint32_t slot_size,
                                   uint32_t used_count,
                                   uint32_t *alloc_count,
                                   void **ptr);

void v3dv_cmd_buffer_emit_pre_draw(struct v3dv_cmd_buffer *cmd_buffer,
                                   bool indexed, bool indirect,
                                   uint32_t vertex_count);

bool v3dv_job_allocate_tile_state(struct v3dv_job *job);

void
v3dv_setup_dynamic_framebuffer(struct v3dv_cmd_buffer *cmd_buffer,
                               const VkRenderingInfoKHR *pRenderingInfo);

void
v3dv_destroy_dynamic_framebuffer(struct v3dv_cmd_buffer *cmd_buffer);

void
v3dv_setup_dynamic_render_pass(struct v3dv_cmd_buffer *cmd_buffer,
                               const VkRenderingInfoKHR *pRenderingInfo);

void
v3dv_setup_dynamic_render_pass_inheritance(struct v3dv_cmd_buffer *cmd_buffer,
                                           const VkCommandBufferInheritanceRenderingInfo *info);

struct v3dv_draw_info {
   uint32_t vertex_count;
   uint32_t instance_count;
   uint32_t first_vertex;
   uint32_t first_instance;
};

struct v3dv_vertex_binding {
   struct v3dv_buffer *buffer;
   VkDeviceSize offset;
   VkDeviceSize size;
};

struct v3dv_descriptor_state {
   struct v3dv_descriptor_set *descriptor_sets[MAX_SETS];
   uint32_t valid;
   uint32_t dynamic_offsets[MAX_DYNAMIC_BUFFERS];
};

struct v3dv_cmd_pipeline_state {
   struct v3dv_pipeline *pipeline;

   struct v3dv_descriptor_state descriptor_state;
};

enum {
   V3DV_BARRIER_GRAPHICS_BIT = (1 << 0),
   V3DV_BARRIER_COMPUTE_BIT  = (1 << 1),
   V3DV_BARRIER_TRANSFER_BIT = (1 << 2),
   V3DV_BARRIER_CPU_BIT      = (1 << 3),
};
#define V3DV_BARRIER_ALL (V3DV_BARRIER_GRAPHICS_BIT | \
                          V3DV_BARRIER_TRANSFER_BIT | \
                          V3DV_BARRIER_COMPUTE_BIT | \
                          V3DV_BARRIER_CPU_BIT);

struct v3dv_barrier_state {
   /* Mask of V3DV_BARRIER_* indicating where we consume a barrier. */
   uint8_t dst_mask;

   /* For each possible consumer of a barrier, a mask of V3DV_BARRIER_*
    * indicating the sources of the dependency.
    */
   uint8_t src_mask_graphics;
   uint8_t src_mask_transfer;
   uint8_t src_mask_compute;

   /* For graphics barriers, access masks involved. Used to decide if we need
    * to execute a binning or render barrier.
    */
   VkAccessFlags2 bcl_buffer_access;
   VkAccessFlags2 bcl_image_access;
};

struct v3dv_cmd_buffer_state {
   struct v3dv_render_pass *pass;
   struct v3dv_framebuffer *framebuffer;

   /* VK_KHR_dynamic_rendering */
   struct v3dv_render_pass dynamic_pass;
   struct v3dv_subpass dynamic_subpass;
   struct v3dv_render_pass_attachment dynamic_attachments[18 /* (8 color + D/S) x 2 (for resolves) */];
   struct v3dv_subpass_attachment dynamic_subpass_attachments[18];
   struct v3dv_framebuffer *dynamic_framebuffer;

   VkRect2D render_area;

   /* Current job being recorded */
   struct v3dv_job *job;

   uint32_t subpass_idx;

   struct v3dv_cmd_pipeline_state gfx;
   struct v3dv_cmd_pipeline_state compute;

   /* For most state tracking we rely on vk_dynamic_graphics_state, but we
    * maintain a custom structure for some state-related data that we want to
    * cache.
    */
   struct v3dv_dynamic_state dynamic;

   /* This dirty is for v3dv_cmd_dirty_bits (FIXME: perhaps we should be more
    * explicit about it). For dirty flags coming from Vulkan dynamic state,
    * use the vk_dynamic_graphics_state handled by the vk_cmd_buffer
    */
   uint32_t dirty;
   VkShaderStageFlagBits dirty_descriptor_stages;
   VkShaderStageFlagBits dirty_push_constants_stages;

   /* Current clip window. We use this to check whether we have an active
    * scissor, since in that case we can't use TLB clears and need to fallback
    * to drawing rects.
    */
   VkRect2D clip_window;

   /* Whether our render area is aligned to tile boundaries. If this is false
    * then we have tiles that are only partially covered by the render area,
    * and therefore, we need to be careful with our loads and stores so we don't
    * modify pixels for the tile area that is not covered by the render area.
    * This means, for example, that we can't use the TLB to clear, since that
    * always clears full tiles.
    */
   bool tile_aligned_render_area;

   /* FIXME: we have just one client-side BO for the push constants,
    * independently of the stageFlags in vkCmdPushConstants, and the
    * pipelineBindPoint in vkCmdBindPipeline. We could probably do more stage
    * tuning in the future if it makes sense.
    */
   uint32_t push_constants_size;
   uint32_t push_constants_data[MAX_PUSH_CONSTANTS_SIZE / 4];

   uint32_t attachment_alloc_count;
   struct v3dv_cmd_buffer_attachment_state *attachments;

   struct v3dv_vertex_binding vertex_bindings[MAX_VBS];

   struct {
      VkBuffer buffer;
      VkDeviceSize offset;
      VkDeviceSize size;
      uint8_t index_size;
   } index_buffer;

   /* Current uniforms */
   struct {
      struct v3dv_cl_reloc vs_bin;
      struct v3dv_cl_reloc vs;
      struct v3dv_cl_reloc gs_bin;
      struct v3dv_cl_reloc gs;
      struct v3dv_cl_reloc fs;
   } uniforms;

   /* Current view index for multiview rendering */
   uint32_t view_index;

   /* Current draw ID for multidraw */
   uint32_t draw_id;

   /* Used to flag OOM conditions during command buffer recording */
   bool oom;

   /* If we are currently recording job(s) for a transfer operation */
   bool is_transfer;

   /* VK_KHR_dynamic_rendering */
   bool suspending;
   bool resuming;

   /* Barrier state tracking */
   struct v3dv_barrier_state barrier;

   /* Secondary command buffer state */
   struct {
      bool occlusion_query_enable;
   } inheritance;

   /* Command buffer state saved during a meta operation */
   struct {
      uint32_t subpass_idx;
      VkRenderPass pass;
      VkFramebuffer framebuffer;

      uint32_t attachment_alloc_count;
      uint32_t attachment_count;
      struct v3dv_cmd_buffer_attachment_state *attachments;

      bool tile_aligned_render_area;
      VkRect2D render_area;

      struct vk_dynamic_graphics_state dynamic_graphics_state;
      struct v3dv_dynamic_state dynamic;

      struct v3dv_cmd_pipeline_state gfx;
      bool has_descriptor_state;

      uint32_t push_constants[MAX_PUSH_CONSTANTS_SIZE / 4];
      uint32_t push_constants_size;
   } meta;

   /* Command buffer state for queries */
   struct {
      /* A list of vkCmdQueryEnd commands recorded in the command buffer during
       * a render pass. We queue these here and then schedule the corresponding
       * CPU jobs for them at the time we finish the GPU job in which they have
       * been recorded.
       */
      struct {
         uint32_t used_count;
         uint32_t alloc_count;
         struct v3dv_end_query_info *states;
      } end;

      struct {
         /* This BO is not NULL if we have an active occlusion query, that is,
          * we have called vkCmdBeginQuery but not vkCmdEndQuery.
          */
         struct v3dv_bo *bo;
         uint32_t offset;
         /* When the driver emits draw calls to implement other operations in
          * the middle of a render pass (such as an attachment clear), we need
          * to pause occlusion query recording and resume it later so that
          * these draw calls don't register in occlussion counters. We use
          * this to store the BO reference in which we should resume occlusion
          * query counters after the driver is done emitting its draw calls.
           */
         struct v3dv_bo *paused_bo;

         /* This pointer is not NULL if we have an active performance query */
         struct v3dv_perf_query *perf;
      } active_query;
   } query;

   /* This is dynamic state since VK_EXT_extended_dynamic_state. */
   bool z_updates_enable;

   /* ez_state can be dynamic since VK_EXT_extended_dynamic_state so we need
    * to keep track of it in the cmd_buffer state
    */
   enum v3dv_ez_state ez_state;

   /* incompatible_ez_test can be dynamic since VK_EXT_extended_dynamic_state
    * so we need to keep track of it in the cmd_buffer state
    */
   bool incompatible_ez_test;
};

void
v3dv_cmd_buffer_state_get_viewport_z_xform(struct v3dv_cmd_buffer *cmd_buffer,
                                           uint32_t vp_idx,
                                           float *translate_z,
                                           float *scale_z);

VkResult
v3dv_query_allocate_resources(struct v3dv_device *decice);

void
v3dv_query_free_resources(struct v3dv_device *decice);

VkResult v3dv_get_query_pool_results_cpu(struct v3dv_device *device,
                                         struct v3dv_query_pool *pool,
                                         uint32_t first,
                                         uint32_t count,
                                         void *data,
                                         VkDeviceSize stride,
                                         VkQueryResultFlags flags);

void v3dv_reset_query_pool_cpu(struct v3dv_device *device,
                               struct v3dv_query_pool *query_pool,
                               uint32_t first,
                               uint32_t last);

void v3dv_cmd_buffer_emit_set_query_availability(struct v3dv_cmd_buffer *cmd_buffer,
                                                 struct v3dv_query_pool *pool,
                                                 uint32_t query, uint32_t count,
                                                 uint8_t availability);

typedef void (*v3dv_cmd_buffer_private_obj_destroy_cb)(VkDevice device,
                                                       uint64_t pobj,
                                                       VkAllocationCallbacks *alloc);
struct v3dv_cmd_buffer_private_obj {
   struct list_head list_link;
   uint64_t obj;
   v3dv_cmd_buffer_private_obj_destroy_cb destroy_cb;
};

extern const struct vk_command_buffer_ops v3dv_cmd_buffer_ops;

struct v3dv_cmd_buffer {
   struct vk_command_buffer vk;

   struct v3dv_device *device;

   VkCommandBufferUsageFlags usage_flags;

   enum v3dv_cmd_buffer_status status;

   struct v3dv_cmd_buffer_state state;

   /* Buffer where we upload push constant data to resolve indirect indexing */
   struct v3dv_cl_reloc push_constants_resource;

   /* Collection of Vulkan objects created internally by the driver (typically
    * during recording of meta operations) that are part of the command buffer
    * and should be destroyed with it.
    */
   struct list_head private_objs; /* v3dv_cmd_buffer_private_obj */

   /* Per-command buffer resources for meta operations. */
   struct {
      struct {
         /* The current descriptor pool for blit sources */
         VkDescriptorPool dspool;
      } blit;
      struct {
         /* The current descriptor pool for texel buffer copy sources */
         VkDescriptorPool dspool;
      } texel_buffer_copy;
      struct {
         /* The current descriptor pool for the copy query results output buffer */
         VkDescriptorPool dspool;
      } query;
   } meta;

   /* List of jobs in the command buffer. For primary command buffers it
    * represents the jobs we want to submit to the GPU. For secondary command
    * buffers it represents jobs that will be merged into a primary command
    * buffer via vkCmdExecuteCommands.
    */
   struct list_head jobs;
};

struct v3dv_job *v3dv_cmd_buffer_start_job(struct v3dv_cmd_buffer *cmd_buffer,
                                           int32_t subpass_idx,
                                           enum v3dv_job_type type);
void v3dv_cmd_buffer_finish_job(struct v3dv_cmd_buffer *cmd_buffer);

struct v3dv_job *v3dv_cmd_buffer_subpass_start(struct v3dv_cmd_buffer *cmd_buffer,
                                               uint32_t subpass_idx);
struct v3dv_job *v3dv_cmd_buffer_subpass_resume(struct v3dv_cmd_buffer *cmd_buffer,
                                                uint32_t subpass_idx);

void v3dv_cmd_buffer_subpass_finish(struct v3dv_cmd_buffer *cmd_buffer);

void v3dv_cmd_buffer_meta_state_push(struct v3dv_cmd_buffer *cmd_buffer,
                                     bool push_descriptor_state);
void v3dv_cmd_buffer_meta_state_pop(struct v3dv_cmd_buffer *cmd_buffer,
                                    bool needs_subpass_resume);

void v3dv_cmd_buffer_begin_query(struct v3dv_cmd_buffer *cmd_buffer,
                                 struct v3dv_query_pool *pool,
                                 uint32_t query,
                                 VkQueryControlFlags flags);

void v3dv_cmd_buffer_pause_occlusion_query(struct v3dv_cmd_buffer *cmd_buffer);
void v3dv_cmd_buffer_resume_occlusion_query(struct v3dv_cmd_buffer *cmd_buffer);

void v3dv_cmd_buffer_end_query(struct v3dv_cmd_buffer *cmd_buffer,
                               struct v3dv_query_pool *pool,
                               uint32_t query);

void v3dv_cmd_buffer_copy_query_results(struct v3dv_cmd_buffer *cmd_buffer,
                                        struct v3dv_query_pool *pool,
                                        uint32_t first,
                                        uint32_t count,
                                        struct v3dv_buffer *dst,
                                        uint32_t offset,
                                        uint32_t stride,
                                        VkQueryResultFlags flags);

void v3dv_cmd_buffer_add_tfu_job(struct v3dv_cmd_buffer *cmd_buffer,
                                 struct drm_v3d_submit_tfu *tfu);

void v3dv_cmd_buffer_rewrite_indirect_csd_job(struct v3dv_device *device,
                                              struct v3dv_csd_indirect_cpu_job_info *info,
                                              const uint32_t *wg_counts);

void v3dv_cmd_buffer_add_private_obj(struct v3dv_cmd_buffer *cmd_buffer,
                                     uint64_t obj,
                                     v3dv_cmd_buffer_private_obj_destroy_cb destroy_cb);

void v3dv_merge_barrier_state(struct v3dv_barrier_state *dst,
                              struct v3dv_barrier_state *src);

void v3dv_cmd_buffer_consume_bcl_sync(struct v3dv_cmd_buffer *cmd_buffer,
                                      struct v3dv_job *job);

bool v3dv_cmd_buffer_check_needs_load(const struct v3dv_cmd_buffer_state *state,
                                      VkImageAspectFlags aspect,
                                      uint32_t first_subpass_idx,
                                      VkAttachmentLoadOp load_op,
                                      uint32_t last_subpass_idx,
                                      VkAttachmentStoreOp store_op);

bool v3dv_cmd_buffer_check_needs_store(const struct v3dv_cmd_buffer_state *state,
                                       VkImageAspectFlags aspect,
                                       uint32_t last_subpass_idx,
                                       VkAttachmentStoreOp store_op);

void v3dv_cmd_buffer_emit_pipeline_barrier(struct v3dv_cmd_buffer *cmd_buffer,
                                           const VkDependencyInfo *info);

bool v3dv_cmd_buffer_copy_image_tfu(struct v3dv_cmd_buffer *cmd_buffer,
                                    struct v3dv_image *dst,
                                    struct v3dv_image *src,
                                    const VkImageCopy2 *region);
void v3dv_cmd_buffer_resolve_image(struct v3dv_cmd_buffer *cmd_buffer,
                                   struct v3dv_image *dst,
                                   struct v3dv_image *src,
                                   VkFormat resolve_format,
                                   const VkImageResolve2 *region);

bool v3dv_job_apply_barrier_state(struct v3dv_job *job,
                                  struct v3dv_barrier_state *barrier);

/* Flags OOM conditions in command buffer state.
 *
 * Note: notice that no-op jobs don't have a command buffer reference.
 */
static inline void
v3dv_flag_oom(struct v3dv_cmd_buffer *cmd_buffer, struct v3dv_job *job)
{
   if (cmd_buffer) {
      cmd_buffer->state.oom = true;
   } else {
      assert(job);
      if (job->cmd_buffer)
         job->cmd_buffer->state.oom = true;
   }
}

static inline struct v3dv_descriptor_state *
v3dv_cmd_buffer_get_descriptor_state(struct v3dv_cmd_buffer *cmd_buffer,
                                     struct v3dv_pipeline *pipeline)
{
   if (v3dv_pipeline_get_binding_point(pipeline) == VK_PIPELINE_BIND_POINT_COMPUTE)
      return &cmd_buffer->state.compute.descriptor_state;
   else
      return &cmd_buffer->state.gfx.descriptor_state;
}

#define v3dv_return_if_oom(_cmd_buffer, _job) do {                  \
   const struct v3dv_cmd_buffer *__cmd_buffer = _cmd_buffer;        \
   if (__cmd_buffer && __cmd_buffer->state.oom)                     \
      return;                                                       \
   const struct v3dv_job *__job = _job;                             \
   if (__job && __job->cmd_buffer && __job->cmd_buffer->state.oom)  \
      return;                                                       \
} while(0)                                                          \

VK_DEFINE_HANDLE_CASTS(v3dv_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

#endif /* V3DV_CMD_BUFFER_H */
