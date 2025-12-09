/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_private.h"

#include "kk_buffer.h"
#include "kk_cmd_buffer.h"
#include "kk_encoder.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "kk_entrypoints.h"

static VkResult
kk_cmd_bind_map_buffer(struct vk_command_buffer *vk_cmd,
                       struct vk_meta_device *meta, VkBuffer _buffer,
                       void **map_out)
{
   struct kk_cmd_buffer *cmd = container_of(vk_cmd, struct kk_cmd_buffer, vk);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   assert(buffer->vk.size < UINT_MAX);
   struct kk_bo *bo = kk_cmd_allocate_buffer(cmd, buffer->vk.size, 16u);
   if (unlikely(bo == NULL))
      return VK_ERROR_OUT_OF_POOL_MEMORY;

   /* Need to retain since VkBuffers release the mtl_handle too */
   mtl_retain(bo->map);
   buffer->mtl_handle = bo->map;
   buffer->vk.device_address = bo->gpu;
   *map_out = bo->cpu;

   return VK_SUCCESS;
}

VkResult
kk_device_init_meta(struct kk_device *dev)
{
   VkResult result = vk_meta_device_init(&dev->vk, &dev->meta);
   if (result != VK_SUCCESS)
      return result;

   dev->meta.use_gs_for_layer = false;
   dev->meta.use_stencil_export = true;
   dev->meta.use_rect_list_pipeline = true;
   dev->meta.cmd_bind_map_buffer = kk_cmd_bind_map_buffer;
   dev->meta.max_bind_map_buffer_size_B = 64 * 1024;

   for (unsigned i = 0; i < VK_META_BUFFER_CHUNK_SIZE_COUNT; ++i) {
      dev->meta.buffer_access.optimal_wg_size[i] = 64;
   }

   return VK_SUCCESS;
}

void
kk_device_finish_meta(struct kk_device *dev)
{
   vk_meta_device_finish(&dev->vk, &dev->meta);
}

struct kk_meta_save {
   struct vk_vertex_input_state _dynamic_vi;
   struct vk_sample_locations_state _dynamic_sl;
   struct vk_dynamic_graphics_state dynamic;
   struct {
      union {
         struct {
            mtl_render_pipeline_state *ps;
            mtl_depth_stencil_state *ds;
            uint32_t attribs_read;
            enum mtl_primitive_type primitive_type;
            enum mtl_visibility_result_mode occlusion;
            bool is_ds_dynamic;
         } gfx;
         struct {
            mtl_compute_pipeline_state *pipeline_state;
            struct mtl_size local_size;
         } cs;
      };
   } pipeline;
   struct kk_descriptor_set *desc0;
   struct kk_push_descriptor_set *push_desc0;
   mtl_buffer *vb0_handle;
   struct kk_addr_range vb0;
   struct kk_buffer_address desc0_set_addr;
   bool has_push_desc0;
   uint8_t push[KK_MAX_PUSH_SIZE];
};

static void
kk_meta_begin(struct kk_cmd_buffer *cmd, struct kk_meta_save *save,
              VkPipelineBindPoint bind_point)
{
   struct kk_descriptor_state *desc = kk_get_descriptors_state(cmd, bind_point);

   if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      save->dynamic = cmd->vk.dynamic_graphics_state;
      save->_dynamic_vi = cmd->state.gfx._dynamic_vi;
      save->_dynamic_sl = cmd->state.gfx._dynamic_sl;
      save->pipeline.gfx.ps = cmd->state.gfx.pipeline_state;
      save->pipeline.gfx.ds = cmd->state.gfx.depth_stencil_state;
      save->pipeline.gfx.attribs_read = cmd->state.gfx.vb.attribs_read;
      save->pipeline.gfx.primitive_type = cmd->state.gfx.primitive_type;
      save->pipeline.gfx.occlusion = cmd->state.gfx.occlusion.mode;
      save->pipeline.gfx.is_ds_dynamic =
         cmd->state.gfx.is_depth_stencil_dynamic;

      cmd->state.gfx.is_depth_stencil_dynamic = false;
      cmd->state.gfx.depth_stencil_state = NULL;
      cmd->state.gfx.occlusion.mode = MTL_VISIBILITY_RESULT_MODE_DISABLED;
      cmd->state.gfx.dirty |= KK_DIRTY_OCCLUSION;
      desc->root_dirty = true;
   } else {
      save->pipeline.cs.pipeline_state = cmd->state.cs.pipeline_state;
      save->pipeline.cs.local_size = cmd->state.cs.local_size;
   }

   save->vb0_handle = cmd->state.gfx.vb.handles[0];
   save->vb0 = cmd->state.gfx.vb.addr_range[0];

   save->desc0 = desc->sets[0];
   save->has_push_desc0 = desc->push[0];
   if (save->has_push_desc0)
      save->push_desc0 = desc->push[0];

   static_assert(sizeof(save->push) == sizeof(desc->root.push),
                 "Size mismatch for push in meta_save");
   memcpy(save->push, desc->root.push, sizeof(save->push));
}

static void
kk_meta_end(struct kk_cmd_buffer *cmd, struct kk_meta_save *save,
            VkPipelineBindPoint bind_point)
{
   struct kk_descriptor_state *desc = kk_get_descriptors_state(cmd, bind_point);
   desc->root_dirty = true;

   if (save->desc0) {
      desc->sets[0] = save->desc0;
      desc->root.sets[0] = save->desc0->addr;
      desc->set_sizes[0] = save->desc0->size;
      desc->push_dirty &= ~BITFIELD_BIT(0);
   } else if (save->has_push_desc0) {
      desc->push[0] = save->push_desc0;
      desc->push_dirty |= BITFIELD_BIT(0);
   }

   if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      /* Restore the dynamic state */
      assert(save->dynamic.vi == &cmd->state.gfx._dynamic_vi);
      assert(save->dynamic.ms.sample_locations == &cmd->state.gfx._dynamic_sl);
      cmd->vk.dynamic_graphics_state = save->dynamic;
      cmd->state.gfx._dynamic_vi = save->_dynamic_vi;
      cmd->state.gfx._dynamic_sl = save->_dynamic_sl;
      memcpy(cmd->vk.dynamic_graphics_state.dirty,
             cmd->vk.dynamic_graphics_state.set,
             sizeof(cmd->vk.dynamic_graphics_state.set));

      if (cmd->state.gfx.is_depth_stencil_dynamic)
         mtl_release(cmd->state.gfx.depth_stencil_state);
      cmd->state.gfx.pipeline_state = save->pipeline.gfx.ps;
      cmd->state.gfx.depth_stencil_state = save->pipeline.gfx.ds;
      cmd->state.gfx.primitive_type = save->pipeline.gfx.primitive_type;
      cmd->state.gfx.vb.attribs_read = save->pipeline.gfx.attribs_read;
      cmd->state.gfx.is_depth_stencil_dynamic =
         save->pipeline.gfx.is_ds_dynamic;
      cmd->state.gfx.dirty |= KK_DIRTY_PIPELINE;

      cmd->state.gfx.vb.addr_range[0] = save->vb0;
      cmd->state.gfx.vb.handles[0] = save->vb0_handle;
      cmd->state.gfx.dirty |= KK_DIRTY_VB;

      cmd->state.gfx.occlusion.mode = save->pipeline.gfx.occlusion;
      cmd->state.gfx.dirty |= KK_DIRTY_OCCLUSION;

      desc->root_dirty = true;
   } else {
      cmd->state.cs.local_size = save->pipeline.cs.local_size;
      cmd->state.cs.pipeline_state = save->pipeline.cs.pipeline_state;
   }

   memcpy(desc->root.push, save->push, sizeof(save->push));
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                 VkDeviceSize dstOffset, VkDeviceSize dstRange, uint32_t data)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct kk_meta_save save;
   kk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
   vk_meta_fill_buffer(&cmd->vk, &dev->meta, dstBuffer, dstOffset, dstRange,
                       data);
   kk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                   VkDeviceSize dstOffset, VkDeviceSize dstRange,
                   const void *pData)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct kk_meta_save save;
   kk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
   vk_meta_update_buffer(&cmd->vk, &dev->meta, dstBuffer, dstOffset, dstRange,
                         pData);
   kk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_COMPUTE);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBlitImage2(VkCommandBuffer commandBuffer,
                 const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct kk_meta_save save;
   kk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_blit_image2(&cmd->vk, &dev->meta, pBlitImageInfo);
   kk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdResolveImage2(VkCommandBuffer commandBuffer,
                    const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct kk_meta_save save;
   kk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_resolve_image2(&cmd->vk, &dev->meta, pResolveImageInfo);
   kk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

static void
kk_meta_init_render(struct kk_cmd_buffer *cmd,
                    struct vk_meta_rendering_info *info)
{
   const struct kk_rendering_state *render = &cmd->state.gfx.render;

   *info = (struct vk_meta_rendering_info){
      .samples = MAX2(render->samples, 1),
      .view_mask = render->view_mask,
      .color_attachment_count = render->color_att_count,
      .depth_attachment_format = render->depth_att.vk_format,
      .stencil_attachment_format = render->stencil_att.vk_format,
   };
   for (uint32_t a = 0; a < render->color_att_count; a++) {
      info->color_attachment_formats[a] = render->color_att[a].vk_format;
      info->color_attachment_write_masks[a] =
         VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                       const VkClearAttachment *pAttachments,
                       uint32_t rectCount, const VkClearRect *pRects)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct vk_meta_rendering_info render_info;
   kk_meta_init_render(cmd, &render_info);

   uint32_t view_mask = cmd->state.gfx.render.view_mask;
   struct kk_encoder *encoder = cmd->encoder;
   uint32_t layer_ids[KK_MAX_MULTIVIEW_VIEW_COUNT] = {};
   mtl_set_vertex_amplification_count(encoder->main.encoder, layer_ids, 1u);

   struct kk_meta_save save;
   kk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_clear_attachments(&cmd->vk, &dev->meta, &render_info,
                             attachmentCount, pAttachments, rectCount, pRects);
   kk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);

   uint32_t count = 0u;
   u_foreach_bit(id, view_mask)
      layer_ids[count++] = id;
   if (view_mask == 0u) {
      layer_ids[count++] = 0;
   }
   mtl_set_vertex_amplification_count(encoder->main.encoder, layer_ids, count);
}

void
kk_meta_resolve_rendering(struct kk_cmd_buffer *cmd,
                          const VkRenderingInfo *pRenderingInfo)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct kk_meta_save save;
   kk_meta_begin(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
   vk_meta_resolve_rendering(&cmd->vk, &dev->meta, pRenderingInfo);
   kk_meta_end(cmd, &save, VK_PIPELINE_BIND_POINT_GRAPHICS);
}
