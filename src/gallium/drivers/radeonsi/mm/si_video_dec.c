/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_video_dec.h"
#include "si_pipe.h"
#include "si_video.h"
#include "util/u_video.h"
#include "util/vl_zscan_data.h"
#include "vl/vl_video_buffer.h"
#include "ac_debug.h"

#define ERROR(fmt, args...) \
   do { \
      vid->error = true; \
      mesa_loge("%s:%d %s DECODER - " fmt "\n", __FILE__, __LINE__, __func__, ##args); \
   } while(0)

static void si_dec_begin_frame(struct pipe_video_codec *decoder, struct pipe_video_buffer *target,
                               struct pipe_picture_desc *picture)
{
   struct si_video_dec *vid = (struct si_video_dec *)decoder;

   vid->bs_size = 0;
   if (vid->bs_buffers[vid->cur_buffer]) {
      vid->bs_ptr = vid->ws->buffer_map(vid->ws, vid->bs_buffers[vid->cur_buffer]->buf,
                                        NULL, PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   }
}

static void si_dec_decode_bitstream(struct pipe_video_codec *decoder, struct pipe_video_buffer *target,
                                    struct pipe_picture_desc *picture, unsigned num_buffers,
                                    const void *const *buffers, const unsigned *sizes)
{
   struct si_video_dec *vid = (struct si_video_dec *)decoder;

   if (vid->error)
      return;

   unsigned total_bs_size = vid->bs_size;
   for (unsigned i = 0; i < num_buffers; i++)
      total_bs_size += sizes[i];

   struct si_resource *buf = vid->bs_buffers[vid->cur_buffer];

   if (!buf || total_bs_size > buf->buf->size) {
      total_bs_size = align(MAX2(64 * 1024, total_bs_size), 128);

      if (buf) {
         vid->ws->buffer_unmap(vid->ws, buf->buf);
         vid->bs_ptr = NULL;
      }

      if (!vid->bs_size) {
         buf = si_resource(pipe_buffer_create(vid->screen, 0, PIPE_USAGE_STAGING, total_bs_size));
         if (!buf) {
            ERROR("Can't create bitstream buffer!");
            return;
         }
         si_resource_reference(&vid->bs_buffers[vid->cur_buffer], NULL);
         vid->bs_buffers[vid->cur_buffer] = buf;
      } else if (!si_vid_resize_buffer(vid->base.context, &vid->bs_buffers[vid->cur_buffer], total_bs_size)) {
         ERROR("Can't resize bitstream buffer!");
         return;
      }

      buf = vid->bs_buffers[vid->cur_buffer];
      vid->bs_ptr = vid->ws->buffer_map(vid->ws, buf->buf, NULL, PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
      if (!vid->bs_ptr) {
         ERROR("Failed to map bitstream buffer!");
         return;
      }

      vid->bs_ptr += vid->bs_size;
   }

   for (unsigned i = 0; i < num_buffers; i++) {
      memcpy(vid->bs_ptr, buffers[i], sizes[i]);
      vid->bs_size += sizes[i];
      vid->bs_ptr += sizes[i];
   }
}

static int si_dec_flush(struct si_video_dec *vid, unsigned flags, struct pipe_fence_handle **fence)
{
   struct si_screen *sscreen = (struct si_screen *)vid->screen;

   if (sscreen->debug_flags & DBG(IB)) {
      struct ac_ib_parser ib_parser = {
         .f = stderr,
         .ib = vid->cs.current.buf,
         .num_dw = vid->cs.current.cdw,
         .gfx_level = sscreen->info.gfx_level,
         .vcn_version = sscreen->info.vcn_ip_version,
         .family = sscreen->info.family,
         .ip_type = vid->dec->ip_type,
      };
      ac_parse_ib(&ib_parser, "IB");
   }

   return vid->ws->cs_flush(&vid->cs, flags, fence);
}

static uint64_t si_dec_buf_address(struct si_video_dec *vid, struct si_resource *buf,
                                   unsigned usage, enum radeon_bo_domain domains)
{
   struct si_screen *sscreen = (struct si_screen *)vid->screen;

   if (sscreen->info.is_amdgpu) {
      vid->ws->cs_add_buffer(&vid->cs, buf->buf, usage | RADEON_USAGE_SYNCHRONIZED, 0);
      return buf->gpu_address;
   }

   uint64_t reloc_idx =
      vid->ws->cs_add_buffer(&vid->cs, buf->buf, usage | RADEON_USAGE_SYNCHRONIZED, domains);
   return vid->ws->buffer_get_reloc_offset(buf->buf) | (reloc_idx * 4) << 32;
}

static void si_dec_fill_surface(struct si_video_dec *vid, struct pipe_resource *res,
                                unsigned usage, struct ac_video_surface *surf)
{
   struct si_screen *sscreen = (struct si_screen *)vid->screen;
   struct si_texture *tex = (struct si_texture *)res;

   surf->format = tex->num_planes > 1 ? tex->multi_plane_format : res->format;
   surf->size += tex->buffer.buf->size;
   surf->num_planes = util_format_get_num_planes(surf->format);

   for (uint32_t i = 0; i < surf->num_planes; i++) {
      assert(tex);
      surf->planes[i].va = si_dec_buf_address(vid, &tex->buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);
      if (sscreen->info.gfx_level >= GFX9) {
         surf->planes[i].va += tex->surface.u.gfx9.surf_offset;
      } else {
         uint64_t offset = (uint64_t)tex->surface.u.legacy.level[0].offset_256B * 256;
         if (sscreen->info.is_amdgpu) {
            surf->planes[i].va += offset;
         } else {
            assert(i != 0 || offset == 0);
            if (i != 0)
               surf->planes[i].va = offset;
         }
      }
      surf->planes[i].surf = &tex->surface;
      tex = (struct si_texture *)tex->buffer.b.b.next;
   }
}

static struct si_resource *si_dec_get_emb_buffer(struct si_video_dec *vid)
{
   struct si_resource *emb_buffer = vid->emb_buffers[vid->cur_buffer];

   if (vid->dec->embedded_size && !emb_buffer) {
      emb_buffer = si_resource(pipe_buffer_create(vid->screen, 0, PIPE_USAGE_DEFAULT, vid->dec->embedded_size));
      vid->emb_buffers[vid->cur_buffer] = emb_buffer;
   }

   return emb_buffer;
}

static int si_dec_init_decoder(struct si_video_dec *vid, struct ac_video_dec_session_param *param, bool protected)
{
   struct si_screen *sscreen = (struct si_screen *)vid->screen;
   int ret = 0;

   vid->dec = ac_create_video_decoder(&sscreen->info, param);
   if (!vid->dec)
      return -EINVAL;

   vid->dpb_size = ac_video_dec_dpb_size(&sscreen->info, param);
   vid->dpb_alignment = ac_video_dec_dpb_alignment(&sscreen->info, param);

   if (sscreen->info.ip[vid->dec->ip_type].num_instances > 1) {
      vid->ectx = sscreen->b.context_create(&sscreen->b, NULL, PIPE_CONTEXT_COMPUTE_ONLY);
      if (vid->ectx)
         vid->base.context = vid->ectx;
   }

   struct si_context *sctx = (struct si_context *)vid->base.context;
   if (!vid->ws->cs_create(&vid->cs, sctx->ctx, vid->dec->ip_type, NULL, NULL)) {
      ERROR("Failed to create command submission context");
      ret = -EINVAL;
      goto err_cs;
   }

   if (vid->dec->session_size) {
      vid->session_buffer = si_resource(pipe_buffer_create(vid->screen, 0, PIPE_USAGE_DEFAULT,
                                                           vid->dec->session_size));
      if (!vid->session_buffer) {
         ret = -ENOMEM;
         goto err;
      }
   }

   if (protected && vid->dec->session_tmz_size) {
      vid->session_tmz_buffer = si_resource(pipe_buffer_create(vid->screen, PIPE_BIND_PROTECTED,
                                                               PIPE_USAGE_DEFAULT, vid->dec->session_tmz_size));
      if (!vid->session_tmz_buffer) {
         ret = -ENOMEM;
         goto err;
      }
   }

   if (vid->dec->init_session_buf) {
      struct si_resource *session_buf = protected ? vid->session_tmz_buffer : vid->session_buffer;
      void *ptr = vid->ws->buffer_map(vid->ws, session_buf->buf, NULL,
                                      PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
      if (!ptr) {
         ret = -ENOMEM;
         goto err;
      }
      vid->dec->init_session_buf(vid->dec, ptr);
      vid->ws->buffer_unmap(vid->ws, session_buf->buf);
   }

   /* No create cmd needed */
   if (vid->dec->max_create_cmd_dw == 0)
      return 0;

   struct si_resource *emb_buffer = NULL;

   if (vid->dec->embedded_size) {
      emb_buffer = si_dec_get_emb_buffer(vid);
      if (!emb_buffer) {
         ret = -ENOMEM;
         goto err;
      }
   }

   struct ac_video_dec_create_cmd cmd = {
      .cmd_buffer = vid->cs.current.buf + vid->cs.current.cdw,
   };

   if (vid->session_buffer)
      cmd.session_va = si_dec_buf_address(vid, vid->session_buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);

   if (emb_buffer) {
      cmd.embedded_va = si_dec_buf_address(vid, emb_buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);
      cmd.embedded_ptr =
         vid->ws->buffer_map(vid->ws, emb_buffer->buf, NULL, PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   }

   ret = vid->dec->build_create_cmd(vid->dec, &cmd);

   if (emb_buffer)
      vid->ws->buffer_unmap(vid->ws, emb_buffer->buf);

   if (ret)
      goto err;

   vid->cs.current.cdw += cmd.out.cmd_dw;
   si_dec_flush(vid, 0, NULL);

   return ret;

err:
   vid->ws->cs_destroy(&vid->cs);

err_cs:
   if (vid->dec) {
      vid->dec->destroy(vid->dec);
      vid->dec = NULL;
   }

   return ret;
}

static int si_dec_destroy_decoder(struct si_video_dec *vid)
{
   /* No destroy cmd needed */
   if (vid->dec->max_destroy_cmd_dw == 0)
      return 0;

   struct si_resource *emb_buffer = NULL;

   if (vid->dec->embedded_size) {
      emb_buffer = si_dec_get_emb_buffer(vid);
      if (!emb_buffer)
         return -ENOMEM;
   }

   struct ac_video_dec_destroy_cmd cmd = {
      .cmd_buffer = vid->cs.current.buf + vid->cs.current.cdw,
   };

   if (emb_buffer) {
      cmd.embedded_va = si_dec_buf_address(vid, emb_buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);
      cmd.embedded_ptr =
         vid->ws->buffer_map(vid->ws, emb_buffer->buf, NULL, PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   }

   int ret = vid->dec->build_destroy_cmd(vid->dec, &cmd);

   if (emb_buffer)
      vid->ws->buffer_unmap(vid->ws, emb_buffer->buf);

   if (ret == 0) {
      vid->cs.current.cdw += cmd.out.cmd_dw;
      si_dec_flush(vid, 0, NULL);
   }

   return ret;
}

static enum ac_video_dec_tier select_tier(struct si_video_dec *vid, struct pipe_video_buffer *target)
{
   struct si_screen *sscreen = (struct si_screen *)vid->screen;

   if (vid->dec->tiers & AC_VIDEO_DEC_TIER3 && !(sscreen->multimedia_debug_flags & DBG(NO_DECODE_TIER3))) {
      /* UDT requires tiling */
      struct si_texture *tex = (struct si_texture *)((struct vl_video_buffer *)target)->resources[0];
      if (!tex->surface.is_linear)
         return AC_VIDEO_DEC_TIER3;
   }

   if (vid->dec->tiers & AC_VIDEO_DEC_TIER2 && !(sscreen->multimedia_debug_flags & DBG(NO_DECODE_TIER2)))
      return AC_VIDEO_DEC_TIER2;

   if (vid->dec->tiers & AC_VIDEO_DEC_TIER1 && !(sscreen->multimedia_debug_flags & DBG(NO_DECODE_TIER1)))
      return AC_VIDEO_DEC_TIER1;

   return AC_VIDEO_DEC_TIER0;
}

static void decode_cmd_init(struct si_video_dec *vid, struct pipe_picture_desc *pic,
                            struct pipe_video_buffer *target, struct ac_video_dec_decode_cmd *cmd)
{
   struct si_screen *sscreen = (struct si_screen *)vid->screen;

   memset(cmd, 0, sizeof(*cmd));

   cmd->cmd_buffer = vid->cs.current.buf + vid->cs.current.cdw;
   cmd->bitstream_size = align(vid->bs_size, 128);
   cmd->width = vid->base.width;
   cmd->height = vid->base.height;
   cmd->tier = select_tier(vid, target);
   cmd->low_latency = sscreen->multimedia_debug_flags & DBG(LOW_LATENCY_DECODE);

   if (pic->protected_playback) {
      cmd->protected_content.mode =
         pic->cenc ? AC_VIDEO_DEC_PROTECTED_CONTENT_CENC : AC_VIDEO_DEC_PROTECTED_CONTENT_LEGACY;
      cmd->protected_content.key = pic->decrypt_key;
      cmd->protected_content.key_size = pic->key_size;

      if (!vid->ws->cs_is_secure(&vid->cs))
         vid->ws->cs_flush(&vid->cs, RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION, NULL);
   }

   si_dec_fill_surface(vid, ((struct vl_video_buffer *)target)->resources[0],
                       RADEON_USAGE_READWRITE, &cmd->decode_surface);

   memset(vid->bs_ptr, 0, cmd->bitstream_size - vid->bs_size);
   vid->ws->buffer_unmap(vid->ws, vid->bs_buffers[vid->cur_buffer]->buf);
   vid->bs_ptr = NULL;
}

static int decode_cmd_build(struct si_video_dec *vid, struct pipe_picture_desc *pic,
                            struct ac_video_dec_decode_cmd *cmd)
{
   struct si_resource *emb_buffer = NULL;

   if (vid->dec->embedded_size) {
      emb_buffer = si_dec_get_emb_buffer(vid);
      if (!emb_buffer)
         return -ENOMEM;
   }

   if (vid->session_buffer)
      cmd->session_va = si_dec_buf_address(vid, vid->session_buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);

   if (vid->session_tmz_buffer)
      cmd->session_tmz_va = si_dec_buf_address(vid, vid->session_tmz_buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);

   if (emb_buffer) {
      cmd->embedded_va = si_dec_buf_address(vid, emb_buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);
      cmd->embedded_ptr =
         vid->ws->buffer_map(vid->ws, emb_buffer->buf, NULL, PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   }

   struct si_resource *bs_buffer = vid->bs_buffers[vid->cur_buffer];
   cmd->bitstream_va = si_dec_buf_address(vid, bs_buffer, RADEON_USAGE_READ, RADEON_DOMAIN_GTT);

   if (cmd->tier == AC_VIDEO_DEC_TIER0 && vid->dpb_size) {
      if (!vid->dpb_buffer) {
         unsigned bind = pic->protected_playback ? PIPE_BIND_PROTECTED : 0;
         vid->dpb_buffer = si_resource(pipe_buffer_create(vid->screen, bind, PIPE_USAGE_DEFAULT, vid->dpb_size));
         if (!vid->dpb_buffer)
            return -ENOMEM;
      }
      cmd->ref_surfaces[0].size = vid->dpb_buffer->buf->size;
      cmd->ref_surfaces[0].planes[0].va =
         si_dec_buf_address(vid, vid->dpb_buffer, RADEON_USAGE_READWRITE, RADEON_DOMAIN_VRAM);
   }

   if (cmd->tier == AC_VIDEO_DEC_TIER1) {
      struct si_resource *resize_dpb = NULL;
      if (vid->dec->param.max_width < vid->base.width || vid->dec->param.max_height < vid->base.height) {
         resize_dpb = vid->dpb_buffer;
         vid->dpb_buffer = NULL;
      }
      if (!vid->dpb_buffer) {
         unsigned bind = PIPE_BIND_VIDEO_DECODE_DPB;
         if (pic->protected_playback)
            bind |= PIPE_BIND_PROTECTED;
         vid->dpb_buffer = si_resource(vid->screen->resource_create(vid->screen, &(struct pipe_resource){
            .format = cmd->decode_surface.format,
            .target = PIPE_TEXTURE_2D_ARRAY,
            .width0 = align(vid->base.width, vid->dpb_alignment),
            .height0 = align(vid->base.height, vid->dpb_alignment),
            .depth0 = 1,
            .array_size = vid->dec->param.max_num_ref,
            .usage = PIPE_USAGE_DEFAULT,
            .bind = bind,
         }));
         if (!vid->dpb_buffer)
            return -ENOMEM;
      }
      if (resize_dpb) {
         uint32_t src_slice_size = ((struct si_texture *)resize_dpb)->surface.u.gfx9.surf_slice_size +
            ((struct si_texture *)resize_dpb->b.b.next)->surface.u.gfx9.surf_slice_size;
         uint32_t dst_slice_size = ((struct si_texture *)vid->dpb_buffer)->surface.u.gfx9.surf_slice_size +
            ((struct si_texture *)vid->dpb_buffer->b.b.next)->surface.u.gfx9.surf_slice_size;
         uint64_t src_offset = 0;
         uint64_t dst_offset = 0;
         for (uint32_t slice = 0; slice < vid->dec->param.max_num_ref; slice++) {
            si_copy_buffer((struct si_context *)vid->base.context, &vid->dpb_buffer->b.b, &resize_dpb->b.b,
                           dst_offset, src_offset, src_slice_size);
            src_offset += src_slice_size;
            dst_offset += dst_slice_size;
         }
         vid->base.context->flush(vid->base.context, NULL, 0);
         si_resource_reference(&resize_dpb, NULL);
         vid->dec->param.max_width = vid->base.width;
         vid->dec->param.max_height = vid->base.height;
         cmd->dpb_resize = true;
      }
      si_dec_fill_surface(vid, &vid->dpb_buffer->b.b, RADEON_USAGE_READWRITE, &cmd->ref_surfaces[0]);
   }

   if (cmd->tier == AC_VIDEO_DEC_TIER2) {
      uint32_t width = align(vid->base.width, vid->dpb_alignment);
      uint32_t height = align(vid->base.height, vid->dpb_alignment);

      cmd->ref_id[cmd->num_refs++] = cmd->cur_id;

      for (uint8_t i = 0; i < cmd->num_refs; i++) {
         uint32_t ref_id = cmd->ref_id[i];
         struct pipe_resource *ref = vid->dpb[ref_id];

         if (ref_id == cmd->cur_id && ref && (ref->width0 != width || ref->height0 != height))
            pipe_resource_reference(&ref, NULL);

         if (!ref) {
            unsigned bind = PIPE_BIND_VIDEO_DECODE_DPB;
            if (pic->protected_playback)
               bind |= PIPE_BIND_PROTECTED;
            ref = vid->screen->resource_create(vid->screen, &(struct pipe_resource){
               .format = cmd->decode_surface.format,
               .target = PIPE_TEXTURE_2D,
               .width0 = width,
               .height0 = height,
               .depth0 = 1,
               .array_size = 1,
               .usage = PIPE_USAGE_DEFAULT,
               .bind = bind,
            });
            if (!ref)
               return -ENOMEM;
            vid->dpb[ref_id] = ref;
         }
         si_dec_fill_surface(vid, ref, RADEON_USAGE_READWRITE, &cmd->ref_surfaces[i]);
      }
   }

   int ret = vid->dec->build_decode_cmd(vid->dec, cmd);

   if (emb_buffer)
      vid->ws->buffer_unmap(vid->ws, emb_buffer->buf);

   if (ret == 0) {
      vid->cs.current.cdw += cmd->out.cmd_dw;
      vid->cur_buffer = (vid->cur_buffer + 1) % ARRAY_SIZE(vid->bs_buffers);
   }

   return ret;
}

static int si_dec_h264(struct si_video_dec *vid, struct pipe_video_buffer *target,
                       struct pipe_h264_picture_desc *pic)
{
   if (!vid->dec) {
      struct ac_video_dec_session_param param = {
         .codec = AC_VIDEO_CODEC_AVC,
         .sub_sample = AC_VIDEO_SUBSAMPLE_420,
         .max_width = vid->base.width,
         .max_height = vid->base.height,
         .max_bit_depth = 8,
         .max_num_ref = vid->base.max_references + 1,
      };
      int ret = si_dec_init_decoder(vid, &param, pic->base.protected_playback);
      if (ret) {
         ERROR("Failed to create h264 decoder");
         return ret;
      }
   }

   struct ac_video_dec_decode_cmd cmd;
   decode_cmd_init(vid, &pic->base, target, &cmd);

   struct ac_video_dec_avc *h264 = &cmd.codec_param.avc;
   h264->sps_flags.direct_8x8_inference_flag = pic->pps->sps->direct_8x8_inference_flag;
   h264->sps_flags.frame_mbs_only_flag = pic->pps->sps->frame_mbs_only_flag;
   h264->sps_flags.delta_pic_order_always_zero_flag = pic->pps->sps->delta_pic_order_always_zero_flag;
   h264->sps_flags.separate_colour_plane_flag = pic->pps->sps->separate_colour_plane_flag;
   h264->sps_flags.gaps_in_frame_num_value_allowed_flag = pic->pps->sps->gaps_in_frame_num_value_allowed_flag;

   h264->pps_flags.transform_8x8_mode_flag = pic->pps->transform_8x8_mode_flag;
   h264->pps_flags.redundant_pic_cnt_present_flag = pic->pps->redundant_pic_cnt_present_flag;
   h264->pps_flags.constrained_intra_pred_flag = pic->pps->constrained_intra_pred_flag;
   h264->pps_flags.deblocking_filter_control_present_flag = pic->pps->deblocking_filter_control_present_flag;
   h264->pps_flags.weighted_pred_flag = pic->pps->weighted_pred_flag;
   h264->pps_flags.bottom_field_pic_order_in_frame_present_flag = pic->pps->bottom_field_pic_order_in_frame_present_flag;
   h264->pps_flags.entropy_coding_mode_flag = pic->pps->entropy_coding_mode_flag;
   h264->pps_flags.weighted_bipred_idc = pic->pps->weighted_bipred_idc;

   h264->pic_flags.bottom_field_flag = pic->bottom_field_flag;
   h264->pic_flags.mbaff_frame_flag = pic->pps->sps->mb_adaptive_frame_field_flag;
   h264->pic_flags.chroma_format_idc = pic->pps->sps->chroma_format_idc;
   h264->pic_flags.ref_pic_flag = pic->is_reference;

   switch (pic->base.profile) {
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      h264->profile_idc = 66;
      break;
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      h264->profile_idc = 77;
      break;
   case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      h264->profile_idc = 100;
      break;
   default:
      UNREACHABLE("Invalid h264 profile");
   }

   h264->level_idc = vid->base.level;
   h264->curr_field_order_cnt[0] = pic->field_order_cnt[0];
   h264->curr_field_order_cnt[1] = pic->field_order_cnt[1];
   h264->frame_num = pic->frame_num;
   h264->max_num_ref_frames = pic->num_ref_frames;
   h264->bit_depth_luma_minus8 = pic->pps->sps->bit_depth_luma_minus8;
   h264->bit_depth_chroma_minus8 = pic->pps->sps->bit_depth_chroma_minus8;
   for (unsigned i = 0; i < H264_MAX_NUM_REF_PICS; i++) {
      h264->field_order_cnt_list[i][0] = pic->field_order_cnt_list[i][0];
      h264->field_order_cnt_list[i][1] = pic->field_order_cnt_list[i][1];
      h264->frame_num_list[i] = pic->frame_num_list[i];
   }
   h264->log2_max_frame_num_minus4 = pic->pps->sps->log2_max_frame_num_minus4;
   h264->pic_order_cnt_type = pic->pps->sps->pic_order_cnt_type;
   h264->log2_max_pic_order_cnt_lsb_minus4 = pic->pps->sps->log2_max_pic_order_cnt_lsb_minus4;
   h264->pic_width_in_mbs_minus1 = pic->pps->sps->pic_width_in_mbs_minus1;
   h264->pic_height_in_mbs_minus1 = pic->pps->sps->pic_height_in_mbs_minus1;
   h264->num_ref_idx_l0_default_active_minus1 = pic->num_ref_idx_l0_active_minus1;
   h264->num_ref_idx_l1_default_active_minus1 = pic->num_ref_idx_l1_active_minus1;
   h264->pic_init_qp_minus26 = pic->pps->pic_init_qp_minus26;
   h264->pic_init_qs_minus26 = pic->pps->pic_init_qs_minus26;
   h264->chroma_qp_index_offset = pic->pps->chroma_qp_index_offset;
   h264->second_chroma_qp_index_offset = pic->pps->second_chroma_qp_index_offset;
   memcpy(h264->scaling_list_4x4, pic->pps->ScalingList4x4, sizeof(h264->scaling_list_4x4));
   memcpy(h264->scaling_list_8x8, pic->pps->ScalingList8x8, sizeof(h264->scaling_list_8x8));
   h264->num_slice_groups_minus1 = pic->pps->num_slice_groups_minus1;
   h264->slice_group_map_type = pic->pps->slice_group_map_type;
   h264->slice_group_change_rate_minus1 = pic->pps->slice_group_change_rate_minus1;

   h264->curr_pic_id = 0xff;
   for (unsigned i = 0; i < H264_MAX_NUM_REF_PICS; i++)
      h264->ref_frame_id_list[i] = 0xff;

   for (unsigned i = 0; i < ARRAY_SIZE(pic->ref) + 1; i++) {
      if (vid->render_pic_list[i]) {
         bool found = false;
         for (unsigned j = 0; j < ARRAY_SIZE(pic->ref); j++) {
            if (vid->render_pic_list[i] == pic->ref[j]) {
               h264->ref_frame_id_list[j] = i;
               if (pic->is_long_term[j])
                  h264->used_for_long_term_ref_flags |= (1 << j);
               if (pic->top_is_reference[j])
                  h264->used_for_reference_flags |= (1 << (2 * j));
               if (pic->bottom_is_reference[j])
                  h264->used_for_reference_flags |= (1 << (2 * j + 1));
               h264->curr_pic_ref_frame_num++;
               si_dec_fill_surface(vid, ((struct vl_video_buffer *)pic->ref[j])->resources[0],
                                   RADEON_USAGE_READWRITE, &cmd.ref_surfaces[cmd.num_refs]);
               cmd.ref_id[cmd.num_refs++] = i;
               found = true;
            }
         }
         if (!found)
            vid->render_pic_list[i] = NULL;
      }
      if (vid->render_pic_list[i] == target)
         h264->curr_pic_id = i;
   }

   /* Target surface can also be a reference (other field) */
   if (h264->curr_pic_id == 0xff) {
      for (unsigned i = 0; i < ARRAY_SIZE(pic->ref) + 1; i++) {
         if (!vid->render_pic_list[i]) {
            vid->render_pic_list[i] = target;
            h264->curr_pic_id = i;
            break;
         }
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(pic->ref) && pic->ref[i]; i++) {
      if (pic->is_non_existing[i] || (pic->ref[i] && h264->ref_frame_id_list[i] == 0xff))
         h264->non_existing_frame_flags |= 1 << i;
   }

   cmd.cur_id = h264->curr_pic_id;
   return decode_cmd_build(vid, &pic->base, &cmd);
}

static int si_dec_h265(struct si_video_dec *vid, struct pipe_video_buffer *target,
                       struct pipe_h265_picture_desc *pic)
{
   if (!vid->dec) {
      struct ac_video_dec_session_param param = {
         .codec = AC_VIDEO_CODEC_HEVC,
         .sub_sample = AC_VIDEO_SUBSAMPLE_420,
         .max_width = vid->base.width,
         .max_height = vid->base.height,
         .max_bit_depth = vid->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10 ? 10 : 8,
         .max_num_ref = 17,
      };
      int ret = si_dec_init_decoder(vid, &param, pic->base.protected_playback);
      if (ret) {
         ERROR("Failed to create h265 decoder");
         return ret;
      }
   }

   struct ac_video_dec_decode_cmd cmd;
   decode_cmd_init(vid, &pic->base, target, &cmd);

   struct ac_video_dec_hevc *h265 = &cmd.codec_param.hevc;
   h265->sps_flags.separate_colour_plane_flag = pic->pps->sps->separate_colour_plane_flag;
   h265->sps_flags.scaling_list_enabled_flag = pic->pps->sps->scaling_list_enabled_flag;
   h265->sps_flags.amp_enabled_flag = pic->pps->sps->amp_enabled_flag;
   h265->sps_flags.sample_adaptive_offset_enabled_flag = pic->pps->sps->sample_adaptive_offset_enabled_flag;
   h265->sps_flags.pcm_enabled_flag = pic->pps->sps->pcm_enabled_flag;
   h265->sps_flags.pcm_loop_filter_disabled_flag = pic->pps->sps->pcm_loop_filter_disabled_flag;
   h265->sps_flags.long_term_ref_pics_present_flag = pic->pps->sps->long_term_ref_pics_present_flag;
   h265->sps_flags.sps_temporal_mvp_enabled_flag = pic->pps->sps->sps_temporal_mvp_enabled_flag;
   h265->sps_flags.strong_intra_smoothing_enabled_flag = pic->pps->sps->strong_intra_smoothing_enabled_flag;

   h265->pps_flags.dependent_slice_segments_enabled_flag = pic->pps->dependent_slice_segments_enabled_flag;
   h265->pps_flags.output_flag_present_flag = pic->pps->output_flag_present_flag;
   h265->pps_flags.sign_data_hiding_enabled_flag = pic->pps->sign_data_hiding_enabled_flag;
   h265->pps_flags.cabac_init_present_flag = pic->pps->cabac_init_present_flag;
   h265->pps_flags.constrained_intra_pred_flag = pic->pps->constrained_intra_pred_flag;
   h265->pps_flags.transform_skip_enabled_flag = pic->pps->transform_skip_enabled_flag;
   h265->pps_flags.cu_qp_delta_enabled_flag = pic->pps->cu_qp_delta_enabled_flag;
   h265->pps_flags.pps_slice_chroma_qp_offsets_present_flag = pic->pps->pps_slice_chroma_qp_offsets_present_flag;
   h265->pps_flags.weighted_pred_flag = pic->pps->weighted_pred_flag;
   h265->pps_flags.weighted_bipred_flag = pic->pps->weighted_bipred_flag;
   h265->pps_flags.transquant_bypass_enabled_flag = pic->pps->transquant_bypass_enabled_flag;
   h265->pps_flags.tiles_enabled_flag = pic->pps->tiles_enabled_flag;
   h265->pps_flags.entropy_coding_sync_enabled_flag = pic->pps->entropy_coding_sync_enabled_flag;
   h265->pps_flags.uniform_spacing_flag = pic->pps->uniform_spacing_flag;
   h265->pps_flags.loop_filter_across_tiles_enabled_flag = pic->pps->loop_filter_across_tiles_enabled_flag;
   h265->pps_flags.pps_loop_filter_across_slices_enabled_flag = pic->pps->pps_loop_filter_across_slices_enabled_flag;
   h265->pps_flags.deblocking_filter_override_enabled_flag = pic->pps->deblocking_filter_override_enabled_flag;
   h265->pps_flags.pps_deblocking_filter_disabled_flag = pic->pps->pps_deblocking_filter_disabled_flag;
   h265->pps_flags.lists_modification_present_flag = pic->pps->lists_modification_present_flag;
   h265->pps_flags.slice_segment_header_extension_present_flag = pic->pps->slice_segment_header_extension_present_flag;

   h265->pic_flags.irap_pic_flag = pic->IntraPicFlag;
   h265->pic_flags.idr_pic_flag = pic->IDRPicFlag;
   h265->pic_flags.is_ref_pic_flag = 1;

   h265->sps_max_dec_pic_buffering_minus1 = pic->pps->sps->sps_max_dec_pic_buffering_minus1;
   h265->chroma_format_idc = pic->pps->sps->chroma_format_idc;
   h265->pic_width_in_luma_samples = pic->pps->sps->pic_width_in_luma_samples;
   h265->pic_height_in_luma_samples = pic->pps->sps->pic_height_in_luma_samples;
   h265->bit_depth_luma_minus8 = pic->pps->sps->bit_depth_luma_minus8;
   h265->bit_depth_chroma_minus8 = pic->pps->sps->bit_depth_chroma_minus8;
   h265->log2_max_pic_order_cnt_lsb_minus4 = pic->pps->sps->log2_max_pic_order_cnt_lsb_minus4;
   h265->log2_min_luma_coding_block_size_minus3 = pic->pps->sps->log2_min_luma_coding_block_size_minus3;
   h265->log2_diff_max_min_luma_coding_block_size = pic->pps->sps->log2_diff_max_min_luma_coding_block_size;
   h265->log2_min_transform_block_size_minus2 = pic->pps->sps->log2_min_transform_block_size_minus2;
   h265->log2_diff_max_min_transform_block_size = pic->pps->sps->log2_diff_max_min_transform_block_size;
   h265->max_transform_hierarchy_depth_inter = pic->pps->sps->max_transform_hierarchy_depth_inter;
   h265->max_transform_hierarchy_depth_intra = pic->pps->sps->max_transform_hierarchy_depth_intra;
   h265->pcm_sample_bit_depth_luma_minus1 = pic->pps->sps->pcm_sample_bit_depth_luma_minus1;
   h265->pcm_sample_bit_depth_chroma_minus1 = pic->pps->sps->pcm_sample_bit_depth_chroma_minus1;
   h265->log2_min_pcm_luma_coding_block_size_minus3 = pic->pps->sps->log2_min_pcm_luma_coding_block_size_minus3;
   h265->log2_diff_max_min_pcm_luma_coding_block_size = pic->pps->sps->log2_diff_max_min_pcm_luma_coding_block_size;
   h265->num_extra_slice_header_bits = pic->pps->num_extra_slice_header_bits;
   h265->init_qp_minus26 = pic->pps->init_qp_minus26;
   h265->diff_cu_qp_delta_depth = pic->pps->diff_cu_qp_delta_depth;
   h265->pps_cb_qp_offset = pic->pps->pps_cb_qp_offset;
   h265->pps_cr_qp_offset = pic->pps->pps_cr_qp_offset;
   h265->pps_beta_offset_div2 = pic->pps->pps_beta_offset_div2;
   h265->pps_tc_offset_div2 = pic->pps->pps_tc_offset_div2;
   h265->log2_parallel_merge_level_minus2 = pic->pps->log2_parallel_merge_level_minus2;
   h265->num_tile_columns_minus1 = pic->pps->num_tile_columns_minus1;
   h265->num_tile_rows_minus1 = pic->pps->num_tile_rows_minus1;

   for (unsigned i = 0; i < H265_TILE_COLS_LIST_SIZE; i++)
      h265->column_width_minus1[i] = pic->pps->column_width_minus1[i];

   for (unsigned i = 0; i < H265_TILE_ROWS_LIST_SIZE; i++)
      h265->row_height_minus1[i] = pic->pps->row_height_minus1[i];

   memcpy(h265->scaling_list_4x4, pic->pps->sps->ScalingList4x4, sizeof(h265->scaling_list_4x4));
   memcpy(h265->scaling_list_8x8, pic->pps->sps->ScalingList8x8, sizeof(h265->scaling_list_8x8));
   memcpy(h265->scaling_list_16x16, pic->pps->sps->ScalingList16x16, sizeof(h265->scaling_list_16x16));
   memcpy(h265->scaling_list_32x32, pic->pps->sps->ScalingList32x32, sizeof(h265->scaling_list_32x32));
   memcpy(h265->scaling_list_dc_coef_16x16, pic->pps->sps->ScalingListDCCoeff16x16, sizeof(h265->scaling_list_dc_coef_16x16));
   memcpy(h265->scaling_list_dc_coef_32x32, pic->pps->sps->ScalingListDCCoeff32x32, sizeof(h265->scaling_list_dc_coef_32x32));

   h265->num_short_term_ref_pic_sets = pic->pps->sps->num_short_term_ref_pic_sets;
   h265->num_long_term_ref_pics_sps = pic->pps->sps->num_long_term_ref_pics_sps;
   h265->num_ref_idx_l0_default_active_minus1 = pic->pps->num_ref_idx_l0_default_active_minus1;
   h265->num_ref_idx_l1_default_active_minus1 = pic->pps->num_ref_idx_l1_default_active_minus1;
   h265->num_delta_pocs_of_ref_rps_idx = pic->NumDeltaPocsOfRefRpsIdx;
   h265->num_bits_for_st_ref_pic_set_in_slice = pic->NumShortTermPictureSliceHeaderBits;
   h265->curr_poc = pic->CurrPicOrderCntVal;

   memcpy(h265->ref_pic_set_st_curr_before, pic->RefPicSetStCurrBefore, sizeof(h265->ref_pic_set_st_curr_before));
   memcpy(h265->ref_pic_set_st_curr_after, pic->RefPicSetStCurrAfter, sizeof(h265->ref_pic_set_st_curr_after));
   memcpy(h265->ref_pic_set_lt_curr, pic->RefPicSetLtCurr, sizeof(h265->ref_pic_set_lt_curr));

   h265->curr_pic_id = 0x7f;
   for (unsigned i = 0; i < H265_MAX_NUM_REF_PICS; i++)
      h265->ref_pic_id_list[i] = 0x7f;

   uint32_t valid_ref = UINT32_MAX;

   for (unsigned i = 0; i < 16; i++) {
      if (vid->render_pic_list[i]) {
         bool found = false;
         for (unsigned j = 0; j < 15; j++) {
            if (vid->render_pic_list[i] == pic->ref[j]) {
               h265->ref_poc_list[j] = pic->PicOrderCntVal[j];
               h265->ref_pic_id_list[j] = i;
               si_dec_fill_surface(vid, ((struct vl_video_buffer *)pic->ref[j])->resources[0],
                                   RADEON_USAGE_READWRITE, &cmd.ref_surfaces[cmd.num_refs]);
               cmd.ref_id[cmd.num_refs++] = i;
               valid_ref = j;
               found = true;
            }
         }
         if (!found)
            vid->render_pic_list[i] = NULL;
      }
      if (h265->curr_pic_id == 0x7f && !vid->render_pic_list[i]) {
         vid->render_pic_list[i] = target;
         h265->curr_pic_id = i;
      }
   }

   if (valid_ref != UINT32_MAX) {
      for (unsigned i = 0; i < 15; i++) {
         if (pic->ref[i] && h265->ref_pic_id_list[i] == 0x7f) {
            h265->ref_poc_list[i] = pic->PicOrderCntVal[valid_ref];
            h265->ref_pic_id_list[i] = h265->ref_pic_id_list[valid_ref];
         }
      }
   }

   cmd.cur_id = h265->curr_pic_id;
   return decode_cmd_build(vid, &pic->base, &cmd);
}

static int si_dec_mjpeg(struct si_video_dec *vid, struct pipe_video_buffer *target,
                        struct pipe_mjpeg_picture_desc *pic)
{
   if (!vid->dec) {
      struct ac_video_dec_session_param param = {
         .codec = AC_VIDEO_CODEC_MJPEG,
         .max_width = vid->base.width,
         .max_height = vid->base.height,
         .max_bit_depth = 8,
      };
      switch (pic->picture_parameter.sampling_factor) {
      case 0x221111:
         param.sub_sample = AC_VIDEO_SUBSAMPLE_420;
         break;
      case 0x211111:
      case 0x221212:
      case 0x222121:
      case 0x121111:
         param.sub_sample = AC_VIDEO_SUBSAMPLE_422;
         break;
      case 0x111111:
      case 0x222222:
      case 0x444444:
         param.sub_sample = AC_VIDEO_SUBSAMPLE_444;
         break;
      case 0x11:
      case 0x44:
         param.sub_sample = AC_VIDEO_SUBSAMPLE_400;
         break;
      default:
         ERROR("Unsupported sampling factor 0x%x", pic->picture_parameter.sampling_factor);
         return 1;
      }
      int ret = si_dec_init_decoder(vid, &param, pic->base.protected_playback);
      if (ret) {
         ERROR("Failed to create mjpeg decoder");
         return ret;
      }
   }

   struct ac_video_dec_decode_cmd cmd;
   decode_cmd_init(vid, &pic->base, target, &cmd);

   struct ac_video_dec_mjpeg *mjpeg = &cmd.codec_param.mjpeg;
   mjpeg->crop_x = pic->picture_parameter.crop_x;
   mjpeg->crop_y = pic->picture_parameter.crop_y;
   mjpeg->crop_width = pic->picture_parameter.crop_width;
   mjpeg->crop_height = pic->picture_parameter.crop_height;
   return decode_cmd_build(vid, &pic->base, &cmd);
}

static int si_dec_vp9(struct si_video_dec *vid, struct pipe_video_buffer *target,
                      struct pipe_vp9_picture_desc *pic)
{
   if (!vid->dec) {
      struct ac_video_dec_session_param param = {
         .codec = AC_VIDEO_CODEC_VP9,
         .sub_sample = AC_VIDEO_SUBSAMPLE_420,
         .max_width = vid->base.width,
         .max_height = vid->base.height,
         .max_bit_depth = vid->base.profile == PIPE_VIDEO_PROFILE_VP9_PROFILE2 ? 10 : 8,
         .max_num_ref = 9,
      };
      int ret = si_dec_init_decoder(vid, &param, pic->base.protected_playback);
      if (ret) {
         ERROR("Failed to create vp9 decoder");
         return ret;
      }
   }

   struct ac_video_dec_decode_cmd cmd;
   decode_cmd_init(vid, &pic->base, target, &cmd);

   struct ac_video_dec_vp9 *vp9 = &cmd.codec_param.vp9;
   vp9->pic_flags.error_resilient_mode = pic->picture_parameter.pic_fields.error_resilient_mode;
   vp9->pic_flags.intra_only = pic->picture_parameter.pic_fields.intra_only;
   vp9->pic_flags.allow_high_precision_mv = pic->picture_parameter.pic_fields.allow_high_precision_mv;
   vp9->pic_flags.refresh_frame_context = pic->picture_parameter.pic_fields.refresh_frame_context;
   vp9->pic_flags.frame_parallel_decoding_mode = pic->picture_parameter.pic_fields.frame_parallel_decoding_mode;
   vp9->pic_flags.use_prev_frame_mvs = pic->picture_parameter.pic_fields.use_prev_frame_mvs;
   vp9->pic_flags.show_frame = pic->picture_parameter.pic_fields.show_frame;
   vp9->pic_flags.use_uncompressed_header = 1;

   vp9->color_config_flags.subsampling_x = pic->picture_parameter.pic_fields.subsampling_x;
   vp9->color_config_flags.subsampling_y = pic->picture_parameter.pic_fields.subsampling_y;

   vp9->profile = pic->picture_parameter.profile;
   vp9->width = pic->picture_parameter.frame_width;
   vp9->height = pic->picture_parameter.frame_height;
   vp9->frame_context_idx = pic->picture_parameter.pic_fields.frame_context_idx;
   vp9->reset_frame_context = pic->picture_parameter.pic_fields.reset_frame_context;
   vp9->bit_depth_luma_minus8 = pic->picture_parameter.bit_depth - 8;
   vp9->bit_depth_chroma_minus8 = pic->picture_parameter.bit_depth - 8;
   vp9->frame_type = pic->picture_parameter.pic_fields.frame_type;
   vp9->interp_filter = pic->picture_parameter.pic_fields.mcomp_filter_type;
   vp9->base_q_idx = pic->picture_parameter.base_qindex;
   vp9->y_dc_delta_q = pic->picture_parameter.y_dc_delta_q;
   vp9->uv_ac_delta_q = pic->picture_parameter.uv_ac_delta_q;
   vp9->uv_dc_delta_q = pic->picture_parameter.uv_dc_delta_q;
   vp9->log2_tile_cols = pic->picture_parameter.log2_tile_columns;
   vp9->log2_tile_rows = pic->picture_parameter.log2_tile_rows;
   vp9->compressed_header_size = pic->picture_parameter.first_partition_size;
   vp9->uncompressed_header_size = pic->picture_parameter.frame_header_length_in_bytes;

   vp9->loop_filter.loop_filter_flags.mode_ref_delta_enabled = pic->picture_parameter.mode_ref_delta_enabled;
   vp9->loop_filter.loop_filter_flags.mode_ref_delta_update = pic->picture_parameter.mode_ref_delta_update;
   vp9->loop_filter.loop_filter_level = pic->picture_parameter.filter_level;
   vp9->loop_filter.loop_filter_sharpness = pic->picture_parameter.sharpness_level;
   memcpy(vp9->loop_filter.loop_filter_ref_deltas, pic->picture_parameter.ref_deltas, sizeof(vp9->loop_filter.loop_filter_ref_deltas));
   memcpy(vp9->loop_filter.loop_filter_mode_deltas, pic->picture_parameter.mode_deltas, sizeof(vp9->loop_filter.loop_filter_mode_deltas));

   vp9->segmentation.flags.segmentation_enabled = pic->picture_parameter.pic_fields.segmentation_enabled;
   if (vp9->segmentation.flags.segmentation_enabled) {
      vp9->segmentation.flags.segmentation_update_map = pic->picture_parameter.pic_fields.segmentation_update_map;
      vp9->segmentation.flags.segmentation_temporal_update = pic->picture_parameter.pic_fields.segmentation_temporal_update;
      vp9->segmentation.flags.segmentation_update_data = pic->picture_parameter.pic_fields.segmentation_update_data;
      vp9->segmentation.flags.segmentation_abs_delta = pic->picture_parameter.abs_delta;
      for (unsigned i = 0; i < VP9_MAX_SEGMENTS; i++) {
         vp9->segmentation.feature_data[i][0] = pic->slice_parameter.seg_param[i].alt_quant;
         vp9->segmentation.feature_data[i][1] = pic->slice_parameter.seg_param[i].alt_lf;
         vp9->segmentation.feature_data[i][2] = pic->slice_parameter.seg_param[i].segment_flags.segment_reference;
         vp9->segmentation.feature_data[i][3] = 0;
         vp9->segmentation.feature_mask[i] = (pic->slice_parameter.seg_param[i].alt_quant_enabled << 0) |
            (pic->slice_parameter.seg_param[i].alt_lf_enabled << 1) |
            (pic->slice_parameter.seg_param[i].segment_flags.segment_reference_enabled << 2) |
            (pic->slice_parameter.seg_param[i].segment_flags.segment_reference_skipped << 3);
      }

      for (unsigned i = 0; i < VP9_MAX_SEGMENTATION_TREE_PROBS; i++)
         vp9->segmentation.tree_probs[i] = pic->picture_parameter.mb_segment_tree_probs[i];

      for (unsigned i = 0; i < VP9_MAX_SEGMENTATION_PRED_PROBS; i++)
         vp9->segmentation.pred_probs[i] = pic->picture_parameter.segment_pred_probs[i];
   }

   vp9->cur_id = 0x7f;
   for (unsigned i = 0; i < VP9_NUM_REF_FRAMES; i++)
      vp9->ref_frame_id_list[i] = 0x7f;

   uint32_t valid_ref = UINT32_MAX;

   for (unsigned i = 0; i < VP9_NUM_REF_FRAMES + 1; i++) {
      if (vid->render_pic_list[i]) {
         bool found = false;
         for (unsigned j = 0; j < VP9_NUM_REF_FRAMES; j++) {
            if (vid->render_pic_list[i] == pic->ref[j]) {
               vp9->ref_frame_id_list[j] = i;
               si_dec_fill_surface(vid, ((struct vl_video_buffer *)pic->ref[j])->resources[0],
                                   RADEON_USAGE_READWRITE, &cmd.ref_surfaces[cmd.num_refs]);
               cmd.ref_id[cmd.num_refs++] = i;
               valid_ref = j;
               found = true;
            }
         }
         if (!found)
            vid->render_pic_list[i] = NULL;
      }
      if (vp9->cur_id == 0x7f && !vid->render_pic_list[i]) {
         vid->render_pic_list[i] = target;
         vp9->cur_id = i;
      }
   }

   for (unsigned i = 0; i < VP9_NUM_REF_FRAMES; i++) {
      if (pic->ref[i] && vp9->ref_frame_id_list[i] == 0x7f)
         vp9->ref_frame_id_list[i] = valid_ref == UINT32_MAX ? 0 : vp9->ref_frame_id_list[valid_ref];
   }

   vp9->ref_frames[0] = vp9->ref_frame_id_list[pic->picture_parameter.pic_fields.last_ref_frame];
   vp9->ref_frame_sign_bias[1] = pic->picture_parameter.pic_fields.last_ref_frame_sign_bias;
   vp9->ref_frames[1] = vp9->ref_frame_id_list[pic->picture_parameter.pic_fields.golden_ref_frame];
   vp9->ref_frame_sign_bias[2] = pic->picture_parameter.pic_fields.golden_ref_frame_sign_bias;
   vp9->ref_frames[2] = vp9->ref_frame_id_list[pic->picture_parameter.pic_fields.alt_ref_frame];
   vp9->ref_frame_sign_bias[3] = pic->picture_parameter.pic_fields.alt_ref_frame_sign_bias;

   cmd.cur_id = vp9->cur_id;
   return decode_cmd_build(vid, &pic->base, &cmd);
}

static int si_dec_av1(struct si_video_dec *vid, struct pipe_video_buffer *target,
                      struct pipe_av1_picture_desc *pic)
{
   if (!vid->dec) {
      struct ac_video_dec_session_param param = {
         .codec = AC_VIDEO_CODEC_AV1,
         .sub_sample = AC_VIDEO_SUBSAMPLE_420,
         .max_width = vid->base.width,
         .max_height = vid->base.height,
         .max_bit_depth = 8 + (pic->picture_parameter.bit_depth_idx << 1),
         .max_num_ref = 9,
      };
      int ret = si_dec_init_decoder(vid, &param, pic->base.protected_playback);
      if (ret) {
         ERROR("Failed to create av1 decoder");
         return ret;
      }
   }

   struct ac_video_dec_decode_cmd cmd;
   decode_cmd_init(vid, &pic->base, pic->film_grain_target ? pic->film_grain_target : target, &cmd);

   struct ac_video_dec_av1 *av1 = &cmd.codec_param.av1;
   av1->width = pic->picture_parameter.frame_width;
   av1->height = pic->picture_parameter.frame_height;
   av1->max_width = pic->picture_parameter.max_width;
   av1->max_height = pic->picture_parameter.max_height;
   av1->superres_denom = pic->picture_parameter.superres_scale_denominator;
   av1->bit_depth = (pic->picture_parameter.bit_depth_idx << 1) + 8;
   av1->seq_profile = pic->picture_parameter.profile;
   av1->tx_mode = pic->picture_parameter.mode_control_fields.tx_mode;
   av1->frame_type = pic->picture_parameter.pic_info_fields.frame_type;
   av1->primary_ref_frame = pic->picture_parameter.primary_ref_frame;
   av1->order_hints = pic->picture_parameter.order_hint;
   av1->order_hint_bits = pic->picture_parameter.order_hint_bits_minus_1 + 1;
   av1->interp_filter = pic->picture_parameter.interp_filter;

   av1->loop_filter.loop_filter_flags.mode_ref_delta_enabled = pic->picture_parameter.loop_filter_info_fields.mode_ref_delta_enabled;
   av1->loop_filter.loop_filter_flags.mode_ref_delta_update = pic->picture_parameter.loop_filter_info_fields.mode_ref_delta_update;
   av1->loop_filter.loop_filter_flags.delta_lf_multi = pic->picture_parameter.mode_control_fields.delta_lf_multi;
   av1->loop_filter.loop_filter_flags.delta_lf_present = pic->picture_parameter.mode_control_fields.delta_lf_present_flag;
   av1->loop_filter.loop_filter_level[0] = pic->picture_parameter.filter_level[0];
   av1->loop_filter.loop_filter_level[1] = pic->picture_parameter.filter_level[1];
   av1->loop_filter.loop_filter_level[2] = pic->picture_parameter.filter_level_u;
   av1->loop_filter.loop_filter_level[3] = pic->picture_parameter.filter_level_v;
   av1->loop_filter.loop_filter_sharpness = pic->picture_parameter.loop_filter_info_fields.sharpness_level;
   memcpy(av1->loop_filter.loop_filter_ref_deltas, pic->picture_parameter.ref_deltas, sizeof(av1->loop_filter.loop_filter_ref_deltas));
   memcpy(av1->loop_filter.loop_filter_mode_deltas, pic->picture_parameter.mode_deltas, sizeof(av1->loop_filter.loop_filter_mode_deltas));
   av1->loop_filter.delta_lf_res = pic->picture_parameter.mode_control_fields.log2_delta_lf_res;

   av1->loop_restoration.frame_restoration_type[0] = pic->picture_parameter.loop_restoration_fields.yframe_restoration_type;
   av1->loop_restoration.frame_restoration_type[1] = pic->picture_parameter.loop_restoration_fields.cbframe_restoration_type;
   av1->loop_restoration.frame_restoration_type[2] = pic->picture_parameter.loop_restoration_fields.crframe_restoration_type;
   for (unsigned i = 0; i < AV1_MAX_NUM_PLANES; i++)
      av1->loop_restoration.log2_restoration_size_minus5[i] = util_logbase2(pic->picture_parameter.lr_unit_size[i]) - 5;

   av1->quantization.flags.delta_q_present = pic->picture_parameter.mode_control_fields.delta_q_present_flag;
   av1->quantization.delta_q_res = pic->picture_parameter.mode_control_fields.log2_delta_q_res;
   av1->quantization.base_q_idx = pic->picture_parameter.base_qindex;
   av1->quantization.delta_q_y_dc = pic->picture_parameter.y_dc_delta_q;
   av1->quantization.delta_q_u_dc = pic->picture_parameter.u_dc_delta_q;
   av1->quantization.delta_q_u_ac = pic->picture_parameter.u_ac_delta_q;
   av1->quantization.delta_q_v_dc = pic->picture_parameter.v_dc_delta_q;
   av1->quantization.delta_q_v_ac = pic->picture_parameter.v_ac_delta_q;
   av1->quantization.qm_y = pic->picture_parameter.qmatrix_fields.qm_y | 0xf0;
   av1->quantization.qm_u = pic->picture_parameter.qmatrix_fields.qm_u | 0xf0;
   av1->quantization.qm_v = pic->picture_parameter.qmatrix_fields.qm_v | 0xf0;

   av1->segmentation.flags.segmentation_enabled = pic->picture_parameter.seg_info.segment_info_fields.enabled;
   av1->segmentation.flags.segmentation_update_map = pic->picture_parameter.seg_info.segment_info_fields.update_map;
   av1->segmentation.flags.segmentation_temporal_update = pic->picture_parameter.seg_info.segment_info_fields.temporal_update;
   av1->segmentation.flags.segmentation_update_data = pic->picture_parameter.seg_info.segment_info_fields.update_data;
   memcpy(av1->segmentation.feature_mask, pic->picture_parameter.seg_info.feature_mask, sizeof(av1->segmentation.feature_mask));
   memcpy(av1->segmentation.feature_data, pic->picture_parameter.seg_info.feature_data, sizeof(av1->segmentation.feature_data));

   av1->cdef.cdef_damping_minus3 = pic->picture_parameter.cdef_damping_minus_3;
   av1->cdef.cdef_bits = pic->picture_parameter.cdef_bits;
   for (unsigned i = 0; i < AV1_MAX_CDEF_FILTER_STRENGTHS; i++) {
      av1->cdef.cdef_y_pri_strength[i] = pic->picture_parameter.cdef_y_strengths[i] >> 2;
      av1->cdef.cdef_y_sec_strength[i] = pic->picture_parameter.cdef_y_strengths[i] & 0x3;
      av1->cdef.cdef_uv_pri_strength[i] = pic->picture_parameter.cdef_uv_strengths[i] >> 2;
      av1->cdef.cdef_uv_sec_strength[i] = pic->picture_parameter.cdef_uv_strengths[i] & 0x3;
   }

   av1->film_grain.flags.apply_grain = pic->picture_parameter.film_grain_info.film_grain_info_fields.apply_grain;
   if (av1->film_grain.flags.apply_grain) {
      av1->film_grain.flags.chroma_scaling_from_luma = pic->picture_parameter.film_grain_info.film_grain_info_fields.chroma_scaling_from_luma;
      av1->film_grain.flags.overlap_flag = pic->picture_parameter.film_grain_info.film_grain_info_fields.overlap_flag;
      av1->film_grain.flags.clip_to_restricted_range = pic->picture_parameter.film_grain_info.film_grain_info_fields.clip_to_restricted_range;
      av1->film_grain.grain_scaling_minus8 = pic->picture_parameter.film_grain_info.film_grain_info_fields.grain_scaling_minus_8;
      av1->film_grain.ar_coeff_lag = pic->picture_parameter.film_grain_info.film_grain_info_fields.ar_coeff_lag;
      av1->film_grain.ar_coeff_shift_minus6 = pic->picture_parameter.film_grain_info.film_grain_info_fields.ar_coeff_shift_minus_6;
      av1->film_grain.grain_scale_shift = pic->picture_parameter.film_grain_info.film_grain_info_fields.grain_scale_shift;
      av1->film_grain.grain_seed = pic->picture_parameter.film_grain_info.grain_seed;
      av1->film_grain.num_y_points = pic->picture_parameter.film_grain_info.num_y_points;
      memcpy(av1->film_grain.point_y_value, pic->picture_parameter.film_grain_info.point_y_value, sizeof(av1->film_grain.point_y_value));
      memcpy(av1->film_grain.point_y_scaling, pic->picture_parameter.film_grain_info.point_y_scaling, sizeof(av1->film_grain.point_y_scaling));
      av1->film_grain.num_cb_points = pic->picture_parameter.film_grain_info.num_cb_points;
      memcpy(av1->film_grain.point_cb_value, pic->picture_parameter.film_grain_info.point_cb_value, sizeof(av1->film_grain.point_cb_value));
      memcpy(av1->film_grain.point_cb_scaling, pic->picture_parameter.film_grain_info.point_cb_scaling, sizeof(av1->film_grain.point_cb_scaling));
      av1->film_grain.num_cr_points = pic->picture_parameter.film_grain_info.num_cr_points;
      memcpy(av1->film_grain.point_cr_value, pic->picture_parameter.film_grain_info.point_cr_value, sizeof(av1->film_grain.point_cr_value));
      memcpy(av1->film_grain.point_cr_scaling, pic->picture_parameter.film_grain_info.point_cr_scaling, sizeof(av1->film_grain.point_cr_scaling));
      for (unsigned i = 0; i < AV1_MAX_NUM_POS_LUMA; i++)
         av1->film_grain.ar_coeffs_y_plus128[i] = pic->picture_parameter.film_grain_info.ar_coeffs_y[i] + 128;
      for (unsigned i = 0; i < AV1_MAX_NUM_POS_CHROMA; i++) {
         av1->film_grain.ar_coeffs_cb_plus128[i] = pic->picture_parameter.film_grain_info.ar_coeffs_cb[i] + 128;
         av1->film_grain.ar_coeffs_cr_plus128[i] = pic->picture_parameter.film_grain_info.ar_coeffs_cr[i] + 128;
      }
      av1->film_grain.cb_mult = pic->picture_parameter.film_grain_info.cb_mult;
      av1->film_grain.cb_luma_mult = pic->picture_parameter.film_grain_info.cb_luma_mult;
      av1->film_grain.cb_offset = pic->picture_parameter.film_grain_info.cb_offset;
      av1->film_grain.cr_mult = pic->picture_parameter.film_grain_info.cr_mult;
      av1->film_grain.cr_luma_mult = pic->picture_parameter.film_grain_info.cr_luma_mult;
      av1->film_grain.cr_offset = pic->picture_parameter.film_grain_info.cr_offset;
   }

   av1->tile_info.tile_cols = pic->picture_parameter.tile_cols;
   av1->tile_info.tile_rows = pic->picture_parameter.tile_rows;
   av1->tile_info.context_update_tile_id = pic->picture_parameter.context_update_tile_id;
   for (unsigned i = 0; i < AV1_MAX_TILE_COLS + 1; i++) {
      av1->tile_info.tile_col_start_sb[i] = pic->picture_parameter.tile_col_start_sb[i];
      av1->tile_info.tile_row_start_sb[i] = pic->picture_parameter.tile_row_start_sb[i];
   }
   memcpy(av1->tile_info.width_in_sbs, pic->picture_parameter.width_in_sbs, sizeof(av1->tile_info.width_in_sbs));
   memcpy(av1->tile_info.height_in_sbs, pic->picture_parameter.height_in_sbs, sizeof(av1->tile_info.height_in_sbs));
   for (unsigned i = 0; i < AV1_MAX_NUM_TILES; i++) {
      av1->tile_info.tile_offset[i] = pic->slice_parameter.slice_data_offset[i];
      av1->tile_info.tile_size[i] = pic->slice_parameter.slice_data_size[i];
   }

   av1->pic_flags.use_128x128_superblock = pic->picture_parameter.seq_info_fields.use_128x128_superblock;
   av1->pic_flags.enable_filter_intra = pic->picture_parameter.seq_info_fields.enable_filter_intra;
   av1->pic_flags.enable_intra_edge_filter = pic->picture_parameter.seq_info_fields.enable_intra_edge_filter;
   av1->pic_flags.enable_interintra_compound = pic->picture_parameter.seq_info_fields.enable_interintra_compound;
   av1->pic_flags.enable_masked_compound = pic->picture_parameter.seq_info_fields.enable_masked_compound;
   av1->pic_flags.enable_dual_filter = pic->picture_parameter.seq_info_fields.enable_dual_filter;
   av1->pic_flags.enable_jnt_comp = pic->picture_parameter.seq_info_fields.enable_jnt_comp;
   av1->pic_flags.enable_ref_frame_mvs = pic->picture_parameter.seq_info_fields.ref_frame_mvs;
   av1->pic_flags.enable_cdef = pic->picture_parameter.seq_info_fields.enable_cdef;
   av1->pic_flags.enable_restoration =
      pic->picture_parameter.loop_restoration_fields.yframe_restoration_type ||
      pic->picture_parameter.loop_restoration_fields.cbframe_restoration_type ||
      pic->picture_parameter.loop_restoration_fields.crframe_restoration_type;
   av1->pic_flags.film_grain_params_present = pic->picture_parameter.seq_info_fields.film_grain_params_present;
   av1->pic_flags.disable_cdf_update = pic->picture_parameter.pic_info_fields.disable_cdf_update;
   av1->pic_flags.use_superres = pic->picture_parameter.pic_info_fields.use_superres;
   av1->pic_flags.allow_screen_content_tools = pic->picture_parameter.pic_info_fields.allow_screen_content_tools;
   av1->pic_flags.force_integer_mv = pic->picture_parameter.pic_info_fields.force_integer_mv;
   av1->pic_flags.allow_intrabc = pic->picture_parameter.pic_info_fields.allow_intrabc;
   av1->pic_flags.allow_high_precision_mv = pic->picture_parameter.pic_info_fields.allow_high_precision_mv;
   av1->pic_flags.is_motion_mode_switchable = pic->picture_parameter.pic_info_fields.is_motion_mode_switchable;
   av1->pic_flags.use_ref_frame_mvs = pic->picture_parameter.pic_info_fields.use_ref_frame_mvs;
   av1->pic_flags.disable_frame_end_update_cdf = pic->picture_parameter.pic_info_fields.disable_frame_end_update_cdf;
   av1->pic_flags.allow_warped_motion = pic->picture_parameter.pic_info_fields.allow_warped_motion;
   av1->pic_flags.reduced_tx_set = pic->picture_parameter.mode_control_fields.reduced_tx_set_used;
   av1->pic_flags.reference_select = pic->picture_parameter.mode_control_fields.reference_select;
   av1->pic_flags.skip_mode_present = pic->picture_parameter.mode_control_fields.skip_mode_present;
   av1->pic_flags.show_frame = pic->picture_parameter.pic_info_fields.show_frame;
   av1->pic_flags.showable_frame = pic->picture_parameter.pic_info_fields.showable_frame;
   av1->pic_flags.ref_frame_update = !!pic->picture_parameter.refresh_frame_flags;

   av1->color_config_flags.mono_chrome = pic->picture_parameter.seq_info_fields.mono_chrome;
   av1->color_config_flags.subsampling_x = pic->picture_parameter.seq_info_fields.subsampling_x;
   av1->color_config_flags.subsampling_y = pic->picture_parameter.seq_info_fields.subsampling_y;

   for (unsigned i = 1; i < AV1_NUM_REF_FRAMES; i++) {
      av1->global_motion.gm_type[i] = pic->picture_parameter.wm[i - 1].wmtype;
      for (unsigned j = 0; j < AV1_GLOBAL_MOTION_PARAMS; j++)
         av1->global_motion.gm_params[i][j] = pic->picture_parameter.wm[i - 1].wmmat[j];
   }

   av1->cur_id = 0x7f;
   for (unsigned i = 0; i < AV1_NUM_REF_FRAMES; i++)
      av1->ref_frame_id_list[i] = 0x7f;

   uint32_t valid_ref = UINT32_MAX;

   for (unsigned i = 0; i < AV1_NUM_REF_FRAMES + 1; i++) {
      if (vid->render_pic_list[i]) {
         bool found = false;
         for (unsigned j = 0; j < AV1_NUM_REF_FRAMES; j++) {
            if (vid->render_pic_list[i] == pic->ref[j]) {
               av1->ref_frame_id_list[j] = i;
               si_dec_fill_surface(vid, ((struct vl_video_buffer *)pic->ref[j])->resources[0],
                                   RADEON_USAGE_READWRITE, &cmd.ref_surfaces[cmd.num_refs]);
               cmd.ref_id[cmd.num_refs++] = i;
               valid_ref = j;
               found = true;
            }
         }
         if (!found)
            vid->render_pic_list[i] = NULL;
      }
      if (av1->cur_id == 0x7f && !vid->render_pic_list[i]) {
         vid->render_pic_list[i] = target;
         av1->cur_id = i;
      }
   }

   /* Film grain is applied to decode target only. */
   if (cmd.tier == AC_VIDEO_DEC_TIER3 && pic->film_grain_target) {
      si_dec_fill_surface(vid, ((struct vl_video_buffer *)target)->resources[0],
                          RADEON_USAGE_READWRITE, &cmd.ref_surfaces[cmd.num_refs]);
      cmd.ref_id[cmd.num_refs++] = av1->cur_id;
   }

   for (unsigned i = 0; i < AV1_NUM_REF_FRAMES; i++) {
      if (pic->ref[i] && av1->ref_frame_id_list[i] == 0x7f)
         av1->ref_frame_id_list[i] = valid_ref == UINT32_MAX ? 0 : av1->ref_frame_id_list[valid_ref];
   }

   for (unsigned i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++)
       av1->ref_frames[i].ref_id = av1->ref_frame_id_list[pic->picture_parameter.ref_frame_idx[i]];

   cmd.cur_id = av1->cur_id;
   return decode_cmd_build(vid, &pic->base, &cmd);
}

static int si_dec_mpeg12(struct si_video_dec *vid, struct pipe_video_buffer *target,
                         struct pipe_mpeg12_picture_desc *pic)
{
   if (!vid->dec) {
      struct ac_video_dec_session_param param = {
         .codec = AC_VIDEO_CODEC_MPEG2,
         .sub_sample = AC_VIDEO_SUBSAMPLE_420,
         .max_width = vid->base.width,
         .max_height = vid->base.height,
         .max_bit_depth = 8,
         .max_num_ref = 6,
      };
      int ret = si_dec_init_decoder(vid, &param, pic->base.protected_playback);
      if (ret) {
         ERROR("Failed to create mpeg12 decoder");
         return ret;
      }
   }

   struct ac_video_dec_decode_cmd cmd;
   decode_cmd_init(vid, &pic->base, target, &cmd);

   struct ac_video_dec_mpeg2 *mpeg2 = &cmd.codec_param.mpeg2;
   const int *zscan = pic->alternate_scan ? vl_zscan_alternate : vl_zscan_normal;
   if (pic->intra_matrix) {
      mpeg2->load_intra_quantiser_matrix = 1;
      for (unsigned i = 0; i < 64; i++)
         mpeg2->intra_quantiser_matrix[i] = pic->intra_matrix[zscan[i]];
   }
   if (pic->non_intra_matrix) {
      mpeg2->load_nonintra_quantiser_matrix = 1;
      for (unsigned i = 0; i < 64; i++)
         mpeg2->nonintra_quantiser_matrix[i] = pic->non_intra_matrix[zscan[i]];
   }
   mpeg2->picture_coding_type = pic->picture_coding_type;
   mpeg2->f_code[0][0] = pic->f_code[0][0] + 1;
   mpeg2->f_code[0][1] = pic->f_code[0][1] + 1;
   mpeg2->f_code[1][0] = pic->f_code[1][0] + 1;
   mpeg2->f_code[1][1] = pic->f_code[1][1] + 1;
   mpeg2->intra_dc_precision = pic->intra_dc_precision;
   mpeg2->pic_structure = pic->picture_structure;
   mpeg2->top_field_first = pic->top_field_first;
   mpeg2->frame_pred_frame_dct = pic->frame_pred_frame_dct;
   mpeg2->concealment_motion_vectors = pic->concealment_motion_vectors;
   mpeg2->q_scale_type = pic->q_scale_type;
   mpeg2->intra_vlc_format = pic->intra_vlc_format;
   mpeg2->alternate_scan = pic->alternate_scan;

   return decode_cmd_build(vid, &pic->base, &cmd);
}

static int si_dec_vc1(struct si_video_dec *vid, struct pipe_video_buffer *target,
                      struct pipe_vc1_picture_desc *pic)
{
   if (!vid->dec) {
      struct ac_video_dec_session_param param = {
         .codec = AC_VIDEO_CODEC_VC1,
         .sub_sample = AC_VIDEO_SUBSAMPLE_420,
         .max_width = vid->base.width,
         .max_height = vid->base.height,
         .max_bit_depth = 8,
         .max_num_ref = 5,
      };
      int ret = si_dec_init_decoder(vid, &param, pic->base.protected_playback);
      if (ret) {
         ERROR("Failed to create vc1 decoder");
         return ret;
      }
   }

   struct ac_video_dec_decode_cmd cmd;
   decode_cmd_init(vid, &pic->base, target, &cmd);

   struct ac_video_dec_vc1 *vc1 = &cmd.codec_param.vc1;
   vc1->postprocflag = pic->postprocflag;
   vc1->pulldown = pic->pulldown;
   vc1->interlace = pic->interlace;
   vc1->tfcntrflag = pic->tfcntrflag;
   vc1->finterpflag = pic->finterpflag;
   vc1->psf = pic->psf;
   vc1->range_mapy_flag = pic->range_mapy_flag;
   vc1->range_mapy = pic->range_mapy;
   vc1->range_mapuv_flag = pic->range_mapuv_flag;
   vc1->range_mapuv = pic->range_mapuv;
   vc1->multires = pic->multires;
   vc1->maxbframes = pic->maxbframes;
   vc1->overlap = pic->overlap;
   vc1->quantizer = pic->quantizer;
   vc1->panscan_flag = pic->panscan_flag;
   vc1->refdist_flag = pic->refdist_flag;
   vc1->vstransform = pic->vstransform;

   if (vid->base.profile != PIPE_VIDEO_PROFILE_VC1_SIMPLE) {
      vc1->syncmarker = pic->syncmarker;
      vc1->rangered = pic->rangered;
      vc1->loopfilter = pic->loopfilter;
      vc1->fastuvmc = pic->fastuvmc;
      vc1->extended_mv = pic->extended_mv;
      vc1->extended_dmv = pic->extended_dmv;
      vc1->dquant = pic->dquant;
   }

   switch (vid->base.profile) {
   case PIPE_VIDEO_PROFILE_VC1_SIMPLE:
      vc1->profile = 0;
      vc1->level = 1;
      break;
   case PIPE_VIDEO_PROFILE_VC1_MAIN:
      vc1->profile = 1;
      vc1->level = 2;
      break;
   case PIPE_VIDEO_PROFILE_VC1_ADVANCED:
      vc1->profile = 2;
      vc1->level = 4;
      break;
   default:
      UNREACHABLE("Invalid vc1 profile");
   }

   return decode_cmd_build(vid, &pic->base, &cmd);
}

static int si_dec_end_frame(struct pipe_video_codec *decoder, struct pipe_video_buffer *target,
                            struct pipe_picture_desc *picture)
{
   struct si_video_dec *vid = (struct si_video_dec *)decoder;
   int ret;

   if (!vid->bs_ptr)
      ERROR("No bitstream to decode");

   if (vid->error)
      return 1;

   switch (u_reduce_video_profile(vid->base.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      ret = si_dec_h264(vid, target, (struct pipe_h264_picture_desc *)picture);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      ret = si_dec_h265(vid, target, (struct pipe_h265_picture_desc *)picture);
      break;

   case PIPE_VIDEO_FORMAT_JPEG:
      ret = si_dec_mjpeg(vid, target, (struct pipe_mjpeg_picture_desc *)picture);
      break;

   case PIPE_VIDEO_FORMAT_VP9:
      ret = si_dec_vp9(vid, target, (struct pipe_vp9_picture_desc *)picture);
      break;

   case PIPE_VIDEO_FORMAT_AV1:
      ret = si_dec_av1(vid, target, (struct pipe_av1_picture_desc *)picture);
      break;

   case PIPE_VIDEO_FORMAT_MPEG12:
      ret = si_dec_mpeg12(vid, target, (struct pipe_mpeg12_picture_desc *)picture);
      break;

   case PIPE_VIDEO_FORMAT_VC1:
      ret = si_dec_vc1(vid, target, (struct pipe_vc1_picture_desc *)picture);
      break;

   default:
      ERROR("Unsupported codec");
      ret = -ENOTSUP;
      break;
   }

   if (ret == 0)
      ret = si_dec_flush(vid, picture->flush_flags, picture->out_fence);

   return ret;
}

static int si_dec_fence_wait(struct pipe_video_codec *decoder, struct pipe_fence_handle *fence,
                             uint64_t timeout)
{
   struct si_video_dec *vid = (struct si_video_dec *)decoder;

   return vid->ws->fence_wait(vid->ws, fence, timeout);
}

static void si_dec_destroy_fence(struct pipe_video_codec *decoder, struct pipe_fence_handle *fence)
{
   struct si_video_dec *vid = (struct si_video_dec *)decoder;

   vid->ws->fence_reference(vid->ws, &fence, NULL);
}

static void si_dec_destroy(struct pipe_video_codec *decoder)
{
   struct si_video_dec *vid = (struct si_video_dec *)decoder;

   if (vid->bs_ptr) {
      vid->ws->buffer_unmap(vid->ws, vid->bs_buffers[vid->cur_buffer]->buf);
      vid->bs_ptr = NULL;
   }

   if (vid->dec) {
      si_dec_destroy_decoder(vid);
      vid->dec->destroy(vid->dec);
      vid->ws->cs_destroy(&vid->cs);
      if (vid->ectx)
         vid->ectx->destroy(vid->ectx);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(vid->bs_buffers); i++) {
      si_resource_reference(&vid->bs_buffers[i], NULL);
      si_resource_reference(&vid->emb_buffers[i], NULL);
   }
   si_resource_reference(&vid->session_buffer, NULL);
   si_resource_reference(&vid->session_tmz_buffer, NULL);

   for (unsigned i = 0; i < ARRAY_SIZE(vid->dpb); i++)
      pipe_resource_reference(&vid->dpb[i], NULL);

   FREE(vid);
}

static struct pipe_video_codec *create_decoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templ)
{
   struct si_context *sctx = (struct si_context *)context;

   struct si_video_dec *dec = CALLOC_STRUCT(si_video_dec);
   if (!dec)
      return NULL;

   dec->base = *templ;
   dec->base.context = context;
   dec->base.destroy = si_dec_destroy;
   dec->base.begin_frame = si_dec_begin_frame;
   dec->base.decode_bitstream = si_dec_decode_bitstream;
   dec->base.end_frame = si_dec_end_frame;
   dec->base.fence_wait = si_dec_fence_wait;
   dec->base.destroy_fence = si_dec_destroy_fence;
   dec->screen = context->screen;
   dec->ws = sctx->ws;

   return &dec->base;
}

static void si_dec_inst_begin_frame(struct pipe_video_codec *decoder, struct pipe_video_buffer *target,
                                    struct pipe_picture_desc *picture)
{
   struct si_video_dec_inst *vid = (struct si_video_dec_inst *)decoder;
   struct pipe_video_codec *inst = vid->inst[vid->cur_inst];

   inst->begin_frame(inst, target, picture);
}

static void si_dec_inst_decode_bitstream(struct pipe_video_codec *decoder, struct pipe_video_buffer *target,
                                         struct pipe_picture_desc *picture, unsigned num_buffers,
                                         const void *const *buffers, const unsigned *sizes)
{
   struct si_video_dec_inst *vid = (struct si_video_dec_inst *)decoder;
   struct pipe_video_codec *inst = vid->inst[vid->cur_inst];

   inst->decode_bitstream(inst, target, picture, num_buffers, buffers, sizes);
}

static int si_dec_inst_end_frame(struct pipe_video_codec *decoder, struct pipe_video_buffer *target,
                                 struct pipe_picture_desc *picture)
{
   struct si_video_dec_inst *vid = (struct si_video_dec_inst *)decoder;
   struct pipe_video_codec *inst = vid->inst[vid->cur_inst];

   int ret = inst->end_frame(inst, target, picture);
   if (ret == 0)
      vid->cur_inst = (vid->cur_inst + 1) % vid->num_inst;
   return ret;
}

static int si_dec_inst_fence_wait(struct pipe_video_codec *decoder, struct pipe_fence_handle *fence,
                                  uint64_t timeout)
{
   struct si_video_dec_inst *vid = (struct si_video_dec_inst *)decoder;
   struct pipe_video_codec *inst = vid->inst[0];

   return inst->fence_wait(inst, fence, timeout);
}

static void si_dec_inst_destroy_fence(struct pipe_video_codec *decoder, struct pipe_fence_handle *fence)
{
   struct si_video_dec_inst *vid = (struct si_video_dec_inst *)decoder;
   struct pipe_video_codec *inst = vid->inst[0];

   inst->destroy_fence(inst, fence);
}

static void si_dec_inst_destroy(struct pipe_video_codec *decoder)
{
   struct si_video_dec_inst *vid = (struct si_video_dec_inst *)decoder;

   for (unsigned i = 0; i < vid->num_inst; i++) {
      struct pipe_video_codec *inst = vid->inst[i];
      inst->destroy(inst);
   }

   FREE(vid->inst);
   FREE(vid);
}

static struct pipe_video_codec *create_inst_decoder(struct pipe_context *context,
                                                    const struct pipe_video_codec *templ,
                                                    unsigned num_inst)
{
   struct si_video_dec_inst *dec = CALLOC_STRUCT(si_video_dec_inst);
   if (!dec)
      return NULL;

   dec->base = *templ;
   dec->base.context = context;
   dec->base.destroy = si_dec_inst_destroy;
   dec->base.begin_frame = si_dec_inst_begin_frame;
   dec->base.decode_bitstream = si_dec_inst_decode_bitstream;
   dec->base.end_frame = si_dec_inst_end_frame;
   dec->base.fence_wait = si_dec_inst_fence_wait;
   dec->base.destroy_fence = si_dec_inst_destroy_fence;

   dec->inst = CALLOC(num_inst, sizeof(struct pipe_video_codec *));
   if (!dec->inst)
      goto err;

   for (unsigned i = 0; i < num_inst; i++) {
      dec->inst[i] = create_decoder(context, templ);
      if (!dec->inst[i])
         goto err;
      dec->num_inst++;
   }

   return &dec->base;

err:
   dec->base.destroy(&dec->base);
   return NULL;
}

struct pipe_video_codec *si_create_video_decoder(struct pipe_context *context,
                                                 const struct pipe_video_codec *templ)
{
   struct si_context *sctx = (struct si_context *)context;

   /* JPEG has no session context, so we can decode multiple frames in parallel
    * by using one context per instance. */
   if (u_reduce_video_profile(templ->profile) == PIPE_VIDEO_FORMAT_JPEG) {
      unsigned num_inst = sctx->screen->info.ip[AMD_IP_VCN_JPEG].num_instances;
      if (num_inst > 1)
         return create_inst_decoder(context, templ, num_inst);
   }

   return create_decoder(context, templ);
}
