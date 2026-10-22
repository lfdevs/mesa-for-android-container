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

#include "bc/panvk_bc_bc6_fs_spv.h"
#include "bc/panvk_bc_bc6_spv.h"
#include "bc/panvk_bc_bc7_fs_spv.h"
#include "bc/panvk_bc_bc7_spv.h"
#include "bc/panvk_bc_rgtc_fs_spv.h"
#include "bc/panvk_bc_rgtc_spv.h"
#include "bc/panvk_bc_s3tc_fs_spv.h"
#include "bc/panvk_bc_s3tc_spv.h"

#include "panvk_cmd_precomp.h"
#include "libpan.h"
#include "libpan_copy.h"
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

#if PAN_ARCH >= 10
/* Compute copies bypass framebuffer CRC tracking, so writes touching the
 * mip-0 CRC region must invalidate its state explicitly.
 */
static bool
copy_buffer_to_image_touches_crc(const struct panvk_image *dst_img,
                                 const VkCopyDeviceMemoryImageInfoKHR *info)
{
   if (!panvk_image_plane_crc_header_addr(dst_img))
      return false;

   for (uint32_t i = 0; i < info->regionCount; i++) {
      const VkImageSubresourceLayers *dst = &info->pRegions[i].imageSubresource;

      if (dst->mipLevel == 0 &&
          panvk_plane_index(dst_img, dst->aspectMask) == 0)
         return true;
   }

   return false;
}

static bool
copy_image_touches_crc(const struct panvk_image *dst_img,
                       const VkCopyImageInfo2 *info)
{
   if (!panvk_image_plane_crc_header_addr(dst_img))
      return false;

   for (uint32_t i = 0; i < info->regionCount; i++) {
      const VkImageSubresourceLayers *dst = &info->pRegions[i].dstSubresource;

      if (dst->mipLevel == 0 &&
          panvk_plane_index(dst_img, dst->aspectMask) == 0)
         return true;
   }

   return false;
}
#endif /* PAN_ARCH >= 10 */

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
      save_ctx->push_set0.dirty =
         BITSET_TEST(cmdbuf->state.compute.desc_state.dirty_push_sets, 0);
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

      if (save_ctx->push_set0.dirty)
         BITSET_SET(cmdbuf->state.compute.desc_state.dirty_push_sets, 0);
      else
         BITSET_CLEAR(cmdbuf->state.compute.desc_state.dirty_push_sets, 0);
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
      save_ctx->push_set0.dirty =
         BITSET_TEST(cmdbuf->state.gfx.desc_state.dirty_push_sets, 0);
      memcpy(save_ctx->push_set0.desc_storage, push_set0->descs.host,
             push_set0->desc_count * PANVK_DESCRIPTOR_SIZE);
   }

   save_ctx->push_constants = cmdbuf->state.push_constants;
   save_ctx->fs.shader = cmdbuf->state.gfx.fs.shader;
   save_ctx->fs.desc = cmdbuf->state.gfx.fs.desc;
   save_ctx->vs.shader = cmdbuf->state.gfx.vs.shader;
   save_ctx->vs.desc = cmdbuf->state.gfx.vs.desc;
   save_ctx->vb0 = cmdbuf->state.gfx.vb.bufs[0];
#if PAN_ARCH >= 10
   save_ctx->gs = cmdbuf->state.gfx.gs.shader;
   save_ctx->tcs = cmdbuf->state.gfx.tcs.shader;
   save_ctx->tes = cmdbuf->state.gfx.tes.shader;
   cmdbuf->state.gfx.gs.shader = NULL;
   cmdbuf->state.gfx.tcs.shader = NULL;
   cmdbuf->state.gfx.tes.shader = NULL;
#endif

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

      if (save_ctx->push_set0.dirty)
         BITSET_SET(cmdbuf->state.gfx.desc_state.dirty_push_sets, 0);
      else
         BITSET_CLEAR(cmdbuf->state.gfx.desc_state.dirty_push_sets, 0);
   }

   cmdbuf->state.push_constants = save_ctx->push_constants;
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);

   cmdbuf->state.gfx.fs.shader = save_ctx->fs.shader;
   cmdbuf->state.gfx.fs.desc = save_ctx->fs.desc;
   cmdbuf->state.gfx.vs.shader = save_ctx->vs.shader;
   cmdbuf->state.gfx.vs.desc = save_ctx->vs.desc;
   cmdbuf->state.gfx.vb.bufs[0] = save_ctx->vb0;
#if PAN_ARCH >= 10
   cmdbuf->state.gfx.gs.shader = save_ctx->gs;
   cmdbuf->state.gfx.tcs.shader = save_ctx->tcs;
   cmdbuf->state.gfx.tes.shader = save_ctx->tes;
#endif

#if PAN_ARCH < 9
   cmdbuf->state.gfx.vs.attribs = 0;
   cmdbuf->state.gfx.vs.attrib_bufs = 0;
   cmdbuf->state.gfx.vs.indirect_attribs_infos = 0;
   cmdbuf->state.gfx.vs.indirect_attrib_bufs_infos = 0;
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

   /* Mali cannot render to R64; alias as RG32UI for vk_meta. */
   VkFormat view_format = img->vk.format;
   if (img->vk.format == VK_FORMAT_R64_UINT ||
       img->vk.format == VK_FORMAT_R64_SINT)
      view_format = VK_FORMAT_R32G32_UINT;

   meta_gfx_start(cmdbuf, &save);
   vk_meta_clear_color_image(&cmdbuf->vk, &dev->meta, &img->vk, imageLayout,
                             view_format, pColor, rangeCount, pRanges);
   meta_gfx_end(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyMemoryKHR)(VkCommandBuffer commandBuffer,
                                 const VkCopyDeviceMemoryInfoKHR* pCopyMemoryInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   meta_compute_start(cmdbuf, &save);
   vk_meta_copy_memory(&cmdbuf->vk, &dev->meta, pCopyMemoryInfo);
   meta_compute_end(cmdbuf, &save);
}

static bool
lower_copy_memory_to_image(
   VkCommandBuffer commandBuffer,
   const VkCopyDeviceMemoryImageInfoKHR* pCopyMemoryInfo)
{
   VK_FROM_HANDLE(panvk_image, dst_img, pCopyMemoryInfo->image);

   const VkImageAspectFlags zs_mask =
      (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
   /* Only required for interleaved depth stencil that are not multi-planar */
   if (vk_format_aspects(dst_img->vk.format) != zs_mask ||
       dst_img->plane_count > 1)
      return false;

   uint32_t num_depth_regions = 0, num_stencil_regions = 0;
   for (uint32_t i = 0; i < pCopyMemoryInfo->regionCount; i++) {
      const VkImageAspectFlags aspect_mask =
         pCopyMemoryInfo->pRegions[i].imageSubresource.aspectMask;
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

   VkCopyDeviceMemoryImageInfoKHR adjusted_info = *pCopyMemoryInfo;
   STACK_ARRAY(VkDeviceMemoryImageCopyKHR, depth_regions, num_depth_regions);
   STACK_ARRAY(VkDeviceMemoryImageCopyKHR, stencil_regions, num_stencil_regions);

   uint32_t depth_idx = 0, stencil_idx = 0;
   for (uint32_t i = 0; i < pCopyMemoryInfo->regionCount; i++) {
      const VkImageAspectFlags aspect_mask =
         pCopyMemoryInfo->pRegions[i].imageSubresource.aspectMask;

      if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
         depth_regions[depth_idx++] = pCopyMemoryInfo->pRegions[i];
      else
         stencil_regions[stencil_idx++] = pCopyMemoryInfo->pRegions[i];
   }

   adjusted_info.regionCount = num_depth_regions;
   adjusted_info.pRegions = depth_regions;
   panvk_per_arch(CmdCopyMemoryToImageKHR)(commandBuffer, &adjusted_info);

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
   panvk_per_arch(CmdCopyMemoryToImageKHR)(commandBuffer, &adjusted_info);

   STACK_ARRAY_FINISH(depth_regions);
   STACK_ARRAY_FINISH(stencil_regions);

   return true;
}

static void copy_memory_to_image_raw(
   VkCommandBuffer commandBuffer,
   const VkCopyDeviceMemoryImageInfoKHR *pCopyMemoryInfo);
static void copy_image_raw(VkCommandBuffer commandBuffer,
                           const VkCopyImageInfo2 *pCopyImageInfo);

struct panvk_bc_decode_push {
   uint32_t src[2];
   uint32_t dst[2];
   uint32_t src_row_B;
   uint32_t src_slice_B;
   uint32_t dst_row_B;
   uint32_t dst_slice_B;
   int32_t width;
   int32_t height;
   int32_t depth;
   int32_t format;
   int32_t x0;
   int32_t y0;
};

struct panvk_bc_decode_key {
   enum panvk_meta_object_key_type type;
   uint32_t shader;
};

struct panvk_bc_decode_gfx_key {
   enum panvk_meta_object_key_type type;
   uint32_t shader;
   VkFormat format;
};

/* Where block (0, 0) of a decode region lives, and its pitches. */
struct bc_decode_src {
   uint64_t addr;
   uint32_t row_B;
   uint32_t slice_B;
};

static uint32_t
bc_decode_shader_index(VkFormat format)
{
   if (format <= VK_FORMAT_BC3_SRGB_BLOCK)
      return 0;
   if (format <= VK_FORMAT_BC5_SNORM_BLOCK)
      return 1;
   if (format <= VK_FORMAT_BC6H_SFLOAT_BLOCK)
      return 2;
   return 3;
}

static VkResult
get_bc_decode_pipeline(struct panvk_device *dev, uint32_t shader,
                       VkPipelineLayout *layout_out, VkPipeline *pipeline_out)
{
   static const struct {
      const uint32_t *code;
      size_t size;
   } spv[] = {
      {panvk_bc_s3tc_spv, sizeof(panvk_bc_s3tc_spv)},
      {panvk_bc_rgtc_spv, sizeof(panvk_bc_rgtc_spv)},
      {panvk_bc_bc6_spv, sizeof(panvk_bc_bc6_spv)},
      {panvk_bc_bc7_spv, sizeof(panvk_bc_bc7_spv)},
   };
   const enum panvk_meta_object_key_type layout_key =
      PANVK_META_OBJECT_KEY_BC_DECODE_LAYOUT;
   const VkPushConstantRange push_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(struct panvk_bc_decode_push),
   };
   VkResult result =
      vk_meta_get_pipeline_layout(&dev->vk, &dev->meta, NULL, &push_range,
                                  &layout_key, sizeof(layout_key), layout_out);
   if (result != VK_SUCCESS)
      return result;

   const struct panvk_bc_decode_key key = {
      .type = PANVK_META_OBJECT_KEY_BC_DECODE_SHADER,
      .shader = shader,
   };
   VkPipeline cached = vk_meta_lookup_pipeline(&dev->meta, &key, sizeof(key));
   if (cached != VK_NULL_HANDLE) {
      *pipeline_out = cached;
      return VK_SUCCESS;
   }

   const VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spv[shader].size,
      .pCode = spv[shader].code,
   };
   const VkComputePipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = &module_info,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .pName = "main",
      },
      .layout = *layout_out,
   };

   return vk_meta_create_compute_pipeline(&dev->vk, &dev->meta, &info, &key,
                                          sizeof(key), pipeline_out);
}

static VkResult
get_bc_decode_gfx_pipeline(struct panvk_device *dev, uint32_t shader,
                           VkFormat format, VkPipelineLayout *layout_out,
                           VkPipeline *pipeline_out)
{
   static const struct {
      const uint32_t *code;
      size_t size;
   } spv[] = {
      {panvk_bc_s3tc_fs_spv, sizeof(panvk_bc_s3tc_fs_spv)},
      {panvk_bc_rgtc_fs_spv, sizeof(panvk_bc_rgtc_fs_spv)},
      {panvk_bc_bc6_fs_spv, sizeof(panvk_bc_bc6_fs_spv)},
      {panvk_bc_bc7_fs_spv, sizeof(panvk_bc_bc7_fs_spv)},
   };
   const enum panvk_meta_object_key_type layout_key =
      PANVK_META_OBJECT_KEY_BC_DECODE_GFX_LAYOUT;
   const VkPushConstantRange push_range = {
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .offset = 0,
      .size = sizeof(struct panvk_bc_decode_push),
   };
   VkResult result =
      vk_meta_get_pipeline_layout(&dev->vk, &dev->meta, NULL, &push_range,
                                  &layout_key, sizeof(layout_key), layout_out);
   if (result != VK_SUCCESS)
      return result;

   const struct panvk_bc_decode_gfx_key key = {
      .type = PANVK_META_OBJECT_KEY_BC_DECODE_GFX_SHADER,
      .shader = shader,
      .format = format,
   };
   VkPipeline cached = vk_meta_lookup_pipeline(&dev->meta, &key, sizeof(key));
   if (cached != VK_NULL_HANDLE) {
      *pipeline_out = cached;
      return VK_SUCCESS;
   }

   const VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spv[shader].size,
      .pCode = spv[shader].code,
   };
   const VkPipelineShaderStageCreateInfo stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = &module_info,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pName = "main",
   };
   const VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 1,
      .pStages = &stage,
      .layout = *layout_out,
   };
   const struct vk_meta_rendering_info render = {
      .samples = 1,
      .color_attachment_count = 1,
      .color_attachment_formats = {format},
      .color_attachment_write_masks = {VK_COLOR_COMPONENT_R_BIT |
                                       VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT |
                                       VK_COLOR_COMPONENT_A_BIT},
   };

   return vk_meta_create_graphics_pipeline(&dev->vk, &dev->meta, &info,
                                           &render, &key, sizeof(key),
                                           pipeline_out);
}

/* Fragment decode of one region into bc_shadow: a layered draw over the
 * region, so the tile writeback produces whatever layout (AFBC,
 * U-interleaved) and format the shadow image got. Must run between
 * meta_gfx_start() and meta_gfx_end(). */
static void
bc_decode_region_gfx(struct panvk_cmd_buffer *cmdbuf, struct panvk_image *img,
                     const VkImageSubresourceLayers *subres, VkOffset3D offset,
                     VkExtent3D extent, const struct bc_decode_src *src)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_device_dispatch_table *disp = &dev->vk.dispatch_table;
   VkCommandBuffer cmd = panvk_cmd_buffer_to_handle(cmdbuf);
   const uint32_t level = subres->mipLevel;
   const VkExtent3D mip = vk_image_mip_level_extent(&img->vk, level);
   const uint32_t x0 = offset.x & ~3, y0 = offset.y & ~3;
   const uint32_t w = MIN2(extent.width + (offset.x - x0), mip.width - x0);
   const uint32_t h = MIN2(extent.height + (offset.y - y0), mip.height - y0);
   const uint32_t layers = vk_image_subresource_layer_count(&img->vk, subres);

   if (!w || !h || !layers)
      return;

   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result = get_bc_decode_gfx_pipeline(
      dev, bc_decode_shader_index(img->vk.format), img->bc_format, &layout,
      &pipeline);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, result);
      return;
   }

   const VkImageViewUsageCreateInfo view_usage = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
   };
   const VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = &view_usage,
      .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
      .image = img->bc_shadow,
      .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
      .format = img->bc_format,
      .subresourceRange = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .baseMipLevel = level,
         .levelCount = 1,
         .baseArrayLayer = subres->baseArrayLayer,
         .layerCount = layers,
      },
   };
   VkImageView view;
   result = vk_meta_create_image_view(&cmdbuf->vk, &dev->meta, &view_info,
                                      &view);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, result);
      return;
   }

   /* A whole-level decode rewrites every texel: skip the tile load. */
   const bool whole = x0 == 0 && y0 == 0 && w == mip.width && h == mip.height;
   const VkRenderingAttachmentInfo att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = view,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = whole ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                      : VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };
   const VkRenderingInfo render = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.offset = {x0, y0}, .extent = {w, h}},
      .layerCount = layers,
      .colorAttachmentCount = 1,
      .pColorAttachments = &att,
   };
   const struct panvk_bc_decode_push push = {
      .src = {(uint32_t)src->addr, (uint32_t)(src->addr >> 32)},
      .src_row_B = src->row_B,
      .src_slice_B = src->slice_B,
      .width = w,
      .height = h,
      .depth = layers,
      .format = img->vk.format,
      .x0 = x0,
      .y0 = y0,
   };
   const struct vk_meta_rect rect = {
      .x0 = x0,
      .y0 = y0,
      .x1 = x0 + w,
      .y1 = y0 + h,
   };

   disp->CmdBeginRendering(cmd, &render);
   disp->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   disp->CmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                          sizeof(push), &push);
   dev->meta.cmd_draw_volume(&cmdbuf->vk, &dev->meta, &rect, layers);
   disp->CmdEndRendering(cmd);
}

/* Source of a decode that reads the raw plane of img itself. */
static struct bc_decode_src
bc_raw_plane_src(const struct panvk_image *img,
                 const VkImageSubresourceLayers *subres, VkOffset3D offset)
{
   const struct pan_image_layout *l = &img->planes[0].plane.layout;
   const struct pan_image_slice_layout *sl = &l->slices[subres->mipLevel];
   const uint32_t blk_B = vk_format_get_blocksize(img->vk.format);

   return (struct bc_decode_src){
      .addr = img->planes[0].plane.base + sl->offset_B +
              subres->baseArrayLayer * l->array_stride_B +
              (offset.y / 4) * sl->tiled_or_linear.row_stride_B +
              (offset.x / 4) * blk_B,
      .row_B = sl->tiled_or_linear.row_stride_B,
      .slice_B = l->array_stride_B,
   };
}

/* Decode raw BC blocks (planes[0]) of one copy region into bc_decoded. */
static void
bc_decode_region(struct panvk_cmd_buffer *cmdbuf, struct panvk_image *img,
                 const VkImageSubresourceLayers *subres, VkOffset3D offset,
                 VkExtent3D extent)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_device_dispatch_table *disp = &dev->vk.dispatch_table;
   VkCommandBuffer cmd = panvk_cmd_buffer_to_handle(cmdbuf);
   VkPipelineLayout layout;
   VkPipeline pipeline;

   VkResult result = get_bc_decode_pipeline(
      dev, bc_decode_shader_index(img->vk.format), &layout, &pipeline);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, result);
      return;
   }

   const uint32_t level = subres->mipLevel;
   const VkExtent3D mip = vk_image_mip_level_extent(&img->vk, level);
   const bool is_3d = img->vk.image_type == VK_IMAGE_TYPE_3D;
   const struct pan_image_layout *sl = &img->planes[0].plane.layout;
   const struct pan_image_layout *dl = &img->bc_decoded.plane.layout;
   const struct pan_image_slice_layout *ss = &sl->slices[level];
   const struct pan_image_slice_layout *ds = &dl->slices[level];
   const unsigned blk_B = vk_format_get_blocksize(img->vk.format);
   const unsigned texel_B =
      vk_format_get_blocksize(panvk_bc_decoded_format(img->vk.format));

   const uint32_t x0 = offset.x & ~3, y0 = offset.y & ~3;
   const uint32_t z0 = is_3d ? offset.z : subres->baseArrayLayer;
   const uint32_t w = MIN2(extent.width + (offset.x - x0), mip.width - x0);
   const uint32_t h = MIN2(extent.height + (offset.y - y0), mip.height - y0);
   const uint32_t d = is_3d ? extent.depth
                            : vk_image_subresource_layer_count(&img->vk, subres);
   const uint64_t src_z_B =
      is_3d ? ss->tiled_or_linear.surface_stride_B : sl->array_stride_B;
   const uint64_t dst_z_B =
      is_3d ? ds->tiled_or_linear.surface_stride_B : dl->array_stride_B;

   if (!w || !h || !d)
      return;

   const uint64_t src = img->planes[0].plane.base + ss->offset_B +
                        z0 * src_z_B +
                        (y0 / 4) * ss->tiled_or_linear.row_stride_B +
                        (x0 / 4) * blk_B;
   const uint64_t dst = img->bc_decoded.plane.base + ds->offset_B +
                        z0 * dst_z_B +
                        y0 * ds->tiled_or_linear.row_stride_B + x0 * texel_B;

   const struct panvk_bc_decode_push push = {
      .src = {(uint32_t)src, (uint32_t)(src >> 32)},
      .dst = {(uint32_t)dst, (uint32_t)(dst >> 32)},
      .src_row_B = ss->tiled_or_linear.row_stride_B,
      .src_slice_B = src_z_B,
      .dst_row_B = ds->tiled_or_linear.row_stride_B,
      .dst_slice_B = dst_z_B,
      .width = w,
      .height = h,
      .depth = d,
      .format = img->vk.format,
   };

   disp->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   disp->CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(push), &push);
   disp->CmdDispatch(cmd, DIV_ROUND_UP(w, 8), DIV_ROUND_UP(h, 8), d);
}

/* BC lowmem transcoder (csf-v11/197), see bc/panvk_bc_etc2.comp. Included
 * here rather than at the top to keep the later series' hunks in place. */
#include "bc/panvk_bc_etc2_spv.h"
#include "bc/panvk_bc_astc_spv.h"
#include "panvk_cmd_alloc.h"
#include "util/os_misc.h"
#include "vk_enum_to_str.h"
#include <stdio.h>

struct panvk_bc_xcode_push {
   uint32_t src[2];
   uint32_t dst[2];
   uint32_t src_row_B;
   uint32_t src_slice_B;
   uint32_t dst_row_B;
   uint32_t dst_slice_B;
   uint32_t bx0;
   uint32_t by0;
   uint32_t bw;
   uint32_t bh;
   /* csf-v11/203: raw-store copy of the source blocks, written by the
    * ETC2/EAC transcoder itself (0: none). */
   uint32_t raw[2];
   uint32_t raw_row_B;
   uint32_t raw_slice_B;
};

/* XC_* in panvk_bc_etc2.comp. */
static uint32_t
bc_xcode_mode(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      return 0;
   case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
      return 1;
   case VK_FORMAT_BC2_UNORM_BLOCK:
   case VK_FORMAT_BC2_SRGB_BLOCK:
      return 2;
   case VK_FORMAT_BC3_UNORM_BLOCK:
   case VK_FORMAT_BC3_SRGB_BLOCK:
      return 3;
   case VK_FORMAT_BC4_UNORM_BLOCK:
      return 4;
   case VK_FORMAT_BC4_SNORM_BLOCK:
      return 5;
   case VK_FORMAT_BC5_UNORM_BLOCK:
      return 6;
   case VK_FORMAT_BC6H_UFLOAT_BLOCK:
      return 8; /* panvk_bc_astc.comp, which ignores it */
   default:
      assert(format == VK_FORMAT_BC5_SNORM_BLOCK);
      return 7;
   }
}

/* Transcoder workgroup size (csf-v11/200): PANVK_BC_LM_WG=<x>x<y>, default
 * 8x8, x * y <= 256. A specialization constant of the shader. */
static void
bc_xcode_wg(uint32_t wg[2])
{
   static uint32_t cached[2];

   if (!p_atomic_read(&cached[0])) {
      const char *s = os_get_option("PANVK_BC_LM_WG");
      unsigned x = 8, y = 8;

      if (s && (sscanf(s, "%ux%u", &x, &y) != 2 || !x || !y ||
                x * y > 256)) {
         mesa_logw("panvk: bad PANVK_BC_LM_WG=%s, using 8x8", s);
         x = 8;
         y = 8;
      }
      cached[1] = y;
      p_atomic_set(&cached[0], x);
   }
   wg[0] = p_atomic_read(&cached[0]);
   wg[1] = cached[1];
}

/* One pipeline per transcode mode: the mode and PANVK_BC_LM_OPTS are
 * specialization constants of panvk_bc_etc2.comp (2 and 3), next to the
 * workgroup size (0, 1). */
static VkResult
get_bc_xcode_pipeline(struct panvk_device *dev, bool astc, uint32_t mode,
                      VkPipelineLayout *layout_out, VkPipeline *pipeline_out)
{
   const enum panvk_meta_object_key_type layout_key =
      PANVK_META_OBJECT_KEY_BC_XCODE_LAYOUT;
   const VkPushConstantRange push_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = sizeof(struct panvk_bc_xcode_push),
   };
   VkResult result =
      vk_meta_get_pipeline_layout(&dev->vk, &dev->meta, NULL, &push_range,
                                  &layout_key, sizeof(layout_key), layout_out);
   if (result != VK_SUCCESS)
      return result;

   struct {
      enum panvk_meta_object_key_type type;
      uint32_t wg[2];
      uint32_t mode;
      uint32_t opts;
      uint32_t astc;
   } key = {
      .type = PANVK_META_OBJECT_KEY_BC_XCODE_SHADER,
      .mode = astc ? 0 : mode,
      .opts = astc ? 0 : panvk_bc_lm_opts(),
      .astc = astc,
   };
   bc_xcode_wg(key.wg);
   VkPipeline cached = vk_meta_lookup_pipeline(&dev->meta, &key, sizeof(key));
   if (cached != VK_NULL_HANDLE) {
      *pipeline_out = cached;
      return VK_SUCCESS;
   }

   const VkSpecializationMapEntry spec_map[4] = {
      {.constantID = 0, .offset = offsetof(__typeof__(key), wg[0]), .size = 4},
      {.constantID = 1, .offset = offsetof(__typeof__(key), wg[1]), .size = 4},
      {.constantID = 2, .offset = offsetof(__typeof__(key), mode), .size = 4},
      {.constantID = 3, .offset = offsetof(__typeof__(key), opts), .size = 4},
   };
   const VkSpecializationInfo spec = {
      .mapEntryCount = astc ? 2 : 4,
      .pMapEntries = spec_map,
      .dataSize = sizeof(key),
      .pData = &key,
   };
   const VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = astc ? sizeof(panvk_bc_astc_spv) : sizeof(panvk_bc_etc2_spv),
      .pCode = astc ? panvk_bc_astc_spv : panvk_bc_etc2_spv,
   };
   const VkComputePipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = &module_info,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .pName = "main",
         .pSpecializationInfo = &spec,
      },
      .layout = *layout_out,
   };

   return vk_meta_create_compute_pipeline(&dev->vk, &dev->meta, &info, &key,
                                          sizeof(key), pipeline_out);
}

/* Transcode the BC blocks of one region at src into the ETC2/EAC shadow of
 * transcoded (panvk_bc_xcoded()) image. Must run between meta_compute_start() and
 * meta_compute_end(). */
static void
bc_xcode_region_raw(struct panvk_cmd_buffer *cmdbuf, struct panvk_image *img,
                    const VkImageSubresourceLayers *subres, VkOffset3D offset,
                    VkExtent3D extent, const struct bc_decode_src *src,
                    const struct bc_decode_src *raw)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_device_dispatch_table *disp = &dev->vk.dispatch_table;
   VkCommandBuffer cmd = panvk_cmd_buffer_to_handle(cmdbuf);
   const uint32_t level = subres->mipLevel;
   const VkExtent3D mip = vk_image_mip_level_extent(&img->vk, level);
   const uint32_t x0 = offset.x & ~3, y0 = offset.y & ~3;
   const uint32_t w = MIN2(extent.width + (offset.x - x0), mip.width - x0);
   const uint32_t h = MIN2(extent.height + (offset.y - y0), mip.height - y0);
   const uint32_t layers = vk_image_subresource_layer_count(&img->vk, subres);

   assert(panvk_bc_xcoded(img));
   if (!w || !h || !layers)
      return;

   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result = get_bc_xcode_pipeline(
      dev, img->bc_lowmem == 3, bc_xcode_mode(img->vk.format), &layout,
      &pipeline);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, result);
      return;
   }

   const struct pan_image_layout *dl = &img->bc_decoded.plane.layout;
   const struct pan_image_slice_layout *ds = &dl->slices[level];
   const uint64_t dst = img->bc_decoded.plane.base + ds->offset_B +
                        subres->baseArrayLayer * dl->array_stride_B;
   const uint32_t bw = DIV_ROUND_UP(w, 4), bh = DIV_ROUND_UP(h, 4);
   const struct panvk_bc_xcode_push push = {
      .src = {(uint32_t)src->addr, (uint32_t)(src->addr >> 32)},
      .dst = {(uint32_t)dst, (uint32_t)(dst >> 32)},
      .src_row_B = src->row_B,
      .src_slice_B = src->slice_B,
      .dst_row_B = ds->tiled_or_linear.row_stride_B,
      .dst_slice_B = dl->array_stride_B,
      .bx0 = x0 / 4,
      .by0 = y0 / 4,
      .bw = bw,
      .bh = bh,
      .raw = {raw ? (uint32_t)raw->addr : 0, raw ? (uint32_t)(raw->addr >> 32) : 0},
      .raw_row_B = raw ? raw->row_B : 0,
      .raw_slice_B = raw ? raw->slice_B : 0,
   };

   disp->CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   disp->CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          sizeof(push), &push);
   uint32_t wg[2];
   bc_xcode_wg(wg);
   disp->CmdDispatch(cmd, DIV_ROUND_UP(bw, wg[0]), DIV_ROUND_UP(bh, wg[1]),
                     layers);
}

static void
bc_xcode_region(struct panvk_cmd_buffer *cmdbuf, struct panvk_image *img,
                const VkImageSubresourceLayers *subres, VkOffset3D offset,
                VkExtent3D extent, const struct bc_decode_src *src)
{
   bc_xcode_region_raw(cmdbuf, img, subres, offset, extent, src, NULL);
}

/* csf-v11/203: the ETC2/EAC transcoder (not the BC6H ASTC one) writes the
 * raw blocks of an upload itself, so the upload reads its source once and
 * needs no separate raw copy pass. 2D images only (the raw plane address
 * steps by array layer). */
static bool
bc_xcode_fuses_raw(const struct panvk_image *img)
{
   return panvk_bc_xcoded(img) && img->bc_lowmem != 3 &&
          img->vk.image_type != VK_IMAGE_TYPE_3D;
}

/* All-zero BC blocks for the decode of a bc_no_raw image leaving
 * ZERO_INITIALIZED: one block row of the base level, read with zero row
 * and layer pitches. */
static struct bc_decode_src
bc_zero_src(struct panvk_cmd_buffer *cmdbuf, const struct panvk_image *img)
{
   const uint32_t size = DIV_ROUND_UP(img->vk.extent.width, 4) *
                         vk_format_get_blocksize(img->vk.format);
   struct pan_ptr ptr = panvk_cmd_alloc_dev_mem(cmdbuf, desc, size, 64);

   if (ptr.cpu)
      memset(ptr.cpu, 0, size);
   return (struct bc_decode_src){.addr = ptr.gpu};
}

/* PANVK_DEBUG=bc_timing (csf-v11/200): GPU time of every BC upload / copy
 * (raw copy + decode or transcode), from two ALL_COMMANDS timestamps in a
 * device-wide query pool. The pairs are read back when the command buffer
 * is reset or freed (it is no longer pending then) and logged one line per
 * operation, plus a running total every 64 operations. Serializes the
 * queue around each operation: for measurements only. */
enum bc_timing_path {
   BC_TIMING_DECODE, /* exact fragment/compute decode */
   BC_TIMING_XCODE,  /* ETC2/EAC (or ASTC) transcode */
};

/* Guards panvk_device::bc (device-wide BC emulation state). */
static simple_mtx_t bc_dev_lock = SIMPLE_MTX_INITIALIZER;

#if PAN_ARCH >= 10
#define BC_TIMING_SLOTS 4096

struct bc_timing_rec {
   uint32_t slot;
   VkFormat format;
   uint32_t w, h, regions, layers;
   enum bc_timing_path path;
};

static uint32_t
bc_timing_begin(struct panvk_cmd_buffer *cmdbuf)
{
   if (!PANVK_DEBUG(BC_TIMING))
      return UINT32_MAX;

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_device_dispatch_table *disp = &dev->vk.dispatch_table;
   VkCommandBuffer cmd = panvk_cmd_buffer_to_handle(cmdbuf);
   uint32_t slot;

   simple_mtx_lock(&bc_dev_lock);
   if (dev->bc.timing_pool == VK_NULL_HANDLE) {
      const VkQueryPoolCreateInfo info = {
         .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
         .queryType = VK_QUERY_TYPE_TIMESTAMP,
         .queryCount = BC_TIMING_SLOTS,
      };
      if (disp->CreateQueryPool(panvk_device_to_handle(dev), &info, NULL,
                                &dev->bc.timing_pool) != VK_SUCCESS)
         dev->bc.timing_pool = VK_NULL_HANDLE;
   }
   slot = dev->bc.timing_next;
   dev->bc.timing_next = (slot + 2) % BC_TIMING_SLOTS;
   VkQueryPool pool = dev->bc.timing_pool;
   simple_mtx_unlock(&bc_dev_lock);

   if (pool == VK_NULL_HANDLE)
      return UINT32_MAX;

   disp->CmdResetQueryPool(cmd, pool, slot, 2);
   disp->CmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, pool,
                            slot);
   return slot;
}

static void
bc_timing_end(struct panvk_cmd_buffer *cmdbuf, uint32_t slot,
              const struct panvk_image *img, VkExtent3D extent,
              uint32_t regions, uint32_t layers, enum bc_timing_path path)
{
   if (slot == UINT32_MAX)
      return;

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_device_dispatch_table *disp = &dev->vk.dispatch_table;
   VkCommandBuffer cmd = panvk_cmd_buffer_to_handle(cmdbuf);
   const struct bc_timing_rec rec = {
      .slot = slot,
      .format = img->vk.format,
      .w = extent.width,
      .h = extent.height,
      .regions = regions,
      .layers = layers,
      .path = path,
   };

   disp->CmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                            dev->bc.timing_pool, slot + 1);
   util_dynarray_append_typed(&cmdbuf->bc_timing, struct bc_timing_rec, rec);
}

static void
bc_timing_flush(struct panvk_cmd_buffer *cmdbuf)
{
   if (!util_dynarray_num_elements(&cmdbuf->bc_timing, struct bc_timing_rec))
      return;

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   const struct vk_device_dispatch_table *disp = &dev->vk.dispatch_table;
   const struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   const double period_ns = panvk_get_gpu_system_timestamp_period(pdev);

   util_dynarray_foreach(&cmdbuf->bc_timing, struct bc_timing_rec, rec) {
      uint64_t ts[2];

      /* Not ready: never submitted, or the slot was reused (more than
       * BC_TIMING_SLOTS / 2 operations in flight). */
      if (disp->GetQueryPoolResults(panvk_device_to_handle(dev),
                                    dev->bc.timing_pool, rec->slot, 2,
                                    sizeof(ts), ts, sizeof(ts[0]),
                                    VK_QUERY_RESULT_64_BIT) != VK_SUCCESS ||
          ts[1] < ts[0])
         continue;

      const double us = (double)(ts[1] - ts[0]) * period_ns / 1000.0;
      uint64_t ops;
      double total;

      simple_mtx_lock(&bc_dev_lock);
      ops = ++dev->bc.timing_ops;
      total = dev->bc.timing_us += us;
      simple_mtx_unlock(&bc_dev_lock);

      mesa_logi("panvk: bc_timing %s %ux%u regions=%u layers=%u %s: %.1f us",
                vk_Format_to_str(rec->format), rec->w, rec->h, rec->regions,
                rec->layers,
                rec->path == BC_TIMING_XCODE ? "xcode" : "decode", us);
      if (!(ops % 64))
         mesa_logi("panvk: bc_timing total %" PRIu64 " ops, %.3f ms", ops,
                   total / 1000.0);
   }
   util_dynarray_clear(&cmdbuf->bc_timing);
}

void
panvk_per_arch(cmd_bc_release)(struct panvk_cmd_buffer *cmdbuf)
{
   bc_timing_flush(cmdbuf);
   panvk_bc_raw_release(&cmdbuf->bc_raw_refs);
}
#else
static inline uint32_t
bc_timing_begin(struct panvk_cmd_buffer *cmdbuf)
{
   return UINT32_MAX;
}

static inline void
bc_timing_end(struct panvk_cmd_buffer *cmdbuf, uint32_t slot,
              const struct panvk_image *img, VkExtent3D extent,
              uint32_t regions, uint32_t layers, enum bc_timing_path path)
{
}
#endif

void
panvk_per_arch(device_bc_finish)(struct panvk_device *dev)
{
   if (dev->bc.timing_pool != VK_NULL_HANDLE) {
      dev->vk.dispatch_table.DestroyQueryPool(panvk_device_to_handle(dev),
                                              dev->bc.timing_pool, NULL);
      dev->bc.timing_pool = VK_NULL_HANDLE;
   }
   panvk_bc_raw_device_finish(dev);
   vk_meta_device_finish(&dev->vk, &dev->meta);
}

/* csf-v11/201: make the raw blocks of a raw-store image resident for this
 * command buffer before any raw access. full: the access overwrites every
 * block (no read-back of a spilled store). False if that failed (the
 * command buffer error is set). */
static bool
bc_raw_use(struct panvk_cmd_buffer *cmdbuf, struct panvk_image *img,
           bool write, bool full)
{
   if (!img->bc_raw)
      return true;
#if PAN_ARCH >= 10
   VkResult result =
      panvk_bc_raw_acquire(img, &cmdbuf->bc_raw_refs, write, full);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmdbuf->vk, result);
      return false;
   }
   return true;
#else
   UNREACHABLE("BC raw stores are v10+ only");
#endif
}

/* 4x4 blocks a copy region writes. */
static uint64_t
bc_region_blocks(const struct panvk_image *img,
                 const VkImageSubresourceLayers *subres, VkExtent3D extent)
{
   return (uint64_t)DIV_ROUND_UP(extent.width, 4) *
          DIV_ROUND_UP(extent.height, 4) * extent.depth *
          vk_image_subresource_layer_count(&img->vk, subres);
}

/* Copy regions never overlap, so they cover the whole image when their
 * blocks add up to its block count. */
static bool
bc_blocks_full(const struct panvk_image *img, uint64_t blocks)
{
   uint64_t total = 0;

   for (uint32_t l = 0; l < img->vk.mip_levels; l++) {
      const VkExtent3D e = vk_image_mip_level_extent(&img->vk, l);
      total += (uint64_t)DIV_ROUND_UP(e.width, 4) * DIV_ROUND_UP(e.height, 4) *
               e.depth;
   }
   return blocks >= total * img->vk.array_layers;
}

static void
bc_decode_barrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst_stage,
                  VkAccessFlags2 dst_access)
{
   const VkMemoryBarrier2 mem_barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
      .dstStageMask = dst_stage,
      .dstAccessMask = dst_access,
   };
   const VkDependencyInfo dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &mem_barrier,
   };
   panvk_per_arch(CmdPipelineBarrier2)(cmd, &dep_info);
}

static void
bc_decode_begin(struct panvk_cmd_buffer *cmdbuf,
                struct panvk_cmd_meta_compute_save_ctx *save)
{
   bc_decode_barrier(panvk_cmd_buffer_to_handle(cmdbuf),
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
   meta_compute_start(cmdbuf, save);
}

void
panvk_per_arch(cmd_bc_decode_zero_initialized)(
   struct panvk_cmd_buffer *cmdbuf, const VkDependencyInfo *dep_info)
{
   struct panvk_cmd_meta_compute_save_ctx save = {0};
   bool started = false, any_gfx = false;

   for (uint32_t i = 0; i < dep_info->imageMemoryBarrierCount; i++) {
      const VkImageMemoryBarrier2 *b = &dep_info->pImageMemoryBarriers[i];
      VK_FROM_HANDLE(panvk_image, img, b->image);

      if (b->oldLayout != VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT ||
          b->newLayout == VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT ||
          !img->bc_emul)
         continue;

      if (img->bc_gfx && !panvk_bc_xcoded(img)) {
         any_gfx = true;
         continue;
      }

      if (!started) {
         bc_decode_begin(cmdbuf, &save);
         started = true;
      }

      const uint32_t level_count =
         vk_image_subresource_level_count(&img->vk, &b->subresourceRange);
      const uint32_t layer_count =
         vk_image_subresource_layer_count(&img->vk, &b->subresourceRange);
      for (uint32_t l = 0; l < level_count; l++) {
         const uint32_t level = b->subresourceRange.baseMipLevel + l;
         const VkImageSubresourceLayers subres = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = level,
            .baseArrayLayer = b->subresourceRange.baseArrayLayer,
            .layerCount = layer_count,
         };
         const VkExtent3D extent = vk_image_mip_level_extent(&img->vk, level);

         if (panvk_bc_xcoded(img)) {
            /* Transcode zero blocks: from the zeroed raw plane, or from a
             * zero row when the image has none. */
            const struct bc_decode_src src =
               (img->bc_no_raw || img->bc_raw)
                  ? bc_zero_src(cmdbuf, img)
                  : bc_raw_plane_src(img, &subres, (VkOffset3D){0, 0, 0});
            bc_xcode_region(cmdbuf, img, &subres, (VkOffset3D){0, 0, 0},
                            extent, &src);
         } else {
            bc_decode_region(cmdbuf, img, &subres, (VkOffset3D){0, 0, 0},
                             extent);
         }
      }
   }

   if (started)
      meta_compute_end(cmdbuf, &save);

   if (any_gfx) {
      struct panvk_cmd_meta_graphics_save_ctx gsave = {0};

      meta_gfx_start(cmdbuf, &gsave);
      for (uint32_t i = 0; i < dep_info->imageMemoryBarrierCount; i++) {
         const VkImageMemoryBarrier2 *b = &dep_info->pImageMemoryBarriers[i];
         VK_FROM_HANDLE(panvk_image, img, b->image);

         if (b->oldLayout != VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT ||
             b->newLayout == VK_IMAGE_LAYOUT_ZERO_INITIALIZED_EXT ||
             !img->bc_gfx || panvk_bc_xcoded(img))
            continue;

         const uint32_t level_count =
            vk_image_subresource_level_count(&img->vk, &b->subresourceRange);
         for (uint32_t l = 0; l < level_count; l++) {
            const VkImageSubresourceLayers subres = {
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .mipLevel = b->subresourceRange.baseMipLevel + l,
               .baseArrayLayer = b->subresourceRange.baseArrayLayer,
               .layerCount = vk_image_subresource_layer_count(
                  &img->vk, &b->subresourceRange),
            };
            const struct bc_decode_src src =
               (img->bc_no_raw || img->bc_raw)
                  ? bc_zero_src(cmdbuf, img)
                  : bc_raw_plane_src(img, &subres, (VkOffset3D){0, 0, 0});
            bc_decode_region_gfx(cmdbuf, img, &subres, (VkOffset3D){0, 0, 0},
                                 vk_image_mip_level_extent(&img->vk,
                                                           subres.mipLevel),
                                 &src);
         }
      }
      meta_gfx_end(cmdbuf, &gsave);
   }

   if (started || any_gfx)
      bc_decode_barrier(panvk_cmd_buffer_to_handle(cmdbuf),
                        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                        VK_ACCESS_2_MEMORY_READ_BIT);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyMemoryToImageKHR)(
   VkCommandBuffer commandBuffer,
   const VkCopyDeviceMemoryImageInfoKHR* pCopyMemoryInfo)
{
   VK_FROM_HANDLE(panvk_image, bc_img, pCopyMemoryInfo->image);
   const VkDeviceMemoryImageCopyKHR *r0 = &pCopyMemoryInfo->pRegions[0];

   if (bc_img->bc_gfx) {
      VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
      struct panvk_cmd_meta_graphics_save_ctx save = {0};
      struct panvk_cmd_meta_compute_save_ctx csave = {0};
      const uint32_t blk_B = vk_format_get_blocksize(bc_img->vk.format);
      const bool xcode = panvk_bc_xcoded(bc_img);
      const uint32_t timing = bc_timing_begin(cmdbuf);

      uint64_t blocks = 0;
      for (uint32_t i = 0; i < pCopyMemoryInfo->regionCount; i++)
         blocks += bc_region_blocks(bc_img,
                                    &pCopyMemoryInfo->pRegions[i].imageSubresource,
                                    pCopyMemoryInfo->pRegions[i].imageExtent);

      /* bc_no_raw images keep no raw blocks: only the shadow is written.
       * The ETC2/EAC transcoder writes the raw blocks itself. */
      const bool fuse = xcode && bc_xcode_fuses_raw(bc_img);
      bool keep_raw = false;
      if (!bc_img->bc_no_raw &&
          bc_raw_use(cmdbuf, bc_img, true, bc_blocks_full(bc_img, blocks))) {
         if (fuse)
            keep_raw = true;
         else
            copy_memory_to_image_raw(commandBuffer, pCopyMemoryInfo);
      }

      /* Decode (or transcode) straight from the copy source: the raw copy
       * and the decode only share reads, so no barrier between them. The
       * app's COPY-stage barriers cover both subqueues. */
      if (xcode)
         meta_compute_start(cmdbuf, &csave);
      else
         meta_gfx_start(cmdbuf, &save);
      for (uint32_t i = 0; i < pCopyMemoryInfo->regionCount; i++) {
         const VkDeviceMemoryImageCopyKHR *r = &pCopyMemoryInfo->pRegions[i];
         const uint32_t row_px =
            r->addressRowLength ? r->addressRowLength : r->imageExtent.width;
         const uint32_t col_px = r->addressImageHeight ? r->addressImageHeight
                                                       : r->imageExtent.height;
         const uint32_t row_B = DIV_ROUND_UP(row_px, 4) * blk_B;
         const struct bc_decode_src src = {
            .addr = r->addressRange.address,
            .row_B = row_B,
            .slice_B = DIV_ROUND_UP(col_px, 4) * row_B,
         };

         if (xcode) {
            const struct bc_decode_src raw =
               keep_raw ? bc_raw_plane_src(bc_img, &r->imageSubresource,
                                           (VkOffset3D){r->imageOffset.x & ~3,
                                                        r->imageOffset.y & ~3, 0})
                        : (struct bc_decode_src){0};
            bc_xcode_region_raw(cmdbuf, bc_img, &r->imageSubresource,
                                r->imageOffset, r->imageExtent, &src,
                                keep_raw ? &raw : NULL);
         } else {
            bc_decode_region_gfx(cmdbuf, bc_img, &r->imageSubresource,
                                 r->imageOffset, r->imageExtent, &src);
         }
      }
      if (xcode)
         meta_compute_end(cmdbuf, &csave);
      else
         meta_gfx_end(cmdbuf, &save);
      bc_timing_end(cmdbuf, timing, bc_img, r0->imageExtent,
                    pCopyMemoryInfo->regionCount,
                    vk_image_subresource_layer_count(&bc_img->vk,
                                                     &r0->imageSubresource),
                    xcode ? BC_TIMING_XCODE : BC_TIMING_DECODE);
      return;
   }

   if (bc_img->bc_emul) {
      VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
      struct panvk_cmd_meta_compute_save_ctx save = {0};
      const uint32_t timing = bc_timing_begin(cmdbuf);

      copy_memory_to_image_raw(commandBuffer, pCopyMemoryInfo);

      bc_decode_begin(cmdbuf, &save);
      for (uint32_t i = 0; i < pCopyMemoryInfo->regionCount; i++) {
         const VkDeviceMemoryImageCopyKHR *r = &pCopyMemoryInfo->pRegions[i];
         bc_decode_region(cmdbuf, bc_img, &r->imageSubresource, r->imageOffset,
                          r->imageExtent);
      }
      meta_compute_end(cmdbuf, &save);
      bc_timing_end(cmdbuf, timing, bc_img, r0->imageExtent,
                    pCopyMemoryInfo->regionCount,
                    vk_image_subresource_layer_count(&bc_img->vk,
                                                     &r0->imageSubresource),
                    BC_TIMING_DECODE);
      return;
   }

   copy_memory_to_image_raw(commandBuffer, pCopyMemoryInfo);
}

static void
copy_memory_to_image_raw(
   VkCommandBuffer commandBuffer,
   const VkCopyDeviceMemoryImageInfoKHR* pCopyMemoryInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, img, pCopyMemoryInfo->image);

   /* Early out if this operation was lowered. */
   if (lower_copy_memory_to_image(commandBuffer, pCopyMemoryInfo))
      return;

   const bool use_gfx_pipeline = copy_to_image_use_gfx_pipeline(img);
   struct vk_meta_copy_image_properties img_props =
      panvk_meta_copy_get_image_properties(img, use_gfx_pipeline, true);

   if (use_gfx_pipeline) {
      struct panvk_cmd_meta_graphics_save_ctx save = {0};

      meta_gfx_start(cmdbuf, &save);
      vk_meta_copy_memory_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyMemoryInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_GRAPHICS);
      meta_gfx_end(cmdbuf, &save);
   } else {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      meta_compute_start(cmdbuf, &save);
#if PAN_ARCH >= 10
      if (copy_buffer_to_image_touches_crc(img, pCopyMemoryInfo)) {
         struct cs_builder *b =
            panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);

         panvk_per_arch(cmd_invalidate_crc)(
            b, panvk_image_plane_crc_header_addr(img));
      }
#endif
      vk_meta_copy_memory_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyMemoryInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_COMPUTE);
      meta_compute_end(cmdbuf, &save);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyImageToMemoryKHR)(
   VkCommandBuffer commandBuffer,
   const VkCopyDeviceMemoryImageInfoKHR* pCopyMemoryInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, img, pCopyMemoryInfo->image);
   struct vk_meta_copy_image_properties img_props =
      panvk_meta_copy_get_image_properties(img, false, false);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   /* Raw blocks of a raw-store image (csf-v11/201). */
   if (!bc_raw_use(cmdbuf, img, false, false))
      return;

   meta_compute_start(cmdbuf, &save);
   vk_meta_copy_image_to_memory(&cmdbuf->vk, &dev->meta, pCopyMemoryInfo,
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

/* Image -> image copy into a bc_no_raw image. There is no raw plane to copy
 * into, so the shadow is written from the source blocks directly: from the
 * raw plane of a BC source (a copy source has TRANSFER_SRC, so it keeps
 * one), else from a staging copy of the source region. */
static void
bc_copy_image_no_raw(struct panvk_cmd_buffer *cmdbuf,
                     const VkCopyImageInfo2 *info)
{
   VkCommandBuffer cmd = panvk_cmd_buffer_to_handle(cmdbuf);
   VK_FROM_HANDLE(panvk_image, src_img, info->srcImage);
   VK_FROM_HANDLE(panvk_image, dst_img, info->dstImage);
   const bool src_blocks = vk_format_is_compressed(src_img->vk.format);
   const bool src_is_3d = src_img->vk.image_type == VK_IMAGE_TYPE_3D;
   const bool src_raw =
      src_blocks && src_img->bc_emul && !src_img->bc_no_raw && !src_is_3d;
   const uint32_t blk_B = vk_format_get_blocksize(dst_img->vk.format);
   bool staged = false;

   STACK_ARRAY(struct bc_decode_src, srcs, info->regionCount);

   for (uint32_t i = 0; i < info->regionCount; i++) {
      const VkImageCopy2 *r = &info->pRegions[i];

      if (src_raw) {
         srcs[i] = bc_raw_plane_src(src_img, &r->srcSubresource, r->srcOffset);
         continue;
      }

      /* Tightly packed blocks (or texels of a size-compatible format). */
      const uint32_t bw =
         src_blocks ? DIV_ROUND_UP(r->extent.width, 4) : r->extent.width;
      const uint32_t bh =
         src_blocks ? DIV_ROUND_UP(r->extent.height, 4) : r->extent.height;
      const uint32_t layers =
         src_is_3d ? r->extent.depth
                   : vk_image_subresource_layer_count(&src_img->vk,
                                                      &r->srcSubresource);
      const uint32_t row_B = bw * blk_B;
      const uint64_t size = (uint64_t)row_B * bh * layers;
      struct pan_ptr mem = panvk_cmd_alloc_dev_mem(cmdbuf, desc, size, 64);

      if (!mem.gpu) {
         STACK_ARRAY_FINISH(srcs);
         return;
      }

      const VkDeviceMemoryImageCopyKHR region = {
         .sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_IMAGE_COPY_KHR,
         .addressRange = {.address = mem.gpu, .size = size},
         .imageSubresource = r->srcSubresource,
         .imageLayout = info->srcImageLayout,
         .imageOffset = r->srcOffset,
         .imageExtent = r->extent,
      };
      const VkCopyDeviceMemoryImageInfoKHR copy = {
         .sType = VK_STRUCTURE_TYPE_COPY_DEVICE_MEMORY_IMAGE_INFO_KHR,
         .image = info->srcImage,
         .regionCount = 1,
         .pRegions = &region,
      };

      panvk_per_arch(CmdCopyImageToMemoryKHR)(cmd, &copy);
      staged = true;
      srcs[i] = (struct bc_decode_src){
         .addr = mem.gpu,
         .row_B = row_B,
         .slice_B = row_B * bh,
      };
   }

   const bool xcode = panvk_bc_xcoded(dst_img);

   if (staged) {
      bc_decode_barrier(cmd,
                        xcode ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                              : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
   }

   struct panvk_cmd_meta_graphics_save_ctx save = {0};
   struct panvk_cmd_meta_compute_save_ctx csave = {0};

   if (xcode)
      meta_compute_start(cmdbuf, &csave);
   else
      meta_gfx_start(cmdbuf, &save);
   for (uint32_t i = 0; i < info->regionCount; i++) {
      const VkImageCopy2 *r = &info->pRegions[i];
      VkExtent3D extent = r->extent;

      if (!src_blocks) {
         extent.width *= 4;
         extent.height *= 4;
      }
      if (xcode)
         bc_xcode_region(cmdbuf, dst_img, &r->dstSubresource, r->dstOffset,
                         extent, &srcs[i]);
      else
         bc_decode_region_gfx(cmdbuf, dst_img, &r->dstSubresource,
                              r->dstOffset, extent, &srcs[i]);
   }
   if (xcode)
      meta_compute_end(cmdbuf, &csave);
   else
      meta_gfx_end(cmdbuf, &save);

   STACK_ARRAY_FINISH(srcs);
}

static void cmd_copy_image(VkCommandBuffer commandBuffer,
                           const VkCopyImageInfo2 *pCopyImageInfo);

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyImage2)(VkCommandBuffer commandBuffer,
                              const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(panvk_image, dst, pCopyImageInfo->dstImage);

   if (!dst->bc_emul || !PANVK_DEBUG(BC_TIMING)) {
      cmd_copy_image(commandBuffer, pCopyImageInfo);
      return;
   }

   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   const VkImageCopy2 *r0 = &pCopyImageInfo->pRegions[0];
   const uint32_t timing = bc_timing_begin(cmdbuf);

   cmd_copy_image(commandBuffer, pCopyImageInfo);
   bc_timing_end(cmdbuf, timing, dst, r0->extent, pCopyImageInfo->regionCount,
                 vk_image_subresource_layer_count(&dst->vk,
                                                  &r0->dstSubresource),
                 panvk_bc_xcoded(dst) ? BC_TIMING_XCODE : BC_TIMING_DECODE);
}

static void
cmd_copy_image(VkCommandBuffer commandBuffer,
               const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(panvk_image, bc_src, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(panvk_image, bc_dst, pCopyImageInfo->dstImage);
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   /* Raw blocks of raw-store images (csf-v11/201): read from the source,
    * written to the destination (unless it keeps none). */
   /* A BC -> BC copy writes whole blocks of the copy extent. */
   uint64_t blocks = 0;
   if (panvk_format_is_bc(bc_src->vk.format)) {
      for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++)
         blocks += bc_region_blocks(bc_dst,
                                    &pCopyImageInfo->pRegions[i].dstSubresource,
                                    pCopyImageInfo->pRegions[i].extent);
   }
   if (!bc_raw_use(cmdbuf, bc_src, false, false) ||
       (!bc_dst->bc_no_raw &&
        !bc_raw_use(cmdbuf, bc_dst, true, bc_blocks_full(bc_dst, blocks))))
      return;

   if (bc_dst->bc_emul && bc_dst->bc_no_raw) {
      bc_copy_image_no_raw(cmdbuf, pCopyImageInfo);
      return;
   }

   copy_image_raw(commandBuffer, pCopyImageInfo);

   if (!bc_dst->bc_emul)
      return;

   const bool src_blocks = vk_format_is_compressed(bc_src->vk.format);

   if (panvk_bc_xcoded(bc_dst)) {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      /* Transcode from the raw blocks the copy just wrote. */
      bc_decode_begin(cmdbuf, &save);
      for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++) {
         const VkImageCopy2 *r = &pCopyImageInfo->pRegions[i];
         VkExtent3D extent = r->extent;

         if (!src_blocks) {
            extent.width *= 4;
            extent.height *= 4;
         }
         const struct bc_decode_src src =
            bc_raw_plane_src(bc_dst, &r->dstSubresource, r->dstOffset);
         bc_xcode_region(cmdbuf, bc_dst, &r->dstSubresource, r->dstOffset,
                         extent, &src);
      }
      meta_compute_end(cmdbuf, &save);
      return;
   }

   if (bc_dst->bc_gfx) {
      struct panvk_cmd_meta_graphics_save_ctx save = {0};

      /* Decode from the raw blocks the copy just wrote. */
      bc_decode_barrier(commandBuffer, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
      meta_gfx_start(cmdbuf, &save);
      for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++) {
         const VkImageCopy2 *r = &pCopyImageInfo->pRegions[i];
         VkExtent3D extent = r->extent;

         if (!src_blocks) {
            extent.width *= 4;
            extent.height *= 4;
         }
         const struct bc_decode_src src =
            bc_raw_plane_src(bc_dst, &r->dstSubresource, r->dstOffset);
         bc_decode_region_gfx(cmdbuf, bc_dst, &r->dstSubresource, r->dstOffset,
                              extent, &src);
      }
      meta_gfx_end(cmdbuf, &save);
      return;
   }

   struct panvk_cmd_meta_compute_save_ctx save = {0};

   bc_decode_begin(cmdbuf, &save);
   for (uint32_t i = 0; i < pCopyImageInfo->regionCount; i++) {
      const VkImageCopy2 *r = &pCopyImageInfo->pRegions[i];
      VkExtent3D extent = r->extent;

      if (!src_blocks) {
         extent.width *= 4;
         extent.height *= 4;
      }
      bc_decode_region(cmdbuf, bc_dst, &r->dstSubresource, r->dstOffset,
                       extent);
   }
   meta_compute_end(cmdbuf, &save);
}

static void
copy_image_raw(VkCommandBuffer commandBuffer,
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
#if PAN_ARCH >= 10
      if (copy_image_touches_crc(dst_img, pCopyImageInfo)) {
         struct cs_builder *b =
            panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);

         panvk_per_arch(cmd_invalidate_crc)(
            b, panvk_image_plane_crc_header_addr(dst_img));
      }
#endif
      vk_meta_copy_image(&cmdbuf->vk, &dev->meta, pCopyImageInfo,
                         &src_img_props, &dst_img_props,
                         VK_PIPELINE_BIND_POINT_COMPUTE);
      meta_compute_end(cmdbuf, &save);
   }
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
   VkRenderingAttachmentFlagsInfoKHR color_att_flags[MAX_RTS];
   for (uint32_t i = 0; i < color_att_count; i++) {

      const struct panvk_resolve_attachment *resolve_info =
         &cmdbuf->state.gfx.render.color_attachments.resolve[i];
      struct panvk_image_view *src_iview =
         cmdbuf->state.gfx.render.color_attachments.iviews[i];

      color_att_flags[i] = (VkRenderingAttachmentFlagsInfoKHR){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_FLAGS_INFO_KHR,
         .flags = resolve_info->flags,
      };

      color_atts[i] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .pNext = &color_att_flags[i],
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

#if PAN_ARCH >= 10

#define COPY_MEM_INDIRECT_MAX_WG 16
#define COPY_MEM_INDIRECT_WG_BYTES                                            \
   (PANLIB_COPY_MEM_WG_SIZE * PANLIB_COPY_MEM_CHUNK_SIZE)

/* Turn the 64-bit byte size at size_addr into a workgroup count in
 * JOB_SIZE_X, capped at COPY_MEM_INDIRECT_MAX_WG. Pre-v13 archs have no CS
 * shift instructions, so the count is only approximated with
 * min(size, cap). The result is never too small, the kernel loops when the
 * dispatch does not cover the whole size.
 */
static void
emit_copy_mem_indirect_wg_count(struct panvk_cmd_buffer *cmdbuf,
                                uint64_t size_addr)
{
   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);

   cs_update_compute_ctx(b) {
      cs_move64_to(b, cs_scratch_reg64(b, 0), size_addr);
      cs_load_to(b, cs_scratch_reg_tuple(b, 2, 2), cs_scratch_reg64(b, 0),
                 BITFIELD_MASK(2), 0);
      cs_flush_loads(b);
#if PAN_ARCH >= 13
      /* wg_count = DIV_ROUND_UP(size, COPY_MEM_INDIRECT_WG_BYTES) */
      cs_add_imm64(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 2),
                   COPY_MEM_INDIRECT_WG_BYTES - 1);
      cs_rshift_imm_u64(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 2),
                        util_logbase2(COPY_MEM_INDIRECT_WG_BYTES));
#endif
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X),
                   COPY_MEM_INDIRECT_MAX_WG);
      cs_umin32(b, cs_scratch_reg32(b, 2), cs_scratch_reg32(b, 2),
                cs_sr_reg32(b, COMPUTE, JOB_SIZE_X));
      /* Keep the cap if the size exceeds 32 bits. */
      cs_if(b, MALI_CS_CONDITION_EQUAL, cs_scratch_reg32(b, 3)) {
         cs_move_reg32(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_X),
                       cs_scratch_reg32(b, 2));
      }
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Y), 1);
      cs_move32_to(b, cs_sr_reg32(b, COMPUTE, JOB_SIZE_Z), 1);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyMemoryIndirectKHR)(
   VkCommandBuffer commandBuffer,
   const VkCopyMemoryIndirectInfoKHR *pCopyMemoryIndirectInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_precomp_ctx ctx = panvk_per_arch(precomp_cs)(cmdbuf);

   for (uint32_t i = 0; i < pCopyMemoryIndirectInfo->copyCount; i++) {
      uint64_t cmd_addr = pCopyMemoryIndirectInfo->copyAddressRange.address +
                          i * pCopyMemoryIndirectInfo->copyAddressRange.stride;

      emit_copy_mem_indirect_wg_count(
         cmdbuf, cmd_addr + offsetof(VkCopyMemoryIndirectCommandKHR, size));
      panlib_copy_mem_indirect(&ctx, panlib_dynamic_csf(),
                               PANLIB_BARRIER_CSF_SYNC, cmd_addr);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyMemoryToImageIndirectKHR)(
   VkCommandBuffer commandBuffer,
   const VkCopyMemoryToImageIndirectInfoKHR *pCopyMemoryToImageIndirectInfo)
{
   assert(!"indirectMemoryToImageCopy is not supported");
}

#endif /* PAN_ARCH >= 10 */
