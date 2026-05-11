/*
 * Copyright © 2021 Collabora Ltd.
 * Copyright © 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_buffer.h"
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

static void
meta_compute_start(struct panvk_cmd_buffer *cmdbuf,
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
   save_ctx->cond_render_enabled = cmdbuf->state.cond_render.enabled;
   save_ctx->cond_render_inherited = cmdbuf->state.cond_render.inherited;
   cmdbuf->state.cond_render.enabled = false;
   cmdbuf->state.cond_render.inherited = false;
#endif

#if PAN_ARCH >= 10
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_COMPUTE, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_META);
#endif
}

static void
meta_compute_end(struct panvk_cmd_buffer *cmdbuf,
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
#if PAN_ARCH >= 10
   cmdbuf->state.cond_render.enabled = save_ctx->cond_render_enabled;
   cmdbuf->state.cond_render.inherited = save_ctx->cond_render_inherited;
#endif
   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
}

static void
meta_gfx_start(struct panvk_cmd_buffer *cmdbuf,
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
   save_ctx->cond_render_enabled = cmdbuf->state.cond_render.enabled;
   save_ctx->cond_render_inherited = cmdbuf->state.cond_render.inherited;
   cmdbuf->state.cond_render.enabled = false;
   cmdbuf->state.cond_render.inherited = false;
#endif

#if PAN_ARCH >= 10
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_VERTEX_TILER, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_META);
   panvk_per_arch(panvk_instr_begin_work)(PANVK_SUBQUEUE_FRAGMENT, cmdbuf,
                                          PANVK_INSTR_WORK_TYPE_META);
#endif
}

static void
meta_gfx_end(struct panvk_cmd_buffer *cmdbuf,
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
#if PAN_ARCH >= 10
   cmdbuf->state.cond_render.enabled = save_ctx->cond_render_enabled;
   cmdbuf->state.cond_render.inherited = save_ctx->cond_render_inherited;
#endif
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBlitImage2)(VkCommandBuffer commandBuffer,
                              const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   meta_gfx_start(cmdbuf, &save);
   vk_meta_blit_image2(&cmdbuf->vk, &dev->meta, pBlitImageInfo);
   meta_gfx_end(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdResolveImage2)(VkCommandBuffer commandBuffer,
                                 const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   meta_gfx_start(cmdbuf, &save);
   vk_meta_resolve_image2(&cmdbuf->vk, &dev->meta, pResolveImageInfo);
   meta_gfx_end(cmdbuf, &save);
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
      .depth_attachment_format = cmdbuf->state.gfx.render.z_attachment.fmt,
      .stencil_attachment_format = cmdbuf->state.gfx.render.s_attachment.fmt,
   };
   for (uint32_t i = 0; i < MAX_RTS; i++) {
      if (!(cmdbuf->state.gfx.render.bound_attachments &
            MESA_VK_RP_ATTACHMENT_COLOR_BIT(i)))
         continue;

      render.color_attachment_count =
         MAX2(render.color_attachment_count, i + 1);
      render.color_attachment_formats[i] =
         cmdbuf->state.gfx.render.color_attachments.fmts[i];
      render.color_attachment_write_masks[i] =
         VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   }

   meta_gfx_start(cmdbuf, &save);
#if PAN_ARCH >= 10
   /* CmdClearAttachments IS affected by conditional rendering, so restore
    * the state that meta_gfx_start disabled.
    */
   cmdbuf->state.cond_render.enabled = save.cond_render_enabled;
   cmdbuf->state.cond_render.inherited = save.cond_render_inherited;
#endif
   vk_meta_clear_attachments(&cmdbuf->vk, &dev->meta, &render, attachmentCount,
                             pAttachments, rectCount, pRects);
   meta_gfx_end(cmdbuf, &save);
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

   meta_gfx_start(cmdbuf, &save);
   vk_meta_clear_depth_stencil_image(&cmdbuf->vk, &dev->meta, &img->vk,
                                     imageLayout, pDepthStencil, rangeCount,
                                     pRanges);
   meta_gfx_end(cmdbuf, &save);
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

   meta_gfx_start(cmdbuf, &save);
   vk_meta_clear_color_image(&cmdbuf->vk, &dev->meta, &img->vk, imageLayout,
                             img->vk.format, pColor, rangeCount, pRanges);
   meta_gfx_end(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyBuffer2)(VkCommandBuffer commandBuffer,
                               const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   meta_compute_start(cmdbuf, &save);
   vk_meta_copy_buffer(&cmdbuf->vk, &dev->meta, pCopyBufferInfo);
   meta_compute_end(cmdbuf, &save);
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

      meta_gfx_start(cmdbuf, &save);
      vk_meta_copy_buffer_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyBufferToImageInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_GRAPHICS);
      meta_gfx_end(cmdbuf, &save);
   } else {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      meta_compute_start(cmdbuf, &save);
      vk_meta_copy_buffer_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyBufferToImageInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_COMPUTE);
      meta_compute_end(cmdbuf, &save);
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

   meta_compute_start(cmdbuf, &save);
   vk_meta_copy_image_to_buffer(&cmdbuf->vk, &dev->meta, pCopyImageToBufferInfo,
                                &img_props);
   meta_compute_end(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdFillBuffer)(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                              VkDeviceSize dstOffset, VkDeviceSize fillSize,
                              uint32_t data)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buffer, dstBuffer);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);

   uint64_t addr = panvk_buffer_gpu_ptr(buffer, dstOffset);
   uint64_t range = panvk_buffer_range(buffer, dstOffset, fillSize) & ~3ULL;
   if (!range)
      return;

   const uint32_t max_wg = phys_dev->vk.properties.maxComputeWorkGroupCount[0];
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   const bool uint4_path =
      util_is_aligned(addr, 16) && util_is_aligned(range, 16);
   const uint32_t elem_size = uint4_path ? 16 : 4;
   const uint32_t wg_bytes = 32 * elem_size;

   while (range >= wg_bytes) {
      const uint32_t wgs = MIN2(range / wg_bytes, max_wg);
      const uint64_t bulk = (uint64_t)wgs * wg_bytes;

      if (uint4_path) {
         panlib_fill_uint4(&ctx, panlib_1d(wgs), PANLIB_BARRIER_NONE, addr,
                           data, data, data, data);
      } else {
         panlib_fill(&ctx, panlib_1d(wgs), PANLIB_BARRIER_NONE, addr, data);
      }

      addr += bulk;
      range -= bulk;
   }

   if (range) {
      const uint32_t tail = range / elem_size;

      if (uint4_path) {
         panlib_fill_uint4_scalar(&ctx, panlib_1d(tail), PANLIB_BARRIER_NONE,
                                  addr, data, data, data, data);
      } else {
         panlib_fill_scalar(&ctx, panlib_1d(tail), PANLIB_BARRIER_NONE, addr,
                            data);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdUpdateBuffer)(VkCommandBuffer commandBuffer,
                                VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                VkDeviceSize dataSize, const void *pData)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   meta_compute_start(cmdbuf, &save);
   vk_meta_update_buffer(&cmdbuf->vk, &dev->meta, dstBuffer, dstOffset,
                         dataSize, pData);
   meta_compute_end(cmdbuf, &save);
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

      meta_gfx_start(cmdbuf, &save);
      vk_meta_copy_image(&cmdbuf->vk, &dev->meta, pCopyImageInfo,
                         &src_img_props, &dst_img_props,
                         VK_PIPELINE_BIND_POINT_GRAPHICS);
      meta_gfx_end(cmdbuf, &save);
   } else {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      meta_compute_start(cmdbuf, &save);
      vk_meta_copy_image(&cmdbuf->vk, &dev->meta, pCopyImageInfo,
                         &src_img_props, &dst_img_props,
                         VK_PIPELINE_BIND_POINT_COMPUTE);
      meta_compute_end(cmdbuf, &save);
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
   if (barrier->oldLayout == barrier->newLayout ||
       panvk_acquire_unmodified(barrier))
      return (struct panvk_image_layout_transition_handler){0};

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

void
panvk_per_arch(cmd_meta_resolve_attachments)(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_layout *fb = &cmdbuf->state.gfx.render.fb.layout;
   bool needs_resolve = false;

   unsigned bound_atts = cmdbuf->state.gfx.render.bound_attachments;
   unsigned color_att_count =
      util_last_bit(bound_atts & MESA_VK_RP_ATTACHMENT_ANY_COLOR_BITS);
   VkRenderingAttachmentInfo color_atts[MAX_RTS];
   for (uint32_t i = 0; i < color_att_count; i++) {

      const struct panvk_resolve_attachment *resolve_info =
         &cmdbuf->state.gfx.render.color_attachments.resolve[i];
      struct panvk_image_view *src_iview =
         cmdbuf->state.gfx.render.color_attachments.iviews[i];

      color_atts[i] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = panvk_image_view_to_handle(src_iview),
         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
         .resolveMode = resolve_info->mode,
         .resolveImageView =
            panvk_image_view_to_handle(resolve_info->dst_iview),
         .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
      };

      if (resolve_info->mode != VK_RESOLVE_MODE_NONE)
         needs_resolve = true;

      if (resolve_info->mode != VK_RESOLVE_MODE_NONE) {
         assert(src_iview->pview.nr_samples > 1);
         assert(resolve_info->dst_iview->pview.nr_samples == 1);
      }
   }

   const struct panvk_resolve_attachment *resolve_info =
      &cmdbuf->state.gfx.render.z_attachment.resolve;
   struct panvk_image_view *src_iview =
      cmdbuf->state.gfx.render.z_attachment.iview;
   VkRenderingAttachmentInfo z_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = panvk_image_view_to_handle(src_iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = resolve_info->mode,
      .resolveImageView = panvk_image_view_to_handle(resolve_info->dst_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   if (resolve_info->mode != VK_RESOLVE_MODE_NONE)
      needs_resolve = true;

   resolve_info = &cmdbuf->state.gfx.render.s_attachment.resolve;
   src_iview = cmdbuf->state.gfx.render.s_attachment.iview;

   VkRenderingAttachmentInfo s_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = panvk_image_view_to_handle(src_iview),
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .resolveMode = resolve_info->mode,
      .resolveImageView = panvk_image_view_to_handle(resolve_info->dst_iview),
      .resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   if (resolve_info->mode != VK_RESOLVE_MODE_NONE)
      needs_resolve = true;

   if (!needs_resolve)
      return;

#if PAN_ARCH >= 10
   /* insert a barrier for resolve */
   const VkMemoryBarrier2 mem_barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
   };
   const VkDependencyInfo dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &mem_barrier,
   };
   panvk_per_arch(CmdPipelineBarrier2)(panvk_cmd_buffer_to_handle(cmdbuf),
                                       &dep_info);
#endif

   const VkRenderingInfo render_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea =
         {
            .offset.x = fb->render_area_px.min_x,
            .offset.y = fb->render_area_px.min_y,
            .extent.width =
               fb->render_area_px.max_x - fb->render_area_px.min_x + 1,
            .extent.height =
               fb->render_area_px.max_y - fb->render_area_px.min_y + 1,
         },
      .layerCount = cmdbuf->state.gfx.render.layer_count,
      .viewMask = cmdbuf->state.gfx.render.view_mask,
      .colorAttachmentCount = color_att_count,
      .pColorAttachments = color_atts,
      .pDepthAttachment = &z_att,
      .pStencilAttachment = &s_att,
   };

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   meta_gfx_start(cmdbuf, &save);
   vk_meta_resolve_rendering(&cmdbuf->vk, &dev->meta, &render_info);
   meta_gfx_end(cmdbuf, &save);
}
