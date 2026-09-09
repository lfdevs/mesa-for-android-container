/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/u_sampler.h"

#include "vl_compositor_proc.h"
#include "vl_compositor.h"
#include "vl_video_buffer.h"

struct vl_compositor_proc {
   struct pipe_video_codec b;

   struct vl_compositor compositor;
   struct vl_compositor_state cstate;
   struct pipe_video_buffer *target;
};

static bool
tva_native_sample_test_enabled(void)
{
   const char *e = getenv("DMD_VA_NATIVE_SAMPLE_TEST");
   return e && (!strcmp(e, "1") || !strcmp(e, "true") || !strcmp(e, "on"));
}

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
   struct pipe_context *pipe = proc->b.context;
   struct pipe_vpp_desc *param = (struct pipe_vpp_desc *)process_properties;
   enum vl_compositor_deinterlace deinterlace = VL_COMPOSITOR_NONE;
   bool src_yuv = util_format_is_yuv(src->buffer_format);
   bool dst_yuv = util_format_is_yuv(dst->buffer_format);

   if (getenv("DMD_VA_PROBE"))
      fprintf(stderr, "tva-proc: enter pid=%d probe=%s dst_yuv=%d dst_fmt=%s "
              "cs=%d gfx=%d fs_rgba=%p cs_rgba=%p\n",
              (int)getpid(), getenv("DMD_VA_PROBE"), dst_yuv,
              util_format_short_name(dst->buffer_format),
              proc->compositor.pipe_cs_composit_supported,
              proc->compositor.pipe_gfx_supported,
              proc->compositor.fs_rgba, proc->compositor.cs_rgba);

   if (getenv("DMD_VA_PROBE")) {
      struct pipe_resource *src_resources[VL_NUM_COMPONENTS] = {0};
      struct pipe_surface *dst_surfaces = dst->get_surfaces(dst);
      src->get_resources(src, src_resources);
      fprintf(stderr, "tva-proc: process src=%s %ux%u dst=%s %ux%u src0=%p bind=%#x dst0=%p bind=%#x\n",
              util_format_short_name(src->buffer_format), src->width, src->height,
              util_format_short_name(dst->buffer_format), dst->width, dst->height,
              (void *)src_resources[0], src_resources[0] ? src_resources[0]->bind : 0,
              dst_surfaces ? (void *)dst_surfaces[0].texture : NULL,
              dst_surfaces && dst_surfaces[0].texture ? dst_surfaces[0].texture->bind : 0);
      fprintf(stderr, "tva-proc: regions src=%d,%d-%d,%d dst=%d,%d-%d,%d orient=%#x blend=%d/%#x alpha=%f colors=%d/%d/%d/%d/%d/%d\n",
              param->src_region.x0, param->src_region.y0,
              param->src_region.x1, param->src_region.y1,
              param->dst_region.x0, param->dst_region.y0,
              param->dst_region.x1, param->dst_region.y1,
              param->orientation, param->blend.enabled, param->blend.mode,
              param->blend.global_alpha, param->in_color_range,
              param->out_color_range, param->in_matrix_coefficients,
              param->out_matrix_coefficients, param->in_color_primaries,
              param->out_color_primaries);
      if (src_resources[0] && pipe->texture_map && pipe->texture_unmap) {
         struct pipe_box box = { .x = 0, .y = 0, .z = 0,
                                 .width = 8, .height = 1, .depth = 1 };
         struct pipe_transfer *transfer = NULL;
         uint8_t *map = pipe->texture_map(pipe, src_resources[0], 0,
                                           PIPE_MAP_READ, &box, &transfer);
         if (map && transfer) {
            fprintf(stderr, "tva-proc: src probe fmt=%s stride=%u bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    util_format_short_name(src_resources[0]->format), transfer->stride,
                    map[0], map[1], map[2], map[3], map[4], map[5], map[6], map[7]);
            pipe->texture_unmap(pipe, transfer);
         } else {
            fprintf(stderr, "tva-proc: src probe map failed res=%p\n",
                    (void *)src_resources[0]);
            if (transfer)
               pipe->texture_unmap(pipe, transfer);
         }
      }
      if (src_resources[0] && pipe->texture_map && pipe->texture_unmap) {
         struct pipe_box box = { .x = 960, .y = 540, .z = 0,
                                 .width = 1, .height = 1, .depth = 1 };
         struct pipe_transfer *transfer = NULL;
         uint8_t *map = pipe->texture_map(pipe, src_resources[0], 0,
                                           PIPE_MAP_READ, &box, &transfer);
         if (map && transfer) {
            fprintf(stderr, "tva-proc: src center fmt=%s stride=%u bytes=%02x %02x %02x %02x\n",
                    util_format_short_name(src_resources[0]->format), transfer->stride,
                    map[0], map[1], map[2], map[3]);
            pipe->texture_unmap(pipe, transfer);
         }
      }
      if (pipe->screen->resource_changed) {
         for (unsigned i = 0; i < VL_NUM_COMPONENTS; i++) {
            if (src_resources[i])
               pipe->screen->resource_changed(pipe->screen, src_resources[i]);
         }
         fprintf(stderr, "tva-proc: notified resource changes\n");
      }
   }

   /* Subsampled formats not supported */
   if (util_format_is_subsampled_422(dst->buffer_format))
      return 1;

   surfaces = dst->get_surfaces(dst);
   if (!surfaces[0].texture)
      return 1;

   /* Debug-only path used to distinguish imported-texture visibility from
    * YUV shader issues on KGSL-only systems. */
   if (getenv("DMD_VA_PROBE") && !dst_yuv &&
       tva_native_sample_test_enabled()) {
      struct pipe_resource *src_resources[VL_NUM_COMPONENTS] = {0};
      src->get_resources(src, src_resources);
      if (!src_resources[0])
         return 1;

      struct pipe_resource templ = *src_resources[0];
      const bool native_rgba = true;
      if (native_rgba)
         templ.format = PIPE_FORMAT_B8G8R8A8_UNORM;
      templ.bind = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_LINEAR;
      templ.usage = PIPE_USAGE_DEFAULT;
      templ.flags = 0;
      templ.next = NULL;
      struct pipe_resource *native = pipe->screen->resource_create(
         pipe->screen, &templ);
      fprintf(stderr, "tva-proc: NATIVE_SAMPLE_TEST imported=%p native=%p format=%s %ux%u\n",
              (void *)src_resources[0], (void *)native,
              native ? util_format_short_name(native->format) : "none",
              native ? native->width0 : 0, native ? native->height0 : 0);
      if (!native)
         return 1;

      const unsigned native_blocksize = util_format_get_blocksize(native->format);
      const unsigned native_stride = native->width0 * native_blocksize;
      struct pipe_box box = { .x = 0, .y = 0, .z = 0,
                              .width = (int)native->width0,
                              .height = (int)native->height0, .depth = 1 };
      if (pipe->texture_subdata) {
         uint8_t *upload = malloc((size_t)native_stride * native->height0);
         if (!upload) {
            pipe_resource_reference(&native, NULL);
            return 1;
         }
         for (unsigned y = 0; y < native->height0; y++) {
            uint8_t *row = upload + (size_t)y * native_stride;
            for (unsigned x = 0; x < native->width0; x++) {
               row[x * native_blocksize + 0] = 0x00;
               row[x * native_blocksize + 1] = 0x00;
               row[x * native_blocksize + 2] = 0xff;
               row[x * native_blocksize + 3] = 0xff;
            }
         }
         pipe->texture_subdata(pipe, native, 0, PIPE_MAP_WRITE, &box,
                               upload, native_stride, native_stride);
         fprintf(stderr, "tva-proc: native upload path=gpu stride=%u bytes=%02x %02x %02x %02x\n",
                 native_stride, upload[0], upload[1], upload[2], upload[3]);
         free(upload);
      } else if (pipe->texture_map && pipe->texture_unmap) {
         struct pipe_transfer *transfer = NULL;
         uint8_t *map = pipe->texture_map(pipe, native, 0, PIPE_MAP_WRITE,
                                          &box, &transfer);
         if (!map || !transfer) {
            fprintf(stderr, "tva-proc: NATIVE_SAMPLE_TEST map failed\n");
            if (transfer)
               pipe->texture_unmap(pipe, transfer);
            pipe_resource_reference(&native, NULL);
            return 1;
         }
         for (unsigned y = 0; y < native->height0; y++) {
            uint8_t *row = map + (size_t)y * transfer->stride;
            for (unsigned x = 0; x < native->width0; x++) {
               row[x * native_blocksize + 0] = 0x00;
               row[x * native_blocksize + 1] = 0x00;
               row[x * native_blocksize + 2] = 0xff;
               row[x * native_blocksize + 3] = 0xff;
            }
         }
         fprintf(stderr, "tva-proc: native upload path=cpu stride=%u bytes=%02x %02x %02x %02x\n",
                 transfer->stride, map[0], map[1], map[2], map[3]);
         pipe->texture_unmap(pipe, transfer);
      } else {
         pipe_resource_reference(&native, NULL);
         return 1;
      }
      if (pipe->screen->resource_changed)
         pipe->screen->resource_changed(pipe->screen, native);

      struct pipe_sampler_view sv_templ;
      memset(&sv_templ, 0, sizeof(sv_templ));
      u_sampler_view_default_template(&sv_templ, native, native->format);
      struct pipe_sampler_view *sv = pipe->create_sampler_view(
         pipe, native, &sv_templ);
      if (!sv) {
         pipe_resource_reference(&native, NULL);
         return 1;
      }

      struct u_rect src_rect = {0, native->width0, 0, native->height0};
      struct u_rect dst_rect = {0, dst->width, 0, dst->height};
      vl_compositor_clear_layers(&proc->cstate);
      vl_compositor_set_rgba_layer(&proc->cstate, &proc->compositor, 0, sv,
                                   &src_rect, &dst_rect, NULL);
      vl_compositor_set_layer_dst_area(&proc->cstate, 0, &dst_rect);
      if (getenv("DMD_VA_OFFSCREEN_TEST")) {
         /* KGSL can clear an imported linear target, but its 3D path may not
          * rasterize directly into that external layout.  Render into a
          * driver-owned texture first, then exercise the copy path into the
          * imported target. */
         struct pipe_resource off_template = *src_resources[0];
         off_template.format = PIPE_FORMAT_B8G8R8A8_UNORM;
         off_template.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
         off_template.usage = PIPE_USAGE_DEFAULT;
         off_template.flags = 0;
         off_template.next = NULL;
         struct pipe_resource *offscreen = pipe->screen->resource_create(
            pipe->screen, &off_template);
         struct pipe_surface off_surface = {0};
         if (offscreen)
            pipe_surface_init(pipe, &off_surface, offscreen, 0, 0);
         fprintf(stderr, "tva-proc: offscreen target=%p surface=%p\n",
                 (void *)offscreen, (void *)offscreen ? (void *)&off_surface : NULL);
         if (offscreen) {
            vl_compositor_render(&proc->cstate, &proc->compositor,
                                 &off_surface, NULL, false);

            if (pipe->texture_map && pipe->texture_unmap) {
               struct pipe_box probe = { .x = 0, .y = 0, .z = 0,
                                         .width = 8, .height = 1, .depth = 1 };
               struct pipe_transfer *transfer = NULL;
               uint8_t *map = pipe->texture_map(pipe, offscreen, 0,
                                                PIPE_MAP_READ, &probe,
                                                &transfer);
               if (map && transfer) {
                  fprintf(stderr, "tva-proc: offscreen probe stride=%u bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                          transfer->stride, map[0], map[1], map[2], map[3],
                          map[4], map[5], map[6], map[7]);
                  pipe->texture_unmap(pipe, transfer);
               } else {
                  fprintf(stderr, "tva-proc: offscreen probe map failed\n");
                  if (transfer)
                     pipe->texture_unmap(pipe, transfer);
               }
            }

            struct pipe_blit_info blit = {0};
            blit.src.resource = offscreen;
            blit.src.level = 0;
            blit.src.box.x = 0;
            blit.src.box.y = 0;
            blit.src.box.z = 0;
            blit.src.box.width = (int)dst->width;
            blit.src.box.height = (int)dst->height;
            blit.src.box.depth = 1;
            blit.src.format = offscreen->format;
            blit.dst.resource = surfaces[0].texture;
            blit.dst.level = surfaces[0].level;
            blit.dst.box.x = 0;
            blit.dst.box.y = 0;
            blit.dst.box.z = 0;
            blit.dst.box.width = (int)dst->width;
            blit.dst.box.height = (int)dst->height;
            blit.dst.box.depth = 1;
            blit.dst.format = surfaces[0].format;
            blit.mask = PIPE_MASK_RGBA;
            blit.filter = PIPE_TEX_FILTER_NEAREST;
            pipe->blit(pipe, &blit);
            pipe_resource_reference(&off_surface.texture, NULL);
            pipe_resource_reference(&offscreen, NULL);
         }
      } else {
         vl_compositor_render(&proc->cstate, &proc->compositor, &surfaces[0],
                              NULL, false);
      }
      pipe->sampler_view_release(pipe, sv);
      pipe_resource_reference(&native, NULL);
      return 0;
   }

   if (getenv("DMD_VA_Y_SAMPLE_TEST") && !dst_yuv) {
      struct pipe_resource *src_resources[VL_NUM_COMPONENTS] = {0};
      src->get_resources(src, src_resources);
      fprintf(stderr, "tva-proc: Y_SAMPLE_TEST src0=%p format=%s %ux%u\n",
              (void *)src_resources[0],
              src_resources[0] ? util_format_short_name(src_resources[0]->format) : "none",
              src_resources[0] ? src_resources[0]->width0 : 0,
              src_resources[0] ? src_resources[0]->height0 : 0);
      if (!src_resources[0])
         return 1;

      struct pipe_sampler_view sv_templ;
      memset(&sv_templ, 0, sizeof(sv_templ));
      u_sampler_view_default_template(&sv_templ, src_resources[0],
                                      src_resources[0]->format);
      struct pipe_sampler_view *sv = pipe->create_sampler_view(
         pipe, src_resources[0], &sv_templ);
      if (!sv)
         return 1;

      struct u_rect src_rect = {0, src_resources[0]->width0, 0,
                                src_resources[0]->height0};
      struct u_rect dst_rect = {0, dst->width, 0, dst->height};
      vl_compositor_clear_layers(&proc->cstate);
      vl_compositor_set_rgba_layer(&proc->cstate, &proc->compositor, 0, sv,
                                   &src_rect, &dst_rect, NULL);
      vl_compositor_set_layer_dst_area(&proc->cstate, 0, &dst_rect);
      vl_compositor_render(&proc->cstate, &proc->compositor, &surfaces[0],
                           NULL, false);
      pipe->sampler_view_release(pipe, sv);
      return 0;
   }

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
      if (getenv("DMD_VA_SOLID")) {
         union pipe_color_union color = { .f = { 1.0f, 0.0f, 0.0f, 1.0f } };
         pipe->clear_render_target(pipe, &surfaces[0], &color, 0, 0,
                                   dst->width, dst->height, false);
      } else {
         vl_compositor_render(&proc->cstate, &proc->compositor,
                              &surfaces[0], NULL, false);
      }
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

   if (getenv("DMD_VA_PROBE") && target && target->get_surfaces &&
       proc->b.context->texture_map && proc->b.context->texture_unmap) {
      if (picture->out_pipe_fence && *picture->out_pipe_fence &&
          proc->b.context->screen->fence_finish)
         proc->b.context->screen->fence_finish(
            proc->b.context->screen, proc->b.context,
            *picture->out_pipe_fence, OS_TIMEOUT_INFINITE);
      struct pipe_surface *surfaces = target->get_surfaces(target);
      struct pipe_resource *res = surfaces ? surfaces[0].texture : NULL;
      if (res) {
         struct pipe_box box = { .x = 0, .y = 0, .z = 0,
                                 .width = 8, .height = 1, .depth = 1 };
         struct pipe_transfer *transfer = NULL;
         uint8_t *map = proc->b.context->texture_map(proc->b.context, res, 0,
                                                      PIPE_MAP_READ, &box,
                                                      &transfer);
         if (map && transfer) {
            fprintf(stderr, "tva-proc: dst probe fmt=%s stride=%u bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                    util_format_short_name(res->format), transfer->stride,
                    map[0], map[1], map[2], map[3], map[4], map[5], map[6], map[7]);
            proc->b.context->texture_unmap(proc->b.context, transfer);
         } else {
            fprintf(stderr, "tva-proc: dst probe map failed res=%p\n", (void *)res);
            if (transfer)
               proc->b.context->texture_unmap(proc->b.context, transfer);
         }

         struct pipe_box center = { .x = 960, .y = 540, .z = 0,
                                    .width = 1, .height = 1, .depth = 1 };
         transfer = NULL;
         uint8_t *center_map = proc->b.context->texture_map(proc->b.context,
                                                              res, 0,
                                                              PIPE_MAP_READ,
                                                              &center,
                                                              &transfer);
         if (center_map && transfer) {
            fprintf(stderr, "tva-proc: dst center fmt=%s stride=%u bytes=%02x %02x %02x %02x\n",
                    util_format_short_name(res->format), transfer->stride,
                    center_map[0], center_map[1], center_map[2], center_map[3]);
            proc->b.context->texture_unmap(proc->b.context, transfer);
         }
      }
   }

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
