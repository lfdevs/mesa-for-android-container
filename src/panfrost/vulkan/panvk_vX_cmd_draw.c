/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device_memory.h"
#include "panvk_entrypoints.h"

#include "pan_desc.h"
#include "pan_util.h"

static enum pan_fb_load_op
get_att_fb_load_op(const VkRenderingAttachmentInfo *att)
{
   switch (att->loadOp) {
   case VK_ATTACHMENT_LOAD_OP_CLEAR:
      return PAN_FB_LOAD_CLEAR;
   case VK_ATTACHMENT_LOAD_OP_LOAD:
      return PAN_FB_LOAD_IMAGE;
   case VK_ATTACHMENT_LOAD_OP_NONE:
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
       * TODO: disable preload when we have clean_tile_write_enable = false
       * as an optimization
       */
      if (att->storeOp == VK_ATTACHMENT_STORE_OP_NONE)
         return PAN_FB_LOAD_IMAGE;
      else
         return PAN_FB_LOAD_NONE;
   default:
      UNREACHABLE("Unsupported loadOp");
   }
}

static enum pan_fb_msaa_copy_op
vk_to_pan_fb_resolve_mode(VkResolveModeFlagBits resolveMode)
{
   switch (resolveMode) {
   case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT:  return PAN_FB_MSAA_COPY_SAMPLE_0;
   case VK_RESOLVE_MODE_AVERAGE_BIT:      return PAN_FB_MSAA_COPY_AVERAGE;
   case VK_RESOLVE_MODE_MIN_BIT:          return PAN_FB_MSAA_COPY_MIN;
   case VK_RESOLVE_MODE_MAX_BIT:          return PAN_FB_MSAA_COPY_MAX;
   default: UNREACHABLE("Unsupported resolveMode");
   }
}

static struct panvk_image_view *
get_ms2ss_image_view(struct panvk_image_view *iview, uint32_t nr_samples)
{
   assert(nr_samples >= 2 && nr_samples <= 16);
   assert(iview->pview.nr_samples == 1);
   assert(iview->vk.image->create_flags &
          VK_IMAGE_CREATE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_BIT_EXT);

   /* sample count 2 is at index 0, 4 at 1, .. */
   uint32_t vidx = 0;
   switch (nr_samples) {
   case VK_SAMPLE_COUNT_2_BIT:
      vidx = 0;
      break;
   case VK_SAMPLE_COUNT_4_BIT:
      vidx = 1;
      break;
   case VK_SAMPLE_COUNT_8_BIT:
      vidx = 2;
      break;
   case VK_SAMPLE_COUNT_16_BIT:
      vidx = 3;
      break;
   default:
      UNREACHABLE("unhandled sample count");
   }
   assert(iview->ms_views[vidx] != VK_NULL_HANDLE);

   struct panvk_image_view *res =
      panvk_image_view_from_handle(iview->ms_views[vidx]);

   assert(res->pview.nr_samples == nr_samples);

   return res;
}

static bool
avoid_direct_resolve_to(const struct pan_image *img)
{
   /* There is an issue with AFBC and small tiles where, if the tile size is
    * not a multiple of superblock size then writes and reads may race within
    * a superblock.  On v7+, there's a hardware bit to help us work around
    * this but it doesn't exist on v6 and earlier.
    *
    * This is particularly likely to happen with MSAA since the sample count
    * is a multiplier on the color allocation, making it much more likely that
    * we'll hit this case.  MSAA images are safe because they don't allow AFBC
    * but we have a real problem if we attempt to resolve directly to an
    * AFBC-compressed single-sampled image.  Skip the resolve optimization in
    * this case.
    *
    * TODO: If we moved this decision later, it could be based on the final
    * framebuffer layout and we could potentially allow direct resolves in
    * more cases.
    */
   return PAN_ARCH < 7 && drm_is_afbc(img->props.modifier);
}

static void
render_state_set_color_attachment(struct panvk_cmd_buffer *cmdbuf,
                                  const VkRenderingAttachmentInfo *att,
                                  uint32_t index)
{
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   VK_FROM_HANDLE(panvk_image_view, iview, att->imageView);

   struct panvk_image_view *iview_ss = NULL;
   const bool ms2ss = render->fb.nr_samples > 1 &&
                      iview->pview.nr_samples == 1;

   if (ms2ss) {
      iview_ss = iview;
      iview = get_ms2ss_image_view(iview, render->fb.nr_samples);
   }

   struct panvk_image *img =
      container_of(iview->vk.image, struct panvk_image, vk);

   render->bound_attachments |= MESA_VK_RP_ATTACHMENT_COLOR_BIT(index);
   render->color_attachments.iviews[index] = iview;
   render->color_attachments.preload_iviews[index] =
      ms2ss ? iview_ss : NULL;
   render->color_attachments.fmts[index] = iview->vk.format;
   render->color_attachments.samples[index] = img->vk.samples;

#if PAN_ARCH < 9
   for (uint8_t p = 0; p < ARRAY_SIZE(iview->pview.planes); p++) {
      struct pan_image_plane_ref pref =
         pan_image_view_get_plane(&iview->pview, p);

      if (!pref.image)
         continue;

      assert(pref.plane_idx < ARRAY_SIZE(img->planes));
      assert(img->planes[pref.plane_idx].mem->bo != NULL);
      render->fb.bos[render->fb.bo_count++] =
         img->planes[pref.plane_idx].mem->bo;
   }
#endif

   render->fb.layout.rt_formats[index] = iview->pview.format;
   render->fb.nr_samples =
      MAX2(render->fb.nr_samples,
           pan_image_view_get_nr_samples(&iview->pview));

   render->fb.load.rts[index] = (struct pan_fb_load_target) {
      .in_bounds_load = get_att_fb_load_op(att),
      .border_load = PAN_FB_LOAD_IMAGE,
      .always = att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR,
      .msaa = ms2ss ? PAN_FB_MSAA_COPY_SINGLE
                    : PAN_FB_MSAA_COPY_ALL,
      .iview = ms2ss ? &iview_ss->pview : &iview->pview,
      .clear.color.ui = {
         att->clearValue.color.uint32[0],
         att->clearValue.color.uint32[1],
         att->clearValue.color.uint32[2],
         att->clearValue.color.uint32[3],
      },
   };
   render->fb.spill.load.rts[index] = pan_fb_load_iview(&iview->pview);
   render->fb.spill.store.rts[index] = pan_fb_store_iview(&iview->pview);
   if (att->storeOp == VK_ATTACHMENT_STORE_OP_STORE && !ms2ss)
      render->fb.store.rts[index] = pan_fb_store_iview(&iview->pview);

   if (att->resolveMode != VK_RESOLVE_MODE_NONE) {
      VK_FROM_HANDLE(panvk_image_view, resolve_iview, att->resolveImageView);

      /* VUID-VkRenderingAttachmentInfo-imageView-06862 and
       * VUID-VkRenderingAttachmentInfo-imageView-06863:
       * If resolveMode != NONE, then
       * resolveView == NULL iff. multisampledRenderToSingleSampledEnable */
      assert(ms2ss == (resolve_iview == NULL));

      const struct panvk_resolve_attachment resolve = {
         .dst_iview = ms2ss ? iview_ss : resolve_iview,
         .mode = att->resolveMode,
      };
      assert(resolve.dst_iview != NULL);
      assert(resolve.dst_iview->pview.nr_samples == 1);

      const struct pan_image *resolve_pimage =
         pan_image_view_get_color_plane(&resolve.dst_iview->pview).image;

      if ((ms2ss || att->storeOp != VK_ATTACHMENT_STORE_OP_STORE) &&
          !avoid_direct_resolve_to(resolve_pimage)) {
         render->fb.resolve.rts[index] = (struct pan_fb_resolve_target) {
            .in_bounds = {
               .resolve = PAN_FB_RESOLVE_RT(index),
               .msaa = vk_to_pan_fb_resolve_mode(att->resolveMode),
            },
            .border = {
               .resolve = PAN_FB_RESOLVE_IMAGE,
               .msaa = PAN_FB_MSAA_COPY_SINGLE,
            },
            .iview = &resolve.dst_iview->pview,
         };
         render->fb.store.rts[index] =
            pan_fb_always_store_iview_s0(&resolve.dst_iview->pview);
      } else {
         /* We need to store so we can do the MSAA resolve later */
         render->fb.store.rts[index] = pan_fb_store_iview(&iview->pview);
         render->color_attachments.resolve[index] = resolve;
      }
   }
}

static struct pan_image_view
get_zs_pan_image_view_aspects(struct panvk_image_view *iview,
                              VkImageAspectFlags aspects)
{
   struct panvk_image *img =
      container_of(iview->vk.image, struct panvk_image, vk);
   unsigned plane_idx = panvk_plane_index(img, aspects);

   /* From the Vulkan 1.4.335 spec:
    *
    *    "The aspectMask of any image view specified for pDepthAttachment
    *    or pStencilAttachment is ignored. Instead, depth attachments are
    *    automatically treated as if VK_IMAGE_ASPECT_DEPTH_BIT was
    *    specified for their aspect masks"
    *
    * We need to re-create the image view more-or-less from scratch.  The old
    * image view might not have the right effective format or point to the
    * right planes.
    */

   struct pan_image_view pview = iview->pview;
   if (panvk_image_is_planar_depth_stencil(img)) {
      assert(util_bitcount(aspects) == 1);
      pview.format = aspects == VK_IMAGE_ASPECT_STENCIL_BIT
                     ? panvk_image_stencil_only_pfmt(img)
                     : panvk_image_depth_only_pfmt(img);
   } else {
      assert(img->plane_count == 1 && plane_idx == 0);
      pview.format = img->planes[0].image.props.format;
   }

   memset(pview.planes, 0, sizeof(pview.planes));
   pview.planes[0] = (struct pan_image_plane_ref) {
      .image = &img->planes[plane_idx].image,
      .plane_idx = 0,
   };

   return pview;
}

static struct pan_image_view
get_z_pan_image_view(struct panvk_image_view *iview)
{
   return get_zs_pan_image_view_aspects(iview, VK_IMAGE_ASPECT_DEPTH_BIT);
}

static struct pan_image_view
get_s_pan_image_view(struct panvk_image_view *iview)
{
   return get_zs_pan_image_view_aspects(iview, VK_IMAGE_ASPECT_STENCIL_BIT);
}

static struct pan_image_view
get_zs_pan_image_view(struct panvk_image_view *iview)
{
   return get_zs_pan_image_view_aspects(iview, VK_IMAGE_ASPECT_DEPTH_BIT |
                                               VK_IMAGE_ASPECT_STENCIL_BIT);
}

static void
render_state_set_zs_attachments(struct panvk_cmd_buffer *cmdbuf,
                                const VkRenderingAttachmentInfo *z_att,
                                const VkRenderingAttachmentInfo *s_att)
{
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   struct panvk_image_view *z_iview = NULL, *z_iview_ss = NULL;
   struct panvk_image_view *s_iview = NULL, *s_iview_ss = NULL;
   const struct panvk_image *z_img = NULL, *s_img = NULL;
   bool z_ms2ss = false, s_ms2ss = false;

   /* First grab our images/views and set up the API-level stuff.  For
    * multisampled render to single sampled, this also includes fetching
    * the multisampled view.
    */

   if (z_att) {
      z_iview = panvk_image_view_from_handle(z_att->imageView);
      z_img = container_of(z_iview->vk.image, struct panvk_image, vk);

      z_ms2ss = render->fb.nr_samples > 1 && z_iview->pview.nr_samples == 1;
      if (z_ms2ss) {
         z_iview_ss = z_iview;
         z_iview = get_ms2ss_image_view(z_iview, render->fb.nr_samples);
      }

      render->bound_attachments |= MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
      render->z_attachment.iview = z_iview;
      render->z_attachment.preload_iview = z_ms2ss ? z_iview_ss : NULL;
      render->z_attachment.fmt = z_iview->vk.format;
      render->fb.nr_samples =
         MAX2(render->fb.nr_samples,
              pan_image_view_get_nr_samples(&z_iview->pview));

#if PAN_ARCH < 9
      /* Depth plane always comes first. */
      render->fb.bos[render->fb.bo_count++] = z_img->planes[0].mem->bo;
#endif
   }

   if (s_att) {
      s_iview = panvk_image_view_from_handle(s_att->imageView);
      s_img = container_of(s_iview->vk.image, struct panvk_image, vk);

      s_ms2ss = render->fb.nr_samples > 1 && s_iview->pview.nr_samples == 1;
      if (s_ms2ss) {
         s_iview_ss = s_iview;
         s_iview = get_ms2ss_image_view(s_iview, render->fb.nr_samples);
      }

      render->bound_attachments |= MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
      render->s_attachment.iview = s_iview;
      render->s_attachment.preload_iview = s_ms2ss ? s_iview_ss : NULL;
      render->s_attachment.fmt = s_iview->vk.format;
      render->fb.nr_samples =
         MAX2(render->fb.nr_samples,
              pan_image_view_get_nr_samples(&s_iview->pview));

#if PAN_ARCH < 9
      /* The stencil plane is always last. */
      render->fb.bos[render->fb.bo_count++] =
         s_img->planes[s_img->plane_count - 1].mem->bo;
#endif
   }

   const bool interleaved_zs =
      (z_img && panvk_image_is_interleaved_depth_stencil(z_img)) ||
      (s_img && panvk_image_is_interleaved_depth_stencil(s_img));

   /* When both depth and stencil are bound, they have to be effecitvly
    * the same image view.  This is a global Vulkan requirement but it
    * really only matters for us in the interleaved case.
    */
   if (interleaved_zs && z_img && s_img) {
      assert(z_img == s_img);
      assert(z_iview->pview.dim == s_iview->pview.dim);
      assert(z_iview->pview.first_level == s_iview->pview.first_level);
      assert(z_iview->pview.last_level == s_iview->pview.last_level);
      assert(z_iview->pview.first_layer_or_z_slice ==
             s_iview->pview.first_layer_or_z_slice);
      assert(z_iview->pview.last_layer_or_z_slice ==
             s_iview->pview.last_layer_or_z_slice);
      assert(z_iview->pview.nr_samples == s_iview->pview.nr_samples);
   }

   /* Set up the framebuffer formats for Z/S as well as framefubber loads or
    * clears, as needed.
    */

   if (z_att) {
      render->fb.layout.z_format = panvk_image_depth_only_pfmt(z_img);
      render->z_pview.load =
         get_z_pan_image_view(z_ms2ss ? z_iview_ss : z_iview);
      render->fb.load.z = (struct pan_fb_load_target) {
         .in_bounds_load = get_att_fb_load_op(z_att),
         .border_load = PAN_FB_LOAD_IMAGE,
         .always = z_att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR,
         .msaa = z_ms2ss ? PAN_FB_MSAA_COPY_SINGLE
                         : PAN_FB_MSAA_COPY_ALL,
         .iview = &render->z_pview.load,
         .clear.depth = z_att->clearValue.depthStencil.depth,
      };
   } else if (interleaved_zs) {
      /* If we have interleaved Z/S and depth isn't bound, we need to load
       * it anyway so it doesn't get stompped when we write out at the end.
       */
      render->fb.layout.z_format = panvk_image_depth_only_pfmt(s_img);
      render->fb.load.z = pan_fb_load_iview(&render->s_pview.load);
   }

   if (s_att) {
      render->fb.layout.s_format = panvk_image_stencil_only_pfmt(s_img);
      render->s_pview.load =
         get_s_pan_image_view(s_ms2ss ? s_iview_ss : s_iview);
      render->fb.load.s = (struct pan_fb_load_target) {
         .in_bounds_load = get_att_fb_load_op(s_att),
         .border_load = PAN_FB_LOAD_IMAGE,
         .always = s_att->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR,
         .msaa = s_ms2ss ? PAN_FB_MSAA_COPY_SINGLE
                         : PAN_FB_MSAA_COPY_ALL,
         .iview = &render->s_pview.load,
         .clear.stencil = s_att->clearValue.depthStencil.stencil,
      };
   } else if (interleaved_zs) {
      /* If we have interleaved Z/S and stencil isn't bound, we need to load
       * it anyway so it doesn't get stompped when we write out at the end.
       */
      render->fb.layout.s_format = panvk_image_stencil_only_pfmt(z_img);
      render->fb.load.s = pan_fb_load_iview(&render->z_pview.load);
   }

   /* Set up spill load/stores for incremental rendering */

   if (interleaved_zs) {
      /* Use the same image view for both spills */
      struct panvk_image_view *iview = z_iview ? z_iview : s_iview;
      render->z_pview.spill = get_zs_pan_image_view(iview);
      render->fb.spill.load.z = pan_fb_load_iview(&render->z_pview.spill);
      render->fb.spill.load.s = pan_fb_load_iview(&render->z_pview.spill);
      render->fb.spill.store.zs = pan_fb_store_iview(&render->z_pview.spill);
   } else {
      if (z_iview) {
         render->z_pview.spill = get_z_pan_image_view(z_iview);
         render->fb.spill.load.z = pan_fb_load_iview(&render->z_pview.spill);
         render->fb.spill.store.zs = pan_fb_store_iview(&render->z_pview.spill);
      }

      if (s_iview) {
         render->s_pview.spill = get_s_pan_image_view(s_iview);
         render->fb.spill.load.s = pan_fb_load_iview(&render->s_pview.spill);
         render->fb.spill.store.s = pan_fb_store_iview(&render->s_pview.spill);
      }
   }

   /* Set up the final render target write(s) at the end, as well as any
    * multisample resolves.
    */

   struct panvk_resolve_attachment z_resolve = { };
   if (z_att && z_att->resolveMode != VK_RESOLVE_MODE_NONE) {
      VK_FROM_HANDLE(panvk_image_view, resolve_iview, z_att->resolveImageView);
      assert(z_ms2ss == (resolve_iview == NULL));

      z_resolve = (struct panvk_resolve_attachment) {
         .dst_iview = z_ms2ss ? z_iview_ss : resolve_iview,
         .mode = z_att->resolveMode,
      };
      assert(z_resolve.dst_iview != NULL);
      assert(z_resolve.dst_iview->pview.nr_samples == 1);
   }

   struct panvk_resolve_attachment s_resolve = { };
   if (s_att && s_att->resolveMode != VK_RESOLVE_MODE_NONE) {
      VK_FROM_HANDLE(panvk_image_view, resolve_iview, s_att->resolveImageView);
      assert(s_ms2ss == (resolve_iview == NULL));

      s_resolve = (struct panvk_resolve_attachment) {
         .dst_iview = s_ms2ss ? s_iview_ss : resolve_iview,
         .mode = s_att->resolveMode,
      };
      assert(s_resolve.dst_iview != NULL);
      assert(s_resolve.dst_iview->pview.nr_samples == 1);
   }

   if (interleaved_zs) {
      /* Store both Z and S together */
      struct panvk_image_view *iview = z_iview ? z_iview : s_iview;
      render->z_pview.store = get_zs_pan_image_view(iview);
      render->fb.store.zs = pan_fb_store_iview(&render->z_pview.store);

      /* It's probably possible to make resolve shaders work with interleaved
       * Z/S but it's tricky at best.  For now, skip this optimization in the
       * interleaved case.
       */
      render->z_attachment.resolve = z_resolve;
      render->s_attachment.resolve = s_resolve;
   } else {
      if (z_iview) {
         render->z_pview.store = get_z_pan_image_view(z_iview);
         if (z_att->storeOp == VK_ATTACHMENT_STORE_OP_STORE && !z_ms2ss)
            render->fb.store.zs = pan_fb_store_iview(&render->z_pview.store);
      }

      if (s_iview) {
         render->s_pview.store = get_s_pan_image_view(s_iview);
         if (s_att->storeOp == VK_ATTACHMENT_STORE_OP_STORE && !s_ms2ss)
            render->fb.store.s = pan_fb_store_iview(&render->s_pview.store);
      }

      if (z_resolve.mode != VK_RESOLVE_MODE_NONE) {
         const struct pan_image *z_resolve_pimage =
            pan_image_view_get_zs_plane(&z_resolve.dst_iview->pview).image;

         if ((z_ms2ss || z_att->storeOp != VK_ATTACHMENT_STORE_OP_STORE) &&
             !avoid_direct_resolve_to(z_resolve_pimage)) {
            render->z_pview.resolve = get_z_pan_image_view(z_resolve.dst_iview);
            render->fb.resolve.z = (struct pan_fb_resolve_target) {
               .in_bounds = {
                  .resolve = PAN_FB_RESOLVE_Z,
                  .msaa = vk_to_pan_fb_resolve_mode(z_att->resolveMode),
               },
               .border = {
                  .resolve = PAN_FB_RESOLVE_IMAGE,
                  .msaa = PAN_FB_MSAA_COPY_SINGLE,
               },
               .iview = &render->z_pview.resolve,
            };
            render->fb.store.zs =
               pan_fb_always_store_iview_s0(&render->z_pview.resolve);
         } else {
            /* We need to store so we can do the MSAA resolve later */
            render->fb.store.zs = pan_fb_store_iview(&render->z_pview.store);
            render->z_attachment.resolve = z_resolve;
         }
      }

      if (s_resolve.mode != VK_RESOLVE_MODE_NONE) {
         const struct pan_image *s_resolve_pimage =
            pan_image_view_get_s_plane(&s_resolve.dst_iview->pview).image;

         if ((s_ms2ss || s_att->storeOp != VK_ATTACHMENT_STORE_OP_STORE) &&
             !avoid_direct_resolve_to(s_resolve_pimage)) {
            render->s_pview.resolve = get_s_pan_image_view(s_resolve.dst_iview);
            render->fb.resolve.s = (struct pan_fb_resolve_target) {
               .in_bounds = {
                  .resolve = PAN_FB_RESOLVE_S,
                  .msaa = vk_to_pan_fb_resolve_mode(s_att->resolveMode),
               },
               .border = {
                  .resolve = PAN_FB_RESOLVE_IMAGE,
                  .msaa = PAN_FB_MSAA_COPY_SINGLE,
               },
               .iview = &render->s_pview.resolve,
            };
            render->fb.store.s =
               pan_fb_always_store_iview_s0(&render->s_pview.resolve);
         } else {
            /* We need to store so we can do the MSAA resolve later */
            render->fb.store.s = pan_fb_store_iview(&render->s_pview.store);
            render->s_attachment.resolve = s_resolve;
         }
      }
   }
}

void
panvk_per_arch(cmd_init_render_state)(struct panvk_cmd_buffer *cmdbuf,
                                      const VkRenderingInfo *pRenderingInfo)
{
   struct panvk_physical_device *phys_dev =
         to_panvk_physical_device(cmdbuf->vk.base.device->physical);
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   uint32_t att_width = UINT32_MAX, att_height = UINT32_MAX;

   render->flags = pRenderingInfo->flags;

   BITSET_SET(cmdbuf->state.gfx.dirty, PANVK_CMD_GRAPHICS_DIRTY_RENDER_STATE);

   render->first_provoking_vertex = U_TRISTATE_UNSET;
#if PAN_ARCH >= 10
   render->maybe_set_tds_provoking_vertex = NULL;
   render->maybe_set_fbds_provoking_vertex = NULL;
#endif
   memset(&render->color_attachments, 0,
          sizeof(render->color_attachments));
   memset(&render->z_attachment, 0, sizeof(render->z_attachment));
   memset(&render->s_attachment, 0, sizeof(render->s_attachment));
   memset(&render->fb, 0, sizeof(render->fb));
   render->bound_attachments = 0;

   const VkMultisampledRenderToSingleSampledInfoEXT *ms2ss_info =
      vk_find_struct_const(pRenderingInfo,
                           MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_INFO_EXT);
   const bool ms2ss = ms2ss_info
                         ? ms2ss_info->multisampledRenderToSingleSampledEnable
                         : VK_FALSE;

   render->layer_count = pRenderingInfo->viewMask
                         ? util_last_bit(pRenderingInfo->viewMask)
                         : pRenderingInfo->layerCount;
   render->view_mask = pRenderingInfo->viewMask;
   render->fb.layout = (struct pan_fb_layout) {
      /* In case ms2ss is enabled, use the provided sample count.
       *
       * All attachments need to have sample count == 1 or the provided value.
       * But, if all attachments have 1, we would end up choosing the wrong
       * value if we don't set it here already.
       */
      .sample_count = 0,

      /* The hardware requires us to have at least one color target, even if
       * it's a dummy.
       */
      .rt_count = MAX2(pRenderingInfo->colorAttachmentCount, 1),

      .tile_rt_budget_B =
         pan_query_optimal_tib_size(PAN_ARCH, phys_dev->model),
      .tile_z_budget_B =
         pan_query_optimal_z_tib_size(PAN_ARCH, phys_dev->model),
   };
   /* In case ms2ss is enabled, use the provided sample count.
    * All attachments need to have sample count == 1 or the provided value.
    * But, if all attachments have 1, we would end up choosing the wrong value
    * if we don't set it here already. */
   render->fb.nr_samples = ms2ss ? ms2ss_info->rasterizationSamples : 1;

   assert(pRenderingInfo->colorAttachmentCount <= PAN_MAX_RTS);
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

   const VkRenderingAttachmentInfo *z_att = NULL, *s_att = NULL;
   if (pRenderingInfo->pDepthAttachment &&
       pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) {
      z_att = pRenderingInfo->pDepthAttachment;

      VK_FROM_HANDLE(panvk_image_view, iview, z_att->imageView);
      att_width = MIN2(iview->vk.extent.width, att_width);
      att_height = MIN2(iview->vk.extent.height, att_height);
   }

   if (pRenderingInfo->pStencilAttachment &&
       pRenderingInfo->pStencilAttachment->imageView != VK_NULL_HANDLE) {
      s_att = pRenderingInfo->pStencilAttachment;

      VK_FROM_HANDLE(panvk_image_view, iview, s_att->imageView);
      att_width = MIN2(iview->vk.extent.width, att_width);
      att_height = MIN2(iview->vk.extent.height, att_height);
   }

   if (z_att || s_att)
      render_state_set_zs_attachments(cmdbuf, z_att, s_att);

   const struct pan_fb_bbox ra_px =
      pan_fb_bbox_from_xywh(pRenderingInfo->renderArea.offset.x,
                            pRenderingInfo->renderArea.offset.y,
                            pRenderingInfo->renderArea.extent.width,
                            pRenderingInfo->renderArea.extent.height);

   if (render->bound_attachments) {
      render->fb.layout.width_px = att_width;
      render->fb.layout.height_px = att_height;
   } else {
      render->fb.layout.width_px = (uint32_t)ra_px.max_x + 1;
      render->fb.layout.height_px = (uint32_t)ra_px.max_y + 1;
   }
   assert(render->fb.layout.width_px > 0 &&
          render->fb.layout.height_px > 0);

   render->fb.layout.render_area_px = ra_px;
   render->fb.layout.tiling_area_px = ra_px;

   GENX(pan_align_fb_tiling_area)(&render->fb.layout, &render->fb.store);
   GENX(pan_align_fb_tiling_area)(&render->fb.layout, &render->fb.spill.store);

   /* Try to optimize and remove unnecessary resolves if we can */
   GENX(pan_fb_fold_resolve_into_store)(&render->fb.layout,
                                        &render->fb.resolve,
                                        &render->fb.store);

   const bool has_partial_tiles =
      pan_fb_has_partial_tiles(&render->fb.layout);
   if (!(pRenderingInfo->flags & VK_RENDERING_RESUMING_BIT) &&
       pan_fb_has_image_load(&render->fb.load, has_partial_tiles)) {
      /* Loads happen through the texture unit so, if we're going to do a
       * load, we need a barrier to ensure that the texture cache gets
       * invalidated prior to the load.
       */
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
   }
}

void
panvk_per_arch(cmd_select_tile_size)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_rendering_state *render = &cmdbuf->state.gfx.render;
   struct pan_fb_layout *fb = &render->fb.layout;

   /* In case we never emitted tiler/framebuffer descriptors, we emit the
    * current sample count and compute tile size */
   if (fb->sample_count == 0) {
      fb->sample_count = render->fb.nr_samples;

      GENX(pan_select_fb_tile_size)(fb);

#if PAN_ARCH != 6
      if (fb->tile_rt_alloc_B > fb->tile_rt_budget_B) {
         vk_perf(VK_LOG_OBJS(&cmdbuf->vk.base),
                 "Using too much tile-memory, disabling pipelining");
      }
#endif
   } else {
      /* In case we already emitted tiler/framebuffer descriptors, we ensure
       * that the sample count didn't change (this should never happen) */
      assert(fb->sample_count == render->fb.nr_samples);
   }
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

      bool dithered = cmdbuf->state.gfx.render.flags &
                      VK_RENDERING_ENABLE_LEGACY_DITHERING_BIT_EXT;

      pan_pack(&conv, INTERNAL_CONVERSION, cfg) {
         cfg.memory_format =
            GENX(pan_dithered_format_from_pipe_format)(pfmt, dithered);
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
                                         const struct panvk_draw_info *info,
                                         const struct panvk_shader_variant *fs)
{
   struct vk_color_blend_state *cb = &cmdbuf->vk.dynamic_graphics_state.cb;
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

   for (unsigned i = 0; i < MAX_RTS; i++) {
      set_gfx_sysval(cmdbuf, dirty_sysvals, fs.blend_descs[i],
                     cmdbuf->state.gfx.fs.blend_descs[i]);
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

   uint32_t used_set_mask = 0;
   used_set_mask |= cmdbuf->state.gfx.vs.shader->desc_info.used_set_mask;
   if (fs)
      used_set_mask |= cmdbuf->state.gfx.fs.shader->desc_info.used_set_mask;

   for (uint32_t i = 0; i < MAX_SETS; i++) {
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
         BITSET_CLEAR_COUNT(dirty_shader_sysvals, 0, 4);

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
}
