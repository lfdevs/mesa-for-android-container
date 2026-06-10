/**************************************************************************
 *
 * Copyright 2011 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "si_video.h"
#include "drm-uapi/drm_fourcc.h"
#include "radeon_uvd_enc.h"
#include "radeon_vce.h"
#include "radeon_vcn_enc.h"
#include "si_pipe.h"
#include "si_vpe.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"
#include "si_video_dec.h"
#include "ac_uvd_dec.h"

enum ac_video_codec si_pipe_video_profile_to_codec(enum pipe_video_profile profile)
{
   switch (u_reduce_video_profile(profile)) {
   case PIPE_VIDEO_FORMAT_MPEG12:
      return AC_VIDEO_CODEC_MPEG2;
   case PIPE_VIDEO_FORMAT_VC1:
      return AC_VIDEO_CODEC_VC1;
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      return AC_VIDEO_CODEC_AVC;
   case PIPE_VIDEO_FORMAT_HEVC:
      return AC_VIDEO_CODEC_HEVC;
   case PIPE_VIDEO_FORMAT_JPEG:
      return AC_VIDEO_CODEC_MJPEG;
   case PIPE_VIDEO_FORMAT_VP9:
      return AC_VIDEO_CODEC_VP9;
   case PIPE_VIDEO_FORMAT_AV1:
      return AC_VIDEO_CODEC_AV1;
   default:
      return AC_VIDEO_CODEC_MAX;
   }
}

unsigned si_vid_alloc_stream_handle()
{
   static struct ac_uvd_stream_handle stream_handle;
   if (!stream_handle.base)
      ac_uvd_init_stream_handle(&stream_handle);
   return ac_uvd_alloc_stream_handle(&stream_handle);
}

struct si_resource *si_vid_create_buffer(struct pipe_screen *screen,
                                         enum pipe_resource_usage usage,
                                         unsigned flags, unsigned size)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   unsigned bind = 0;

   /* Disable suballocation for UVD/VCE with no VM support */
   if (sscreen->info.family < CHIP_STONEY)
      bind |= PIPE_BIND_CUSTOM;

   if (flags & PIPE_RESOURCE_FLAG_ENCRYPTED)
      bind |= PIPE_BIND_PROTECTED;

   return si_resource(screen->resource_create(screen, &(struct pipe_resource){
      .width0 = size,
      .height0 = 1,
      .depth0 = 1,
      .array_size = 1,
      .format = PIPE_FORMAT_R8_UNORM,
      .target = PIPE_BUFFER,
      .usage = usage,
      .bind = bind,
      .flags = flags,
   }));
}

bool si_vid_resize_buffer(struct pipe_context *context,
                          struct si_resource **buf, unsigned new_size)
{
   struct si_context *sctx = (struct si_context *)context;
   struct si_screen *sscreen = (struct si_screen *)context->screen;
   struct radeon_winsys *ws = sscreen->ws;
   struct si_resource *new_buf = *buf;
   unsigned bytes = MIN2(new_buf->buf->size, new_size);
   struct si_resource *old_buf = new_buf;
   void *src = NULL, *dst = NULL;

   new_buf = si_vid_create_buffer(context->screen, old_buf->b.b.usage,
                                  old_buf->b.b.flags, new_size);
   if (!new_buf)
      goto error;

   if (old_buf->b.b.usage == PIPE_USAGE_STAGING) {
      src = ws->buffer_map(ws, old_buf->buf, NULL, PIPE_MAP_READ | RADEON_MAP_TEMPORARY);
      if (!src)
         goto error;

      dst = ws->buffer_map(ws, new_buf->buf, NULL, PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
      if (!dst)
         goto error;

      memcpy(dst, src, bytes);
      if (new_size > bytes) {
         new_size -= bytes;
         dst += bytes;
         memset(dst, 0, new_size);
      }
      ws->buffer_unmap(ws, new_buf->buf);
      ws->buffer_unmap(ws, old_buf->buf);
   } else {
      si_barrier_before_simple_buffer_op(sctx, 0, &new_buf->b.b, &old_buf->b.b);
      bytes = MIN2(new_buf->b.b.width0, old_buf->b.b.width0);
      si_copy_buffer(sctx, &new_buf->b.b, &old_buf->b.b, 0, 0, bytes);
      context->flush(context, NULL, 0);
   }

   si_resource_reference(&old_buf, NULL);
   *buf = new_buf;
   return true;

error:
   if (src)
      ws->buffer_unmap(ws, old_buf->buf);
   si_resource_reference(&new_buf, NULL);
   return false;
}

/**
 * creates a video buffer with an UVD compatible memory layout
 */
struct pipe_video_buffer *si_video_buffer_create_with_modifiers(struct pipe_context *pipe,
                                                                const struct pipe_video_buffer *tmpl,
                                                                const uint64_t *modifiers,
                                                                unsigned int modifiers_count)
{
   struct si_screen *sscreen = (struct si_screen *)pipe->screen;
   uint64_t *allowed_modifiers;
   unsigned int allowed_modifiers_count, i;

   allowed_modifiers = calloc(modifiers_count, sizeof(uint64_t));
   if (!allowed_modifiers)
      return NULL;

   allowed_modifiers_count = 0;
   for (i = 0; i < modifiers_count; i++) {
      uint64_t mod = modifiers[i];

      if (!ac_modifier_supports_video(&sscreen->info, mod))
         continue;

      if (mod != DRM_FORMAT_MOD_LINEAR) {
         if (sscreen->multimedia_debug_flags & DBG(NO_VIDEO_TILING))
            continue;

         if (!sscreen->info.has_image_opcodes)
            continue;
      }

      allowed_modifiers[allowed_modifiers_count++] = mod;
   }

   struct pipe_video_buffer *buf =
      vl_video_buffer_create_as_resource(pipe, tmpl, allowed_modifiers, allowed_modifiers_count);
   free(allowed_modifiers);
   return buf;
}

struct pipe_video_buffer *si_video_buffer_create(struct pipe_context *pipe,
                                                 const struct pipe_video_buffer *tmpl)
{
   struct si_screen *sscreen = (struct si_screen *)pipe->screen;
   struct pipe_video_buffer vidbuf = *tmpl;
   uint64_t *modifiers = NULL;
   int modifiers_count = 0;

   if (vidbuf.bind & (PIPE_BIND_VIDEO_DECODE_DPB | PIPE_BIND_VIDEO_ENCODE_DPB))
      return vl_video_buffer_create_as_resource(pipe, &vidbuf, NULL, 0);

   /* Ensure resource_get_handle doesn't need to reallocate the texture
    * which would fail with compute-only context.
    * This is only needed with AMD_DEBUG=tmz because in this case the frontend
    * is not aware of the buffer being created as protected.
    */
   if (sscreen->debug_flags & DBG(TMZ) && !(vidbuf.bind & PIPE_BIND_PROTECTED))
      vidbuf.bind |= PIPE_BIND_SHARED;

   if (pipe->screen->resource_create_with_modifiers && !(vidbuf.bind & PIPE_BIND_LINEAR)) {
      pipe->screen->query_dmabuf_modifiers(pipe->screen, vidbuf.buffer_format, 0,
                                           NULL, NULL, &modifiers_count);
      modifiers = calloc(modifiers_count, sizeof(uint64_t));
      if (!modifiers)
         return NULL;

      pipe->screen->query_dmabuf_modifiers(pipe->screen, vidbuf.buffer_format, modifiers_count,
                                           modifiers, NULL, &modifiers_count);

      struct pipe_video_buffer *buf =
         si_video_buffer_create_with_modifiers(pipe, &vidbuf, modifiers, modifiers_count);
      free(modifiers);
      return buf;
   }

   uint64_t mod = DRM_FORMAT_MOD_LINEAR;
   if (pipe->screen->resource_create_with_modifiers) {
      modifiers = &mod;
      modifiers_count = 1;
   }
   vidbuf.bind |= PIPE_BIND_LINEAR;

   return vl_video_buffer_create_as_resource(pipe, &vidbuf, modifiers, modifiers_count);
}

/* get the radeon resources for VCE */
static void si_vce_get_buffer(struct pipe_resource *resource, struct pb_buffer_lean **handle,
                              struct radeon_surf **surface)
{
   struct si_texture *res = (struct si_texture *)resource;

   if (handle)
      *handle = res->buffer.buf;

   if (surface)
      *surface = &res->surface;
}

static bool si_vcn_need_context(struct si_context *ctx)
{
   /* Kernel does VCN instance scheduling per context, so when we have
    * multiple instances we should use new context to be able to utilize
    * all of them.
    */
   return ctx->screen->info.ip[AMD_IP_VCN_ENC].num_instances > 1;
}

struct pipe_video_codec *si_video_codec_create(struct pipe_context *context,
                                               const struct pipe_video_codec *templ)
{
   struct si_context *ctx = (struct si_context *)context;
   bool vcn = ctx->vcn_ip_ver >= VCN_1_0_0;
   struct pipe_video_codec *codec = NULL;

   if (templ->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      if (vcn) {
         codec = radeon_create_encoder(context, templ, ctx->ws, si_vce_get_buffer);
         ctx->vcn_has_ctx = si_vcn_need_context(ctx);
         return codec;
      } else {
         if (u_reduce_video_profile(templ->profile) == PIPE_VIDEO_FORMAT_HEVC)
            return radeon_uvd_create_encoder(context, templ, ctx->ws, si_vce_get_buffer);
         else
            return si_vce_create_encoder(context, templ, ctx->ws, si_vce_get_buffer);
      }
   } else if (((struct si_screen *)(context->screen))->info.ip[AMD_IP_VPE].num_queues &&
              templ->entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING)
      return si_vpe_create_processor(context, templ);

   return si_create_video_decoder(context, templ);
}

