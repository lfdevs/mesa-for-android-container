/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_frame_shaders.h"
#include "panvk_cmd_pool.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_meta.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"

#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_props.h"
#include "pan_samples.h"

#include "vk_descriptor_update_template.h"
#include "vk_format.h"

static VkResult
panvk_cmd_prepare_fragment_job(struct panvk_cmd_buffer *cmdbuf, uint64_t fbd)
{
   const struct pan_fb_layout *fb = &cmdbuf->state.gfx.render.fb.layout;
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct pan_ptr job_ptr = panvk_cmd_alloc_desc(cmdbuf, FRAGMENT_JOB);

   if (!job_ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   pan_section_pack(job_ptr.cpu, FRAGMENT_JOB, PAYLOAD, payload) {
      assert(pan_fb_bbox_is_valid(fb->tiling_area_px));
      payload.bound_min_x = fb->tiling_area_px.min_x >> MALI_TILE_SHIFT;
      payload.bound_min_y = fb->tiling_area_px.min_y >> MALI_TILE_SHIFT;
      payload.bound_max_x = fb->tiling_area_px.max_x >> MALI_TILE_SHIFT;
      payload.bound_max_y = fb->tiling_area_px.max_y >> MALI_TILE_SHIFT;

      payload.framebuffer = fbd;
   }

   pan_section_pack(job_ptr.cpu, FRAGMENT_JOB, HEADER, header) {
      header.type = MALI_JOB_TYPE_FRAGMENT;
      header.index = 1;
   }

   pan_jc_add_job(&batch->frag_jc, MALI_JOB_TYPE_FRAGMENT, false, false, 0, 0,
                  &job_ptr, false);
   util_dynarray_append(&batch->jobs, job_ptr.cpu);
   return VK_SUCCESS;
}

void
panvk_per_arch(cmd_close_batch)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   if (!batch)
      return;

   assert(batch);

   if (!batch->fb.desc.gpu && !batch->vtc_jc.first_job) {
      if (util_dynarray_num_elements(&batch->event_ops,
                                     struct panvk_cmd_event_op) == 0) {
         /* Content-less batch, let's drop it */
         vk_free(&cmdbuf->vk.pool->alloc, batch);
      } else {
         /* Batch has no jobs but is needed for synchronization, let's add a
          * NULL job so the SUBMIT ioctl doesn't choke on it.
          */
         struct pan_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, JOB_HEADER);

         if (ptr.gpu) {
            util_dynarray_append(&batch->jobs, ptr.cpu);
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_NULL, false, false, 0,
                           0, &ptr, false);
         }

         list_addtail(&batch->node, &cmdbuf->batches);
      }
      cmdbuf->cur_batch = NULL;
      return;
   }

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   list_addtail(&batch->node, &cmdbuf->batches);

   if (batch->tlsinfo.tls.size) {
      unsigned thread_tls_alloc =
         pan_query_thread_tls_alloc(&phys_dev->kmod.dev->props);
      unsigned core_id_range =
         pan_query_core_id_range(&phys_dev->kmod.dev->props);

      unsigned size = pan_get_total_stack_size(batch->tlsinfo.tls.size,
                                               thread_tls_alloc, core_id_range);
      batch->tlsinfo.tls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, size, 4096).gpu;
   }

   if (batch->tlsinfo.wls.size) {
      assert(batch->wls_total_size);
      batch->tlsinfo.wls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, batch->wls_total_size, 4096).gpu;
   }

   if (batch->tls.cpu)
      GENX(pan_emit_tls)(&batch->tlsinfo, batch->tls.cpu);

   if (batch->fb.desc.cpu &&
       (cmdbuf->cur_batch->vtc_jc.first_tiler ||
        cmdbuf->state.gfx.render.fb.needs_store)) {
      panvk_per_arch(cmd_select_tile_size)(cmdbuf);

      /* At this point, we should know sample count and the tile size should
       * have been calculated
       */
      const struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
      assert(render->fb.layout.sample_count > 0);
      assert(render->fb.layout.tile_size_px > 0);

      const uint8_t sample_count = render->fb.layout.sample_count;
      struct pan_fb_desc_info fbd_info = {
         .fb = &render->fb.layout,
         .load = render->fb.needs_load ? &render->fb.load :
                                         &render->fb.spill.load,
         .store = render->fb.needs_store ? &render->fb.store :
                                           &render->fb.spill.store,
         .sample_pos_array_pointer = dev->sample_positions->addr.dev +
            pan_sample_positions_offset(pan_sample_pattern(sample_count)),
         .provoking_vertex_first =
            cmdbuf->state.gfx.render.first_provoking_vertex != U_TRISTATE_NO,
         .tls = &batch->tlsinfo,
         .tiler_ctx = &batch->tiler.ctx,
      };

      struct pan_fb_frame_shaders fs;
      VkResult result = panvk_per_arch(cmd_get_frame_shaders)(
         cmdbuf, &render->fb.layout, fbd_info.load,
         render->fb.needs_store ? &render->fb.resolve : NULL, &fs);
      if (result != VK_SUCCESS)
         return;

      uint32_t view_mask = cmdbuf->state.gfx.render.view_mask;
      assert(view_mask == 0 || util_bitcount(view_mask) <= batch->fb.layer_count);
      uint32_t enabled_layer_count = view_mask ?
         util_bitcount(view_mask) :
         batch->fb.layer_count;

      const bool has_zs_ext = pan_fb_has_zs(&render->fb.layout);
      for (uint32_t i = 0; i < enabled_layer_count; i++) {
         uint32_t layer_id = (view_mask != 0) ? u_bit_scan(&view_mask) : i;
         VkResult result;

         result = panvk_per_arch(cmd_prepare_tiler_context)(cmdbuf, layer_id);
         if (result != VK_SUCCESS)
            break;

         const struct pan_ptr fbd =
            pan_ptr_offset(batch->fb.desc, batch->fb.desc_stride * layer_id);
         uint64_t tagged_fbd_ptr = fbd.gpu;

         fbd_info.layer = layer_id;
         fbd_info.frame_shaders = fs;
         fbd_info.frame_shaders.dcd_pointer += layer_id * 3 * pan_size(DRAW);

         const struct pan_fb_descs fb_descs = {
            .fbd = fbd.cpu,
            .zs_crc = has_zs_ext ? fbd.cpu + pan_size(FRAMEBUFFER) : NULL,
            .rts = has_zs_ext ? fbd.cpu + pan_size(FRAMEBUFFER) +
                                   pan_size(ZS_CRC_EXTENSION)
                              : fbd.cpu + pan_size(FRAMEBUFFER),
         };
         tagged_fbd_ptr |= GENX(pan_emit_fb_desc)(&fbd_info, &fb_descs);

         result = panvk_cmd_prepare_fragment_job(cmdbuf, tagged_fbd_ptr);
         if (result != VK_SUCCESS)
            break;
      }

      /* We've now done the load.  Everything from now on should spill */
      cmdbuf->state.gfx.render.fb.needs_load = false;
   }

   cmdbuf->cur_batch = NULL;
}

VkResult
panvk_per_arch(cmd_alloc_fb_desc)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   if (batch->fb.desc.gpu)
      return VK_SUCCESS;

   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   const struct pan_fb_layout *fb = &render->fb.layout;
   bool has_zs_ext = pan_fb_has_zs(fb);
   batch->fb.layer_count = render->layer_count;
   unsigned fbd_size = pan_size(FRAMEBUFFER);

   if (has_zs_ext)
      fbd_size = ALIGN_POT(fbd_size, pan_alignment(ZS_CRC_EXTENSION)) +
                 pan_size(ZS_CRC_EXTENSION);

   fbd_size = ALIGN_POT(fbd_size, pan_alignment(RENDER_TARGET)) +
              (fb->rt_count * pan_size(RENDER_TARGET));

   batch->fb.bo_count = cmdbuf->state.gfx.render.fb.bo_count;
   memcpy(batch->fb.bos, cmdbuf->state.gfx.render.fb.bos,
          batch->fb.bo_count * sizeof(batch->fb.bos[0]));

   batch->fb.desc =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, fbd_size * batch->fb.layer_count,
                              pan_alignment(FRAMEBUFFER));
   batch->fb.desc_stride = fbd_size;

   return batch->fb.desc.gpu ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult
panvk_per_arch(cmd_alloc_tls_desc)(struct panvk_cmd_buffer *cmdbuf, bool gfx)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   assert(batch);
   if (!batch->tls.gpu) {
      batch->tls = panvk_cmd_alloc_desc(cmdbuf, LOCAL_STORAGE);
      if (!batch->tls.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(cmd_prepare_tiler_context)(struct panvk_cmd_buffer *cmdbuf,
                                          uint32_t layer_idx)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);
   struct panvk_batch *batch = cmdbuf->cur_batch;
   uint64_t tiler_desc;

   if (batch->tiler.ctx_descs.gpu) {
      tiler_desc =
         batch->tiler.ctx_descs.gpu + (pan_size(TILER_CONTEXT) * layer_idx);
      goto out_set_layer_ctx;
   }

   const struct pan_fb_layout *fb = &cmdbuf->state.gfx.render.fb.layout;
   uint32_t layer_count = cmdbuf->state.gfx.render.layer_count;
   batch->tiler.heap_desc = panvk_cmd_alloc_desc(cmdbuf, TILER_HEAP);
   batch->tiler.ctx_descs =
      panvk_cmd_alloc_desc_array(cmdbuf, layer_count, TILER_CONTEXT);
   if (!batch->tiler.heap_desc.gpu || !batch->tiler.ctx_descs.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   tiler_desc =
      batch->tiler.ctx_descs.gpu + (pan_size(TILER_CONTEXT) * layer_idx);

   pan_pack(&batch->tiler.heap_templ, TILER_HEAP, cfg) {
      cfg.size = pan_kmod_bo_size(dev->tiler_heap->bo);
      cfg.base = dev->tiler_heap->addr.dev;
      cfg.bottom = dev->tiler_heap->addr.dev;
      cfg.top = cfg.base + cfg.size;
   }

   pan_pack(&batch->tiler.ctx_templ, TILER_CONTEXT, cfg) {
      cfg.hierarchy_mask = panvk_select_tiler_hierarchy_mask(
         phys_dev, &cmdbuf->state.gfx, pan_kmod_bo_size(dev->tiler_heap->bo));
      cfg.fb_width = fb->width_px;
      cfg.fb_height = fb->height_px;
      cfg.heap = batch->tiler.heap_desc.gpu;
      cfg.sample_pattern = pan_sample_pattern(fb->sample_count);
   }

   memcpy(batch->tiler.heap_desc.cpu, &batch->tiler.heap_templ,
          sizeof(batch->tiler.heap_templ));

   struct mali_tiler_context_packed *ctxs = batch->tiler.ctx_descs.cpu;

   assert(layer_count > 0);
   for (uint32_t i = 0; i < layer_count; i++) {
      STATIC_ASSERT(
         !(pan_size(TILER_CONTEXT) & (pan_alignment(TILER_CONTEXT) - 1)));

      memcpy(&ctxs[i], &batch->tiler.ctx_templ, sizeof(*ctxs));
   }

out_set_layer_ctx:
   if (PAN_ARCH >= 9)
      batch->tiler.ctx.valhall.desc = tiler_desc;
   else
      batch->tiler.ctx.bifrost.desc = tiler_desc;

   return VK_SUCCESS;
}

struct panvk_batch *
panvk_per_arch(cmd_open_batch)(struct panvk_cmd_buffer *cmdbuf)
{
   assert(!cmdbuf->cur_batch);
   cmdbuf->cur_batch =
      vk_zalloc(&cmdbuf->vk.pool->alloc, sizeof(*cmdbuf->cur_batch), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   cmdbuf->cur_batch->jobs = UTIL_DYNARRAY_INIT;
   cmdbuf->cur_batch->event_ops = UTIL_DYNARRAY_INIT;
   assert(cmdbuf->cur_batch);
   return cmdbuf->cur_batch;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(EndCommandBuffer)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   panvk_per_arch(cmd_close_batch)(cmdbuf);

   panvk_pool_flush_maps(&cmdbuf->desc_pool);

   return vk_command_buffer_end(&cmdbuf->vk);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPipelineBarrier2)(VkCommandBuffer commandBuffer,
                                    const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   /* Caches are flushed/invalidated at batch boundaries for now, nothing to do
    * for memory barriers assuming we implement barriers with the creation of a
    * new batch.
    * FIXME: We can probably do better with a CacheFlush job that has the
    * barrier flag set to true.
    */
   if (cmdbuf->cur_batch) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }
}

static void
panvk_reset_cmdbuf(struct vk_command_buffer *vk_cmdbuf,
                   VkCommandBufferResetFlags flags)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);

   vk_command_buffer_reset(&cmdbuf->vk);

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->vk.pool->alloc, batch);
   }

   panvk_pool_reset(&cmdbuf->desc_pool);
   panvk_pool_reset(&cmdbuf->tls_pool);
   panvk_pool_reset(&cmdbuf->varying_pool);
   panvk_cmd_buffer_obj_list_reset(cmdbuf, push_sets);

   memset(&cmdbuf->state, 0, sizeof(cmdbuf->state));
}

static void
panvk_destroy_cmdbuf(struct vk_command_buffer *vk_cmdbuf)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->vk.pool->alloc, batch);
   }

#if PAN_ARCH < 9
   panvk_shader_link_cleanup(&cmdbuf->state.gfx.link);
#endif

   panvk_pool_cleanup(&cmdbuf->desc_pool);
   panvk_pool_cleanup(&cmdbuf->tls_pool);
   panvk_pool_cleanup(&cmdbuf->varying_pool);
   panvk_cmd_buffer_obj_list_cleanup(cmdbuf, push_sets);
   vk_command_buffer_finish(&cmdbuf->vk);
   vk_free(&dev->vk.alloc, cmdbuf);
}

static VkResult
panvk_create_cmdbuf(struct vk_command_pool *vk_pool, VkCommandBufferLevel level,
                    struct vk_command_buffer **cmdbuf_out)
{
   struct panvk_device *device =
      container_of(vk_pool->base.device, struct panvk_device, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_pool, struct panvk_cmd_pool, vk);
   struct panvk_cmd_buffer *cmdbuf;

   cmdbuf = vk_zalloc(&device->vk.alloc, sizeof(*cmdbuf), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmdbuf)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init_with_params(
      &cmdbuf->vk,
      &(struct vk_command_buffer_init_params) {
         .pool = &pool->vk,
         .ops = &panvk_per_arch(cmd_buffer_ops),
         .level = level,
         .needs_cmd_queue = level == VK_COMMAND_BUFFER_LEVEL_SECONDARY,
      });
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, cmdbuf);
      return result;
   }

   panvk_cmd_buffer_obj_list_init(cmdbuf, push_sets);
   cmdbuf->vk.dynamic_graphics_state.vi = &cmdbuf->state.gfx.dynamic.vi;
   cmdbuf->vk.dynamic_graphics_state.ms.sample_locations =
      &cmdbuf->state.gfx.dynamic.sl;

   struct panvk_pool_properties desc_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_WB_MMAP),
      .slab_size = 64 * 1024,
      .label = "Command buffer descriptor pool",
      .prealloc = true,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->desc_pool, device, &pool->desc_bo_pool, NULL,
                   &desc_pool_props);

   struct panvk_pool_properties tls_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_NO_MMAP),
      .slab_size = 64 * 1024,
      .label = "TLS pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->tls_pool, device, &pool->tls_bo_pool, &pool->tls_big_bo_pool,
                   &tls_pool_props);

   struct panvk_pool_properties var_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_NO_MMAP),
      .slab_size = 64 * 1024,
      .label = "Varying pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->varying_pool, device, &pool->varying_bo_pool, NULL,
                   &var_pool_props);

   list_inithead(&cmdbuf->batches);
   *cmdbuf_out = &cmdbuf->vk;
   return VK_SUCCESS;
}

const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops) = {
   .create = panvk_create_cmdbuf,
   .reset = panvk_reset_cmdbuf,
   .destroy = panvk_destroy_cmdbuf,
};

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(BeginCommandBuffer)(VkCommandBuffer commandBuffer,
                                   const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   vk_command_buffer_begin(&cmdbuf->vk, pBeginInfo);

   return VK_SUCCESS;
}
