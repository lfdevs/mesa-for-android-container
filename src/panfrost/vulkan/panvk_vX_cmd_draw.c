/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_meta.h"
#include "panvk_device_memory.h"
#include "panvk_entrypoints.h"

#include "pan_desc.h"
#include "pan_util.h"

static void
att_set_clear_preload(const VkRenderingAttachmentInfo *att, bool *clear, bool *preload)
{
   switch (att->loadOp) {
   case VK_ATTACHMENT_LOAD_OP_CLEAR:
      *clear = true;
      break;
   case VK_ATTACHMENT_LOAD_OP_LOAD:
      *preload = true;
      break;
   case VK_ATTACHMENT_LOAD_OP_NONE:
      break;
   case VK_ATTACHMENT_LOAD_OP_DONT_CARE:
      /* This is a very frustrating corner case. From the spec:
       *
       *     VK_ATTACHMENT_STORE_OP_NONE specifies the contents within the
       *     render area are not accessed by the store operation as long as
       *     no values are written to the attachment during the render pass.
       *
       * With VK_ATTACHMENT_LOAD_OP_DONT_CARE + VK_ATTACHMENT_STORE_OP_NONE,
       * we need to preserve the contents throughout partial renders. The
       * easiest way to do that is forcing a preload, so that partial stores
       * for unused attachments will be no-op'd by writing existing contents.
       *
       * TODO: disable preload when we have clean_pixel_write_enable = false
       * as an optimization
       */
      *preload |= att->storeOp == VK_ATTACHMENT_STORE_OP_NONE;
      break;
   default:
      UNREACHABLE("Unsupported loadOp");
   }
}

static void
render_state_set_color_attachment(struct panvk_cmd_buffer *cmdbuf,
                                  const VkRenderingAttachmentInfo *att,
                                  uint32_t index)
{
   struct panvk_physical_device *phys_dev =
         to_panvk_physical_device(cmdbuf->vk.base.device->physical);
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   struct pan_fb_info *fbinfo = &state->render.fb.info;
   VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);
   struct panvk_image *img =
      container_of(iview->vk.image, struct panvk_image, vk);

   state->render.bound_attachments |= MESA_VK_RP_ATTACHMENT_COLOR_BIT(index);
   state->render.color_attachments.iviews[index] = iview;
   state->render.color_attachments.fmts[index] = iview->vk.format;
   state->render.color_attachments.samples[index] = img->vk.samples;

#if PAN_ARCH < 9
   for (uint8_t p = 0; p < ARRAY_SIZE(iview->pview.planes); p++) {
      struct pan_image_plane_ref pref =
         pan_image_view_get_plane(&iview->pview, p);

      if (!pref.image)
         continue;

      assert(pref.plane_idx < ARRAY_SIZE(img->planes));
      assert(img->planes[pref.plane_idx].mem->bo != NULL);
      state->render.fb.bos[state->render.fb.bo_count++] =
         img->planes[pref.plane_idx].mem->bo;
   }
#endif

   fbinfo->rts[index].view = &iview->pview;
   fbinfo->rts[index].crc_valid = &state->render.fb.crc_valid[index];
   state->render.fb.nr_samples =
      MAX2(state->render.fb.nr_samples,
           pan_image_view_get_nr_samples(&iview->pview));

   if (att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
      enum pipe_format fmt = vk_format_to_pipe_format(iview->vk.format);
      union pipe_color_union *col =
         (union pipe_color_union *)&att->clearValue.color;
      pan_pack_color(phys_dev->formats.blendable,
                     fbinfo->rts[index].clear_value, col, fmt, false);
   }

   att_set_clear_preload(att, &fbinfo->rts[index].clear,
                         &fbinfo->rts[index].preload);

   if (att->resolveMode != VK_RESOLVE_MODE_NONE) {
      struct panvk_resolve_attachment *resolve_info =
         &state->render.color_attachments.resolve[index];
      VK_FROM_HANDLE(panvk_image_view, resolve_iview, att->resolveImageView);

      resolve_info->mode = att->resolveMode;
      resolve_info->dst_iview = resolve_iview;
   }
}

static void
render_state_set_z_attachment(struct panvk_cmd_buffer *cmdbuf,
                              const VkRenderingAttachmentInfo *att)
{
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   struct pan_fb_info *fbinfo = &state->render.fb.info;
   VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);
   struct panvk_image *img =
      container_of(iview->vk.image, struct panvk_image, vk);

#if PAN_ARCH < 9
   /* Depth plane always comes first. */
   state->render.fb.bos[state->render.fb.bo_count++] = img->planes[0].mem->bo;
#endif

   state->render.z_attachment.fmt = iview->vk.format;
   state->render.bound_attachments |= MESA_VK_RP_ATTACHMENT_DEPTH_BIT;

   state->render.zs_pview = iview->pview;
   fbinfo->zs.view.zs = &state->render.zs_pview;

   /* Fixup view format when the image is multiplanar. */
   if (panvk_image_is_planar_depth_stencil(img))
      state->render.zs_pview.format = panvk_image_depth_only_pfmt(img);

   state->render.zs_pview.planes[0] = (struct pan_image_plane_ref){
      .image = &img->planes[0].image,
      .plane_idx = 0,
   };
   state->render.zs_pview.planes[1] = (struct pan_image_plane_ref){0};
   state->render.fb.nr_samples =
      MAX2(state->render.fb.nr_samples,
           pan_image_view_get_nr_samples(&iview->pview));
   state->render.z_attachment.iview = iview;

   /* D24S8 is a single plane format where the depth/stencil are interleaved.
    * If we touch the depth component, we need to make sure the stencil
    * component is preserved, hence the preload, and the view format adjusment.
    */
   if (panvk_image_is_interleaved_depth_stencil(img)) {
      fbinfo->zs.preload.s = true;
      cmdbuf->state.gfx.render.zs_pview.format =
         img->planes[0].image.props.format;
   } else {
      state->render.zs_pview.format = panvk_image_depth_only_pfmt(img);
   }

   if (att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
      fbinfo->zs.clear_value.depth = att->clearValue.depthStencil.depth;

   att_set_clear_preload(att, &fbinfo->zs.clear.z, &fbinfo->zs.preload.z);

   if (att->resolveMode != VK_RESOLVE_MODE_NONE) {
      struct panvk_resolve_attachment *resolve_info =
         &state->render.z_attachment.resolve;
      VK_FROM_HANDLE(panvk_image_view, resolve_iview, att->resolveImageView);

      resolve_info->mode = att->resolveMode;
      resolve_info->dst_iview = resolve_iview;
   }
}

static void
render_state_set_s_attachment(struct panvk_cmd_buffer *cmdbuf,
                              const VkRenderingAttachmentInfo *att)
{
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   struct pan_fb_info *fbinfo = &state->render.fb.info;
   VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);
   struct panvk_image *img =
      container_of(iview->vk.image, struct panvk_image, vk);

#if PAN_ARCH < 9
   /* The stencil plane is always last. */
   state->render.fb.bos[state->render.fb.bo_count++] =
      img->planes[img->plane_count - 1].mem->bo;
#endif

   state->render.s_attachment.fmt = iview->vk.format;
   state->render.bound_attachments |= MESA_VK_RP_ATTACHMENT_STENCIL_BIT;

   state->render.s_pview = iview->pview;
   fbinfo->zs.view.s = &state->render.s_pview;

   if (panvk_image_is_planar_depth_stencil(img)) {
      state->render.s_pview.format = panvk_image_stencil_only_pfmt(img);
      state->render.s_pview.planes[0] = (struct pan_image_plane_ref){0};
      state->render.s_pview.planes[1] = (struct pan_image_plane_ref){
         .image = &img->planes[1].image,
         .plane_idx = 0,
      };
   } else {
      state->render.s_pview.format = panvk_image_stencil_only_pfmt(img);
      state->render.s_pview.planes[0] = (struct pan_image_plane_ref){
         .image = &img->planes[0].image,
         .plane_idx = 0,
      };
      state->render.s_pview.planes[1] = (struct pan_image_plane_ref){0};
   }

   state->render.fb.nr_samples =
      MAX2(state->render.fb.nr_samples,
           pan_image_view_get_nr_samples(&iview->pview));
   state->render.s_attachment.iview = iview;

   /* If the depth and stencil attachments point to the same image,
    * and the format is D24S8, we can combine them in a single view
    * addressing both components.
    */
   if (state->render.s_pview.format == PIPE_FORMAT_X24S8_UINT &&
       state->render.z_attachment.iview &&
       state->render.z_attachment.iview->vk.image == iview->vk.image) {
      state->render.zs_pview.format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
      fbinfo->zs.preload.s = false;
      fbinfo->zs.view.s = NULL;

   /* If there was no depth attachment, and the image format is D24S8,
    * we use the depth+stencil slot, so we can benefit from AFBC, which
    * is not supported on the stencil-only slot on Bifrost.
    */
   } else if (img->vk.format == VK_FORMAT_D24_UNORM_S8_UINT &&
              state->render.s_pview.format == PIPE_FORMAT_X24S8_UINT &&
              fbinfo->zs.view.zs == NULL) {
      fbinfo->zs.view.zs = &state->render.s_pview;
      state->render.s_pview.format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
      fbinfo->zs.preload.z = true;
      fbinfo->zs.view.s = NULL;
   }

   if (att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)
      fbinfo->zs.clear_value.stencil = att->clearValue.depthStencil.stencil;

   att_set_clear_preload(att, &fbinfo->zs.clear.s, &fbinfo->zs.preload.s);

   if (att->resolveMode != VK_RESOLVE_MODE_NONE) {
      struct panvk_resolve_attachment *resolve_info =
         &state->render.s_attachment.resolve;
      VK_FROM_HANDLE(panvk_image_view, resolve_iview, att->resolveImageView);

      resolve_info->mode = att->resolveMode;
      resolve_info->dst_iview = resolve_iview;
   }
}

void
panvk_per_arch(cmd_init_render_state)(struct panvk_cmd_buffer *cmdbuf,
                                      const VkRenderingInfo *pRenderingInfo)
{
   struct panvk_physical_device *phys_dev =
         to_panvk_physical_device(cmdbuf->vk.base.device->physical);
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   struct pan_fb_info *fbinfo = &state->render.fb.info;
   uint32_t att_width = UINT32_MAX, att_height = UINT32_MAX;

   state->render.flags = pRenderingInfo->flags;

   BITSET_SET(state->dirty, PANVK_CMD_GRAPHICS_DIRTY_RENDER_STATE);

#if PAN_ARCH < 9
   state->render.fb.bo_count = 0;
   memset(state->render.fb.bos, 0, sizeof(state->render.fb.bos));
#endif

   state->render.first_provoking_vertex = U_TRISTATE_UNSET;
#if PAN_ARCH >= 10
   state->render.maybe_set_tds_provoking_vertex = NULL;
   state->render.maybe_set_fbds_provoking_vertex = NULL;
#endif
   memset(state->render.fb.crc_valid, 0, sizeof(state->render.fb.crc_valid));
   memset(&state->render.color_attachments, 0,
          sizeof(state->render.color_attachments));
   memset(&state->render.z_attachment, 0, sizeof(state->render.z_attachment));
   memset(&state->render.s_attachment, 0, sizeof(state->render.s_attachment));
   state->render.bound_attachments = 0;

   cmdbuf->state.gfx.render.layer_count = pRenderingInfo->viewMask ?
      util_last_bit(pRenderingInfo->viewMask) :
      pRenderingInfo->layerCount;
   cmdbuf->state.gfx.render.view_mask = pRenderingInfo->viewMask;
   *fbinfo = (struct pan_fb_info){
      .tile_buf_budget = pan_query_optimal_tib_size(PAN_ARCH, phys_dev->model),
      .z_tile_buf_budget = pan_query_optimal_z_tib_size(PAN_ARCH, phys_dev->model),
      .nr_samples = 0,
      .rt_count = pRenderingInfo->colorAttachmentCount,
   };
   cmdbuf->state.gfx.render.fb.nr_samples = 1;

   assert(pRenderingInfo->colorAttachmentCount <= ARRAY_SIZE(fbinfo->rts));

   for (uint32_t i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      const VkRenderingAttachmentInfo *att =
         &pRenderingInfo->pColorAttachments[i];
      VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);

      if (!iview)
         continue;

      render_state_set_color_attachment(cmdbuf, att, i);
      att_width = MIN2(iview->vk.extent.width, att_width);
      att_height = MIN2(iview->vk.extent.height, att_height);
   }

   if (pRenderingInfo->pDepthAttachment &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) {
      const VkRenderingAttachmentInfo *att = pRenderingInfo->pDepthAttachment;
      VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);

      if (iview) {
         assert(iview->vk.image->aspects & VK_IMAGE_ASPECT_DEPTH_BIT);
         render_state_set_z_attachment(cmdbuf, att);
         att_width = MIN2(iview->vk.extent.width, att_width);
         att_height = MIN2(iview->vk.extent.height, att_height);
      }
   }

   if (pRenderingInfo->pStencilAttachment &&
       pRenderingInfo->pStencilAttachment->imageView != VK_NULL_HANDLE) {
      const VkRenderingAttachmentInfo *att = pRenderingInfo->pStencilAttachment;
      VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);

      if (iview) {
         assert(iview->vk.image->aspects & VK_IMAGE_ASPECT_STENCIL_BIT);
         render_state_set_s_attachment(cmdbuf, att);
         att_width = MIN2(iview->vk.extent.width, att_width);
         att_height = MIN2(iview->vk.extent.height, att_height);
      }
   }

   fbinfo->draw_extent.minx = pRenderingInfo->renderArea.offset.x;
   fbinfo->draw_extent.maxx = pRenderingInfo->renderArea.offset.x +
                              pRenderingInfo->renderArea.extent.width - 1;
   fbinfo->draw_extent.miny = pRenderingInfo->renderArea.offset.y;
   fbinfo->draw_extent.maxy = pRenderingInfo->renderArea.offset.y +
                              pRenderingInfo->renderArea.extent.height - 1;

   fbinfo->frame_bounding_box = fbinfo->draw_extent;

   if (state->render.bound_attachments) {
      fbinfo->width = att_width;
      fbinfo->height = att_height;
   } else {
      fbinfo->width = fbinfo->draw_extent.maxx + 1;
      fbinfo->height = fbinfo->draw_extent.maxy + 1;
   }

   assert(fbinfo->width && fbinfo->height);
}

void
panvk_per_arch(cmd_select_tile_size)(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   /* In case we never emitted tiler/framebuffer descriptors, we emit the
    * current sample count and compute tile size */
   if (fbinfo->nr_samples == 0) {
      fbinfo->nr_samples = cmdbuf->state.gfx.render.fb.nr_samples;
      GENX(pan_select_tile_size)(fbinfo);

#if PAN_ARCH != 6
      if (fbinfo->cbuf_allocation > fbinfo->tile_buf_budget) {
         vk_perf(VK_LOG_OBJS(&cmdbuf->vk.base),
                 "Using too much tile-memory, disabling pipelining");
      }
#endif
   } else {
      /* In case we already emitted tiler/framebuffer descriptors, we ensure
       * that the sample count didn't change (this should never happen) */
      assert(fbinfo->nr_samples == cmdbuf->state.gfx.render.fb.nr_samples);
   }
}

void
panvk_per_arch(cmd_resolve_attachments)(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
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
            .offset.x = fbinfo->draw_extent.minx,
            .offset.y = fbinfo->draw_extent.miny,
            .extent.width =
               fbinfo->draw_extent.maxx - fbinfo->draw_extent.minx + 1,
            .extent.height =
               fbinfo->draw_extent.maxy - fbinfo->draw_extent.miny + 1,
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

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_resolve_rendering(&cmdbuf->vk, &dev->meta, &render_info);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

void
panvk_per_arch(cmd_force_fb_preload)(struct panvk_cmd_buffer *cmdbuf,
                                     const VkRenderingInfo *render_info)
{
   /* We force preloading for all active attachments when the render area is
    * unaligned or when a barrier flushes prior draw calls in the middle of a
    * render pass.  The two cases can be distinguished by whether a
    * render_info is provided.
    *
    * When the render area is unaligned, we force preloading to preserve
    * contents falling outside of the render area.  We also make sure the
    * initial attachment clears are performed.
    */
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   struct pan_fb_info *fbinfo = &state->render.fb.info;
   VkClearAttachment clear_atts[MAX_RTS + 2];
   uint32_t clear_att_count = 0;

   if (!state->render.bound_attachments)
      return;

   for (unsigned i = 0; i < fbinfo->rt_count; i++) {
      if (!fbinfo->rts[i].view)
         continue;

      fbinfo->rts[i].preload = true;

      if (fbinfo->rts[i].clear) {
         if (render_info) {
            const VkRenderingAttachmentInfo *att =
               &render_info->pColorAttachments[i];

            clear_atts[clear_att_count++] = (VkClearAttachment){
               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .colorAttachment = i,
               .clearValue = att->clearValue,
            };
         }
         fbinfo->rts[i].clear = false;
      }
   }

   if (fbinfo->zs.view.zs) {
      fbinfo->zs.preload.z = true;

      if (fbinfo->zs.clear.z) {
         if (render_info) {
            const VkRenderingAttachmentInfo *att =
               render_info->pDepthAttachment;

            clear_atts[clear_att_count++] = (VkClearAttachment){
               .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
               .clearValue = att->clearValue,
            };
         }
         fbinfo->zs.clear.z = false;
      }
   }

   if (fbinfo->zs.view.s ||
       (fbinfo->zs.view.zs &&
        util_format_is_depth_and_stencil(fbinfo->zs.view.zs->format))) {
      fbinfo->zs.preload.s = true;

      if (fbinfo->zs.clear.s) {
         if (render_info) {
            const VkRenderingAttachmentInfo *att =
               render_info->pStencilAttachment;

            clear_atts[clear_att_count++] = (VkClearAttachment){
               .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
               .clearValue = att->clearValue,
            };
         }

         fbinfo->zs.clear.s = false;
      }
   }

#if PAN_ARCH >= 10
   /* insert a barrier for preload */
   const VkMemoryBarrier2 mem_barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
   };
   const VkDependencyInfo dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &mem_barrier,
   };
   panvk_per_arch(CmdPipelineBarrier2)(panvk_cmd_buffer_to_handle(cmdbuf),
                                       &dep_info);
#endif

   if (clear_att_count && render_info) {
      VkClearRect clear_rect = {
         .rect = render_info->renderArea,
         .baseArrayLayer = 0,
         .layerCount = render_info->viewMask ? 1 : render_info->layerCount,
      };

      panvk_per_arch(CmdClearAttachments)(panvk_cmd_buffer_to_handle(cmdbuf),
                                          clear_att_count, clear_atts, 1,
                                          &clear_rect);
   }
}

void
panvk_per_arch(cmd_preload_render_area_border)(
   struct panvk_cmd_buffer *cmdbuf, const VkRenderingInfo *render_info)
{
   const unsigned meta_tile_size = pan_meta_tile_size(PAN_ARCH);
   struct panvk_cmd_graphics_state *state = &cmdbuf->state.gfx;
   struct pan_fb_info *fbinfo = &state->render.fb.info;

   bool render_area_is_aligned =
      ((fbinfo->draw_extent.minx | fbinfo->draw_extent.miny) %
       meta_tile_size) == 0 &&
      (fbinfo->draw_extent.maxx + 1 == fbinfo->width ||
       (fbinfo->draw_extent.maxx % meta_tile_size) == (meta_tile_size - 1)) &&
      (fbinfo->draw_extent.maxy + 1 == fbinfo->height ||
       (fbinfo->draw_extent.maxy % meta_tile_size) == (meta_tile_size - 1));

   /* If the render area is aligned on the meta tile size, we're good. */
   if (!render_area_is_aligned)
      panvk_per_arch(cmd_force_fb_preload)(cmdbuf, render_info);
}

static void
prepare_iam_sysvals(struct panvk_cmd_buffer *cmdbuf, BITSET_WORD *dirty_sysvals)
{
   const struct vk_input_attachment_location_state *ial =
      &cmdbuf->vk.dynamic_graphics_state.ial;
   struct panvk_input_attachment_info iam[INPUT_ATTACHMENT_MAP_SIZE];
   uint32_t catt_count =
      ial->color_attachment_count == MESA_VK_COLOR_ATTACHMENT_COUNT_UNKNOWN
         ? MAX_RTS
         : ial->color_attachment_count;

   memset(iam, ~0, sizeof(iam));

   assert(catt_count <= MAX_RTS);

   for (uint32_t i = 0; i < catt_count; i++) {
      if (ial->color_map[i] == MESA_VK_ATTACHMENT_UNUSED ||
          !(cmdbuf->state.gfx.render.bound_attachments &
            MESA_VK_RP_ATTACHMENT_COLOR_BIT(i)))
         continue;

      VkFormat fmt = cmdbuf->state.gfx.render.color_attachments.fmts[i];
      enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
      struct mali_internal_conversion_packed conv;
      uint32_t ia_idx = ial->color_map[i] + 1;
      assert(ia_idx < ARRAY_SIZE(iam));

      iam[ia_idx].target = PANVK_COLOR_ATTACHMENT(i);

      pan_pack(&conv, INTERNAL_CONVERSION, cfg) {
         cfg.memory_format =
            GENX(pan_dithered_format_from_pipe_format)(pfmt, false);
#if PAN_ARCH < 9
         cfg.register_format =
            vk_format_is_uint(fmt)   ? MALI_REGISTER_FILE_FORMAT_U32
            : vk_format_is_sint(fmt) ? MALI_REGISTER_FILE_FORMAT_I32
                                     : MALI_REGISTER_FILE_FORMAT_F32;
#endif
      }

      iam[ia_idx].conversion = conv.opaque[0];
   }

   if (ial->depth_att != MESA_VK_ATTACHMENT_UNUSED) {
      uint32_t ia_idx =
         ial->depth_att == MESA_VK_ATTACHMENT_NO_INDEX ? 0 : ial->depth_att + 1;

      assert(ia_idx < ARRAY_SIZE(iam));
      iam[ia_idx].target = PANVK_ZS_ATTACHMENT;

#if PAN_ARCH < 9
      /* On v7, we need to pass the depth format around. If we use a conversion
       * of zero, like we do on v9+, the GPU reports an INVALID_INSTR_ENC. */
      VkFormat fmt = cmdbuf->state.gfx.render.z_attachment.fmt;
      enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
      struct mali_internal_conversion_packed conv;

      pan_pack(&conv, INTERNAL_CONVERSION, cfg) {
         cfg.register_format = MALI_REGISTER_FILE_FORMAT_F32;
         cfg.memory_format =
            GENX(pan_dithered_format_from_pipe_format)(pfmt, false);
      }
      iam[ia_idx].conversion = conv.opaque[0];
#endif
   }

   if (ial->stencil_att != MESA_VK_ATTACHMENT_UNUSED) {
      uint32_t ia_idx =
         ial->stencil_att == MESA_VK_ATTACHMENT_NO_INDEX ? 0 : ial->stencil_att + 1;

      assert(ia_idx < ARRAY_SIZE(iam));
      iam[ia_idx].target = PANVK_ZS_ATTACHMENT;
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(iam); i++)
      set_gfx_sysval(cmdbuf, dirty_sysvals, iam[i], iam[i]);
}

/* This value has been selected to get
 * dEQP-VK.draw.renderpass.inverted_depth_ranges.nodepthclamp_deltazero passing.
 */
#define MIN_DEPTH_CLIP_RANGE 37.7E-06f

void
panvk_per_arch(cmd_prepare_draw_sysvals)(struct panvk_cmd_buffer *cmdbuf,
                                         const struct panvk_draw_info *info)
{
   struct vk_color_blend_state *cb = &cmdbuf->vk.dynamic_graphics_state.cb;
   const struct panvk_shader_variant *fs =
      panvk_shader_only_variant(get_fs(cmdbuf));
   uint32_t noperspective_varyings = fs ? fs->info.varyings.noperspective : 0;
   BITSET_DECLARE(dirty_sysvals, MAX_SYSVAL_FAUS) = {0};

   set_gfx_sysval(cmdbuf, dirty_sysvals, vs.noperspective_varyings,
                  noperspective_varyings);
   set_gfx_sysval(cmdbuf, dirty_sysvals, vs.first_vertex, info->vertex.base);
   set_gfx_sysval(cmdbuf, dirty_sysvals, vs.base_instance, info->instance.base);

#if PAN_ARCH < 9
   set_gfx_sysval(cmdbuf, dirty_sysvals, vs.raw_vertex_offset,
                  info->vertex.raw_offset);
   set_gfx_sysval(cmdbuf, dirty_sysvals, layer_id, info->layer_id);
#endif

   if (dyn_gfx_state_dirty(cmdbuf, CB_BLEND_CONSTANTS)) {
      for (unsigned i = 0; i < ARRAY_SIZE(cb->blend_constants); i++) {
         set_gfx_sysval(cmdbuf, dirty_sysvals, blend.constants[i],
                        cb->blend_constants[i]);
      }
   }

   if (dyn_gfx_state_dirty(cmdbuf, VP_VIEWPORTS) ||
       dyn_gfx_state_dirty(cmdbuf, VP_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLIP_ENABLE) ||
       dyn_gfx_state_dirty(cmdbuf, RS_DEPTH_CLAMP_ENABLE)) {
      const struct vk_rasterization_state *rs =
         &cmdbuf->vk.dynamic_graphics_state.rs;
      const struct vk_viewport_state *vp =
         &cmdbuf->vk.dynamic_graphics_state.vp;
      const VkViewport *viewport = &vp->viewports[0];

      /* Doing the viewport transform in the vertex shader and then depth
       * clipping with the viewport depth range gets a similar result to
       * clipping in clip-space, but loses precision when the viewport depth
       * range is very small. When minDepth == maxDepth, this completely
       * flattens the clip-space depth and results in never clipping.
       *
       * To work around this, set a lower limit on depth range when clipping is
       * enabled. This results in slightly incorrect fragment depth values, and
       * doesn't help with the precision loss, but at least clipping isn't
       * completely broken.
       */
      float z_min = viewport->minDepth;
      float z_max = viewport->maxDepth;
      if (vk_rasterization_state_depth_clip_enable(rs) &&
          fabsf(z_max - z_min) < MIN_DEPTH_CLIP_RANGE) {
         float z_sign = z_min <= z_max ? 1.0f : -1.0f;

         float z_center = 0.5f * (z_max + z_min);
         /* Bump offset off-center if necessary, to not go out of range */
         z_center = CLAMP(z_center, 0.5f * MIN_DEPTH_CLIP_RANGE,
                          1.0f - 0.5f * MIN_DEPTH_CLIP_RANGE);

         z_min = z_center - 0.5f * z_sign * MIN_DEPTH_CLIP_RANGE;
         z_max = z_center + 0.5f * z_sign * MIN_DEPTH_CLIP_RANGE;
      }

      /* Upload the viewport scale. Defined as (px/2, py/2, pz) at the start of
       * section 24.5 ("Controlling the Viewport") of the Vulkan spec. At the
       * end of the section, the spec defines:
       *
       * px = width
       * py = height
       * pz = maxDepth - minDepth         if negativeOneToOne is false
       * pz = (maxDepth - minDepth) / 2   if negativeOneToOne is true
       */
      set_gfx_sysval(cmdbuf, dirty_sysvals, viewport.scale.x,
                     0.5f * viewport->width);
      set_gfx_sysval(cmdbuf, dirty_sysvals, viewport.scale.y,
                     0.5f * viewport->height);
      set_gfx_sysval(cmdbuf, dirty_sysvals, viewport.scale.z,
                     vp->depth_clip_negative_one_to_one ?
                        0.5f * (z_max - z_min) : z_max - z_min);

      /* Upload the viewport offset. Defined as (ox, oy, oz) at the start of
       * section 24.5 ("Controlling the Viewport") of the Vulkan spec. At the
       * end of the section, the spec defines:
       *
       * ox = x + width/2
       * oy = y + height/2
       * oz = minDepth                    if negativeOneToOne is false
       * oz = (maxDepth + minDepth) / 2   if negativeOneToOne is true
       */
      set_gfx_sysval(cmdbuf, dirty_sysvals, viewport.offset.x,
                     (0.5f * viewport->width) + viewport->x);
      set_gfx_sysval(cmdbuf, dirty_sysvals, viewport.offset.y,
                     (0.5f * viewport->height) + viewport->y);
      set_gfx_sysval(cmdbuf, dirty_sysvals, viewport.offset.z,
                     vp->depth_clip_negative_one_to_one ?
                        0.5f * (z_min + z_max) : z_min);

   }

   if (dyn_gfx_state_dirty(cmdbuf, INPUT_ATTACHMENT_MAP))
      prepare_iam_sysvals(cmdbuf, dirty_sysvals);

   const struct panvk_shader_variant *vs =
      panvk_shader_hw_variant(cmdbuf->state.gfx.vs.shader);

#if PAN_ARCH < 9
   struct panvk_descriptor_state *desc_state = &cmdbuf->state.gfx.desc_state;
   struct panvk_shader_desc_state *vs_desc_state = &cmdbuf->state.gfx.vs.desc;
   struct panvk_shader_desc_state *fs_desc_state = &cmdbuf->state.gfx.fs.desc;

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, VS)) {
      set_gfx_sysval(cmdbuf, dirty_sysvals,
                     desc.sets[PANVK_DESC_TABLE_VS_DYN_SSBOS],
                     vs_desc_state->dyn_ssbos);
   }

   if (gfx_state_dirty(cmdbuf, DESC_STATE) || gfx_state_dirty(cmdbuf, FS)) {
      set_gfx_sysval(cmdbuf, dirty_sysvals,
                     desc.sets[PANVK_DESC_TABLE_FS_DYN_SSBOS],
                     fs_desc_state->dyn_ssbos);
   }

   for (uint32_t i = 0; i < MAX_SETS; i++) {
      uint32_t used_set_mask =
         vs->desc_info.used_set_mask | (fs ? fs->desc_info.used_set_mask : 0);

      if (used_set_mask & BITFIELD_BIT(i)) {
         set_gfx_sysval(cmdbuf, dirty_sysvals, desc.sets[i],
                        desc_state->sets[i]->descs.dev);
      }
   }
#endif

   /* We mask the dirty sysvals by the shader usage, and only flag
    * the push uniforms dirty if those intersect. */
   BITSET_DECLARE(dirty_shader_sysvals, MAX_SYSVAL_FAUS);
   BITSET_AND(dirty_shader_sysvals, dirty_sysvals, vs->fau.used_sysvals);
   if (!BITSET_IS_EMPTY(dirty_shader_sysvals))
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

   if (fs) {
      BITSET_AND(dirty_shader_sysvals, dirty_sysvals, fs->fau.used_sysvals);

      /* If blend constants are not read by the blend shader, we can consider
       * they are not read at all, so clear the dirty bits to avoid re-emitting
       * FAUs when we can. */
      if (!cmdbuf->state.gfx.cb.info.shader_loads_blend_const)
         BITSET_CLEAR_RANGE(dirty_shader_sysvals, 0, 3);

      if (!BITSET_IS_EMPTY(dirty_shader_sysvals))
         gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindVertexBuffers2)(VkCommandBuffer commandBuffer,
                                      uint32_t firstBinding,
                                      uint32_t bindingCount,
                                      const VkBuffer *pBuffers,
                                      const VkDeviceSize *pOffsets,
                                      const VkDeviceSize *pSizes,
                                      const VkDeviceSize *pStrides)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   assert(firstBinding + bindingCount <= MAX_VBS);

   if (pStrides) {
      vk_cmd_set_vertex_binding_strides(&cmdbuf->vk, firstBinding,
                                        bindingCount, pStrides);
   }

   for (uint32_t i = 0; i < bindingCount; i++) {
      VK_FROM_HANDLE(panvk_buffer, buffer, pBuffers[i]);

      if (buffer) {
         cmdbuf->state.gfx.vb.bufs[firstBinding + i].address =
            panvk_buffer_gpu_ptr(buffer, pOffsets[i]);
         cmdbuf->state.gfx.vb.bufs[firstBinding + i].size = panvk_buffer_range(
            buffer, pOffsets[i], pSizes ? pSizes[i] : VK_WHOLE_SIZE);
      } else {
         cmdbuf->state.gfx.vb.bufs[firstBinding + i].address = 0;
         cmdbuf->state.gfx.vb.bufs[firstBinding + i].size = 0;
      }
   }

   cmdbuf->state.gfx.vb.count =
      MAX2(cmdbuf->state.gfx.vb.count, firstBinding + bindingCount);
   gfx_state_set_dirty(cmdbuf, VB);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBindIndexBuffer2)(VkCommandBuffer commandBuffer,
                                    VkBuffer buffer, VkDeviceSize offset,
                                    VkDeviceSize size, VkIndexType indexType)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_buffer, buf, buffer);

   if (buf) {
      cmdbuf->state.gfx.ib.size = panvk_buffer_range(buf, offset, size);
      assert(cmdbuf->state.gfx.ib.size <= UINT32_MAX);
      cmdbuf->state.gfx.ib.dev_addr = panvk_buffer_gpu_ptr(buf, offset);
   } else {
      cmdbuf->state.gfx.ib.size = 0;
      /* In case of NullDescriptors, we need to set a non-NULL address and rely
       * on out-of-bounds behavior against the zero size of the buffer. Note
       * that this only works for v10+, as v9 does not have a way to specify the
       * index buffer size. */
      cmdbuf->state.gfx.ib.dev_addr = PAN_ARCH >= 10 ? 0x1000 : 0;
   }
   cmdbuf->state.gfx.ib.index_size = vk_index_type_to_bytes(indexType);

   gfx_state_set_dirty(cmdbuf, IB);
}
