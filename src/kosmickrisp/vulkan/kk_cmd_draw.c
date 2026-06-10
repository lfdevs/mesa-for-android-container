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

#include "kosmickrisp/libkk/kk_tessellator.h"

#include "poly/geometry.h"
#include "poly/tessellator.h"

#include "vulkan/runtime/vk_render_pass.h"
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
      .flags = vk_get_rendering_attachment_flags(info),
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
   /* If a view is being used e.g. for format change, it already points to the
    * required slice and level */
   if (!iview->planes[0].render_is_view) {
      mtl_render_pass_attachment_descriptor_set_level(descriptor,
                                                      iview->vk.base_mip_level);
      mtl_render_pass_attachment_descriptor_set_slice(
         descriptor, iview->vk.base_array_layer);
   }
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
kk_CmdEndRendering2KHR(VkCommandBuffer commandBuffer,
                       UNUSED const VkRenderingEndInfoKHR *pRenderingEndInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_rendering_state *render = &cmd->state.gfx.render;
   bool need_resolve = false;

   /* Translate render state back to VK for meta */
   VkRenderingAttachmentInfo vk_color_att[KK_MAX_RTS];
   VkRenderingAttachmentFlagsInfoKHR vk_color_att_flags[KK_MAX_RTS];
   for (uint32_t i = 0; i < render->color_att_count; i++) {
      if (render->color_att[i].resolve_mode != VK_RESOLVE_MODE_NONE)
         need_resolve = true;

      vk_color_att_flags[i] = (VkRenderingAttachmentFlagsInfoKHR){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_FLAGS_INFO_KHR,
         .flags = render->color_att[i].flags,
      };

      vk_color_att[i] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .pNext = &vk_color_att_flags[i],
         .imageView = kk_image_view_to_handle(render->color_att[i].iview),
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
         .resolveMode = render->color_att[i].resolve_mode,
         .resolveImageView =
            kk_image_view_to_handle(render->color_att[i].resolve_iview),
         .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
      };
   }

   const VkRenderingAttachmentFlagsInfoKHR vk_depth_att_flags = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_FLAGS_INFO_KHR,
      .flags = render->depth_att.flags,
   };
   const VkRenderingAttachmentInfo vk_depth_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .pNext = &vk_depth_att_flags,
      .imageView = kk_image_view_to_handle(render->depth_att.iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = render->depth_att.resolve_mode,
      .resolveImageView =
         kk_image_view_to_handle(render->depth_att.resolve_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };
   if (render->depth_att.resolve_mode != VK_RESOLVE_MODE_NONE)
      need_resolve = true;

   const VkRenderingAttachmentFlagsInfoKHR vk_stencil_att_flags = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_FLAGS_INFO_KHR,
      .flags = render->stencil_att.flags,
   };
   const VkRenderingAttachmentInfo vk_stencil_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .pNext = &vk_stencil_att_flags,
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

   if (render->flags &
       (VK_RENDERING_SUSPENDING_BIT | VK_RENDERING_CUSTOM_RESOLVE_BIT_EXT))
      need_resolve = false;

   memset(render, 0, sizeof(*render));

   if (need_resolve) {
      kk_meta_resolve_rendering(cmd, &vk_render);
   }
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBeginCustomResolveEXT(
   VkCommandBuffer commandBuffer,
   UNUSED const VkBeginCustomResolveInfoEXT *pBeginCustomResolveInfo)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   struct kk_rendering_state *render = &cmd->state.gfx.render;

   VkRenderingAttachmentInfo color_atts[KK_MAX_RTS];
   for (uint32_t i = 0; i < render->color_att_count; i++) {
      color_atts[i] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      };

      if (render->color_att[i].resolve_mode != VK_RESOLVE_MODE_CUSTOM_BIT_EXT)
         continue;

      struct kk_image_view *iview = render->color_att[i].resolve_iview;

      color_atts[i].imageView = kk_image_view_to_handle(iview);
      color_atts[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
   }

   VkRenderingAttachmentInfo depth_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
   };
   if (render->depth_att.resolve_mode == VK_RESOLVE_MODE_CUSTOM_BIT_EXT) {
      struct kk_image_view *iview = render->depth_att.resolve_iview;

      depth_att.imageView = kk_image_view_to_handle(iview);
      depth_att.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
   }

   VkRenderingAttachmentInfo stencil_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
   };
   if (render->stencil_att.resolve_mode == VK_RESOLVE_MODE_CUSTOM_BIT_EXT) {
      struct kk_image_view *iview = render->stencil_att.resolve_iview;

      stencil_att.imageView = kk_image_view_to_handle(iview);
      stencil_att.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
   }

   VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_LOCAL_READ_CONCURRENT_ACCESS_CONTROL_BIT_KHR,
      .renderArea = render->area,
      .layerCount = render->layer_count,
      .viewMask = render->view_mask,
      .colorAttachmentCount = render->color_att_count,
      .pColorAttachments = color_atts,
      .pDepthAttachment = &depth_att,
      .pStencilAttachment = &stencil_att,
   };

   const VkRenderingEndInfoKHR end_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_END_INFO_KHR,
   };

   kk_CmdEndRendering2KHR(commandBuffer, &end_info);
   kk_CmdBeginRendering(commandBuffer, &rendering_info);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdBindIndexBuffer2(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                       VkDeviceSize offset, VkDeviceSize size,
                       VkIndexType indexType)
{
   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   cmd->state.gfx.index.handle = buffer ? buffer->mtl_handle : NULL;
   cmd->state.gfx.index.buffer_size = buffer ? buffer->vk.size : 0u;
   cmd->state.gfx.index.range =
      buffer ? vk_buffer_range(&buffer->vk, offset, size) : 0;
   cmd->state.gfx.index.offset = offset;
   cmd->state.gfx.index.bytes_per_index = vk_index_type_to_bytes(indexType);

   vk_cmd_set_index_buffer_type(&cmd->vk, indexType);
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
      cmd->state.gfx.vb.handles[idx] = buffer ? buffer->mtl_handle : NULL;
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

static bool
is_primitive_culled(VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return false;
   default:
      return true;
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

static bool
kk_flush_sample_locations(struct kk_cmd_buffer *cmd)
{
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct kk_rendering_state *render = &gfx->render;
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;

   bool needs_update = false;

   /* Determine if the user-provided custom sample locations have changed */
   if (IS_DIRTY(MS_SAMPLE_LOCATIONS_ENABLE) || IS_DIRTY(MS_SAMPLE_LOCATIONS)) {
      needs_update |=
         render->sample_locations_enable != dyn->ms.sample_locations_enable;
      render->sample_locations_enable = dyn->ms.sample_locations_enable;

      if (render->sample_locations_enable) {
         struct vk_sample_locations_state *sample_locations =
            dyn->ms.sample_locations;

         uint32_t count = sample_locations->per_pixel *
                          sample_locations->grid_size.width *
                          sample_locations->grid_size.height;
         needs_update |= render->sample_locations_count != count;
         needs_update |= memcmp(render->sample_locations,
                                dyn->ms.sample_locations->locations,
                                count * sizeof(VkSampleLocationEXT)) != 0;

         render->sample_locations_count = count;
         typed_memcpy(render->sample_locations,
                      dyn->ms.sample_locations->locations, count);
      }
   }

   /* Determine if we have switched to or from multisampled bresenham lines */
   if (IS_DIRTY(IA_PRIMITIVE_TOPOLOGY) || IS_DIRTY(RS_LINE_MODE)) {
      bool was_ms_bresenham_lines = render->ms_bresenham_lines;
      render->ms_bresenham_lines =
         u_reduced_prim(dyn->ia.primitive_topology) == MESA_PRIM_LINES &&
         dyn->rs.line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM &&
         render->samples > 1;

      needs_update |= was_ms_bresenham_lines != render->ms_bresenham_lines;
   }

   if (needs_update) {
      if (render->sample_locations_enable) {
         /* Prioritize user-provided sample locations */
         struct mtl_sample_position sample_positions[KK_MAX_SAMPLES];
         for (uint32_t i = 0; i < KK_MAX_SAMPLES; i++) {
            VkSampleLocationEXT sl = render->sample_locations[i];
            sample_positions[i] = (struct mtl_sample_position){
               /* Metal asserts if values are out of range. Vulkan spec says
                * values are clamped, and dynamic state CTS tests hit this */
               .x = CLAMP(sl.x, KK_MIN_SAMPLE_LOCATION, KK_MAX_SAMPLE_LOCATION),
               .y = CLAMP(sl.y, KK_MIN_SAMPLE_LOCATION, KK_MAX_SAMPLE_LOCATION),
            };
         }

         mtl_render_pass_descriptor_set_sample_positions(
            gfx->render_pass_descriptor, sample_positions,
            render->sample_locations_count);
      } else if (render->ms_bresenham_lines) {
         /* For default sample locations with bresenham lines, set all to center
          * to provide correct rasterization */
         static const struct mtl_sample_position center = {0.5f, 0.5f};
         static const struct mtl_sample_position center_all[KK_MAX_SAMPLES] = {
            center, center, center, center, center, center, center, center};
         mtl_render_pass_descriptor_set_sample_positions(
            gfx->render_pass_descriptor, center_all, gfx->render.samples);
      } else {
         /* If custom sample locations are not needed, reset them */
         mtl_render_pass_descriptor_set_sample_positions(
            gfx->render_pass_descriptor, NULL, 0);
      }
   }

   return needs_update;
}

static void
kk_force_attachment_load(struct kk_cmd_buffer *cmd)
{
   struct kk_rendering_state *render = &cmd->state.gfx.render;

   for (uint32_t i = 0; i < render->color_att_count; i++) {
      if (render->color_att[i].iview) {
         mtl_render_pass_attachment_descriptor *att =
            mtl_render_pass_descriptor_get_color_attachment(
               cmd->state.gfx.render_pass_descriptor, i);
         mtl_render_pass_attachment_descriptor_set_load_action(
            att, MTL_LOAD_ACTION_LOAD);
      }
   }
   if (render->depth_att.iview) {
      mtl_render_pass_attachment_descriptor *att =
         mtl_render_pass_descriptor_get_depth_attachment(
            cmd->state.gfx.render_pass_descriptor);
      mtl_render_pass_attachment_descriptor_set_load_action(
         att, MTL_LOAD_ACTION_LOAD);
   }
   if (render->stencil_att.iview) {
      mtl_render_pass_attachment_descriptor *att =
         mtl_render_pass_descriptor_get_stencil_attachment(
            cmd->state.gfx.render_pass_descriptor);
      mtl_render_pass_attachment_descriptor_set_load_action(
         att, MTL_LOAD_ACTION_LOAD);
   }
}

static void
kk_flush_render_pass(struct kk_cmd_buffer *cmd)
{
   bool needs_restart = kk_flush_sample_locations(cmd);

   /* If render pass state changes and the pass is currently active, end the
    * current encoder and prepare to restart it */
   bool active_render = cmd->encoder->main.last_used == KK_ENC_RENDER &&
                        cmd->encoder->main.encoder;
   if (needs_restart && active_render) {
      kk_encoder_signal_fence_and_end(cmd);
      kk_cmd_buffer_dirty_all_gfx(cmd);
      cmd->state.gfx.need_to_start_render_pass = true;

      /* Override load action to prevent data loss between encoders.
       * TODO_KOSMICKRISP: Handle store action if we stop always setting it to
       * STORE. Metal allows it to be encoded later. */
      kk_force_attachment_load(cmd);
   }
}

static void
kk_flush_pipeline(struct kk_cmd_buffer *cmd)
{
   struct kk_device *device = kk_cmd_buffer_device(cmd);
   mtl_render_encoder *enc = kk_render_encoder(cmd);
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;

   /* Depth/stencil state may be dynamic, handle it as part of the pipeline. */
   if (cmd->state.gfx.is_depth_stencil_dynamic &&
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
      mtl_render_set_pipeline_state(enc, vs->pipeline.gfx.render);
      if (gfx->depth_stencil_state)
         mtl_set_depth_stencil_state(enc, gfx->depth_stencil_state);
   }

   /* Merge tess info before GS construction since that depends on
    * gfx->tess.prim
    */
   if ((IS_SHADER_DIRTY(TESS_CTRL) || IS_SHADER_DIRTY(TESS_EVAL)) &&
       cmd->state.shaders[MESA_SHADER_TESS_CTRL]) {
      struct kk_shader *tesc = cmd->state.shaders[MESA_SHADER_TESS_CTRL];
      struct kk_shader *tese = cmd->state.shaders[MESA_SHADER_TESS_EVAL];

      gfx->tess.info =
         kk_tess_info_merge(tese->info.tess.info, tesc->info.tess.info);

      /* Determine primitive based on the merged state */
      if (gfx->tess.info.points) {
         gfx->tess.prim = MESA_PRIM_POINTS;
      } else if (gfx->tess.info.mode == TESS_PRIMITIVE_ISOLINES) {
         gfx->tess.prim = MESA_PRIM_LINES;
      } else {
         gfx->tess.prim = MESA_PRIM_TRIANGLES;
      }
   }
}

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

enum kk_predicate_op : uint16_t {
   /* value > draw_id */
   KK_PREDICATE_GT_DRAW_ID,
   /* value == 0 */
   KK_PREDICATE_EQ_ZERO,
   /* value != 0 */
   KK_PREDICATE_NEQ_ZERO,
};

struct kk_draw_command {
   enum mesa_prim prim;
   /* Mask of stages that need per-draw data uploaded */
   uint32_t upload_mask;
   mtl_buffer *index_buffer;
   uint64_t index_buffer_offset;
   uint64_t index_buffer_range_B;
   uint64_t index_buffer_size_B;
   uint32_t restart_index;
   uint8_t index_buffer_el_size_B;
   bool indirect;
   bool indexed;
   bool restart;
   uint32_t predicate_count;
   enum kk_predicate_op predicate_op[2];
   uint32_t draw_count;
   uint32_t pad_;
   uint64_t predicate_addr[2];

   union {
      struct {
         mtl_buffer *buffer;
         uint64_t offset;
         uint32_t stride;
      } indirect_command;
      /* These arrays will be >1 when draw_count is >1 as this struct is
       * dynamically allocated. */
      VkDrawIndirectCommand draws[1];
      VkDrawIndexedIndirectCommand indexed_draws[1];
   };
};
static_assert(sizeof(struct kk_draw_command) == 104u, "Packed struct");

struct kk_draw_data {
   /* For non-indirect, 0 is vertex/index count, 1 instance count and 2 first
    * instance */
   struct kk_grid grid;
   struct {
      mtl_buffer *buffer;
      uint64_t offset;
      uint64_t range;
      uint32_t el_size_B;
   } index;
   uint32_t vertex_offset;
   enum mtl_primitive_type primitive_type;
};

static uint64_t
kk_upload_vertex_params(struct kk_cmd_buffer *cmd,
                        const struct kk_draw_data *data)
{
   const uint32_t wg_size[3] = {1, 1, 1};

   struct poly_vertex_params params;
   poly_vertex_params_init(&params, 0, wg_size);

   /* XXX: We should deduplicate this logic */
   bool indirect = kk_grid_is_indirect(data->grid);

   if (!indirect)
      poly_vertex_params_set_draw(&params, data->grid.size.x,
                                  data->grid.size.y);

   if (data->index.buffer) {
      params.index_buffer =
         mtl_buffer_get_gpu_address(data->index.buffer) + data->index.offset;
      params.index_buffer_range_el = data->index.range / data->index.el_size_B;
   }

   struct kk_shader *vs = cmd->state.shaders[MESA_SHADER_VERTEX];
   params.outputs = vs->info.vs.outputs_written;

   if (!indirect) {
      uint32_t verts = data->grid.size.x, instances = data->grid.size.y;
      unsigned vb_size =
         poly_tcs_in_size(verts * instances, vs->info.vs.outputs_written);

      /* Allocate if there are any outputs, or use the null sink to trap
       * reads if there aren't. Those reads are undefined but should not
       * fault. Affects:
       *
       *    dEQP-VK.pipeline.monolithic.no_position.explicit_declarations.basic.single_view.v0_g1
       */
      if (vb_size)
         params.output_buffer = kk_pool_alloc(cmd, vb_size, 4).gpu;
      else
         params.output_buffer = 0u;
   }

   cmd->state.gfx.per_draw_data.vertex_outputs = params.outputs;

   return kk_pool_upload(cmd, &params, sizeof(params), 8).gpu;
}

static void
kk_upload_tess_params(struct kk_cmd_buffer *cmd, struct poly_tess_params *out,
                      const struct kk_draw_data *draw)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct kk_shader *tcs = cmd->state.shaders[MESA_SHADER_TESS_CTRL];

   enum poly_tess_partitioning partitioning =
      gfx->tess.info.spacing == TESS_SPACING_EQUAL
         ? POLY_TESS_PARTITIONING_INTEGER
      : gfx->tess.info.spacing == TESS_SPACING_FRACTIONAL_ODD
         ? POLY_TESS_PARTITIONING_FRACTIONAL_ODD
         : POLY_TESS_PARTITIONING_FRACTIONAL_EVEN;

   struct poly_tess_params args = {
      .heap = kk_heap(cmd),
      .tcs_stride_el = tcs->info.tess.tcs_output_stride / 4,
      .statistic = 0u,
      .input_patch_size = dyn->ts.patch_control_points,
      .output_patch_size = tcs->info.tess.tcs_output_patch_size,
      .tcs_patch_constants = tcs->info.tess.tcs_nr_patch_outputs,
      .tcs_per_vertex_outputs = tcs->info.tess.tcs_per_vertex_outputs,
      .partitioning = partitioning,
      .points_mode = gfx->tess.info.points,
      .isolines = gfx->tess.info.mode == TESS_PRIMITIVE_ISOLINES,
   };

   if (!args.points_mode && gfx->tess.info.mode != TESS_PRIMITIVE_ISOLINES) {
      args.ccw = gfx->tess.info.ccw;
      args.ccw ^=
         dyn->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
   }

   uint32_t draw_stride_el = 5;
   size_t draw_stride_B = draw_stride_el * sizeof(uint32_t);

   /* heap is allocated by kk_heap */
   /* TODO_KOSMICKRISP Self-contained until we have rodata at the device. */
   args.patch_coord_buffer = dev->heap->gpu + sizeof(struct poly_heap);

   if (!kk_grid_is_indirect(draw->grid)) {
      unsigned in_patches = draw->grid.size.x / args.input_patch_size;
      unsigned unrolled_patches = in_patches * draw->grid.size.y;

      uint32_t alloc = 0;
      uint32_t tcs_out_offs = alloc;
      alloc += unrolled_patches * args.tcs_stride_el * sizeof(uint32_t);

      uint32_t patch_coord_offs = alloc;
      alloc += unrolled_patches * sizeof(uint32_t);

      uint32_t count_offs = alloc;
      alloc += unrolled_patches * sizeof(uint32_t);

      /* Single API draw */
      uint32_t draw_offs = alloc;
      alloc += draw_stride_B;

      struct kk_ptr ptr = kk_pool_alloc(cmd, alloc, 4);
      gfx->tess.out_draws_buffer = ptr.buffer;
      gfx->tess.out_draws_offset = ptr.offset + draw_offs;
      uint64_t addr = ptr.gpu;
      args.tcs_buffer = addr + tcs_out_offs;
      args.patches_per_instance = in_patches;
      args.coord_allocs = addr + patch_coord_offs;
      args.nr_patches = unrolled_patches;
      args.out_draws = addr + draw_offs;
      args.counts = addr + count_offs;
   } else {
      /* Allocate 3x indirect global+local grids for VS/TCS/tess */
      uint32_t grid_stride = sizeof(uint32_t) * 3;
      gfx->tess.indirect_ptr = kk_pool_alloc(cmd, grid_stride * 3, 4);

      struct kk_ptr ptr = kk_pool_alloc(cmd, draw_stride_B, 4);
      gfx->tess.out_draws_buffer = ptr.buffer;
      gfx->tess.out_draws_offset = ptr.offset;
      args.out_draws = ptr.gpu;
   }

   memcpy(out, &args, sizeof(args));
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

   if (IS_DIRTY(RS_CULL_MODE) || IS_DIRTY(IA_PRIMITIVE_TOPOLOGY)) {
      /* Only disable rendering if primitive type is culled. */
      gfx->is_cull_front_and_back =
         dyn->rs.cull_mode == VK_CULL_MODE_FRONT_AND_BACK &&
         is_primitive_culled(dyn->ia.primitive_topology);
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

   if (IS_DIRTY(VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE)) {
      desc->root.draw.clip_z_coeff =
         dyn->vp.depth_clip_negative_one_to_one ? 0.5f : 0.0f;
      desc->root_dirty = true;
   }

   if (IS_DIRTY(RS_FRONT_FACE) || IS_DIRTY(TS_DOMAIN_ORIGIN) ||
       IS_SHADER_DIRTY(TESS_CTRL) || IS_SHADER_DIRTY(TESS_EVAL)) {
      bool front_face_ccw = dyn->rs.front_face != VK_FRONT_FACE_CLOCKWISE;
      if (cmd->state.shaders[MESA_SHADER_TESS_EVAL]) {
         front_face_ccw ^= gfx->tess.info.ccw;
         front_face_ccw ^=
            dyn->ts.domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
      }
      mtl_set_front_face_winding(enc, front_face_ccw
                                         ? MTL_WINDING_COUNTER_CLOCKWISE
                                         : MTL_WINDING_CLOCKWISE);
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

   struct kk_ptr root_buffer = desc->root.root_buffer;
   if (root_buffer.gpu) {
      mtl_set_vertex_buffer(enc, root_buffer.buffer, root_buffer.offset, 0);
      mtl_set_fragment_buffer(enc, root_buffer.buffer, root_buffer.offset, 0);
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

   kk_flush_render_pass(cmd);
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

/* Returns true if the draw was successfully converted. */
static bool
kk_convert_to_indirect_draw(struct kk_cmd_buffer *cmd,
                            struct kk_draw_command *data)
{
   if (data->indirect)
      return true;

   uint32_t draw_stride = data->indexed ? sizeof(VkDrawIndexedIndirectCommand)
                                        : sizeof(VkDrawIndirectCommand);
   struct kk_ptr indirect_draw =
      kk_pool_upload(cmd, &data->draws[0], data->draw_count * draw_stride, 4u);

   if (unlikely(!indirect_draw.gpu))
      return false;

   data->indirect_command.buffer = indirect_draw.buffer;
   data->indirect_command.offset = indirect_draw.offset;
   data->indirect_command.stride = draw_stride;
   data->indirect = true;

   return true;
}

/* Returns true if the call succeeds. */
static bool
kk_predicate_draws(struct kk_cmd_buffer *cmd, struct kk_draw_command *data)
{
   assert(data->predicate_count);

   if (unlikely(!kk_convert_to_indirect_draw(cmd, data)))
      return false;

   assert((data->indirect_command.stride % sizeof(uint32_t)) == 0 &&
          "stride is not aligned");

   uint32_t out_stride = data->indexed ? sizeof(VkDrawIndexedIndirectCommand)
                                       : sizeof(VkDrawIndirectCommand);
   struct kk_ptr patched =
      kk_pool_alloc(cmd, out_stride * data->draw_count, 4u);
   if (unlikely(!patched.gpu))
      return false;

   uint64_t in_addr =
      mtl_buffer_get_gpu_address(data->indirect_command.buffer) +
      data->indirect_command.offset;
   uint32_t out_stride_el = out_stride / sizeof(uint32_t);
   uint32_t in_stride_el = data->indirect_command.stride / sizeof(uint32_t);

   /* TODO_KOSMICKRISP: This can be accomplished more efficiently using device
    * generated commands, constructing an indirect command buffer on the GPU
    * which only contains the commands to run if the condition is true. For the
    * time being, we apply predicates by zeroing out disabled indirect data */
   struct kk_grid grid = kk_grid_1d(data->draw_count);
   for (uint32_t i = 0; i < data->predicate_count; i++) {
      uint64_t addr = data->predicate_addr[i];
      switch (data->predicate_op[i]) {
      case KK_PREDICATE_GT_DRAW_ID:
         libkk_predicate_indirect_gt_draw_id(cmd, grid, true, patched.gpu,
                                             in_addr, addr, out_stride_el,
                                             in_stride_el);
         break;
      case KK_PREDICATE_EQ_ZERO:
         libkk_predicate_indirect_eq_zero(cmd, grid, true, patched.gpu, in_addr,
                                          addr, out_stride_el, in_stride_el);
         break;
      case KK_PREDICATE_NEQ_ZERO:
         libkk_predicate_indirect_neq_zero(cmd, grid, true, patched.gpu,
                                           in_addr, addr, out_stride_el,
                                           in_stride_el);
         break;
      default:
         UNREACHABLE("Unsupported indirect draw predicate");
      }

      if (i == 0) {
         /* Further predicates will operate on previous patched data */
         in_addr = patched.gpu;
         in_stride_el = out_stride_el;
      }
   }

   data->indirect_command.buffer = patched.buffer;
   data->indirect_command.offset = patched.offset;
   data->indirect_command.stride = out_stride;
   data->predicate_count = 0;

   return true;
}

/* Unrolling will always be done through indirect rendering, so if this is
 * called from non-indirect calls, we will fake it. */
static bool
kk_unroll_geometry(struct kk_cmd_buffer *cmd, struct kk_draw_command *data)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   if (unlikely(!kk_convert_to_indirect_draw(cmd, data)))
      return false;

   assert((data->indirect_command.stride % sizeof(uint32_t)) == 0 &&
          "stride is not aligned");

   struct kk_ptr out_draws = kk_pool_alloc(
      cmd, data->draw_count * sizeof(VkDrawIndexedIndirectCommand), 4u);
   if (unlikely(!out_draws.gpu))
      return false;

   struct libkk_unroll_geometry_args info = {
      .index_buffer = mtl_buffer_get_gpu_address(data->index_buffer) +
                      data->index_buffer_offset,
      .heap = kk_heap(cmd),
      .in_draw = mtl_buffer_get_gpu_address(data->indirect_command.buffer) +
                 data->indirect_command.offset,
      .out_draw = out_draws.gpu,
      .in_draw_stride_el = data->indirect_command.stride / sizeof(uint32_t),
      /* Handle primitive restart disable by forcing index to UINT32_MAX */
      .restart_index = !data->restart ? UINT32_MAX : data->restart_index,
      .index_buffer_size_el = data->indexed ? data->index_buffer_range_B /
                                                 data->index_buffer_el_size_B
                                            : 0u,
      .in_el_size_B = data->index_buffer_el_size_B,
      .out_el_size_B = 4u,
      .flatshade_first = true,
      .mode = data->prim,
   };

   libkk_unroll_geometry_struct(cmd, kk_grid_1d(1024 * data->draw_count), true,
                                info);

   data->prim = u_decomposed_prim(data->prim);
   data->index_buffer = dev->heap->map;
   /* TODO_KOSMICKRISP Self-contained until we have rodata at the device. */
   data->index_buffer_offset = sizeof(struct poly_heap);
   data->index_buffer_range_B = dev->heap->size_B - sizeof(struct poly_heap);
   data->index_buffer_size_B = dev->heap->size_B;
   data->restart_index = UINT32_MAX;
   data->index_buffer_el_size_B = 4u;
   data->indirect = true;
   data->indexed = true;
   data->restart = false;
   data->indirect_command.buffer = out_draws.buffer;
   data->indirect_command.offset = out_draws.offset;
   data->indirect_command.stride = sizeof(VkDrawIndexedIndirectCommand);

   return true;
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

static uint32_t
build_per_draw_upload_mask(struct kk_cmd_buffer *cmd)
{
   uint32_t mask = 0;

   struct kk_shader *vertex = cmd->state.shaders[MESA_SHADER_VERTEX];
   if (vertex && vertex->info.uses_per_draw_data) {
      mask |= BITFIELD_BIT(MESA_SHADER_VERTEX);
   }

   /* Tessellation will always require per draw data to be submitted. */
   struct kk_shader *tese = cmd->state.shaders[MESA_SHADER_TESS_EVAL];
   if (tese) {
      mask |= BITFIELD_BIT(MESA_SHADER_TESS_EVAL);
   }

   struct kk_shader *fragment = cmd->state.shaders[MESA_SHADER_FRAGMENT];
   if (fragment && fragment->info.uses_per_draw_data) {
      mask |= BITFIELD_BIT(MESA_SHADER_FRAGMENT);
   }

   return mask;
}

static void
kk_dispatch_draw(mtl_render_encoder *enc, struct kk_draw_data data)
{
   if (kk_grid_is_indirect(data.grid)) {
      if (data.index.buffer) {
         mtl_draw_indexed_primitives_indirect(
            enc, data.primitive_type,
            index_size_in_bytes_to_mtl_index_type(data.index.el_size_B),
            data.index.buffer, data.index.offset, data.grid.indirect,
            data.grid.offset);
      } else {
         mtl_draw_primitives_indirect(enc, data.primitive_type,
                                      data.grid.indirect, data.grid.offset);
      }
   } else {
      if (data.index.buffer) {
         mtl_draw_indexed_primitives(
            enc, data.primitive_type, data.grid.size.x,
            index_size_in_bytes_to_mtl_index_type(data.index.el_size_B),
            data.index.buffer, data.index.offset, data.grid.size.y,
            data.vertex_offset, data.grid.size.z);
      } else {
         /* Avoid Metal validation error. Empty draws from tessellation will
          * have values set to 0. */
         if (data.grid.size.x != 0 && data.grid.size.y != 0)
            mtl_draw_primitives(enc, data.primitive_type, data.vertex_offset,
                                data.grid.size.x, data.grid.size.y,
                                data.grid.size.z);
      }
   }
}

static bool
requires_index_promotion(const struct kk_draw_command *data)
{
   if (!data->indexed)
      return false;

   /* uint8_t indices must be promoted since they are not natively supported. */
   if (data->index_buffer_el_size_B == sizeof(uint8_t))
      return true;

   /* For primitive types that support primitive restart, if restart is disabled
    * and the index size is less than uint32_t, we need to make sure to perform
    * unrolling as it automatically promotes the type to uint32_t, preventing
    * valid indices from being treated as restarts. For uint32_t indices with
    * restart disabled, we realistically will never have enough vertices for the
    * restart index to be valid anyway. */
   switch (data->prim) {
   case MESA_PRIM_LINE_STRIP:
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_TRIANGLE_FAN:
      return (!data->restart &&
              data->index_buffer_el_size_B < sizeof(uint32_t));
   default:
      return false;
   }
}

static bool
requires_unroll_restart(struct kk_cmd_buffer *cmd,
                        const struct kk_draw_command *data)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   if (!data->restart || !data->indexed)
      return false;

   switch (data->prim) {
   case MESA_PRIM_POINTS:
   case MESA_PRIM_LINES:
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_LINES_ADJACENCY:
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      /* Unroll list restart only if the user requests it, to avoid associated
       * cost otherwise. Some applications unintentionally leave the primitive
       * restart flag enabled while using list primitives without any restarts,
       * and in these cases we can avoid the cost of unroll, even though they
       * are technically against spec. */
      return dev->vk.enabled_features.primitiveTopologyListRestart;
   default:
      break;
   }

   /* For topologies that natively support restart, unroll if unusual primitive
    * restart index is set by user */
   uint32_t default_idx = BITFIELD_RANGE(0, data->index_buffer_el_size_B * 8);
   return data->restart_index != default_idx;
}

/* TODO_KOSMICKRISP: Index robustness should not need special handling with
 * Metal 4 command encoders */
static bool
requires_index_robustness(struct kk_cmd_buffer *cmd,
                          const struct kk_draw_command *data)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);

   /* No need for robustness if the draw does not use an index buffer */
   if (!data->indexed)
      return false;

   /* Geometry or tessellation use robust software index buffer fetch anyway */
   if (cmd->state.shaders[MESA_SHADER_GEOMETRY] ||
       cmd->state.shaders[MESA_SHADER_TESS_EVAL])
      return false;

   /* Metal indexed draw commands require a non-null index buffer */
   if (data->index_buffer == NULL)
      return true;

   /* No need to for robustness if robustBufferAccess2 is not enabled
    * TODO_KOSMICKRISP: Which pipeline robustness option controls this? */
   if (!dev->vk.enabled_features.robustBufferAccess2 &&
       !dev->vk.enabled_features.pipelineRobustness)
      return false;

   /* We can't tell if the draw over-reads up-front with indirect draws, so we
    * always have to handle it */
   if (data->indirect)
      return true;

   /* For direct draws, we can check now if any over-read the index buffer */
   for (uint32_t i = 0; i < data->draw_count; i++) {
      const VkDrawIndexedIndirectCommand *draw = &data->indexed_draws[i];
      if ((draw->firstIndex + draw->indexCount) * data->index_buffer_el_size_B >
          data->index_buffer_range_B) {
         return true;
      }
   }

   return false;
}

static uint64_t
upload_base_vertex(struct kk_cmd_buffer *cmd, const struct kk_draw_data *data)
{
   if (kk_grid_is_indirect(data->grid)) {
      uint64_t first_vertex_offset =
         data->index.buffer
            ? offsetof(VkDrawIndexedIndirectCommand, vertexOffset)
            : offsetof(VkDrawIndirectCommand, firstVertex);
      return mtl_buffer_get_gpu_address(data->grid.indirect) +
             data->grid.offset + first_vertex_offset;
   } else {
      return kk_pool_upload(cmd, &data->vertex_offset,
                            sizeof(data->vertex_offset), 4u)
         .gpu;
   }
}

static uint64_t
upload_base_instance(struct kk_cmd_buffer *cmd, const struct kk_draw_data *data)
{
   if (kk_grid_is_indirect(data->grid)) {
      uint64_t base_instance_offset =
         data->index.buffer
            ? offsetof(VkDrawIndexedIndirectCommand, firstInstance)
            : offsetof(VkDrawIndirectCommand, firstInstance);
      return mtl_buffer_get_gpu_address(data->grid.indirect) +
             data->grid.offset + base_instance_offset;
   } else {
      return kk_pool_upload(cmd, &data->grid.size.z, sizeof(data->grid.size.z),
                            4u)
         .gpu;
   }
}

/* Prepares emulation data for stages like tessellation. It also upload system
 * values not present in Metal such as drawID. */
static void
kk_upload_per_draw_data(struct kk_cmd_buffer *cmd, uint32_t upload_mask,
                        uint32_t draw_id, const struct kk_draw_data *draw)
{
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   gfx->per_draw_data.draw_id = draw_id;

   /* Prepare emulation data for tessellation. */
   bool tess = upload_mask & BITFIELD_BIT(MESA_SHADER_TESS_EVAL);
   if (tess) {
      gfx->per_draw_data.index_size = draw->index.el_size_B;
      gfx->per_draw_data.base_vertex_addr = upload_base_vertex(cmd, draw);
      gfx->per_draw_data.base_instance_addr = upload_base_instance(cmd, draw);
      gfx->per_draw_data.vertex_params = kk_upload_vertex_params(cmd, draw);
      struct kk_ptr tess_args =
         kk_pool_alloc(cmd, sizeof(struct poly_tess_params), 4);
      gfx->per_draw_data.tess_params = tess_args.gpu;
      if (tess_args.gpu) {
         kk_upload_tess_params(cmd, tess_args.cpu, draw);
      }
   }

   struct kk_ptr shader_data_gpu =
      kk_pool_upload(cmd, &gfx->per_draw_data, sizeof(gfx->per_draw_data), 8u);
   if (unlikely(!shader_data_gpu.gpu))
      return;

   mtl_render_encoder *enc = kk_render_encoder(cmd);
   if (upload_mask & BITFIELD_BIT(MESA_SHADER_VERTEX)) {
      mtl_set_vertex_buffer(enc, shader_data_gpu.buffer, shader_data_gpu.offset,
                            2);
   }
   if (tess) {
      mtl_compute_set_buffer(kk_encoder_pre_gfx_encoder(cmd),
                             shader_data_gpu.buffer, shader_data_gpu.offset, 2);
      mtl_set_vertex_buffer(enc, shader_data_gpu.buffer, shader_data_gpu.offset,
                            2);
   }
   if (upload_mask & BITFIELD_BIT(MESA_SHADER_FRAGMENT)) {
      mtl_set_fragment_buffer(enc, shader_data_gpu.buffer,
                              shader_data_gpu.offset, 2);
   }
}

static void
kk_dispatch_compute(mtl_compute_encoder *enc, struct kk_grid grid,
                    struct mtl_size local_size)
{
   if (grid.mode == KK_GRID_DIRECT)
      mtl_dispatch_threads(enc, grid.size, local_size);
   else
      mtl_dispatch_threadgroups_with_indirect_buffer(enc, grid.indirect,
                                                     grid.offset, local_size);
}

static struct kk_draw_data
kk_launch_tess(struct kk_cmd_buffer *cmd, struct kk_draw_data draw)
{
   struct kk_device *dev = kk_cmd_buffer_device(cmd);
   struct kk_graphics_state *gfx = &cmd->state.gfx;
   struct kk_grid grid_vs, grid_tcs, grid_tess;

   struct kk_shader *vs = cmd->state.shaders[MESA_SHADER_VERTEX];
   struct kk_shader *tcs = cmd->state.shaders[MESA_SHADER_TESS_CTRL];

   struct vk_dynamic_graphics_state *dyn = &cmd->vk.dynamic_graphics_state;
   uint32_t input_patch_size = dyn->ts.patch_control_points;
   uint64_t state = gfx->per_draw_data.tess_params;
   struct kk_tess_info info = gfx->tess.info;

   /* Setup grids */
   if (kk_grid_is_indirect(draw.grid)) {
      struct libkk_tess_setup_indirect_args args = {
         .p = state,
         .grids = gfx->tess.indirect_ptr.gpu,
         .indirect =
            mtl_buffer_get_gpu_address(draw.grid.indirect) + draw.grid.offset,
         .vp = gfx->per_draw_data.vertex_params,
         .vertex_outputs = vs->info.vs.outputs_written,
         .tcs_statistic = 0,
      };

      if (draw.index.buffer) {
         args.in_index_buffer =
            mtl_buffer_get_gpu_address(draw.index.buffer) + draw.index.offset;
         args.in_index_size_B = draw.index.el_size_B;
         args.in_index_buffer_range_el =
            draw.index.range / args.in_index_size_B;
      }

      libkk_tess_setup_indirect_struct(cmd, kk_grid_1d(1), true, args);

      uint32_t grid_stride = sizeof(uint32_t) * 3;
      grid_vs =
         kk_grid_indirect(gfx->tess.indirect_ptr.buffer,
                          gfx->tess.indirect_ptr.offset + 0u * grid_stride);
      grid_tcs =
         kk_grid_indirect(gfx->tess.indirect_ptr.buffer,
                          gfx->tess.indirect_ptr.offset + 1u * grid_stride);
      grid_tess =
         kk_grid_indirect(gfx->tess.indirect_ptr.buffer,
                          gfx->tess.indirect_ptr.offset + 2u * grid_stride);
   } else {
      uint32_t patches = draw.grid.size.x / input_patch_size;
      grid_vs = grid_tcs = kk_grid_2d(draw.grid.size.x, draw.grid.size.y);

      grid_tcs.size.x = patches * tcs->info.tess.tcs_output_patch_size;
      grid_tess = kk_grid_1d(patches * draw.grid.size.y);
   }

   /* First launch the VS and TCS */

   mtl_compute_encoder *enc = kk_encoder_pre_gfx_encoder(cmd);
   {
      mtl_compute_pipeline_state *pipeline = vs->pipeline.gfx.pre_render[0];
      struct mtl_size local_size = {64, 1, 1};
      mtl_compute_set_pipeline_state(enc, pipeline);
      /* Bind root buffer here since indirect path dirties the binding at 0. */
      mtl_compute_set_buffer(enc, gfx->descriptors.root.root_buffer.buffer,
                             gfx->descriptors.root.root_buffer.offset, 0u);
      kk_dispatch_compute(enc, grid_vs, local_size);
      /* TODO_KOSMICKRISP Maybe too big of a barrier? We could definitely just
       * barrier the buffers we know we modify. */
      mtl_memory_barrier_with_scope(enc, MTL_BARRIER_SCOPE_BUFFERS);
   }
   {
      mtl_compute_pipeline_state *pipeline = vs->pipeline.gfx.pre_render[1];
      struct mtl_size local_size = {tcs->info.tess.tcs_output_patch_size, 1, 1};
      /* Avoid Metal validation error by trying to launch empty compute. Return
       * empty data. We set restart to true to avoid unroll. */
      if (grid_tcs.mode == KK_GRID_DIRECT && grid_tcs.size.x == 0u)
         return (struct kk_draw_data){.grid = kk_grid_1d(0u)};
      mtl_compute_set_pipeline_state(enc, pipeline);
      kk_dispatch_compute(enc, grid_tcs, local_size);
      mtl_memory_barrier_with_scope(enc, MTL_BARRIER_SCOPE_BUFFERS);
   }

   /* First generate counts, then prefix sum them, and then tessellate. */
   libkk_tessellate(cmd, grid_tess, true, info.mode, POLY_TESS_MODE_COUNT,
                    state);
   mtl_memory_barrier_with_scope(enc, MTL_BARRIER_SCOPE_BUFFERS);

   libkk_prefix_sum_tess(cmd, kk_grid_1d(1024u), true, state);
   mtl_memory_barrier_with_scope(enc, MTL_BARRIER_SCOPE_BUFFERS);

   libkk_tessellate(cmd, grid_tess, true, info.mode, POLY_TESS_MODE_WITH_COUNTS,
                    state);
   mtl_memory_barrier_with_scope(enc, MTL_BARRIER_SCOPE_BUFFERS);

   draw.grid =
      kk_grid_indirect(gfx->tess.out_draws_buffer, gfx->tess.out_draws_offset);

   draw.index.buffer = dev->heap->map;
   draw.index.offset = sizeof(struct poly_heap);
   draw.index.el_size_B = 4u;
   draw.primitive_type = mesa_prim_to_mtl_primitive_type(gfx->tess.prim);
   return draw;
}

/* Get modifiable per draw data. */
static struct kk_draw_data
build_draw_data(struct kk_cmd_buffer *cmd, struct kk_draw_command *data,
                uint32_t draw_id)
{
   bool tess = cmd->state.shaders[MESA_SHADER_TESS_EVAL];
   struct kk_draw_data draw = {
      .index.buffer = data->index_buffer,
      .index.offset = data->index_buffer_offset,
      .index.el_size_B = data->index_buffer_el_size_B,
      .index.range = data->index_buffer_range_B,
      .primitive_type = tess ? 0u : mesa_prim_to_mtl_primitive_type(data->prim),
   };

   if (data->indirect) {
      uint64_t indirect_offset = data->indirect_command.offset +
                                 draw_id * data->indirect_command.stride;
      draw.grid =
         kk_grid_indirect(data->indirect_command.buffer, indirect_offset);
   } else if (data->indexed) {
      VkDrawIndexedIndirectCommand draw_cmd = data->indexed_draws[draw_id];
      draw.grid = kk_grid_3d(draw_cmd.indexCount, draw_cmd.instanceCount,
                             draw_cmd.firstInstance);
      draw.vertex_offset = draw_cmd.vertexOffset;
      draw.index.offset += draw_cmd.firstIndex * data->index_buffer_el_size_B;
   } else {
      VkDrawIndirectCommand draw_cmd = data->draws[draw_id];
      draw.grid = kk_grid_3d(draw_cmd.vertexCount, draw_cmd.instanceCount,
                             draw_cmd.firstInstance);
      draw.vertex_offset = draw_cmd.firstVertex;
   }

   return draw;
}

static void
kk_draw(struct kk_cmd_buffer *cmd, struct kk_draw_command *data)
{
   kk_flush_gfx_state(cmd);

   data->restart = cmd->vk.dynamic_graphics_state.ia.primitive_restart_enable;
   data->restart_index =
      cmd->vk.dynamic_graphics_state.ia.primitive_restart_index;

   /* Convert to indirect and process predicates. Skip draw if we fail. */
   if (data->predicate_count > 0 && !kk_predicate_draws(cmd, data))
      return;

   bool tess = cmd->state.shaders[MESA_SHADER_TESS_EVAL];

   /* Unroll geometry. Skip draw if we fail. No need to unroll if tessellation
    * is present since it also handles unrolling. */
   bool requires_unroll = !tess && (data->prim == MESA_PRIM_TRIANGLE_FAN ||
                                    requires_index_promotion(data) ||
                                    requires_unroll_restart(cmd, data) ||
                                    requires_index_robustness(cmd, data));
   if (requires_unroll && !kk_unroll_geometry(cmd, data))
      return;

   for (uint32_t i = 0; i < data->draw_count; i++) {
      struct kk_draw_data draw_data = build_draw_data(cmd, data, i);

      if (data->upload_mask)
         kk_upload_per_draw_data(cmd, data->upload_mask, i, &draw_data);

      if (tess)
         draw_data = kk_launch_tess(cmd, draw_data);
      kk_dispatch_draw(kk_render_encoder(cmd), draw_data);
   }
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

   struct kk_draw_command data = {
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .predicate_count = cmd->state.cond_render.enabled ? 1u : 0u,
      .predicate_op[0] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .draw_count = 1,
      .predicate_addr[0] = cmd->state.cond_render.address,
      .draws[0] = {
         .vertexCount = vertexCount,
         .instanceCount = instanceCount,
         .firstVertex = firstVertex,
         .firstInstance = firstInstance,
      }};

   kk_draw(cmd, &data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawMultiEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                   const VkMultiDrawInfoEXT *pVertexInfo,
                   uint32_t instanceCount, uint32_t firstInstance,
                   uint32_t stride)
{
   /* Metal validation dislikes empty calls */
   if (drawCount == 0 || instanceCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   /* Build final draw list from parameters */
   struct kk_draw_command *data =
      rzalloc_size(NULL, sizeof(struct kk_draw_command) +
                            sizeof(VkDrawIndirectCommand) * (drawCount - 1u));
   if (!data) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   *data = (struct kk_draw_command){
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .predicate_count = cmd->state.cond_render.enabled ? 1u : 0u,
      .predicate_op[0] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .predicate_addr[0] = cmd->state.cond_render.address,
   };

   for (uint32_t i = 0; i < drawCount; ++i) {
      /* Metal validation dislikes empty calls */
      if (pVertexInfo->vertexCount > 0) {
         data->draws[data->draw_count] = (VkDrawIndirectCommand){
            .vertexCount = pVertexInfo->vertexCount,
            .instanceCount = instanceCount,
            .firstVertex = pVertexInfo->firstVertex,
            .firstInstance = firstInstance,
         };
         data->draw_count += 1u;
      }

      pVertexInfo = ((void *)pVertexInfo) + stride;
   }

   if (data->draw_count > 0)
      kk_draw(cmd, data);

   ralloc_free(data);
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

   struct kk_draw_command data = {
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .index_buffer = cmd->state.gfx.index.handle,
      .index_buffer_offset = cmd->state.gfx.index.offset,
      .index_buffer_range_B = cmd->state.gfx.index.range,
      .index_buffer_size_B = cmd->state.gfx.index.buffer_size,
      .index_buffer_el_size_B = cmd->state.gfx.index.bytes_per_index,
      .indexed = true,
      .predicate_count = cmd->state.cond_render.enabled ? 1u : 0u,
      .predicate_op[0] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .draw_count = 1,
      .predicate_addr[0] = cmd->state.cond_render.address,
      .indexed_draws[0] =
         {
            .indexCount = indexCount,
            .instanceCount = instanceCount,
            .firstIndex = firstIndex,
            .vertexOffset = vertexOffset,
            .firstInstance = firstInstance,
         },
   };

   kk_draw(cmd, &data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer, uint32_t drawCount,
                          const VkMultiDrawIndexedInfoEXT *pIndexInfo,
                          uint32_t instanceCount, uint32_t firstInstance,
                          uint32_t stride, const int32_t *pVertexOffset)
{
   /* Metal validation dislikes empty calls */
   if (drawCount == 0 || instanceCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   /* Build final draw list from parameters */
   struct kk_draw_command *data = ralloc_size(
      NULL, sizeof(struct kk_draw_command) +
               sizeof(VkDrawIndexedIndirectCommand) * (drawCount - 1u));
   if (!data) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   *data = (struct kk_draw_command){
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .index_buffer = cmd->state.gfx.index.handle,
      .index_buffer_offset = cmd->state.gfx.index.offset,
      .index_buffer_range_B = cmd->state.gfx.index.range,
      .index_buffer_size_B = cmd->state.gfx.index.buffer_size,
      .index_buffer_el_size_B = cmd->state.gfx.index.bytes_per_index,
      .indexed = true,
      .predicate_count = cmd->state.cond_render.enabled ? 1u : 0u,
      .predicate_op[0] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .predicate_addr[0] = cmd->state.cond_render.address,
   };

   for (uint32_t i = 0; i < drawCount; ++i) {
      /* Metal validation dislikes empty calls */
      if (pIndexInfo->indexCount > 0) {
         data->indexed_draws[data->draw_count] = (VkDrawIndexedIndirectCommand){
            .indexCount = pIndexInfo->indexCount,
            .instanceCount = instanceCount,
            .firstIndex = pIndexInfo->firstIndex,
            .vertexOffset = pVertexOffset != NULL ? *pVertexOffset
                                                  : pIndexInfo->vertexOffset,
            .firstInstance = firstInstance,
         };
         data->draw_count += 1u;
      }

      pIndexInfo = ((void *)pIndexInfo) + stride;
   }

   if (data->draw_count > 0)
      kk_draw(cmd, data);

   ralloc_free(data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                   VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
   if (drawCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct kk_draw_command data = {
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .indirect = true,
      .predicate_count = cmd->state.cond_render.enabled ? 1u : 0u,
      .predicate_op[0] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .draw_count = drawCount,
      .predicate_addr[0] = cmd->state.cond_render.address,
      .indirect_command.buffer = buffer->mtl_handle,
      .indirect_command.offset = offset,
      .indirect_command.stride = stride,
   };

   kk_draw(cmd, &data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                          VkDeviceSize offset, uint32_t drawCount,
                          uint32_t stride)
{
   if (drawCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct kk_draw_command data = {
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .index_buffer = cmd->state.gfx.index.handle,
      .index_buffer_offset = cmd->state.gfx.index.offset,
      .index_buffer_range_B = cmd->state.gfx.index.range,
      .index_buffer_size_B = cmd->state.gfx.index.buffer_size,
      .index_buffer_el_size_B = cmd->state.gfx.index.bytes_per_index,
      .indirect = true,
      .indexed = true,
      .predicate_count = cmd->state.cond_render.enabled ? 1u : 0u,
      .predicate_op[0] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .draw_count = drawCount,
      .predicate_addr[0] = cmd->state.cond_render.address,
      .indirect_command.buffer = buffer->mtl_handle,
      .indirect_command.offset = offset,
      .indirect_command.stride = stride,
   };

   kk_draw(cmd, &data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                        VkDeviceSize offset, VkBuffer countBuffer,
                        VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
                        uint32_t stride)
{
   if (maxDrawCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(kk_buffer, count_buffer, countBuffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct kk_draw_command data = {
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .indirect = true,
      .predicate_count = cmd->state.cond_render.enabled ? 2u : 1u,
      .predicate_op[0] = KK_PREDICATE_GT_DRAW_ID,
      .predicate_op[1] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .draw_count = maxDrawCount,
      .predicate_addr[0] =
         vk_buffer_address(&count_buffer->vk, countBufferOffset),
      .predicate_addr[1] = cmd->state.cond_render.address,
      .indirect_command.buffer = buffer->mtl_handle,
      .indirect_command.offset = offset,
      .indirect_command.stride = stride,
   };

   kk_draw(cmd, &data);
}

VKAPI_ATTR void VKAPI_CALL
kk_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                               VkDeviceSize offset, VkBuffer countBuffer,
                               VkDeviceSize countBufferOffset,
                               uint32_t maxDrawCount, uint32_t stride)
{
   if (maxDrawCount == 0)
      return;

   VK_FROM_HANDLE(kk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(kk_buffer, buffer, _buffer);
   VK_FROM_HANDLE(kk_buffer, count_buffer, countBuffer);

   const struct vk_dynamic_graphics_state *dyn =
      &cmd->vk.dynamic_graphics_state;

   struct kk_draw_command data = {
      .prim = vk_topology_to_mesa(dyn->ia.primitive_topology),
      .upload_mask = build_per_draw_upload_mask(cmd),
      .index_buffer = cmd->state.gfx.index.handle,
      .index_buffer_offset = cmd->state.gfx.index.offset,
      .index_buffer_range_B = cmd->state.gfx.index.range,
      .index_buffer_size_B = cmd->state.gfx.index.buffer_size,
      .index_buffer_el_size_B = cmd->state.gfx.index.bytes_per_index,
      .indirect = true,
      .indexed = true,
      .predicate_count = cmd->state.cond_render.enabled ? 2u : 1u,
      .predicate_op[0] = KK_PREDICATE_GT_DRAW_ID,
      .predicate_op[1] = cmd->state.cond_render.inverted
                            ? KK_PREDICATE_EQ_ZERO
                            : KK_PREDICATE_NEQ_ZERO,
      .draw_count = maxDrawCount,
      .predicate_addr[0] =
         vk_buffer_address(&count_buffer->vk, countBufferOffset),
      .predicate_addr[1] = cmd->state.cond_render.address,
      .indirect_command.buffer = buffer->mtl_handle,
      .indirect_command.offset = offset,
      .indirect_command.stride = stride,
   };

   kk_draw(cmd, &data);
}
