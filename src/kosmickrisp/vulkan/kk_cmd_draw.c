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

#include "poly/geometry.h"

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
    * renderTargetWidth/Height to non-0 values and defaultRasterSampleCount.
    * However, since the sample count will only be known at first pipeline bind,
    * we need to delay the start of the pass until then since Metal will ignore
    * bound pipeline's sample count. */
   bool no_framebuffer =
      framebuffer_extent.width == 0u && framebuffer_extent.height == 0u;
   if (no_framebuffer) {
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

   /* Rendering with no attachments requires pushing the start of the render
    * pass to first pipeline binding to know sample count. */
   if (!no_framebuffer)
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
   cmd->state.gfx.need_to_start_render_pass = no_framebuffer;

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

#define IS_DIRTY(bit) BITSET_TEST(dyn->dirty, MESA_VK_DYNAMIC_##bit)
#define IS_SHADER_DIRTY(bit)                                                   \
   (cmd->state.dirty_shaders & BITFIELD_BIT(MESA_SHADER_##bit))

static void
kk_flush_pipeline(struct kk_cmd_buffer *cmd)
{
   struct kk_device *device = kk_cmd_buffer_device(cmd);
   mtl_render_encoder *enc = kk_render_encoder(cmd);
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;

   /* Depth/stencil state may be dynamic, handle it as part of the pipeline. */
   if (cmd->state.gfx.is_depth_stencil_dynamic &&
       (cmd->state.gfx.render.depth_att.vk_format != VK_FORMAT_UNDEFINED ||
        cmd->state.gfx.render.stencil_att.vk_format != VK_FORMAT_UNDEFINED) &&
       (IS_DIRTY(DS_DEPTH_TEST_ENABLE) | IS_DIRTY(DS_DEPTH_WRITE_ENABLE) |
        IS_DIRTY(DS_DEPTH_COMPARE_OP) | IS_DIRTY(DS_STENCIL_TEST_ENABLE) |
        IS_DIRTY(DS_STENCIL_OP) | IS_DIRTY(DS_STENCIL_COMPARE_MASK) |
        IS_DIRTY(DS_STENCIL_WRITE_MASK))) {
      kk_cmd_release_dynamic_ds_state(cmd);

      bool has_depth = dyn->rp.attachments & MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
      bool has_stencil =
         dyn->rp.attachments & MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
      gfx->depth_stencil_state = kk_compile_depth_stencil_state(
         device, &dyn->ds, has_depth, has_stencil);
      mtl_set_depth_stencil_state(enc, gfx->depth_stencil_state);
   }

   if (IS_SHADER_DIRTY(VERTEX)) {
      struct kk_shader *vs = cmd->state.shaders[MESA_SHADER_VERTEX];
      mtl_render_set_pipeline_state(enc, vs->pipeline.gfx.handle);
      if (gfx->depth_stencil_state)
         mtl_set_depth_stencil_state(enc, gfx->depth_stencil_state);
   }
}

static void
kk_flush_dynamic_state(struct kk_cmd_buffer *cmd)
{
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct kk_descriptor_state *desc = &gfx->descriptors;
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;
   mtl_render_encoder *enc = kk_render_encoder(cmd);

   if (IS_DIRTY(VI_BINDING_STRIDES)) {
      u_foreach_bit(ndx, dyn->vi->bindings_valid) {
         desc->root.draw.buffer_strides[ndx] = dyn->vi_binding_strides[ndx];
      }
      desc->root_dirty = true;
   }

   if (IS_DIRTY(RS_RASTERIZER_DISCARD_ENABLE)) {
      if (dyn->rs.rasterizer_discard_enable) {
         set_empty_scissor(enc);
      } else {
         /* Enforce setting the correct scissors */
         BITSET_SET(dyn->dirty, MESA_VK_DYNAMIC_VP_VIEWPORT_COUNT);
      }
   }

   if (IS_DIRTY(RS_CULL_MODE)) {
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

   /* We enable raster discard by setting scissor to size (0, 0) */
   if (!(dyn->rs.rasterizer_discard_enable || gfx->is_cull_front_and_back) &&
       (IS_DIRTY(VP_VIEWPORT_COUNT) || IS_DIRTY(VP_VIEWPORTS) ||
        IS_DIRTY(VP_SCISSOR_COUNT) || IS_DIRTY(VP_SCISSORS)))
      kk_flush_vp_state(cmd);

   if (IS_DIRTY(RS_FRONT_FACE)) {
      mtl_set_front_face_winding(
         enc, vk_front_face_to_mtl_winding(
                 cmd->vk.dynamic_graphics_state.rs.front_face));
   }

   if (IS_DIRTY(RS_DEPTH_BIAS_FACTORS) || IS_DIRTY(RS_DEPTH_BIAS_ENABLE)) {
      if (dyn->rs.depth_bias.enable)
         mtl_set_depth_bias(enc, dyn->rs.depth_bias.constant_factor,
                            dyn->rs.depth_bias.slope_factor,
                            dyn->rs.depth_bias.clamp);
      else
         mtl_set_depth_bias(enc, 0.0f, 0.0f, 0.0f);
   }

   if (IS_DIRTY(RS_DEPTH_CLAMP_ENABLE)) {
      enum mtl_depth_clip_mode mode = dyn->rs.depth_clamp_enable
                                         ? MTL_DEPTH_CLIP_MODE_CLAMP
                                         : MTL_DEPTH_CLIP_MODE_CLIP;
      mtl_set_depth_clip_mode(enc, mode);
   }

   if (IS_DIRTY(DS_STENCIL_REFERENCE))
      mtl_set_stencil_references(
         enc, cmd->vk.dynamic_graphics_state.ds.stencil.front.reference,
         cmd->vk.dynamic_graphics_state.ds.stencil.back.reference);

   if (IS_DIRTY(CB_BLEND_CONSTANTS)) {
      static_assert(sizeof(desc->root.draw.blend_constant) ==
                       sizeof(dyn->cb.blend_constants),
                    "common size");

      memcpy(desc->root.draw.blend_constant, dyn->cb.blend_constants,
             sizeof(dyn->cb.blend_constants));
      desc->root_dirty = true;
   }

   if (IS_DIRTY(VI) || IS_DIRTY(VI_BINDINGS_VALID) ||
       IS_DIRTY(VI_BINDING_STRIDES) || gfx->dirty & KK_DIRTY_VB) {
      struct kk_shader *vs = cmd->state.shaders[MESA_SHADER_VERTEX];
      unsigned slot = 0;
      u_foreach_bit(i, vs->info.vs.attribs_read) {
         if (dyn->vi->attributes_valid & BITFIELD_BIT(i)) {
            struct vk_vertex_attribute_state attr = dyn->vi->attributes[i];
            struct kk_addr_range vb = gfx->vb.addr_range[attr.binding];

            desc->root.draw.attrib_clamps[slot] = kk_calculate_vbo_clamp(
               vb.addr, 0, vk_format_to_pipe_format(attr.format), vb.range,
               dyn->vi_binding_strides[attr.binding], attr.offset,
               &desc->root.draw.attrib_base[slot]);
            desc->root.draw.buffer_strides[attr.binding] =
               dyn->vi_binding_strides[attr.binding];
         }
         slot++;
      }
      desc->root_dirty = true;
   }

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
}

static void
kk_flush_gfx_state(struct kk_cmd_buffer *cmd)
{
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct kk_descriptor_state *desc = &gfx->descriptors;

   kk_flush_pipeline(cmd);

   if (desc->push_dirty)
      kk_cmd_buffer_flush_push_descriptors(cmd, desc);

   kk_flush_dynamic_state(cmd);

   cmd->state.dirty_shaders = 0u;
   cmd->state.gfx.dirty = 0u;
   vk_dynamic_graphics_state_clear_dirty(&cmd->vk.dynamic_graphics_state);
}
#undef IS_SHADER_DIRTY
#undef IS_DIRTY

struct kk_draw_data {
   union {
      /* Vertex/index count and instance count. */
      uint32_t count[2];
      mtl_buffer *indirect_buffer;
   };
   mtl_buffer *index_buffer;
   uint64_t index_buffer_offset;
   uint64_t indirect_buffer_offset;
   uint32_t index_buffer_range_B;
   uint32_t first_index;
   uint32_t first_vertex;
   uint32_t first_instance;
   enum mesa_prim prim;
   uint8_t index_size;
   bool indirect;
   bool indexed;
   bool restart;
};

static void
kk_init_heap(const void *data)
{
   struct kk_cmd_buffer *cmd = (struct kk_cmd_buffer *)data;
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   size_t size = 128 * 1024 * 1024;
   kk_alloc_bo(dev, &dev->vk.base, size, 0, &dev->heap);

   struct poly_heap *map = (struct poly_heap *)dev->heap->cpu;

   /* TODO_KOSMICKRISP Self-contained until we have rodata at the device. */
   *map = (struct poly_heap){
      .base = dev->heap->gpu + sizeof(struct poly_heap),
      .size = size - sizeof(struct poly_heap),
   };
}

static uint64_t
kk_heap(struct kk_cmd_buffer *cmd)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   util_call_once_data(&dev->heap_init_once, kk_init_heap, cmd);

   /* We need to free all allocations after each command buffer execution */
   if (!cmd->uses_heap) {
      uint64_t addr = dev->heap->gpu;

      /* Zeroing the allocated index frees everything */
      kk_cmd_write(cmd, (struct libkk_imm_write){
                           addr + offsetof(struct poly_heap, bottom), 0});

      cmd->uses_heap = true;
   }

   return dev->heap->gpu;
}

/* Unrolling will always be done through indirect rendering, so if this is
 * called from non-indirect calls, we will fake it. */
static struct kk_draw_data
kk_unroll_geometry(struct kk_cmd_buffer *cmd, struct kk_draw_data data,
                   bool promote_index_type)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   if (!data.indirect) {
      if (data.indexed) {
         VkDrawIndexedIndirectCommand draw = {
            .indexCount = data.count[0],
            .instanceCount = data.count[1],
            .firstIndex = data.first_index,
            .vertexOffset = data.first_vertex,
            .firstInstance = data.first_instance,
         };

         data.indirect_buffer =
            kk_pool_upload(cmd, &draw, sizeof(draw), 4u).handle;
         data.indirect = true;
      } else {
         VkDrawIndirectCommand draw = {
            .vertexCount = data.count[0],
            .instanceCount = data.count[1],
            .firstVertex = data.first_vertex,
            .firstInstance = data.first_instance,
         };
         data.indirect_buffer =
            kk_pool_upload(cmd, &draw, sizeof(draw), 4u).handle;
         data.indirect = true;
      }
   }

   struct kk_bo *out_draw =
      kk_cmd_allocate_buffer(cmd, sizeof(VkDrawIndexedIndirectCommand), 4u);

   if (!out_draw)
      return data;

   struct libkk_unroll_geometry_and_restart_args info = {
      .index_buffer = mtl_buffer_get_gpu_address(data.index_buffer) +
                      data.index_buffer_offset,
      .heap = kk_heap(cmd),
      .in_draw = mtl_buffer_get_gpu_address(data.indirect_buffer) +
                 data.indirect_buffer_offset,
      .out_draw = out_draw->gpu,
      .restart_index =
         promote_index_type ? UINT32_MAX : cmd->state.gfx.index.restart,
      .index_buffer_size_el = data.index_buffer_range_B,
      .in_el_size_B = data.index_size,
      .out_el_size_B = 4u,
      .flatshade_first = true,
      .mode = data.prim,
   };

   struct mtl_size grid = {1, 1, 1};
   libkk_unroll_geometry_and_restart_struct(cmd, grid, true, info);

   data.indirect_buffer = out_draw->map;
   data.index_buffer = dev->heap->map;
   /* TODO_KOSMICKRISP Self-contained until we have rodata at the device. */
   data.index_buffer_offset = sizeof(struct poly_heap);
   data.indirect_buffer_offset = 0u;
   data.index_buffer_range_B = dev->heap->size_B - sizeof(struct poly_heap);
   data.first_index = 0u;
   data.prim = u_decomposed_prim(data.prim);
   data.index_size = 4u;
   data.indirect = true;
   data.indexed = true;
   data.restart = false;
   return data;
}

static enum mtl_primitive_type
mesa_prim_to_mtl_primitive_type(enum mesa_prim prim)
{
   switch (prim) {
   case MESA_PRIM_POINTS:
      return MTL_PRIMITIVE_TYPE_POINT;
   case MESA_PRIM_LINES:
      return MTL_PRIMITIVE_TYPE_LINE;
   case MESA_PRIM_LINE_STRIP:
      return MTL_PRIMITIVE_TYPE_LINE_STRIP;
   case MESA_PRIM_TRIANGLES:
      return MTL_PRIMITIVE_TYPE_TRIANGLE;
   case MESA_PRIM_TRIANGLE_STRIP:
      return MTL_PRIMITIVE_TYPE_TRIANGLE_STRIP;
   default:
      UNREACHABLE("Unsupported primitive type");
   }
}

static void
kk_dispatch_draw(struct kk_cmd_buffer *cmd, struct kk_draw_data data)
{
   mtl_render_encoder *enc = kk_render_encoder(cmd);
   enum mtl_primitive_type primitive_type =
      mesa_prim_to_mtl_primitive_type(data.prim);
   if (data.indirect) {
      if (data.indexed) {
         enum mtl_index_type index_type =
            index_size_in_bytes_to_mtl_index_type(data.index_size);
         mtl_draw_indexed_primitives_indirect(
            enc, primitive_type, index_type, data.index_buffer,
            data.index_buffer_offset, data.indirect_buffer,
            data.indirect_buffer_offset);
      } else {
         mtl_draw_primitives_indirect(enc, primitive_type, data.indirect_buffer,
                                      data.indirect_buffer_offset);
      }
   } else {
      if (data.indexed) {
         enum mtl_index_type index_type =
            index_size_in_bytes_to_mtl_index_type(data.index_size);
         uint32_t index_buffer_offset =
            data.first_index * data.index_size + data.index_buffer_offset;

         mtl_render_encoder *enc = kk_render_encoder(cmd);
         mtl_draw_indexed_primitives(enc, primitive_type, data.count[0],
                                     index_type, cmd->state.gfx.index.handle,
                                     index_buffer_offset, data.count[1],
                                     data.first_vertex, data.first_instance);
      } else {
         mtl_draw_primitives(enc, primitive_type, data.first_vertex,
                             data.count[0], data.count[1], data.first_instance);
      }
   }
}

static bool
requires_index_promotion(struct kk_cmd_buffer *cmd)
{
   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;
   switch (dyn->ia.primitive_topology) {
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return (!dyn->ia.primitive_restart_enable &&
              cmd->state.gfx.index.bytes_per_index < sizeof(uint32_t));
   default:
      return false;
   }
}

static void
kk_draw(struct kk_cmd_buffer *cmd, struct kk_draw_data data)
{
   /* Filter trivial calls. */
   if (!data.indirect && (data.count[0] == 0u || data.count[1] == 0u))
      return;

   kk_flush_gfx_state(cmd);

   /* If the restart bool is set, it means that primitive restart is disabled
    * but index type is not uint32_t which requires promoting the type to
    * uint32_t since we cannot disable primitive restart in Metal. */
   bool promote_index_type = requires_index_promotion(cmd);

   /* We always need to unroll triangle fans. */
   data.restart = (data.prim == MESA_PRIM_TRIANGLE_FAN);

   if (promote_index_type || data.restart)
      data = kk_unroll_geometry(cmd, data, promote_index_type);

   kk_dispatch_draw(cmd, data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
           uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
   /* Metal validation dislikes empty calls */
   if (instanceCount == 0 || vertexCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct kk_draw_data data = {
      .count[0] = vertexCount,
      .count[1] = instanceCount,
      .first_vertex = firstVertex,
      .first_instance = firstInstance,
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
   };

   kk_draw(cmd, data);
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

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct kk_draw_data data = {
      .count[0] = indexCount,
      .count[1] = instanceCount,
      .index_buffer = cmd->state.gfx.index.handle,
      .index_buffer_offset = cmd->state.gfx.index.offset,
      .index_buffer_range_B =
         cmd->state.gfx.index.size - cmd->state.gfx.index.offset,
      .first_index = firstIndex,
      .first_vertex = vertexOffset,
      .first_instance = firstInstance,
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .index_size = cmd->state.gfx.index.bytes_per_index,
      .indexed = true,
   };

   kk_draw(cmd, data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                   VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   for (uint32_t i = 0u; i < drawCount; ++i, offset += stride) {
      /* TODO_KOSMICKRISP
       * Move this to a separate buffer from the root so we don't have to upload
       * it every single loop. Pass it to the kk_draw call as a parameter that
       * will later be uploaded.
       */
      cmd->state.gfx.descriptors.root_dirty = true;
      cmd->state.gfx.descriptors.root.draw.draw_id = i;

      struct kk_draw_data data = {
         .indirect_buffer = buffer->mtl_handle,
         .indirect_buffer_offset = offset,
         .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
         .indirect = true,
      };

      kk_draw(cmd, data);
   }
   /* TODO_KOSMICKRISP Remove once above is done */
   cmd->state.gfx.descriptors.root_dirty = true;
   cmd->state.gfx.descriptors.root.draw.draw_id = 0;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                          VkDeviceSize offset, uint32_t drawCount,
                          uint32_t stride)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   for (uint32_t i = 0u; i < drawCount; ++i, offset += stride) {
      /* TODO_KOSMICKRISP
       * Move this to a separate buffer from the root so we don't have to upload
       * it every single loop. Pass it to the kk_draw call as a parameter that
       * will later be uploaded.
       */
      cmd->state.gfx.descriptors.root_dirty = true;
      cmd->state.gfx.descriptors.root.draw.draw_id = i;

      struct kk_draw_data data = {
         .indirect_buffer = buffer->mtl_handle,
         .index_buffer = cmd->state.gfx.index.handle,
         .indirect_buffer_offset = offset,
         .index_buffer_offset = cmd->state.gfx.index.offset,
         .index_buffer_range_B =
            cmd->state.gfx.index.size - cmd->state.gfx.index.offset,
         .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
         .index_size = cmd->state.gfx.index.bytes_per_index,
         .indirect = true,
         .indexed = true,
      };

      kk_draw(cmd, data);
   }
   /* TODO_KOSMICKRISP Remove once above is done */
   cmd->state.gfx.descriptors.root_dirty = true;
   cmd->state.gfx.descriptors.root.draw.draw_id = 0;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                        VkDeviceSize offset, VkBuffer countBuffer,
                        VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                        uint32_t stride)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(kk_buffer, count_buffer, countBuffer);

   assert((stride % 4) == 0 && "aligned");

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   size_t out_stride = sizeof(uint32_t) * 4;
   struct kk_bo *patched =
      kk_cmd_allocate_buffer(cmd, out_stride * maxDrawCount, 4);
   uint64_t in = vk_buffer_address(&buffer->vk, offset);
   uint64_t count_addr =
      vk_buffer_address(&count_buffer->vk, countBufferOffset);

   struct mtl_size grid = {maxDrawCount, 1u, 1u};
   libkk_predicate_indirect(cmd, grid, true, patched->gpu, in, count_addr,
                            stride / 4, false);

   for (unsigned i = 0; i < maxDrawCount; ++i) {
      /* TODO_KOSMICKRISP
       * Move this to a separate buffer from the root so we don't have to upload
       * it every single loop. Pass it to the kk_draw call as a parameter that
       * will later be uploaded.
       */
      cmd->state.gfx.descriptors.root_dirty = true;
      cmd->state.gfx.descriptors.root.draw.draw_id = i;

      struct kk_draw_data data = {
         .indirect_buffer = patched->map,
         .indirect_buffer_offset = out_stride * i,
         .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
         .indirect = true,
      };

      kk_draw(cmd, data);
   }
   /* TODO_KOSMICKRISP Remove once above is done */
   cmd->state.gfx.descriptors.root_dirty = true;
   cmd->state.gfx.descriptors.root.draw.draw_id = 0;
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                               VkDeviceSize offset, VkBuffer countBuffer,
                               VkDeviceSize countBufferOffset,
                               uint32_t maxDrawCount, uint32_t stride)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(kk_buffer, count_buffer, countBuffer);

   assert((stride % 4) == 0 && "aligned");

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   size_t out_stride = sizeof(uint32_t) * 5;
   struct kk_bo *patched =
      kk_cmd_allocate_buffer(cmd, out_stride * maxDrawCount, 4);
   uint64_t in = vk_buffer_address(&buffer->vk, offset);
   uint64_t count_addr =
      vk_buffer_address(&count_buffer->vk, countBufferOffset);

   struct mtl_size grid = {maxDrawCount, 1u, 1u};
   libkk_predicate_indirect(cmd, grid, true, patched->gpu, in, count_addr,
                            stride / 4, true);

   for (unsigned i = 0; i < maxDrawCount; ++i) {
      /* TODO_KOSMICKRISP
       * Move this to a separate buffer from the root so we don't have to upload
       * it every single loop. Pass it to the kk_draw call as a parameter that
       * will later be uploaded.
       */
      cmd->state.gfx.descriptors.root_dirty = true;
      cmd->state.gfx.descriptors.root.draw.draw_id = i;

      struct kk_draw_data data = {
         .indirect_buffer = patched->map,
         .index_buffer = cmd->state.gfx.index.handle,
         .indirect_buffer_offset = out_stride * i,
         .index_buffer_offset = cmd->state.gfx.index.offset,
         .index_buffer_range_B =
            cmd->state.gfx.index.size - cmd->state.gfx.index.offset,
         .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
         .index_size = cmd->state.gfx.index.bytes_per_index,
         .indirect = true,
         .indexed = true,
      };

      kk_draw(cmd, data);
   }
   /* TODO_KOSMICKRISP Remove once above is done */
   cmd->state.gfx.descriptors.root_dirty = true;
   cmd->state.gfx.descriptors.root.draw.draw_id = 0;
}
