/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "vl_compositor_proc.h"
#include "vl_compositor.h"
#include "vl_video_buffer.h"

struct vl_compositor_proc {
   struct pipe_video_codec b;

   struct vl_compositor compositor;
   struct vl_compositor_state cstate;
   struct pipe_video_buffer *target;
};

static void
compositor_proc_destroy(struct pipe_video_codec *codec)
{
   struct vl_compositor_proc *proc = (struct vl_compositor_proc *)codec;

   vl_compositor_cleanup_state(&proc->cstate);
   vl_compositor_cleanup(&proc->compositor);
   free(proc);
}

static void
compositor_proc_begin_frame(struct pipe_video_codec *codec,
                            struct pipe_video_buffer *target,
                            struct pipe_picture_desc *picture)
{
   struct vl_compositor_proc *proc = (struct vl_compositor_proc *)codec;

   proc->target = target;
}

static int
compositor_proc_process_frame(struct pipe_video_codec *codec,
                              struct pipe_video_buffer *src,
                              const struct pipe_vpp_desc *process_properties)
{
   struct vl_compositor_proc *proc = (struct vl_compositor_proc *)codec;
   struct pipe_surface *surfaces;
   enum vl_compositor_rotation rotation;
   enum vl_compositor_mirror mirror;
   struct pipe_video_buffer *dst = proc->target;
   struct pipe_vpp_desc *param = (struct pipe_vpp_desc *)process_properties;
   enum vl_compositor_deinterlace deinterlace = VL_COMPOSITOR_NONE;
   bool src_yuv = util_format_is_yuv(src->buffer_format);
   bool dst_yuv = util_format_is_yuv(dst->buffer_format);

   /* Subsampled formats not supported */
   if (util_format_is_subsampled_422(dst->buffer_format))
      return 1;

   surfaces = dst->get_surfaces(dst);
   if (!surfaces[0].texture)
      return 1;

   if (util_format_get_nr_components(src->buffer_format) == 1) {
      /* Identity */
      vl_csc_get_rgbyuv_matrix(PIPE_VIDEO_VPP_MCF_RGB, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &proc->cstate.yuv2rgb);
      vl_csc_get_rgbyuv_matrix(PIPE_VIDEO_VPP_MCF_RGB, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &proc->cstate.rgb2yuv);
   } else if (src_yuv == dst_yuv) {
      if (!src_yuv) {
         /* RGB to RGB */
         vl_csc_get_rgbyuv_matrix(PIPE_VIDEO_VPP_MCF_RGB, src->buffer_format, dst->buffer_format,
                                  param->in_color_range, param->out_color_range, &proc->cstate.yuv2rgb);
         vl_csc_get_rgbyuv_matrix(PIPE_VIDEO_VPP_MCF_RGB, src->buffer_format, dst->buffer_format,
                                  param->in_color_range, param->out_color_range, &proc->cstate.csc_matrix);
      } else {
         /* YUV to YUV (convert to RGB for transfer function and primaries) */
         enum pipe_format rgb_format = util_format_get_plane_format(src->buffer_format, 0);
         assert(!util_format_is_yuv(rgb_format));
         vl_csc_get_rgbyuv_matrix(param->in_matrix_coefficients, src->buffer_format, rgb_format,
                                  param->in_color_range, PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL,
                                  &proc->cstate.yuv2rgb);
         vl_csc_get_rgbyuv_matrix(param->out_matrix_coefficients, rgb_format, dst->buffer_format,
                                  PIPE_VIDEO_VPP_CHROMA_COLOR_RANGE_FULL, param->out_color_range,
                                  &proc->cstate.rgb2yuv);
      }
   } else if (src_yuv) {
      /* YUV to RGB */
      vl_csc_get_rgbyuv_matrix(param->in_matrix_coefficients, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &proc->cstate.yuv2rgb);
      vl_csc_get_rgbyuv_matrix(param->in_matrix_coefficients, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &proc->cstate.csc_matrix);
   } else {
      /* RGB to YUV */
      vl_csc_get_rgbyuv_matrix(param->out_matrix_coefficients, src->buffer_format, dst->buffer_format,
                               param->in_color_range, param->out_color_range, &proc->cstate.rgb2yuv);
   }

   vl_csc_get_primaries_matrix(param->in_color_primaries, param->out_color_primaries,
                               &proc->cstate.primaries);

   proc->cstate.chroma_location = VL_COMPOSITOR_LOCATION_NONE;
   proc->cstate.in_transfer_characteristic = param->in_transfer_characteristics;
   proc->cstate.out_transfer_characteristic = param->out_transfer_characteristics;

   if (src_yuv || dst_yuv) {
      enum pipe_format format = src_yuv ? src->buffer_format : dst->buffer_format;
      enum pipe_video_vpp_chroma_siting chroma_siting =
         src_yuv ? param->in_chroma_siting : param->out_chroma_siting;

      if (util_format_get_plane_height(format, 1, 4) != 4) {
         if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_TOP)
            proc->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_VERTICAL_TOP;
         else if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_VERTICAL_BOTTOM)
            proc->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_VERTICAL_BOTTOM;
         else
            proc->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_VERTICAL_CENTER;
      }

      if (util_format_is_subsampled_422(format) ||
          util_format_get_plane_width(format, 1, 4) != 4) {
         if (chroma_siting & PIPE_VIDEO_VPP_CHROMA_SITING_HORIZONTAL_CENTER)
            proc->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_HORIZONTAL_CENTER;
         else
            proc->cstate.chroma_location |= VL_COMPOSITOR_LOCATION_HORIZONTAL_LEFT;
      }
   }

   if (param->orientation & PIPE_VIDEO_VPP_ROTATION_90)
      rotation = VL_COMPOSITOR_ROTATE_90;
   else if (param->orientation & PIPE_VIDEO_VPP_ROTATION_180)
      rotation = VL_COMPOSITOR_ROTATE_180;
   else if (param->orientation & PIPE_VIDEO_VPP_ROTATION_270)
      rotation = VL_COMPOSITOR_ROTATE_270;
   else
      rotation = VL_COMPOSITOR_ROTATE_0;

   if (param->orientation & PIPE_VIDEO_VPP_FLIP_VERTICAL)
      mirror = VL_COMPOSITOR_MIRROR_VERTICAL;
   else if (param->orientation & PIPE_VIDEO_VPP_FLIP_HORIZONTAL)
      mirror = VL_COMPOSITOR_MIRROR_HORIZONTAL;
   else
      mirror = VL_COMPOSITOR_MIRROR_NONE;

   vl_compositor_clear_layers(&proc->cstate);

   vl_compositor_set_layer_rotation(&proc->cstate, 0, rotation);
   vl_compositor_set_layer_mirror(&proc->cstate, 0, mirror);

   proc->cstate.layers[0].blend_enabled = param->blend.enabled;
   proc->cstate.layers[0].blend_mode = param->blend.mode;
   proc->cstate.layers[0].blend_alpha = param->blend.global_alpha;

   if (src->interlaced && !dst->interlaced)
      deinterlace = VL_COMPOSITOR_WEAVE;

   if (dst_yuv) {
      if (src_yuv) {
         /* YUV -> YUV */
         if (src->interlaced == dst->interlaced)
            deinterlace = VL_COMPOSITOR_NONE;
         vl_compositor_yuv_deint_full(&proc->cstate, &proc->compositor,
                                      src, dst, &param->src_region, &param->dst_region,
                                      deinterlace);
      } else {
         /* RGB -> YUV */
         struct pipe_resource *resources[VL_NUM_COMPONENTS];
         src->get_resources(src, resources);
         vl_compositor_convert_rgb_to_yuv(&proc->cstate, &proc->compositor, 0, resources[0],
                                          dst, &param->src_region, &param->dst_region);
      }
   } else {
      /* YUV/RGB -> RGB */
      vl_compositor_set_buffer_layer(&proc->cstate, &proc->compositor, 0, src,
                                     &param->src_region, NULL, deinterlace);
      vl_compositor_set_layer_dst_area(&proc->cstate, 0, &param->dst_region);
      vl_compositor_render(&proc->cstate, &proc->compositor, &surfaces[0], NULL, false);
   }

   return 0;
}

static int
compositor_proc_end_frame(struct pipe_video_codec *codec,
                          struct pipe_video_buffer *target,
                          struct pipe_picture_desc *picture)
{
   struct vl_compositor_proc *proc = (struct vl_compositor_proc *)codec;

   proc->b.context->flush(proc->b.context, picture->out_pipe_fence, picture->flush_flags);

   return 0;
}

struct pipe_video_codec *
vl_compositor_create_proc(struct pipe_context *context, bool compute_only)
{
   struct vl_compositor_proc *proc = calloc(1, sizeof(*proc));
   if (!proc)
      return NULL;

   if (!vl_compositor_init(&proc->compositor, context, compute_only))
      goto error_compositor;
   if (!vl_compositor_init_state(&proc->cstate, context))
      goto error_compositor_state;

   proc->b.context = context;
   proc->b.destroy = compositor_proc_destroy;
   proc->b.begin_frame = compositor_proc_begin_frame;
   proc->b.process_frame = compositor_proc_process_frame;
   proc->b.end_frame = compositor_proc_end_frame;
   return &proc->b;

error_compositor_state:
   vl_compositor_cleanup(&proc->compositor);
error_compositor:
   free(proc);
   return NULL;
}
