/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_BUFFER_H
#define PANVK_CMD_BUFFER_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vulkan/runtime/vk_command_buffer.h"

#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_dispatch.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_descriptor_set.h"
#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"
#include "panvk_shader.h"

#include "pan_jc.h"

#include "util/list.h"

#include "genxml/gen_macros.h"

struct panvk_batch {
   struct list_head node;
   struct util_dynarray jobs;
#if PAN_ARCH == 9
   /* MALLOC_VERTEX jobs, the GPU writes their vertex array pointer. */
   struct util_dynarray malloc_vertex_jobs;
#endif
   /* The vertex/tiler chain writes SYSTEM_TIMESTAMP values (v9, and v6/v7
    * since jm-v7 021). */
   bool timestamps;
   /* The vertex/tiler chain reads or writes query memory the fragment jobs
    * of earlier batches count into (occlusion, pipeline statistics): it
    * never takes the PANVK_OVERLAP path (jm-v7 023). */
   bool after_frag;
   struct util_dynarray event_ops;
   struct pan_jc vtc_jc;
   struct pan_jc frag_jc;
   struct {
      struct pan_ptr desc;
      uint32_t desc_stride;
      uint32_t bo_count;

      /* One slot per color, two more slots for the depth/stencil buffers. */
      struct pan_kmod_bo *bos[MAX_RTS + 2];
      uint32_t layer_count;
   } fb;
   struct {
      struct pan_kmod_bo *src, *dst;
   } blit;
   struct pan_ptr tls;
   struct {
      struct pan_tiler_context ctx;
      struct pan_ptr heap_desc;
      struct pan_ptr ctx_descs;
      struct mali_tiler_heap_packed heap_templ;
      struct mali_tiler_context_packed ctx_templ;
   } tiler;
   struct pan_tls_info tlsinfo;
   unsigned wls_total_size;
   bool issued;
   /* libpoly heap bottom reset already queued in this batch */
   bool poly_heap_reset;
   /* v67b: tiler heap half used by this batch */
   unsigned heap_half;
   /* jm-v7 106: worst-case indirect varying bytes of the tess draws in this
    * batch, and the next free byte of tess_gs_scratch[heap_half].
    */
   uint64_t tess_vary_w;
   uint64_t tess_gs_off;
   /* jm-v7 106: the offset words (uint32_t *, CPU side) of the indirect
    * varying allocators created in this batch, reset to 0 when the batch
    * is issued again.
    */
   struct util_dynarray vary_infos;
};

enum panvk_cmd_event_op_type {
   PANVK_EVENT_OP_SET,
   PANVK_EVENT_OP_RESET,
   PANVK_EVENT_OP_WAIT,
};

struct panvk_cmd_event_op {
   enum panvk_cmd_event_op_type type;
   struct panvk_event *event;
};

struct panvk_cmd_buffer {
   struct vk_command_buffer vk;

   struct panvk_pool desc_pool;
   struct panvk_pool varying_pool;
   /* Growable GPU-only memory: geometry emulation of large tess draws. */
   struct panvk_pool gs_pool;
   struct panvk_pool tls_pool;
   /* jm-v7 107: the TLS and WLS buffers of this command buffer, one per
    * PANVK_OVERLAP heap half, reused by every batch of that half that
    * fits (a batch runs after the previous batch of its half completed).
    * Before, every batch allocated its own, so splitting a frame into
    * many batches multiplied them.
    */
   struct {
      uint64_t tls_ptr, wls_ptr;
      uint64_t tls_size, wls_size;
   } tls_half[2];
   struct list_head batches;
   struct list_head push_sets;
   struct panvk_batch *cur_batch;
   /* VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT: record-time CPU clears of
    * GPU-written scratch are safe (jm-v7 080).
    */
   bool one_time_submit;

   struct {
      struct panvk_cmd_graphics_state gfx;
      struct panvk_cmd_compute_state compute;
      struct panvk_push_constant_state push_constants;
      bool uses_poly_heap;
#if PAN_ARCH <= 9
      struct {
         bool enabled;
         /* Saved by meta, secondaries replay into the primary on JM. */
         bool inherited;
         bool inverted;
         uint64_t addr;
         /* Jobs of the current conditional draw that are never NULLed
          * (the xfb scans, jm-v7 074): they read the predicate themselves.
          */
         struct mali_job_header_packed *keep[8];
         uint32_t keep_count;
      } cond_render;
#endif
   } state;
};

#if PAN_ARCH <= 9
/* A draw or dispatch recorded while conditional rendering is active: the
 * predicate job queued before its jobs (see cmd_cond_render_begin). */
struct panvk_cond_render_mark {
   struct panvk_batch *batch;
   struct mali_job_header_packed *job;
   uint64_t *list;
   unsigned index;
};
#endif

VK_DEFINE_HANDLE_CASTS(panvk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

#define panvk_cmd_buffer_obj_list_init(cmdbuf, list_name)                      \
   list_inithead(&(cmdbuf)->list_name)

#define panvk_cmd_buffer_obj_list_cleanup(cmdbuf, list_name)                   \
   do {                                                                        \
      struct panvk_cmd_pool *__pool =                                          \
         container_of(cmdbuf->vk.pool, struct panvk_cmd_pool, vk);             \
      list_splicetail(&(cmdbuf)->list_name, &__pool->list_name);               \
   } while (0)

#define panvk_cmd_buffer_obj_list_reset(cmdbuf, list_name)                     \
   do {                                                                        \
      struct panvk_cmd_pool *__pool =                                          \
         container_of(cmdbuf->vk.pool, struct panvk_cmd_pool, vk);             \
      list_splicetail(&(cmdbuf)->list_name, &__pool->list_name);               \
      list_inithead(&(cmdbuf)->list_name);                                     \
   } while (0)

static inline struct panvk_descriptor_state *
panvk_cmd_get_desc_state(struct panvk_cmd_buffer *cmdbuf,
                         VkPipelineBindPoint bindpoint)
{
   switch (bindpoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmdbuf->state.gfx.desc_state;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmdbuf->state.compute.desc_state;

   default:
      assert(!"Unsupported bind point");
      return NULL;
   }
}

extern const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops);

struct panvk_batch *
   panvk_per_arch(cmd_open_batch)(struct panvk_cmd_buffer *cmdbuf);

void panvk_per_arch(cmd_close_batch)(struct panvk_cmd_buffer *cmdbuf);

#if PAN_ARCH <= 9
void panvk_per_arch(cmd_cond_render_begin)(struct panvk_cmd_buffer *cmdbuf,
                                           struct panvk_cond_render_mark *m);
void panvk_per_arch(cmd_cond_render_end)(struct panvk_cmd_buffer *cmdbuf,
                                         struct panvk_cond_render_mark *m);
#endif

VkResult panvk_per_arch(cmd_alloc_fb_desc)(struct panvk_cmd_buffer *cmdbuf);

VkResult panvk_per_arch(cmd_alloc_tls_desc)(struct panvk_cmd_buffer *cmdbuf,
                                            bool gfx);

VkResult
   panvk_per_arch(cmd_prepare_tiler_context)(struct panvk_cmd_buffer *cmdbuf,
                                             uint32_t layer_idx);

void panvk_per_arch(cmd_bind_shaders)(struct vk_command_buffer *vk_cmd,
                                      uint32_t stage_count,
                                      const mesa_shader_stage *stages,
                                      struct vk_shader **const shaders);

/* If a job was added to reset the heap, it's job id is returned in 'job_id'
 * so that it can be used in dependencies */
VkResult panvk_per_arch(cmd_init_poly_heap)(struct panvk_cmd_buffer *cmdbuf,
                                            unsigned *job_id);

#endif
