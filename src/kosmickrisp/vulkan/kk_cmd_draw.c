/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_entrypoints.h"

#include "kk_buffer.h"
#include "kk_cmd_buffer.h"
#include "kk_encoder.h"
#include "kk_format.h"
#include "kk_image_view.h"
#include "kk_query_pool.h"

#include "kosmickrisp/bridge/mtl_bridge.h"
#include "kosmickrisp/bridge/vk_to_mtl_map.h"

#include "vulkan/util/vk_format.h"

static void
kk_cmd_buffer_dirty_render_pass(struct kk_cmd_buffer *cmd)
{
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;

   /* These depend on color attachment count */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_COLOR_WRITE_ENABLES);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_ENABLES);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_CB_WRITE_MASKS);

   /* These depend on the depth/stencil format */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE);
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE);

   /* This may depend on render targets for ESO */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES);

   /* This may depend on render targets */
   BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_COLOR_ATTACHMENT_MAP);
}

static void
kk_attachment_init(struct kk_attachment *att,
                   const VkRenderingAttachmentInfo *info)
{
   if (info == NULL || info->imageView == VK_NULL_HANDLE) {
      *att = (struct kk_attachment){
         .iview = NULL,
      };
      return;
   }

   VK_FROM_HANDLE(kk_image_view, iview, info->imageView);
   *att = (struct kk_attachment){
      .vk_format = iview->vk.format,
      .iview = iview,
   };

   if (info->resolveMode != VK_RESOLVE_MODE_NONE) {
      VK_FROM_HANDLE(kk_image_view, res_iview, info->resolveImageView);
      att->resolve_mode = info->resolveMode;
      att->resolve_iview = res_iview;
   }

   att->store_op = info->storeOp;
}

VKAPI_ATTR void VKAPI_CALL
kk_GetRenderingAreaGranularityKHR(
   VkDevice device, const VkRenderingAreaInfoKHR *pRenderingAreaInfo,
   VkExtent2D *pGranularity)
{
   *pGranularity = (VkExtent2D){.width = 1, .height = 1};
}

static void
kk_merge_render_iview(VkExtent2D *extent, struct kk_image_view *iview)
{
   if (iview) {
      /* TODO: is this right for ycbcr? */
      unsigned level = iview->vk.base_mip_level;
      unsigned width = u_minify(iview->vk.image->extent.width, level);
      unsigned height = u_minify(iview->vk.image->extent.height, level);

      extent->width = MAX2(extent->width, width);
      extent->height = MAX2(extent->height, height);
   }
}

static void
kk_fill_common_attachment_description(
   mtl_render_pass_attachment_descriptor *descriptor,
   const struct kk_image_view *iview, const VkRenderingAttachmentInfo *info,
   bool force_attachment_load)
{
   assert(iview->plane_count ==
          1); /* TODO_KOSMICKRISP Handle multiplanar images? */
   mtl_render_pass_attachment_descriptor_set_texture(
      descriptor, iview->planes[0].mtl_handle_render);
   mtl_render_pass_attachment_descriptor_set_level(descriptor,
                                                   iview->vk.base_mip_level);
   mtl_render_pass_attachment_descriptor_set_slice(descriptor,
                                                   iview->vk.base_array_layer);
   enum mtl_load_action load_action =
      force_attachment_load
         ? MTL_LOAD_ACTION_LOAD
         : vk_attachment_load_op_to_mtl_load_action(info->loadOp);

   /* TODO_KOSMICKRISP Need to tackle issue #14344 */
   if (load_action == MTL_LOAD_ACTION_DONT_CARE)
      load_action = MTL_LOAD_ACTION_LOAD;
   mtl_render_pass_attachment_descriptor_set_load_action(descriptor,
                                                         load_action);
   /* We need to force attachment store to correctly handle situations where the
    * attachment is written to in a subpass, and later read from in the next one
    * with the store operation being something else than store. The other reason
    * being that we break renderpasses when a pipeline barrier is used, so we
    * need to not loose the information of the attachment when we restart it. */
   enum mtl_store_action store_action = MTL_STORE_ACTION_STORE;
   mtl_render_pass_attachment_descriptor_set_store_action(descriptor,
                                                          store_action);
}

static struct mtl_clear_color
vk_clear_color_value_to_mtl_clear_color(union VkClearColorValue color,
                                        enum pipe_format format)
{
   struct mtl_clear_color value;
   if (util_format_is_pure_sint(format)) {
      value.red = color.int32[0];
      value.green = color.int32[1];
      value.blue = color.int32[2];
      value.alpha = color.int32[3];
   } else if (util_format_is_pure_uint(format)) {
      value.red = color.uint32[0];
      value.green = color.uint32[1];
      value.blue = color.uint32[2];
      value.alpha = color.uint32[3];
   } else {
      value.red = color.float32[0];
      value.green = color.float32[1];
      value.blue = color.float32[2];
      value.alpha = color.float32[3];
   }

   /* Apply swizzle to color since Metal does not allow swizzle for renderable
    * textures, but we need to support that for formats like
    * VK_FORMAT_B4G4R4A4_UNORM_PACK16 */
   const struct kk_va_format *supported_format = kk_get_va_format(format);
   struct mtl_clear_color swizzled_color;
   for (uint32_t i = 0u; i < 4; ++i)
      swizzled_color.channel[supported_format->unswizzle.channels[i]] =
         value.channel[i];

   return swizzled_color;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBeginRendering(VkCommandBuffer commandBuffer,
                     const VkRenderingInfo *pRenderingInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   struct kk_rendering_state *render = &cmd->state.gfx.render;

   memset(render, 0, sizeof(*render));

   render->flags = pRenderingInfo->flags;
   render->area = pRenderingInfo->renderArea;
   render->view_mask = pRenderingInfo->viewMask;
   render->layer_count = pRenderingInfo->layerCount;
   render->samples = 0;
   render->color_att_count = pRenderingInfo->colorAttachmentCount;

   const uint32_t layer_count = render->view_mask
                                   ? util_last_bit(render->view_mask)
                                   : render->layer_count;

   VkExtent2D framebuffer_extent = {.width = 0u, .height = 0u};
   bool does_any_attachment_clear = false;
   for (uint32_t i = 0; i < render->color_att_count; i++) {
      kk_attachment_init(&render->color_att[i],
                         &pRenderingInfo->pColorAttachments[i]);
      kk_merge_render_iview(&framebuffer_extent, render->color_att[i].iview);
      does_any_attachment_clear |=
         (pRenderingInfo->pColorAttachments[i].loadOp ==
          VK_ATTACHMENT_LOAD_OP_CLEAR);
   }
   if (pRenderingInfo->pDepthAttachment)
      does_any_attachment_clear |= (pRenderingInfo->pDepthAttachment->loadOp ==
                                    VK_ATTACHMENT_LOAD_OP_CLEAR);
   if (pRenderingInfo->pStencilAttachment)
      does_any_attachment_clear |=
         (pRenderingInfo->pStencilAttachment->loadOp ==
          VK_ATTACHMENT_LOAD_OP_CLEAR);

   kk_attachment_init(&render->depth_att, pRenderingInfo->pDepthAttachment);
   kk_attachment_init(&render->stencil_att, pRenderingInfo->pStencilAttachment);
   kk_merge_render_iview(&framebuffer_extent,
                         render->depth_att.iview ?: render->stencil_att.iview);

   const VkRenderingFragmentShadingRateAttachmentInfoKHR *fsr_att_info =
      vk_find_struct_const(pRenderingInfo->pNext,
                           RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR);
   if (fsr_att_info != NULL && fsr_att_info->imageView != VK_NULL_HANDLE) {
      VK_FROM_HANDLE(kk_image_view, iview, fsr_att_info->imageView);
      render->fsr_att = (struct kk_attachment){
         .vk_format = iview->vk.format,
         .iview = iview,
         .store_op = VK_ATTACHMENT_STORE_OP_NONE,
      };
   }

   const VkRenderingAttachmentLocationInfoKHR ral_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_LOCATION_INFO_KHR,
      .colorAttachmentCount = pRenderingInfo->colorAttachmentCount,
   };
   vk_cmd_set_rendering_attachment_locations(&cmd->vk, &ral_info);

   kk_cmd_buffer_dirty_render_pass(cmd);
   mtl_render_pass_descriptor *pass_descriptor =
      mtl_new_render_pass_descriptor();

   /* Framebufferless rendering, need to set pass_descriptors
    * renderTargetWidth/Height to non-0 values and defaultRasterSampleCount */
   if (framebuffer_extent.width == 0u && framebuffer_extent.height == 0u) {
      framebuffer_extent.width = render->area.extent.width;
      framebuffer_extent.height = render->area.extent.height;
      mtl_render_pass_descriptor_set_render_target_width(
         pass_descriptor, framebuffer_extent.width);
      mtl_render_pass_descriptor_set_render_target_height(
         pass_descriptor, framebuffer_extent.height);
      mtl_render_pass_descriptor_set_default_raster_sample_count(
         pass_descriptor, 1u);
   }

   /* Check if we are rendering to the whole framebuffer. Required to understand
    * if we need to load to avoid clearing all attachment when loading.
    */
   bool is_whole_framebuffer =
      framebuffer_extent.width == render->area.extent.width &&
      framebuffer_extent.height == render->area.extent.height &&
      render->area.offset.x == 0u && render->area.offset.y == 0u &&
      (render->view_mask == 0u ||
       render->view_mask == BITFIELD64_MASK(render->layer_count));

   /* Understand if the render area is tile aligned so we know if we actually
    * need to load the tile to not lose information. */
   uint32_t tile_alignment = 31u;
   bool is_tile_aligned = !(render->area.offset.x & tile_alignment) &&
                          !(render->area.offset.y & tile_alignment) &&
                          !(render->area.extent.width & tile_alignment) &&
                          !(render->area.extent.height & tile_alignment);

   /* Rendering to the whole framebuffer */
   is_tile_aligned |= is_whole_framebuffer;

   /* There are 3 cases where we need to force a load instead of using the user
    * defined load operation:
    * 1. Render area is not tile aligned
    * 2. Load operation is clear but doesn't render to the whole attachment
    * 3. Resuming renderpass
    */
   bool force_attachment_load =
      !is_tile_aligned ||
      (!is_whole_framebuffer && does_any_attachment_clear) ||
      (render->flags & VK_RENDERING_RESUMING_BIT);

   for (uint32_t i = 0; i < render->color_att_count; i++) {
      const struct kk_image_view *iview = render->color_att[i].iview;
      if (!iview)
         continue;

      assert(iview->plane_count ==
             1); /* TODO_KOSMICKRISP Handle multiplanar images? */
      const struct kk_image *image =
         container_of(iview->vk.image, struct kk_image, vk);
      render->samples = image->vk.samples;

      mtl_render_pass_attachment_descriptor *attachment_descriptor =
         mtl_render_pass_descriptor_get_color_attachment(pass_descriptor, i);
      kk_fill_common_attachment_description(
         attachment_descriptor, iview, &pRenderingInfo->pColorAttachments[i],
         force_attachment_load);
      struct mtl_clear_color clear_color =
         vk_clear_color_value_to_mtl_clear_color(
            pRenderingInfo->pColorAttachments[i].clearValue.color,
            iview->planes[0].format);
      mtl_render_pass_attachment_descriptor_set_clear_color(
         attachment_descriptor, clear_color);
   }

   if (render->depth_att.iview) {
      const struct kk_image_view *iview = render->depth_att.iview;
      const struct kk_image *image =
         container_of(iview->vk.image, struct kk_image, vk);
      render->samples = image->vk.samples;

      mtl_render_pass_attachment_descriptor *attachment_descriptor =
         mtl_render_pass_descriptor_get_depth_attachment(pass_descriptor);
      kk_fill_common_attachment_description(
         attachment_descriptor, render->depth_att.iview,
         pRenderingInfo->pDepthAttachment, force_attachment_load);

      /* clearValue.depthStencil.depth could have invalid values such as NaN
       * which will trigger a Metal validation error. Ensure we only use this
       * value if the attachment is actually cleared. */
      if (pRenderingInfo->pDepthAttachment->loadOp ==
          VK_ATTACHMENT_LOAD_OP_CLEAR)
         mtl_render_pass_attachment_descriptor_set_clear_depth(
            attachment_descriptor,
            pRenderingInfo->pDepthAttachment->clearValue.depthStencil.depth);
   }
   if (render->stencil_att.iview) {
      const struct kk_image_view *iview = render->stencil_att.iview;
      const struct kk_image *image =
         container_of(iview->vk.image, struct kk_image, vk);
      render->samples = image->vk.samples;

      mtl_render_pass_attachment_descriptor *attachment_descriptor =
         mtl_render_pass_descriptor_get_stencil_attachment(pass_descriptor);
      kk_fill_common_attachment_description(
         attachment_descriptor, render->stencil_att.iview,
         pRenderingInfo->pStencilAttachment, force_attachment_load);
      mtl_render_pass_attachment_descriptor_set_clear_stencil(
         attachment_descriptor,
         pRenderingInfo->pStencilAttachment->clearValue.depthStencil.stencil);
   }

   /* Render targets are always arrays */
   mtl_render_pass_descriptor_set_render_target_array_length(
      pass_descriptor, layer_count ? layer_count : 1u);

   /* Set global visibility buffer */
   mtl_render_pass_descriptor_set_visibility_buffer(
      pass_descriptor, dev->occlusion_queries.bo->map);

   // TODO_KOSMICKRISP Fragment shading rate support goes here if Metal supports
   // it

   /* Start new encoder and encode sync commands from previous barriers (aka
    * fences) */
   kk_encoder_start_render(cmd, pass_descriptor, render->view_mask);

   /* Store descriptor in case we need to restart the pass at pipeline barrier,
    * but force loads */
   for (uint32_t i = 0; i < render->color_att_count; i++) {
      const struct kk_image_view *iview = render->color_att[i].iview;
      if (!iview)
         continue;
      mtl_render_pass_attachment_descriptor *attachment_descriptor =
         mtl_render_pass_descriptor_get_color_attachment(pass_descriptor, i);
      mtl_render_pass_attachment_descriptor_set_load_action(
         attachment_descriptor, MTL_LOAD_ACTION_LOAD);
   }
   if (render->depth_att.iview) {
      mtl_render_pass_attachment_descriptor *attachment_descriptor =
         mtl_render_pass_descriptor_get_depth_attachment(pass_descriptor);
      mtl_render_pass_attachment_descriptor_set_load_action(
         attachment_descriptor, MTL_LOAD_ACTION_LOAD);
   }
   if (render->stencil_att.iview) {
      mtl_render_pass_attachment_descriptor *attachment_descriptor =
         mtl_render_pass_descriptor_get_stencil_attachment(pass_descriptor);
      mtl_render_pass_attachment_descriptor_set_load_action(
         attachment_descriptor, MTL_LOAD_ACTION_LOAD);
   }
   cmd->state.gfx.render_pass_descriptor = pass_descriptor;

   kk_cmd_buffer_dirty_all_gfx(cmd);

   if (render->flags & VK_RENDERING_RESUMING_BIT)
      return;

   /* Clear attachments if we forced a load and there's a clear */
   if (!force_attachment_load || !does_any_attachment_clear)
      return;

   uint32_t clear_count = 0;
   VkClearAttachment clear_att[KK_MAX_RTS + 1];
   for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      const VkRenderingAttachmentInfo *att_info =
         &pRenderingInfo->pColorAttachments[i];
      if (att_info->imageView == VK_NULL_HANDLE ||
          att_info->loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
         continue;

      clear_att[clear_count++] = (VkClearAttachment){
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .colorAttachment = i,
         .clearValue = att_info->clearValue,
      };
   }

   clear_att[clear_count] = (VkClearAttachment){
      .aspectMask = 0,
   };
   if (pRenderingInfo->pDepthAttachment != NULL &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE &&
       pRenderingInfo->pDepthAttachment->loadOp ==
          VK_ATTACHMENT_LOAD_OP_CLEAR) {
      clear_att[clear_count].aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
      clear_att[clear_count].clearValue.depthStencil.depth =
         pRenderingInfo->pDepthAttachment->clearValue.depthStencil.depth;
   }
   if (pRenderingInfo->pStencilAttachment != NULL &&
       pRenderingInfo->pStencilAttachment->imageView != VK_NULL_HANDLE &&
       pRenderingInfo->pStencilAttachment->loadOp ==
          VK_ATTACHMENT_LOAD_OP_CLEAR) {
      clear_att[clear_count].aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
      clear_att[clear_count].clearValue.depthStencil.stencil =
         pRenderingInfo->pStencilAttachment->clearValue.depthStencil.stencil;
   }
   if (clear_att[clear_count].aspectMask != 0)
      clear_count++;

   if (clear_count > 0) {
      const VkClearRect clear_rect = {
         .rect = render->area,
         .baseArrayLayer = 0,
         .layerCount = render->view_mask ? 1 : render->layer_count,
      };

      kk_CmdClearAttachments(kk_cmd_buffer_to_handle(cmd), clear_count,
                             clear_att, 1, &clear_rect);
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdEndRendering(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_rendering_state *render = &cmd->state.gfx.render;
   bool need_resolve = false;

   /* Translate render state back to VK for meta */
   VkRenderingAttachmentInfo vk_color_att[KK_MAX_RTS];
   for (uint32_t i = 0; i < render->color_att_count; i++) {
      if (render->color_att[i].resolve_mode != VK_RESOLVE_MODE_NONE)
         need_resolve = true;

      vk_color_att[i] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = kk_image_view_to_handle(render->color_att[i].iview),
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
         .resolveMode = render->color_att[i].resolve_mode,
         .resolveImageView =
            kk_image_view_to_handle(render->color_att[i].resolve_iview),
         .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
      };
   }

   const VkRenderingAttachmentInfo vk_depth_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = kk_image_view_to_handle(render->depth_att.iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = render->depth_att.resolve_mode,
      .resolveImageView =
         kk_image_view_to_handle(render->depth_att.resolve_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };
   if (render->depth_att.resolve_mode != VK_RESOLVE_MODE_NONE)
      need_resolve = true;

   const VkRenderingAttachmentInfo vk_stencil_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = kk_image_view_to_handle(render->stencil_att.iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = render->stencil_att.resolve_mode,
      .resolveImageView =
         kk_image_view_to_handle(render->stencil_att.resolve_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };
   if (render->stencil_att.resolve_mode != VK_RESOLVE_MODE_NONE)
      need_resolve = true;

   const VkRenderingInfo vk_render = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = render->area,
      .layerCount = render->layer_count,
      .viewMask = render->view_mask,
      .colorAttachmentCount = render->color_att_count,
      .pColorAttachments = vk_color_att,
      .pDepthAttachment = &vk_depth_att,
      .pStencilAttachment = &vk_stencil_att,
   };

   /* Clean up previous encoder */
   kk_encoder_signal_fence_and_end(cmd);
   mtl_release(cmd->state.gfx.render_pass_descriptor);
   cmd->state.gfx.render_pass_descriptor = NULL;

   if (render->flags & VK_RENDERING_SUSPENDING_BIT)
      need_resolve = false;

   memset(render, 0, sizeof(*render));

   if (need_resolve) {
      kk_meta_resolve_rendering(cmd, &vk_render);
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBindIndexBuffer2KHR(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                          VkDeviceSize offset, VkDeviceSize size,
                          VkIndexType indexType)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   cmd->state.gfx.index.handle = buffer->mtl_handle;
   cmd->state.gfx.index.size = size;
   cmd->state.gfx.index.offset = offset;
   cmd->state.gfx.index.bytes_per_index = vk_index_type_to_bytes(indexType);
   cmd->state.gfx.index.restart = vk_index_to_restart(indexType);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBindVertexBuffers2(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                         uint32_t bindingCount, const VkBuffer *pBuffers,
                         const VkDeviceSize *pOffsets,
                         const VkDeviceSize *pSizes,
                         const VkDeviceSize *pStrides)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   if (pStrides) {
      vk_cmd_set_vertex_binding_strides(&cmd->vk, firstBinding, bindingCount,
                                        pStrides);
   }

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(kk_buffer, buffer, pBuffers[i]);
      uint32_t idx = firstBinding + i;
      uint64_t size = pSizes ? pSizes[i] : VK_WHOLE_SIZE;
      const struct kk_addr_range addr_range =
         kk_buffer_addr_range(buffer, pOffsets[i], size);
      cmd->state.gfx.vb.addr_range[idx] = addr_range;
      cmd->state.gfx.vb.handles[idx] = buffer->mtl_handle;
      cmd->state.gfx.dirty |= KK_DIRTY_VB;
   }
}

static void
kk_flush_vp_state(struct kk_cmd_buffer *cmd)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   /* We always need at least 1 viewport for the hardware. With rasterizer
    * discard the app may not supply any, but we can just program garbage.
    */
   unsigned count = MAX2(dyn->vp.scissor_count, 1);

   /* Need to clamp scissor rectangles to render area, otherwise Metal doesn't
    * like it */
   struct mtl_scissor_rect rects[KK_MAX_VIEWPORTS] = {0};
   VkOffset2D origin = cmd->state.gfx.render.area.offset;
   VkOffset2D end = {.x = origin.x + cmd->state.gfx.render.area.extent.width,
                     .y = origin.y + cmd->state.gfx.render.area.extent.height};
   for (uint32_t i = 0; i < dyn->vp.scissor_count; i++) {
      const VkRect2D *rect = &dyn->vp.scissors[i];

      size_t x0 = CLAMP(rect->offset.x, origin.x, end.x);
      size_t x1 = CLAMP(rect->offset.x + rect->extent.width, origin.x, end.x);
      size_t y0 = CLAMP(rect->offset.y, origin.y, end.y);
      size_t y1 = CLAMP(rect->offset.y + rect->extent.height, origin.y, end.y);
      size_t minx = MIN2(x0, x1);
      size_t miny = MIN2(y0, y1);
      size_t maxx = MAX2(x0, x1);
      size_t maxy = MAX2(y0, y1);
      rects[i].x = minx;
      rects[i].y = miny;
      rects[i].width = maxx - minx;
      rects[i].height = maxy - miny;
   }

   mtl_set_scissor_rects(kk_render_encoder(cmd), rects, count);

   count = MAX2(dyn->vp.viewport_count, 1);

   struct mtl_viewport viewports[KK_MAX_VIEWPORTS] = {0};

   /* NDC in Metal is pointing downwards. Vulkan is pointing upwards. Account
    * for that here */
   for (uint32_t i = 0; i < dyn->vp.viewport_count; i++) {
      const VkViewport *vp = &dyn->vp.viewports[i];

      viewports[i].originX = vp->x;
      viewports[i].originY = vp->y + vp->height;
      viewports[i].width = vp->width;
      viewports[i].height = -vp->height;

      viewports[i].znear = vp->minDepth;
      viewports[i].zfar = vp->maxDepth;
   }

   mtl_set_viewports(kk_render_encoder(cmd), viewports, count);
}

static inline uint32_t
kk_calculate_vbo_clamp(uint64_t vbuf, uint64_t sink, enum pipe_format format,
                       uint32_t size_B, uint32_t stride_B, uint32_t offset_B,
                       uint64_t *vbuf_out)
{
   unsigned elsize_B = util_format_get_blocksize(format);
   unsigned subtracted_B = offset_B + elsize_B;

   /* If at least one index is valid, determine the max. Otherwise, direct reads
    * to zero.
    */
   if (size_B >= subtracted_B) {
      *vbuf_out = vbuf + offset_B;

      /* If stride is zero, do not clamp, everything is valid. */
      if (stride_B)
         return ((size_B - subtracted_B) / stride_B);
      else
         return UINT32_MAX;
   } else {
      *vbuf_out = sink;
      return 0;
   }
}

static void
set_empty_scissor(mtl_render_encoder *enc)
{
   struct mtl_scissor_rect rect = {.x = 0u, .y = 0u, .width = 0u, .height = 0u};
   mtl_set_scissor_rects(enc, &rect, 1);
}

/* TODO_KOSMICKRISP: Move to common */
static inline enum mesa_prim
vk_conv_topology(VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MESA_PRIM_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return MESA_PRIM_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MESA_PRIM_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
   case VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA:
#pragma GCC diagnostic pop
      return MESA_PRIM_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return MESA_PRIM_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return MESA_PRIM_TRIANGLE_FAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      return MESA_PRIM_LINES_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return MESA_PRIM_LINE_STRIP_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      return MESA_PRIM_TRIANGLES_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return MESA_PRIM_TRIANGLE_STRIP_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return MESA_PRIM_PATCHES;
   default:
      UNREACHABLE("invalid");
   }
}

static void
kk_flush_draw_state(struct kk_cmd_buffer *cmd)
{
   struct kk_device *device = kk_cmd_buffer_device(cmd);
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;
   struct kk_descriptor_state *desc = &cmd->state.gfx.descriptors;
   mtl_render_encoder *enc = kk_render_encoder(cmd);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VI_BINDING_STRIDES)) {
      u_foreach_bit(ndx, dyn->vi->bindings_valid) {
         desc->root.draw.buffer_strides[ndx] = dyn->vi_binding_strides[ndx];
      }
      desc->root_dirty = true;
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_RASTERIZER_DISCARD_ENABLE)) {
      if (dyn->rs.rasterizer_discard_enable) {
         set_empty_scissor(enc);
      } else {
         /* Enforce setting the correct scissors */
         BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT);
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_CULL_MODE)) {
      gfx->is_cull_front_and_back =
         dyn->rs.cull_mode == VK_CULL_MODE_FRONT_AND_BACK;
      if (gfx->is_cull_front_and_back) {
         set_empty_scissor(enc);
      } else {
         mtl_set_cull_mode(enc,
                           vk_front_face_to_mtl_cull_mode(dyn->rs.cull_mode));
         /* Enforce setting the correct scissors */
         BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT);
      }
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY)) {
      gfx->primitive_type = vk_primitive_topology_to_mtl_primitive_type(
         dyn->ia.primitive_topology);
      gfx->prim = vk_conv_topology(dyn->ia.primitive_topology);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_IA_PRIMITIVE_RESTART_ENABLE)) {
      gfx->restart_disabled = !dyn->ia.primitive_restart_enable;
   }

   /* We enable raster discard by setting scissor to size (0, 0) */
   if (!(dyn->rs.rasterizer_discard_enable || gfx->is_cull_front_and_back) &&
       (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORTS) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSOR_COUNT) ||
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_VP_SCISSORS)))
      kk_flush_vp_state(cmd);

   if (cmd->state.gfx.is_depth_stencil_dynamic &&
       (cmd->state.gfx.render.depth_att.vk_format != VK_FORMAT_UNDEFINED ||
        cmd->state.gfx.render.stencil_att.vk_format != VK_FORMAT_UNDEFINED) &&
       (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_TEST_ENABLE) |
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_WRITE_ENABLE) |
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_COMPARE_OP) |
        // BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_ENABLE)
        // | BITSET_TEST(dyn->dirty,
        // MESA_VK_DYNAMIC_DS_DEPTH_BOUNDS_TEST_BOUNDS) |
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_TEST_ENABLE) |
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_OP) |
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_COMPARE_MASK) |
        BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_WRITE_MASK))) {
      kk_cmd_release_dynamic_ds_state(cmd);

      bool has_depth = dyn->rp.attachments & MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
      bool has_stencil =
         dyn->rp.attachments & MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
      gfx->depth_stencil_state = kk_compile_depth_stencil_state(
         device, &dyn->ds, has_depth, has_stencil);
      mtl_set_depth_stencil_state(enc, gfx->depth_stencil_state);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_FRONT_FACE)) {
      mtl_set_front_face_winding(
         enc, vk_front_face_to_mtl_winding(
                 cmd->vk.dynamic_graphics_state.rs.front_face));
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_BIAS_FACTORS)) {
      mtl_set_depth_bias(enc, dyn->rs.depth_bias.constant_factor,
                         dyn->rs.depth_bias.slope_factor,
                         dyn->rs.depth_bias.clamp);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_RS_DEPTH_CLAMP_ENABLE)) {
      enum mtl_depth_clip_mode mode = dyn->rs.depth_clamp_enable
                                         ? MTL_DEPTH_CLIP_MODE_CLAMP
                                         : MTL_DEPTH_CLIP_MODE_CLIP;
      mtl_set_depth_clip_mode(enc, mode);
   }

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_DS_STENCIL_REFERENCE))
      mtl_set_stencil_references(
         enc, cmd->vk.dynamic_graphics_state.ds.stencil.front.reference,
         cmd->vk.dynamic_graphics_state.ds.stencil.back.reference);

   if (BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS)) {
      static_assert(sizeof(desc->root.draw.blend_constant) ==
                       sizeof(dyn->cb.blend_constants),
                    "common size");

      memcpy(desc->root.draw.blend_constant, dyn->cb.blend_constants,
             sizeof(dyn->cb.blend_constants));
      desc->root_dirty = true;
   }

   if (gfx->dirty & KK_DIRTY_VB) {
      unsigned slot = 0;
      cmd->state.gfx.vb.max_vertices = 0u;
      u_foreach_bit(i, cmd->state.gfx.vb.attribs_read) {
         if (dyn->vi->attributes_valid & BITFIELD_BIT(i)) {
            struct vk_vertex_attribute_state attr = dyn->vi->attributes[i];
            struct kk_addr_range vb = gfx->vb.addr_range[attr.binding];

            mtl_render_use_resource(enc, gfx->vb.handles[attr.binding],
                                    MTL_RESOURCE_USAGE_READ);
            desc->root.draw.attrib_clamps[slot] = kk_calculate_vbo_clamp(
               vb.addr, 0, vk_format_to_pipe_format(attr.format), vb.range,
               dyn->vi_binding_strides[attr.binding], attr.offset,
               &desc->root.draw.attrib_base[slot]);
            desc->root.draw.buffer_strides[attr.binding] =
               dyn->vi_binding_strides[attr.binding];

            cmd->state.gfx.vb.max_vertices =
               MAX2(vb.range / dyn->vi_binding_strides[attr.binding],
                    cmd->state.gfx.vb.max_vertices);
         }
         slot++;
      }
      desc->root_dirty = true;
   }

   if (gfx->dirty & KK_DIRTY_PIPELINE) {
      mtl_render_set_pipeline_state(enc, gfx->pipeline_state);
      if (gfx->depth_stencil_state)
         mtl_set_depth_stencil_state(enc, gfx->depth_stencil_state);
   }

   if (desc->push_dirty)
      kk_cmd_buffer_flush_push_descriptors(cmd, desc);
   if (desc->root_dirty)
      kk_upload_descriptor_root(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

   struct kk_bo *root_buffer = desc->root.root_buffer;
   if (root_buffer) {
      mtl_set_vertex_buffer(enc, root_buffer->map, 0, 0);
      mtl_set_fragment_buffer(enc, root_buffer->map, 0, 0);
   }

   if (gfx->dirty & KK_DIRTY_OCCLUSION) {
      mtl_set_visibility_result_mode(enc, gfx->occlusion.mode,
                                     gfx->occlusion.index * sizeof(uint64_t));
   }

   gfx->dirty = 0u;
   vk_dynamic_graphics_state_clear_dirty(dyn);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
           uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
   /* Metal validation dislikes empty calls */
   if (instanceCount == 0 || vertexCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   kk_flush_draw_state(cmd);

   /* Metal does not support triangle fans */
   bool requires_unroll = cmd->state.gfx.prim == MESA_PRIM_TRIANGLE_FAN;
   if (requires_unroll) {
      VkDrawIndirectCommand draw = {
         .vertexCount = vertexCount,
         .instanceCount = instanceCount,
         .firstVertex = firstVertex,
         .firstInstance = firstInstance,
      };
      struct kk_pool pool = kk_pool_upload(cmd, &draw, sizeof(draw), 4u);
      kk_encoder_render_triangle_fan_indirect(cmd, pool.handle, 0u);
   } else {
      mtl_render_encoder *enc = kk_render_encoder(cmd);
      mtl_draw_primitives(enc, cmd->state.gfx.primitive_type, firstVertex,
                          vertexCount, instanceCount, firstInstance);
   }
}

static bool
requires_increasing_index_el_size(struct kk_cmd_buffer *cmd)
{
   enum mesa_prim prim = cmd->state.gfx.prim;
   switch (prim) {
   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_TRIANGLE_FAN:
      return (cmd->state.gfx.restart_disabled &&
              cmd->state.gfx.index.bytes_per_index < sizeof(uint32_t));
   default:
      return false;
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                  uint32_t instanceCount, uint32_t firstIndex,
                  int32_t vertexOffset, uint32_t firstInstance)
{
   /* Metal validation dislikes empty calls */
   if (instanceCount == 0 || indexCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   kk_flush_draw_state(cmd);

   /* Metal does not support triangle fans */
   bool requires_triangle_fan_unroll =
      cmd->state.gfx.prim == MESA_PRIM_TRIANGLE_FAN;

   /* Metal does not support disabling primitive restart. We need to create a
    * new index buffer for primitives that allow restart (line strip, triangle
    * strip and triangle fan). Never ever support
    * VK_EXT_primitive_topology_list_restart since it'll just add overhead */
   bool increase_index_el_size = requires_increasing_index_el_size(cmd);
   if (requires_triangle_fan_unroll || increase_index_el_size) {
      VkDrawIndexedIndirectCommand draw = {
         .indexCount = indexCount,
         .instanceCount = instanceCount,
         .firstIndex = firstIndex,
         .vertexOffset = vertexOffset,
         .firstInstance = firstInstance,
      };
      struct kk_pool pool = kk_pool_upload(cmd, &draw, sizeof(draw), 4u);
      kk_encoder_render_triangle_fan_indexed_indirect(cmd, pool.handle, 0u,
                                                      increase_index_el_size);
   } else {
      uint32_t bytes_per_index = cmd->state.gfx.index.bytes_per_index;
      enum mtl_index_type index_type =
         index_size_in_bytes_to_mtl_index_type(bytes_per_index);
      uint32_t index_buffer_offset_B =
         firstIndex * bytes_per_index + cmd->state.gfx.index.offset;

      mtl_render_encoder *enc = kk_render_encoder(cmd);
      mtl_draw_indexed_primitives(
         enc, cmd->state.gfx.primitive_type, indexCount, index_type,
         cmd->state.gfx.index.handle, index_buffer_offset_B, instanceCount,
         vertexOffset, firstInstance);
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                   VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   mtl_render_encoder *enc = kk_render_encoder(cmd);

   for (uint32_t i = 0u; i < drawCount; ++i, offset += stride) {
      cmd->state.gfx.descriptors.root_dirty = true;
      cmd->state.gfx.descriptors.root.draw.draw_id = i;

      kk_flush_draw_state(cmd);

      /* Metal does not support triangle fans */
      bool requires_unroll = cmd->state.gfx.prim == MESA_PRIM_TRIANGLE_FAN;

      if (requires_unroll) {
         kk_encoder_render_triangle_fan_indirect(cmd, buffer->mtl_handle,
                                                 offset);
      } else {
         mtl_draw_primitives_indirect(enc, cmd->state.gfx.primitive_type,
                                      buffer->mtl_handle, offset);
      }
   }
   cmd->state.gfx.descriptors.root_dirty = true;
   cmd->state.gfx.descriptors.root.draw.draw_id = 0;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                        VkDeviceSize offset, VkBuffer countBuffer,
                        VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                        uint32_t stride)
{
   /* TODO_KOSMICKRISP */
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                          VkDeviceSize offset, uint32_t drawCount,
                          uint32_t stride)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   /* Metal does not support disabling primitive restart. We need to create a
    * new index buffer for primitives that allow restart (line strip, triangle
    * strip and triangle fan). Never ever support
    * VK_EXT_primitive_topology_list_restart since it'll just add overhead */
   bool increase_index_el_size = requires_increasing_index_el_size(cmd);
   for (uint32_t i = 0u; i < drawCount; ++i, offset += stride) {
      cmd->state.gfx.descriptors.root_dirty = true;
      cmd->state.gfx.descriptors.root.draw.draw_id = i;

      kk_flush_draw_state(cmd);

      /* Metal does not support triangle fans */
      bool requires_unroll = cmd->state.gfx.prim == MESA_PRIM_TRIANGLE_FAN;

      if (requires_unroll || increase_index_el_size) {
         kk_encoder_render_triangle_fan_indexed_indirect(
            cmd, buffer->mtl_handle, offset, increase_index_el_size);
      } else {
         uint32_t bytes_per_index = cmd->state.gfx.index.bytes_per_index;
         enum mtl_index_type index_type =
            index_size_in_bytes_to_mtl_index_type(bytes_per_index);
         uint32_t index_buffer_offset = cmd->state.gfx.index.offset;

         mtl_render_encoder *enc = kk_render_encoder(cmd);
         mtl_draw_indexed_primitives_indirect(
            enc, cmd->state.gfx.primitive_type, index_type,
            cmd->state.gfx.index.handle, index_buffer_offset,
            buffer->mtl_handle, offset);
      }
   }
   cmd->state.gfx.descriptors.root_dirty = true;
   cmd->state.gfx.descriptors.root.draw.draw_id = 0;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                               VkDeviceSize offset, VkBuffer countBuffer,
                               VkDeviceSize countBufferOffset,
                               uint32_t maxDrawCount, uint32_t stride)
{
   /* TODO_KOSMICKRISP */
}
