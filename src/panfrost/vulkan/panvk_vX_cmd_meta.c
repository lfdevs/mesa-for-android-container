/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_meta.h"
#include "panvk_entrypoints.h"
#include "panvk_meta.h"
#include "panvk_tracepoints.h"
#if PAN_ARCH >= 10
#include "csf/panvk_instr.h"
#endif

#include "panvk_cmd_precomp.h"
#include "libpan.h"
#include "libpan_dgc.h"

static bool
copy_to_image_use_gfx_pipeline(struct panvk_image *dst_img)
{
   /* Don't force gfx-based copies if the format is bigger than 32-bit. */
   if (PANVK_DEBUG(COPY_GFX) &&
       vk_format_get_blocksize(dst_img->vk.format) <= 4)
      return true;

   /* Writes to AFBC images must go through the graphics pipeline. */
   if (drm_is_afbc(dst_img->vk.drm_format_mod))
      return true;

   return false;
}

void
panvk_per_arch(cmd_meta_compute_start)(
   struct panvk_cmd_buffer *cmdbuf,
   struct panvk_cmd_meta_compute_save_ctx *save_ctx)
{
   const struct panvk_descriptor_set *set0 =
      cmdbuf->state.compute.desc_state.sets[0];
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.compute.desc_state.push_sets[0];

   save_ctx->set0 = set0;
   if (push_set0 && push_set0 == set0) {
      save_ctx->push_set0.desc_count = push_set0->desc_count;
      save_ctx->push_set0.descs_dev_addr = push_set0->descs.dev;
      memcpy(save_ctx->push_set0.desc_storage, push_set0->descs.host,
             push_set0->desc_count * PANVK_DESCRIPTOR_SIZE);
   }

   save_ctx->push_constants = cmdbuf->state.push_constants;
   save_ctx->cs.shader = cmdbuf->state.compute.shader;
   save_ctx->cs.desc = cmdbuf->state.compute.cs.desc;

#if PAN_ARCH >= 10
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_COMPUTE, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_META);
#endif
}

void
panvk_per_arch(cmd_meta_compute_end)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_cmd_meta_compute_save_ctx *save_ctx)
{
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.compute.desc_state.push_sets[0];

#if PAN_ARCH >= 10
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_COMPUTE, cmdbuf, PANVK_INSTR_WORK_TYPE_META, NULL,
      cs_defer(dev->csf.sb.all_iters_mask, 0));
#endif

   cmdbuf->state.compute.desc_state.sets[0] = save_ctx->set0;
   if (save_ctx->push_set0.desc_count) {
      memcpy(push_set0->descs.host, save_ctx->push_set0.desc_storage,
             save_ctx->push_set0.desc_count * PANVK_DESCRIPTOR_SIZE);
      push_set0->descs.dev = save_ctx->push_set0.descs_dev_addr;
      push_set0->desc_count = save_ctx->push_set0.desc_count;
   }

   cmdbuf->state.push_constants = save_ctx->push_constants;
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);

   cmdbuf->state.compute.shader = save_ctx->cs.shader;
   cmdbuf->state.compute.cs.desc = save_ctx->cs.desc;
   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
}

void
panvk_per_arch(cmd_meta_gfx_start)(
   struct panvk_cmd_buffer *cmdbuf,
   struct panvk_cmd_meta_graphics_save_ctx *save_ctx)
{
   const struct panvk_descriptor_set *set0 =
      cmdbuf->state.gfx.desc_state.sets[0];
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.gfx.desc_state.push_sets[0];

   save_ctx->set0 = set0;
   if (push_set0 && push_set0 == set0) {
      save_ctx->push_set0.desc_count = push_set0->desc_count;
      save_ctx->push_set0.descs_dev_addr = push_set0->descs.dev;
      memcpy(save_ctx->push_set0.desc_storage, push_set0->descs.host,
             push_set0->desc_count * PANVK_DESCRIPTOR_SIZE);
   }

   save_ctx->push_constants = cmdbuf->state.push_constants;
   save_ctx->fs.shader = cmdbuf->state.gfx.fs.shader;
   save_ctx->fs.desc = cmdbuf->state.gfx.fs.desc;
   save_ctx->vs.shader = cmdbuf->state.gfx.vs.shader;
   save_ctx->vs.desc = cmdbuf->state.gfx.vs.desc;
   save_ctx->vb0 = cmdbuf->state.gfx.vb.bufs[0];

   save_ctx->dyn_state.all = cmdbuf->vk.dynamic_graphics_state;
   save_ctx->dyn_state.vi = cmdbuf->state.gfx.dynamic.vi;
   save_ctx->dyn_state.sl = cmdbuf->state.gfx.dynamic.sl;
   save_ctx->occlusion_query = cmdbuf->state.gfx.occlusion_query;

   /* Ensure occlusion queries are disabled */
   cmdbuf->state.gfx.occlusion_query.ptr = 0;
   cmdbuf->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
   gfx_state_set_dirty(cmdbuf, OQ);

   cmdbuf->state.gfx.vk_meta = true;

#if PAN_ARCH >= 10
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_VERTEX_TILER, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_META);
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_FRAGMENT, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_META);
#endif
}

void
panvk_per_arch(cmd_meta_gfx_end)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_cmd_meta_graphics_save_ctx *save_ctx)
{
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.gfx.desc_state.push_sets[0];

#if PAN_ARCH >= 10
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_VERTEX_TILER, cmdbuf, PANVK_INSTR_WORK_TYPE_META, NULL,
      cs_defer(dev->csf.sb.all_iters_mask, 0));
   panvk_per_arch(panvk_instr_end_work_async)(
      PANVK_SUBQUEUE_FRAGMENT, cmdbuf, PANVK_INSTR_WORK_TYPE_META, NULL,
      cs_defer(dev->csf.sb.all_iters_mask, 0));
#endif

   cmdbuf->state.gfx.desc_state.sets[0] = save_ctx->set0;
   if (save_ctx->push_set0.desc_count) {
      memcpy(push_set0->descs.host, save_ctx->push_set0.desc_storage,
             save_ctx->push_set0.desc_count * PANVK_DESCRIPTOR_SIZE);
      push_set0->descs.dev = save_ctx->push_set0.descs_dev_addr;
      push_set0->desc_count = save_ctx->push_set0.desc_count;
   }

   cmdbuf->state.push_constants = save_ctx->push_constants;
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);

   cmdbuf->state.gfx.fs.shader = save_ctx->fs.shader;
   cmdbuf->state.gfx.fs.desc = save_ctx->fs.desc;
   cmdbuf->state.gfx.vs.shader = save_ctx->vs.shader;
   cmdbuf->state.gfx.vs.desc = save_ctx->vs.desc;
   cmdbuf->state.gfx.vb.bufs[0] = save_ctx->vb0;

#if PAN_ARCH < 9
   cmdbuf->state.gfx.vs.attribs = 0;
   cmdbuf->state.gfx.vs.attrib_bufs = 0;
   cmdbuf->state.gfx.vs.indirect_attribs_infos = 0;
   cmdbuf->state.gfx.vs.indirect_attrib_bufs_infos = 0;
   cmdbuf->state.gfx.vs.indirect_varying_bufs_infos = 0;
   cmdbuf->state.gfx.fs.rsd = 0;
#else
   cmdbuf->state.gfx.fs.desc.res_table = 0;
   cmdbuf->state.gfx.vs.desc.res_table = 0;
#endif

   cmdbuf->vk.dynamic_graphics_state = save_ctx->dyn_state.all;
   cmdbuf->state.gfx.dynamic.vi = save_ctx->dyn_state.vi;
   cmdbuf->state.gfx.dynamic.sl = save_ctx->dyn_state.sl;
   cmdbuf->state.gfx.occlusion_query = save_ctx->occlusion_query;
   memcpy(cmdbuf->vk.dynamic_graphics_state.dirty,
          cmdbuf->vk.dynamic_graphics_state.set,
          sizeof(cmdbuf->vk.dynamic_graphics_state.set));
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, FS);
   gfx_state_set_dirty(cmdbuf, VB);
   gfx_state_set_dirty(cmdbuf, OQ);
   gfx_state_set_dirty(cmdbuf, DESC_STATE);
   gfx_state_set_dirty(cmdbuf, RENDER_STATE);

   cmdbuf->state.gfx.vk_meta = false;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBlitImage2)(VkCommandBuffer commandBuffer,
                              const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_blit_image2(&cmdbuf->vk, &dev->meta, pBlitImageInfo);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdResolveImage2)(VkCommandBuffer commandBuffer,
                                 const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_resolve_image2(&cmdbuf->vk, &dev->meta, pResolveImageInfo);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdClearAttachments)(VkCommandBuffer commandBuffer,
                                    uint32_t attachmentCount,
                                    const VkClearAttachment *pAttachments,
                                    uint32_t rectCount,
                                    const VkClearRect *pRects)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};
   struct vk_meta_rendering_info render = {
      .view_mask = cmdbuf->state.gfx.render.view_mask,
      .samples = cmdbuf->state.gfx.render.fb.nr_samples,
      .color_attachment_count = cmdbuf->state.gfx.render.fb.info.rt_count,
      .depth_attachment_format = cmdbuf->state.gfx.render.z_attachment.fmt,
      .stencil_attachment_format = cmdbuf->state.gfx.render.s_attachment.fmt,
   };
   for (uint32_t i = 0; i < render.color_attachment_count; i++) {
       render.color_attachment_formats[i] =
          cmdbuf->state.gfx.render.color_attachments.fmts[i];
       render.color_attachment_write_masks[i] =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   }

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_clear_attachments(&cmdbuf->vk, &dev->meta, &render, attachmentCount,
                             pAttachments, rectCount, pRects);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdClearDepthStencilImage)(
   VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
   const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
   const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_image, img, image);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_clear_depth_stencil_image(&cmdbuf->vk, &dev->meta, &img->vk,
                                     imageLayout, pDepthStencil, rangeCount,
                                     pRanges);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdClearColorImage)(VkCommandBuffer commandBuffer, VkImage image,
                                   VkImageLayout imageLayout,
                                   const VkClearColorValue *pColor,
                                   uint32_t rangeCount,
                                   const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_image, img, image);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_clear_color_image(&cmdbuf->vk, &dev->meta, &img->vk, imageLayout,
                             img->vk.format, pColor, rangeCount, pRanges);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyBuffer2)(VkCommandBuffer commandBuffer,
                               const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_copy_buffer(&cmdbuf->vk, &dev->meta, pCopyBufferInfo);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

static bool
lower_copy_buffer_to_image(
   VkCommandBuffer commandBuffer,
   const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(panvk_image, dst_img, pCopyBufferToImageInfo->dstImage);

   const VkImageAspectFlags zs_mask =
      (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
   /* Only required for interleaved depth stencil that are not multi-planar */
   if (vk_format_aspects(dst_img->vk.format) != zs_mask ||
       dst_img->plane_count > 1)
      return false;

   uint32_t num_depth_regions = 0, num_stencil_regions = 0;
   for (uint32_t i = 0; i < pCopyBufferToImageInfo->regionCount; i++) {
      const VkImageAspectFlags aspect_mask =
         pCopyBufferToImageInfo->pRegions[i].imageSubresource.aspectMask;
      assert((aspect_mask & ~zs_mask) == 0);
      if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
         num_depth_regions++;
      else
         num_stencil_regions++;
   }

   /* If we have both depth and stencil writes to an interleaved depth stencil
    * image, we must split the writes per aspect with a barrier between them to
    * avoid a write-after-write race. */
   const bool lowering_needed = (num_depth_regions && num_stencil_regions);
   if (!lowering_needed)
      return false;

   VkCopyBufferToImageInfo2 adjusted_info = *pCopyBufferToImageInfo;
   STACK_ARRAY(VkBufferImageCopy2, depth_regions, num_depth_regions);
   STACK_ARRAY(VkBufferImageCopy2, stencil_regions, num_stencil_regions);

   uint32_t depth_idx = 0, stencil_idx = 0;
   for (uint32_t i = 0; i < pCopyBufferToImageInfo->regionCount; i++) {
      const VkImageAspectFlags aspect_mask =
         pCopyBufferToImageInfo->pRegions[i].imageSubresource.aspectMask;

      if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
         depth_regions[depth_idx++] = pCopyBufferToImageInfo->pRegions[i];
      else
         stencil_regions[stencil_idx++] = pCopyBufferToImageInfo->pRegions[i];
   }

   adjusted_info.regionCount = num_depth_regions;
   adjusted_info.pRegions = depth_regions;
   panvk_per_arch(CmdCopyBufferToImage2)(commandBuffer, &adjusted_info);

   const VkMemoryBarrier2 mem_barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT};
   const VkDependencyInfo dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &mem_barrier,
   };
   panvk_per_arch(CmdPipelineBarrier2)(commandBuffer, &dep_info);

   adjusted_info.regionCount = num_stencil_regions;
   adjusted_info.pRegions = stencil_regions;
   panvk_per_arch(CmdCopyBufferToImage2)(commandBuffer, &adjusted_info);

   STACK_ARRAY_FINISH(depth_regions);
   STACK_ARRAY_FINISH(stencil_regions);

   return true;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyBufferToImage2)(
   VkCommandBuffer commandBuffer,
   const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, img, pCopyBufferToImageInfo->dstImage);

   /* Early out if this operation was lowered. */
   if (lower_copy_buffer_to_image(commandBuffer, pCopyBufferToImageInfo))
      return;

   const bool use_gfx_pipeline = copy_to_image_use_gfx_pipeline(img);
   struct vk_meta_copy_image_properties img_props =
      panvk_meta_copy_get_image_properties(img, use_gfx_pipeline, true);

   if (use_gfx_pipeline) {
      struct panvk_cmd_meta_graphics_save_ctx save = {0};

      panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
      vk_meta_copy_buffer_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyBufferToImageInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_GRAPHICS);
      panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
   } else {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
      vk_meta_copy_buffer_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyBufferToImageInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_COMPUTE);
      panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyImageToBuffer2)(
   VkCommandBuffer commandBuffer,
   const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, img, pCopyImageToBufferInfo->srcImage);
   struct vk_meta_copy_image_properties img_props =
      panvk_meta_copy_get_image_properties(img, false, false);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_copy_image_to_buffer(&cmdbuf->vk, &dev->meta, pCopyImageToBufferInfo,
                                &img_props);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdFillBuffer)(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                              VkDeviceSize dstOffset, VkDeviceSize fillSize,
                              uint32_t data)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_fill_buffer(&cmdbuf->vk, &dev->meta, dstBuffer, dstOffset, fillSize,
                       data);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdUpdateBuffer)(VkCommandBuffer commandBuffer,
                                VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                VkDeviceSize dataSize, const void *pData)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_update_buffer(&cmdbuf->vk, &dev->meta, dstBuffer, dstOffset,
                         dataSize, pData);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

static bool
lower_copy_image(VkCommandBuffer commandBuffer,
                 const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(panvk_image, dst_img, pCopyImageInfo->dstImage);

   const VkImageAspectFlags zs_mask =
      (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
   /* Only required for interleaved depth stencil that are not multi-planar */
   if (vk_format_aspects(dst_img->vk.format) != zs_mask ||
       dst_img->plane_count > 1)
      return false;

   uint32_t num_depth_regions = 0, num_stencil_regions = 0;
   for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++) {
      const VkImageAspectFlags aspect_mask =
         pCopyImageInfo->pRegions[i].dstSubresource.aspectMask;
      assert((aspect_mask & ~zs_mask) == 0);
      if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
         num_depth_regions++;
      else
         num_stencil_regions++;
   }

   /* If we have both depth and stencil writes to an interleaved depth stencil
    * image, we must split the writes per aspect with a barrier between them to
    * avoid a write-after-write race. */
   const bool lowering_needed = (num_depth_regions && num_stencil_regions);
   if (!lowering_needed)
      return false;

   VkCopyImageInfo2 adjusted_info = *pCopyImageInfo;
   STACK_ARRAY(VkImageCopy2, depth_regions, num_depth_regions);
   STACK_ARRAY(VkImageCopy2, stencil_regions, num_stencil_regions);

   uint32_t depth_idx = 0, stencil_idx = 0;
   for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++) {
      const VkImageAspectFlags aspect_mask =
         pCopyImageInfo->pRegions[i].dstSubresource.aspectMask;

      if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
         depth_regions[depth_idx++] = pCopyImageInfo->pRegions[i];
      else
         stencil_regions[stencil_idx++] = pCopyImageInfo->pRegions[i];
   }

   adjusted_info.regionCount = num_depth_regions;
   adjusted_info.pRegions = depth_regions;
   panvk_per_arch(CmdCopyImage2)(commandBuffer, &adjusted_info);

   const VkMemoryBarrier2 mem_barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT};
   const VkDependencyInfo dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &mem_barrier,
   };
   panvk_per_arch(CmdPipelineBarrier2)(commandBuffer, &dep_info);

   adjusted_info.regionCount = num_stencil_regions;
   adjusted_info.pRegions = stencil_regions;
   panvk_per_arch(CmdCopyImage2)(commandBuffer, &adjusted_info);

   STACK_ARRAY_FINISH(depth_regions);
   STACK_ARRAY_FINISH(stencil_regions);

   return true;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyImage2)(VkCommandBuffer commandBuffer,
                              const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, src_img, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(panvk_image, dst_img, pCopyImageInfo->dstImage);

   /* Early out if this operation was lowered. */
   if (lower_copy_image(commandBuffer, pCopyImageInfo))
      return;

   const bool use_gfx_pipeline = copy_to_image_use_gfx_pipeline(dst_img);
   struct vk_meta_copy_image_properties dst_img_props =
      panvk_meta_copy_get_image_properties(dst_img, use_gfx_pipeline, true);
   struct vk_meta_copy_image_properties src_img_props =
      panvk_meta_copy_get_image_properties(src_img, use_gfx_pipeline, false);

   if (use_gfx_pipeline) {
      struct panvk_cmd_meta_graphics_save_ctx save = {0};

      panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
      vk_meta_copy_image(&cmdbuf->vk, &dev->meta, pCopyImageInfo,
                         &src_img_props, &dst_img_props,
                         VK_PIPELINE_BIND_POINT_GRAPHICS);
      panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
   } else {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
      vk_meta_copy_image(&cmdbuf->vk, &dev->meta, pCopyImageInfo,
                         &src_img_props, &dst_img_props,
                         VK_PIPELINE_BIND_POINT_COMPUTE);
      panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
   }
}

static bool
panvk_image_has_afbc(struct panvk_image *img, VkImageSubresourceRange range)
{
   VkImageAspectFlags aspect_mask =
      vk_image_expand_aspect_mask(&img->vk, range.aspectMask);
   u_foreach_bit(aspect, aspect_mask) {
      unsigned plane_index = panvk_plane_index(img, 1u << aspect);
      struct panvk_image_plane *plane = &img->planes[plane_index];

      if (drm_is_afbc(plane->image.props.modifier))
         return true;
   }

   return false;
}

static void
cmd_clear_afbc_metadata(VkCommandBuffer _cmdbuf,
                        const VkImageMemoryBarrier2 *barrier)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, _cmdbuf);
   struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, img, barrier->image);
   VkImageSubresourceRange range = barrier->subresourceRange;
   VkImageAspectFlags aspect_mask =
      vk_image_expand_aspect_mask(&img->vk, range.aspectMask);
   uint32_t level_count = vk_image_subresource_level_count(&img->vk, &range);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);

   u_foreach_bit(aspect, aspect_mask) {
      unsigned plane_index = panvk_plane_index(img, 1u << aspect);
      struct panvk_image_plane *plane = &img->planes[plane_index];

      if (!drm_is_afbc(plane->image.props.modifier))
         continue;

      for (uint32_t level = range.baseMipLevel;
            level < range.baseMipLevel + level_count;
            level++) {
         const struct pan_image_slice_layout *slayout =
            &plane->plane.layout.slices[level];

         uint32_t layers_or_slices;
         if (img->vk.image_type == VK_IMAGE_TYPE_2D) {
            layers_or_slices =
               vk_image_subresource_layer_count(&img->vk, &range);
         } else if (img->vk.image_type == VK_IMAGE_TYPE_3D) {
            layers_or_slices =
               vk_image_subresource_slice_count(&dev->vk,
                  &img->vk,
                  &(VkImageSubresourceLayers) {
                     .mipLevel = level,
                     .baseArrayLayer = range.baseArrayLayer,
                     .layerCount = range.layerCount,
                  });
         } else {
            UNREACHABLE("Unsupported image type");
         }

         uint32_t layer_or_slice_stride = slayout->afbc.surface_stride_B;

         uint32_t ptr = plane->plane.base + slayout->offset_B +
            range.baseArrayLayer * layer_or_slice_stride;

         struct panlib_clear_afbc_metadata_args args = {
            .p = ptr,
            .layer_or_slice_stride = layer_or_slice_stride,
         };
         panlib_clear_afbc_metadata_struct(
            &precomp_ctx,
            panlib_3d(slayout->afbc.header.surface_size_B / 16,
                      layers_or_slices, 1),
            PAN_ARCH >= 10 ? PANLIB_BARRIER_CSF_SYNC :
                             PANLIB_BARRIER_JM_BARRIER,
            args);
      }
   }

   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

static bool
panvk_acquire_unmodified(const VkImageMemoryBarrier2 *barrier)
{
   if (barrier->srcQueueFamilyIndex != VK_QUEUE_FAMILY_EXTERNAL &&
       barrier->srcQueueFamilyIndex != VK_QUEUE_FAMILY_FOREIGN_EXT)
      return false;

   const VkExternalMemoryAcquireUnmodifiedEXT *acquire_unmodified =
      vk_find_struct_const(barrier->pNext,
                           EXTERNAL_MEMORY_ACQUIRE_UNMODIFIED_EXT);
   return acquire_unmodified &&
          acquire_unmodified->acquireUnmodifiedMemory == VK_TRUE;
}

/* TODO: pass less data than what's in a VkImageMemoryBarrier2 */

struct panvk_image_layout_transition_handler {
   void (*cmd)(VkCommandBuffer cmdbuf, const VkImageMemoryBarrier2 *barrier);
   VkPipelineStageFlags2 stages;
   VkAccessFlags2 access;
};

static struct panvk_image_layout_transition_handler
panvk_get_image_layout_transition_handler(const VkImageMemoryBarrier2 *barrier)
{
   VK_FROM_HANDLE(panvk_image, img, barrier->image);

   if (barrier->oldLayout == barrier->newLayout ||
       panvk_acquire_unmodified(barrier))
      return (struct panvk_image_layout_transition_handler){0};

   if (barrier->oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
       panvk_image_has_afbc(img, barrier->subresourceRange)) {
      return (struct panvk_image_layout_transition_handler){
         .cmd = cmd_clear_afbc_metadata,
         .stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
         .access = VK_ACCESS_2_MEMORY_WRITE_BIT,
      };
   }

   return (struct panvk_image_layout_transition_handler){0};
}

void
panvk_per_arch(transition_image_layout_sync_scope)(
   const VkImageMemoryBarrier2 *barrier,
   VkPipelineStageFlags2 *out_stages, VkAccessFlags2 *out_access)
{
   struct panvk_image_layout_transition_handler handler =
      panvk_get_image_layout_transition_handler(barrier);

   *out_stages = handler.stages;
   *out_access = handler.access;
}

void
panvk_per_arch(cmd_transition_image_layout)(
   VkCommandBuffer cmdbuf, const VkImageMemoryBarrier2 *barrier)
{
   struct panvk_image_layout_transition_handler handler =
      panvk_get_image_layout_transition_handler(barrier);

   if (handler.cmd)
      handler.cmd(cmdbuf, barrier);
}
