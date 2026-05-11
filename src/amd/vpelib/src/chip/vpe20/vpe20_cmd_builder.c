/* Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */
#include "vpe_assert.h"
#include "common.h"
#include "vpe_priv.h"
#include "vpe20_command.h"
#include "vpe20_plane_desc_writer.h"
#include "vpe10_cmd_builder.h"
#include "vpe20_cmd_builder.h"
#include "vpe20_resource.h"

#define LOG_INPUT_PLANE  1
#define LOG_OUTPUT_PLANE 0

static void get_np_and_subop(struct vpe_priv *vpe_priv, struct vpe_cmd_info *cmd_info,
    struct vpe20_plane_desc_header *header);

static enum VPE_PLANE_CFG_ELEMENT_SIZE vpe_get_element_size(
    enum vpe_surface_pixel_format format, int plane_idx);

static void log_plane_desc_event(struct vpe_priv *vpe_priv, struct vpe_cmd_info *cmd_info,
    struct vpe20_plane_desc_header *header, struct vpe20_plane_desc_src *src,
    struct vpe20_plane_desc_dst *dst, uint32_t cmd_idx, uint32_t io_idx, uint32_t plane_idx,
    bool input_flag);

void vpe20_construct_cmd_builder(struct vpe_priv *vpe_priv, struct cmd_builder *builder)
{
    builder->build_noops            = vpe10_build_noops;
    builder->build_vpe_cmd          = vpe20_build_vpe_cmd;
    builder->build_plane_descriptor = vpe20_build_plane_descriptor;
}

enum vpe_status vpe20_build_vpe_cmd(
    struct vpe_priv *vpe_priv, struct vpe_build_bufs *cur_bufs, uint32_t cmd_idx)
{
    struct cmd_builder     *builder         = &vpe_priv->resource.cmd_builder;
    struct vpe_desc_writer *vpe_desc_writer = &vpe_priv->vpe_desc_writer;
    struct vpe_buf         *emb_buf         = &cur_bufs->emb_buf;
    struct output_ctx      *output_ctx;
    struct pipe_ctx        *pipe_ctx = NULL;
    uint32_t                pipe_idx, config_idx;
    struct vpe_vector      *config_vector;
    struct config_record   *config;
    struct vpe_cmd_info    *cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
    enum vpe_status         status   = VPE_STATUS_OK;

    VPE_ASSERT(cmd_info);
    if (!cmd_info)
        return VPE_STATUS_ERROR;

    vpe_desc_writer->init(vpe_desc_writer, &cur_bufs->cmd_buf, cmd_info->cd);

    // plane descriptor
    builder->build_plane_descriptor(vpe_priv, emb_buf, cmd_idx);

    vpe_desc_writer->add_plane_desc(
        vpe_desc_writer, vpe_priv->plane_desc_writer.base_gpu_va, (uint8_t)emb_buf->tmz);

    // reclaim any pipe if the owner no longer presents
    vpe_pipe_reclaim(vpe_priv, cmd_info);

    config_writer_init(&vpe_priv->config_writer, emb_buf);

    vpe_priv->resource.reset_pipes(vpe_priv);

    /* 3D LUT FL programming */
    vpe_priv->resource.program_fastload(vpe_priv, cmd_idx);

    // frontend programming
    for (pipe_idx = 0; pipe_idx < cmd_info->num_inputs; pipe_idx++) {
        bool               reuse;
        struct stream_ctx *stream_ctx;
        uint16_t           stream_idx;
        enum vpe_cmd_type  cmd_type = VPE_CMD_TYPE_COUNT;

        // keep using the same pipe whenever possible
        // this would allow reuse of the previous register configs
        stream_idx = cmd_info->inputs[pipe_idx].stream_idx;
        pipe_ctx   = &vpe_priv->pipe_ctx[pipe_idx];

        reuse = (pipe_ctx->owner == stream_idx);
        VPE_ASSERT(pipe_ctx->owner == PIPE_CTX_NO_OWNER || pipe_ctx->owner == stream_idx);
        pipe_ctx->owner = stream_idx;
        stream_ctx      = &vpe_priv->stream_ctx[cmd_info->inputs[pipe_idx].stream_idx];

        if (!reuse) {
            vpe_priv->resource.program_frontend(
                vpe_priv, pipe_ctx->pipe_idx, cmd_idx, pipe_idx, false);
        } else {
            if (vpe_priv->init.debug.disable_reuse_bit)
                reuse = false;

            // frame specific for same type of command
            switch (cmd_info->ops) {
            case VPE_CMD_OPS_BG:
                cmd_type = VPE_CMD_TYPE_BG;
                break;
            case VPE_CMD_OPS_COMPOSITING:
                cmd_type = VPE_CMD_TYPE_COMPOSITING;
                break;
            case VPE_CMD_OPS_BLENDING:
                cmd_type = VPE_CMD_TYPE_BLENDING;
                break;
            case VPE_CMD_OPS_BG_VSCF_INPUT:
                cmd_type = VPE_CMD_TYPE_BG_VSCF_INPUT;
                break;
            case VPE_CMD_OPS_BG_VSCF_OUTPUT:
                cmd_type = VPE_CMD_TYPE_BG_VSCF_OUTPUT;
                break;
            case VPE_CMD_OPS_BG_VSCF_PIPE0:
                cmd_type = VPE_CMD_TYPE_BG_VSCF_PIPE0;
                break;
            case VPE_CMD_OPS_BG_VSCF_PIPE1:
                cmd_type = VPE_CMD_TYPE_BG_VSCF_PIPE1;
                break;
            case VPE_CMD_OPS_ALPHA_THROUGH_LUMA:
                cmd_type = VPE_CMD_TYPE_ALPHA_THROUGH_LUMA;
                break;
            default:
                VPE_ASSERT(0);
                status = VPE_STATUS_ERROR;
                break;
            }

            // follow the same order of config generation in "non-reuse" case
            // stream sharing
            config_vector = stream_ctx->configs[pipe_idx];
            VPE_ASSERT(config_vector->num_elements);
            for (config_idx = 0; config_idx < config_vector->num_elements; config_idx++) {
                config = (struct config_record *)vpe_vector_get(config_vector, config_idx);
                if (!config) {
                    status = VPE_STATUS_ERROR;
                    break;
                }

                vpe_desc_writer->add_config_desc(
                    vpe_desc_writer, config->config_base_addr, reuse, (uint8_t)emb_buf->tmz);
            }

            if (status != VPE_STATUS_OK)
                break;

            // stream-op sharing
            config_vector = stream_ctx->stream_op_configs[pipe_idx][cmd_type];
            for (config_idx = 0; config_idx < config_vector->num_elements; config_idx++) {
                config = (struct config_record *)vpe_vector_get(config_vector, config_idx);
                if (!config) {
                    status = VPE_STATUS_ERROR;
                    break;
                }

                vpe_desc_writer->add_config_desc(
                    vpe_desc_writer, config->config_base_addr, false, (uint8_t)emb_buf->tmz);
            }

            if (status != VPE_STATUS_OK)
                break;

            // command specific
            vpe_priv->resource.program_frontend(
                vpe_priv, pipe_ctx->pipe_idx, cmd_idx, pipe_idx, true);
        }
    }

    // If config writer has been crashed due to buffer overflow
    if ((status == VPE_STATUS_OK) && (vpe_priv->config_writer.status != VPE_STATUS_OK)) {
        status = vpe_priv->config_writer.status;
    }

    // Back End Programming
    if (status == VPE_STATUS_OK) {
        // backend programming
        output_ctx = &vpe_priv->output_ctx;

        for (pipe_idx = 0; pipe_idx < cmd_info->num_outputs; pipe_idx++) {
            config_vector = output_ctx->configs[pipe_idx];
            bool seg_only;

            if (!config_vector->num_elements) {
                seg_only = false;
            } else {
                seg_only   = true;
                bool reuse = !vpe_priv->init.debug.disable_reuse_bit;

                // re-use output register configs
                for (config_idx = 0; config_idx < config_vector->num_elements; config_idx++) {
                    config = (struct config_record *)vpe_vector_get(
                        output_ctx->configs[pipe_idx], config_idx);
                    if (!config) {
                        status = VPE_STATUS_ERROR;
                        break;
                    }

                    vpe_desc_writer->add_config_desc(
                        vpe_desc_writer, config->config_base_addr, reuse, (uint8_t)emb_buf->tmz);
                }
                if (status != VPE_STATUS_OK)
                    break;
            }
            vpe_priv->resource.program_backend(vpe_priv, pipe_idx, cmd_idx, seg_only);
        }
    }

    // If config writer has been crashed due to buffer overflow
    if ((status == VPE_STATUS_OK) && (vpe_priv->config_writer.status != VPE_STATUS_OK)) {
        status = vpe_priv->config_writer.status;
    }

    /* If writer crashed due to buffer overflow */
    if ((status == VPE_STATUS_OK) && (vpe_desc_writer->status != VPE_STATUS_OK)) {
        status = vpe_desc_writer->status;
    }

    if (status == VPE_STATUS_OK) {
        vpe_desc_writer->complete(vpe_desc_writer);
    }

    return status;
}

enum vpe_status vpe20_build_plane_descriptor(
    struct vpe_priv *vpe_priv, struct vpe_buf *buf, uint32_t cmd_idx)
{
    struct stream_ctx                    *stream_ctx;
    struct vpe_surface_info              *surface_info;
    int32_t                               stream_idx;
    struct vpe_cmd_info                  *cmd_info;
    PHYSICAL_ADDRESS_LOC                 *addrloc;
    PHYSICAL_ADDRESS_LOC                  addrhist;
    struct vpe20_plane_desc_src           src;
    struct vpe20_plane_desc_dst           dst;

    struct vpe20_plane_desc_header        header            = {0};
    struct cmd_builder                   *builder           = &vpe_priv->resource.cmd_builder;
    struct plane_desc_writer             *plane_desc_writer = &vpe_priv->plane_desc_writer;
    uint32_t                              viewport_divider  = FROD_DOWNSAMPLING_RATIO;
    const uint32_t                        hist_size         = 1024; // 256 bins, each 4 bytes
    uint32_t                              hist_dsets        = 0;
    uint32_t                              line_offset;
    uint32_t                              num_pipes = 1;
    uint32_t                              performance_mode_offset;
    cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
    VPE_ASSERT(plane_desc_writer);
    VPE_ASSERT(cmd_info);
    if (!cmd_info)
        return VPE_STATUS_ERROR;

    VPE_ASSERT(cmd_info->num_inputs <= 2);

    // obtains number of planes for each source/destination stream
    for (int i = 0; i < cmd_info->num_inputs; i++) {
        stream_idx    = cmd_info->inputs[i].stream_idx;
        stream_ctx    = &vpe_priv->stream_ctx[stream_idx];
        src.scan      = stream_ctx->scan;
        surface_info  = &stream_ctx->stream.surface_info;
        if (i == 0)
            header.dcomp0 = surface_info->dcc.enable ? 1 : 0;
        else if (i == 1)
            header.dcomp1 = surface_info->dcc.enable ? 1 : 0;
    }

    get_np_and_subop(vpe_priv, cmd_info, &header);

    if (cmd_info->frod_param.enable_frod) {
        header.frod = 1;
    }

    for (int i = 0; i < cmd_info->num_inputs; i++) {
        stream_idx = cmd_info->inputs[i].stream_idx;
        stream_ctx = &vpe_priv->stream_ctx[stream_idx];
        cmd_info->histo_dsets[i] = stream_ctx->stream.hist_params.hist_dsets;
        if ((cmd_info->histo_dsets[i] > 0) && (cmd_info->histo_dsets[i] <= MAX_HISTO_SETS)) {
            if (i == 0) {
                header.hist0_dsets = (uint8_t)stream_ctx->stream.hist_params.hist_dsets;
            }
            else {
                header.hist1_dsets = (uint8_t)stream_ctx->stream.hist_params.hist_dsets;
            }
        }
    }
    plane_desc_writer->init(&vpe_priv->plane_desc_writer, buf, &header);

    for (int i = 0; i < cmd_info->num_inputs && i < MAX_INPUT_PIPE; i++) {
        struct dscl_prog_data* dscl_data = &cmd_info->inputs[i].scaler_data.dscl_prog_data;

        stream_idx   = cmd_info->inputs[i].stream_idx;
        stream_ctx   = &vpe_priv->stream_ctx[stream_idx];
        surface_info = &stream_ctx->stream.surface_info;

        src.tmz      = surface_info->address.tmz_surface;
        src.swizzle  = surface_info->swizzle;
        src.scan     = stream_ctx->scan;
        src.format   = 0;

        if (surface_info->address.type == VPE_PLN_ADDR_TYPE_VIDEO_PROGRESSIVE) {

            addrloc = &surface_info->address.video_progressive.luma_addr;

            src.base_addr_lo = addrloc->u.low_part;
            src.base_addr_hi = (uint32_t)addrloc->u.high_part;
            src.pitch        = (uint16_t)surface_info->plane_size.surface_pitch;

            addrloc               = &surface_info->address.video_progressive.luma_meta_addr;
            src.meta_base_addr_lo = addrloc->u.low_part;
            src.meta_base_addr_hi = (uint32_t)addrloc->u.high_part;
            src.meta_pitch        = (uint16_t)surface_info->dcc.src.meta_pitch;
            src.dcc_ind_blk       = surface_info->dcc.src.dcc_ind_blk_c;
            src.comp_mode         = surface_info->dcc.enable;

            src.viewport_x = (uint16_t)dscl_data->viewport.x;
            src.viewport_y = (uint16_t)dscl_data->viewport.y;
            src.viewport_w = (uint16_t)dscl_data->viewport.width;
            src.viewport_h = (uint16_t)dscl_data->viewport.height;
            src.elem_size = (uint8_t)(vpe_get_element_size(surface_info->format, 0));
            if (vpe_is_yuv_packed(surface_info->format) && vpe_is_yuv422(surface_info->format)) {
                if (dscl_data->viewport_c.x < (dscl_data->viewport.x / 2))
                    src.viewport_x = (uint16_t)dscl_data->viewport_c.x;
                else
                    src.viewport_x = (uint16_t)dscl_data->viewport.x / 2;

                if ((dscl_data->viewport_c.width * 2) > dscl_data->viewport.width)
                    src.viewport_w = (uint16_t)dscl_data->viewport_c.width;
                else
                    src.viewport_w = (uint16_t)dscl_data->viewport.width / 2;

                src.pitch = (uint16_t)surface_info->plane_size.surface_pitch / 2;
            }

            plane_desc_writer->add_source(&vpe_priv->plane_desc_writer, &src, true);
            // log vpe event - plane0
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 0, LOG_INPUT_PLANE);

            if (vpe_is_dual_plane_format(surface_info->format)) {
                addrloc = &surface_info->address.video_progressive.chroma_addr;

                src.base_addr_lo = addrloc->u.low_part;
                src.base_addr_hi = (uint32_t)addrloc->u.high_part;
                src.pitch        = (uint16_t)surface_info->plane_size.chroma_pitch;

                src.viewport_x = (uint16_t)dscl_data->viewport_c.x;
                src.viewport_y = (uint16_t)dscl_data->viewport_c.y;
                src.viewport_w = (uint16_t)dscl_data->viewport_c.width;
                src.viewport_h = (uint16_t)dscl_data->viewport_c.height;
                src.elem_size  = (uint8_t)(vpe_get_element_size(surface_info->format, 1));

                plane_desc_writer->add_source(&vpe_priv->plane_desc_writer, &src, false);
                // log vpe event - plane1
                log_plane_desc_event(
                    vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 1, LOG_INPUT_PLANE);
            }
        } else if (surface_info->address.type == VPE_PLN_ADDR_TYPE_PLANAR) {
            /* Planar Formats always packed into VPEC as:
                PLane0 = Y_g
                Plane1 = Cb_b
                Plane2 = Cr_r
            */

            // assuming all planes have the same rect/pitch properties
            src.viewport_x = (uint16_t)cmd_info->inputs[i].scaler_data.dscl_prog_data.viewport.x;
            src.viewport_y = (uint16_t)cmd_info->inputs[i].scaler_data.dscl_prog_data.viewport.y;
            src.viewport_w =
                (uint16_t)cmd_info->inputs[i].scaler_data.dscl_prog_data.viewport.width;
            src.viewport_h =
                (uint16_t)cmd_info->inputs[i].scaler_data.dscl_prog_data.viewport.height;
            src.elem_size = (uint8_t)(vpe_get_element_size(surface_info->format, 0));
            src.pitch     = (uint16_t)surface_info->plane_size.surface_pitch;

            // Y_g
            addrloc          = &surface_info->address.planar.y_g_addr;
            src.base_addr_lo = addrloc->u.low_part;
            src.base_addr_hi = (uint32_t)addrloc->u.high_part;
            plane_desc_writer->add_source(&vpe_priv->plane_desc_writer, &src, true);
            // log vpe event - plane0
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 0, LOG_INPUT_PLANE);

            // Cb_b
            addrloc          = &surface_info->address.planar.cb_b_addr;
            src.base_addr_lo = addrloc->u.low_part;
            src.base_addr_hi = (uint32_t)addrloc->u.high_part;
            plane_desc_writer->add_source(&vpe_priv->plane_desc_writer, &src, false);
            // log vpe event - plane1
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 1, LOG_INPUT_PLANE);

            // Cr_r
            addrloc          = &surface_info->address.planar.cr_r_addr;
            src.base_addr_lo = addrloc->u.low_part;
            src.base_addr_hi = (uint32_t)addrloc->u.high_part;
            plane_desc_writer->add_source(&vpe_priv->plane_desc_writer, &src, false);
            // log vpe event - plane2
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 2, LOG_INPUT_PLANE);
        } else {
            addrloc = &surface_info->address.grph.addr;

            src.base_addr_lo = addrloc->u.low_part;
            src.base_addr_hi = (uint32_t)addrloc->u.high_part;
            src.pitch        = (uint16_t)surface_info->plane_size.surface_pitch;

            src.viewport_x = (uint16_t)dscl_data->viewport.x;
            src.viewport_y = (uint16_t)dscl_data->viewport.y;
            src.viewport_w = (uint16_t)dscl_data->viewport.width;
            src.viewport_h = (uint16_t)dscl_data->viewport.height;
            src.elem_size  = (uint8_t)(vpe_get_element_size(surface_info->format, 0));

            plane_desc_writer->add_source(&vpe_priv->plane_desc_writer, &src, true);
            // log vpe event - plane0
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 0, LOG_INPUT_PLANE);
        }
    }

    if (plane_desc_writer->add_meta && (header.dcomp0 || header.dcomp1)) {
        for (int i = 0; i < cmd_info->num_inputs && i < MAX_INPUT_PIPE; i++) {
            if ((header.dcomp0 && i == 0) || (header.dcomp1 && i == 1)) {
                stream_idx            = cmd_info->inputs[i].stream_idx;
                stream_ctx            = &vpe_priv->stream_ctx[stream_idx];
                surface_info          = &stream_ctx->stream.surface_info;
                addrloc               = &surface_info->address.video_progressive.luma_meta_addr;
                src.meta_base_addr_lo = addrloc->u.low_part;
                src.meta_base_addr_hi = (uint32_t)addrloc->u.high_part;
                src.meta_pitch        = (uint16_t)surface_info->dcc.src.meta_pitch;
                src.dcc_ind_blk       = surface_info->dcc.src.dcc_ind_blk_c;
                src.comp_mode         = surface_info->dcc.enable;
                src.format            = vpe20_get_hw_surface_format(surface_info->format);
                plane_desc_writer->add_meta(&vpe_priv->plane_desc_writer, &src);
            }
        }
    }

    for (int i = 0; i < cmd_info->num_outputs && i < MAX_OUTPUT_PIPE; i++) {
        surface_info = &vpe_priv->output_ctx.surface;
        if ((cmd_info->frod_param.enable_frod) && (i > 0)) {
            surface_info = &vpe_priv->output_ctx.frod_surface[i-1];
            vpe_priv->resource.set_frod_output_viewport(&cmd_info->outputs[i],
                &cmd_info->outputs[0], viewport_divider, surface_info->format);
            viewport_divider *= FROD_DOWNSAMPLING_RATIO;
        }
        if (surface_info->address.type == VPE_PLN_ADDR_TYPE_VIDEO_PROGRESSIVE) {
            addrloc = &surface_info->address.video_progressive.luma_addr;
            dst.tmz     = surface_info->address.tmz_surface;
            dst.swizzle = surface_info->swizzle;

            dst.base_addr_lo = addrloc->u.low_part;
            dst.base_addr_hi = (uint32_t)addrloc->u.high_part;
            dst.pitch        = (uint16_t)surface_info->plane_size.surface_pitch;

            dst.viewport_x = (uint16_t)cmd_info->outputs[i].dst_viewport.x;
            dst.viewport_y = (uint16_t)cmd_info->outputs[i].dst_viewport.y;
            dst.viewport_w = (uint16_t)cmd_info->outputs[i].dst_viewport.width;
            dst.viewport_h = (uint16_t)cmd_info->outputs[i].dst_viewport.height;
            dst.elem_size  = (uint8_t)(vpe_get_element_size(surface_info->format, 0));
            if (vpe_is_yuv_packed(surface_info->format) && vpe_is_yuv422(surface_info->format)) {
                dst.viewport_x = (uint16_t)cmd_info->outputs[i].dst_viewport.x / 2;
                dst.viewport_w = (uint16_t)cmd_info->outputs[i].dst_viewport.width / 2;
                dst.pitch      = (uint16_t)surface_info->plane_size.surface_pitch / 2;
            }
            plane_desc_writer->add_destination(&vpe_priv->plane_desc_writer, &dst, true);
            // log vpe event - plane0
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 0, LOG_OUTPUT_PLANE);
            if (vpe_is_dual_plane_format(surface_info->format)) {
                addrloc = &surface_info->address.video_progressive.chroma_addr;
                dst.tmz     = surface_info->address.tmz_surface;
                dst.swizzle = surface_info->swizzle;

                dst.base_addr_lo = addrloc->u.low_part;
                dst.base_addr_hi = (uint32_t)addrloc->u.high_part;
                dst.pitch        = (uint16_t)surface_info->plane_size.chroma_pitch;

                dst.viewport_x = (uint16_t)cmd_info->outputs[i].dst_viewport_c.x;
                dst.viewport_y = (uint16_t)cmd_info->outputs[i].dst_viewport_c.y;
                dst.viewport_w = (uint16_t)cmd_info->outputs[i].dst_viewport_c.width;
                dst.viewport_h = (uint16_t)cmd_info->outputs[i].dst_viewport_c.height;
                dst.elem_size  = (uint8_t)(vpe_get_element_size(surface_info->format, 1));

                plane_desc_writer->add_destination(&vpe_priv->plane_desc_writer, &dst, false);
                // log vpe event - plane1
                log_plane_desc_event(
                    vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 1, LOG_OUTPUT_PLANE);
            }
        } else if (surface_info->address.type == VPE_PLN_ADDR_TYPE_PLANAR) {
            /* Planar Formats always packed as:
                PLane0 = Y_g
                Plane1 = Cb_b
                Plane2 = Cr_r
            */

            // assuming all planes have the same rect/pitch/tmz/swizzle properties
            dst.tmz        = surface_info->address.tmz_surface;
            dst.swizzle    = surface_info->swizzle;
            dst.viewport_x = (uint16_t)cmd_info->outputs[i].dst_viewport_c.x;
            dst.viewport_y = (uint16_t)cmd_info->outputs[i].dst_viewport_c.y;
            dst.viewport_w = (uint16_t)cmd_info->outputs[i].dst_viewport_c.width;
            dst.viewport_h = (uint16_t)cmd_info->outputs[i].dst_viewport_c.height;
            dst.elem_size  = (uint8_t)(vpe_get_element_size(surface_info->format, 1));
            dst.pitch      = (uint16_t)surface_info->plane_size.surface_pitch;

            // Y_g
            addrloc          = &surface_info->address.planar.y_g_addr;
            dst.base_addr_lo = addrloc->u.low_part;
            dst.base_addr_hi = (uint32_t)addrloc->u.high_part;
            plane_desc_writer->add_destination(&vpe_priv->plane_desc_writer, &dst, true);
            // log vpe event - plane0
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 0, LOG_OUTPUT_PLANE);

            // Cb_b
            addrloc          = &surface_info->address.planar.cb_b_addr;
            dst.base_addr_lo = addrloc->u.low_part;
            dst.base_addr_hi = (uint32_t)addrloc->u.high_part;
            plane_desc_writer->add_destination(&vpe_priv->plane_desc_writer, &dst, false);
            // log vpe event - plane1
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 1, LOG_OUTPUT_PLANE);

            // Cr_r
            addrloc          = &surface_info->address.planar.cr_r_addr;
            dst.base_addr_lo = addrloc->u.low_part;
            dst.base_addr_hi = (uint32_t)addrloc->u.high_part;
            plane_desc_writer->add_destination(&vpe_priv->plane_desc_writer, &dst, false);
            // log vpe event - plane2
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 2, LOG_OUTPUT_PLANE);
        } else {
            addrloc = &surface_info->address.grph.addr;
            dst.tmz     = surface_info->address.tmz_surface;
            dst.swizzle = surface_info->swizzle;

            dst.base_addr_lo = addrloc->u.low_part;
            dst.base_addr_hi = (uint32_t)addrloc->u.high_part;
            dst.pitch        = (uint16_t)surface_info->plane_size.surface_pitch;

            dst.viewport_x = (uint16_t)cmd_info->outputs[i].dst_viewport.x;
            dst.viewport_y = (uint16_t)cmd_info->outputs[i].dst_viewport.y;
            dst.viewport_w = (uint16_t)cmd_info->outputs[i].dst_viewport.width;
            dst.viewport_h = (uint16_t)cmd_info->outputs[i].dst_viewport.height;
            dst.elem_size  = (uint8_t)(vpe_get_element_size(surface_info->format, 0));

            plane_desc_writer->add_destination(&vpe_priv->plane_desc_writer, &dst, true);
            // log vpe event - plane0
            log_plane_desc_event(
                vpe_priv, cmd_info, &header, &src, &dst, cmd_idx, i, 0, LOG_OUTPUT_PLANE);
        }
    }

    for (int i = 0; i < cmd_info->num_inputs && i < MAX_INPUT_PIPE; i++) {
        if ((header.hist0_dsets > 0) || (header.hist1_dsets > 0)) {
            stream_idx = cmd_info->inputs[i].stream_idx;
            stream_ctx = &vpe_priv->stream_ctx[stream_idx];
            num_pipes  = vpe_priv->resource.get_num_pipes_available(vpe_priv, stream_ctx);
            hist_dsets = header.hist0_dsets;
            if(i > 0)
                hist_dsets = header.hist1_dsets;
            for (uint32_t histIndex = 0; histIndex < hist_dsets; histIndex++) {
                surface_info = &stream_ctx->stream.hist_params.hist_collection_param[histIndex].hist_output;
                if ((surface_info != NULL) &&
                    ((cmd_info->cd * num_pipes) <
                        surface_info->plane_size.surface_size
                            .height)) { // writing to valid address within surface
                    performance_mode_offset = (num_pipes > 1) ? i : 0;
                    line_offset =
                        hist_size * ((cmd_info->cd * num_pipes) + performance_mode_offset);
                    addrhist.quad_part = surface_info->address.video_progressive.luma_addr.quad_part;
                    addrhist.quad_part += line_offset; // count down value on  segments
                    dst.base_addr_lo = addrhist.u.low_part;
                    dst.base_addr_hi = (uint32_t)addrhist.u.high_part;
                    plane_desc_writer->add_histo(
                        &vpe_priv->plane_desc_writer, &dst, histIndex, NULL);
                }
            }
        }
    }
    return vpe_priv->plane_desc_writer.status;
}

// Function logs plane descriptor information like number of planes, plane
// descriptor type, base address, pitch, viewport, swizzle, etc. to etw events
static void log_plane_desc_event(struct vpe_priv *vpe_priv, struct vpe_cmd_info *cmd_info,
    struct vpe20_plane_desc_header *header, struct vpe20_plane_desc_src *src,
    struct vpe20_plane_desc_dst *dst, uint32_t cmd_idx, uint32_t io_idx, uint32_t plane_idx,
    bool input_flag)
{
    // check if event is for input or output plane
    if (input_flag) {
        vpe_event(VPE_EVENT_PLANE_DESC_INPUT, vpe_priv->pub.level,
            vpe_priv->vpe_cmd_vector->num_elements, cmd_idx, cmd_info->num_inputs, io_idx,
            (header->nps0 + 1), (header->nps1 + 1), 0, plane_idx, 0, 0, 0, src->base_addr_lo,
            src->base_addr_hi, src->viewport_x, src->viewport_y, src->viewport_w, src->viewport_h,
            src->swizzle, header->dcomp0, header->dcomp1);
    } else {
        vpe_event(VPE_EVENT_PLANE_DESC_OUTPUT, vpe_priv->pub.level,
            vpe_priv->vpe_cmd_vector->num_elements, cmd_idx, cmd_info->num_outputs, io_idx,
            (header->npd0 + 1), (header->npd1 + 1), plane_idx, dst->base_addr_lo, dst->base_addr_hi,
            dst->viewport_x, dst->viewport_y, dst->viewport_w, dst->viewport_h, dst->swizzle);
    }
}

static void get_np_and_subop(struct vpe_priv *vpe_priv, struct vpe_cmd_info *cmd_info,
    struct vpe20_plane_desc_header *header)
{

    // Init second pipe src and destination plane count to 0
    header->nps1 = 0;
    header->npd1 = 0;

    // Populate number of planes for source 0.
    if (vpe_is_planar_format(
            vpe_priv->stream_ctx[cmd_info->inputs[0].stream_idx].stream.surface_info.format))
        header->nps0 = VPE_PLANE_CFG_THREE_PLANES;
    else if (vpe_is_dual_plane_format(
                 vpe_priv->stream_ctx[cmd_info->inputs[0].stream_idx].stream.surface_info.format))
        header->nps0 = VPE_PLANE_CFG_TWO_PLANES;
    else
        header->nps0 = VPE_PLANE_CFG_ONE_PLANE;

    // Populate number of planes for source 1 if it exists.
    if (cmd_info->num_inputs == 2) {
        if (vpe_is_planar_format(
                vpe_priv->stream_ctx[cmd_info->inputs[1].stream_idx].stream.surface_info.format))
            header->nps1 = VPE_PLANE_CFG_THREE_PLANES;
        else if (vpe_is_dual_plane_format(vpe_priv->stream_ctx[cmd_info->inputs[1].stream_idx]
                                              .stream.surface_info.format))
            header->nps1 = VPE_PLANE_CFG_TWO_PLANES;
        else
            header->nps1 = VPE_PLANE_CFG_ONE_PLANE;
    }

    // Populate number of planes for destination 0.
    if (vpe_is_planar_format(vpe_priv->output_ctx.surface.format))
        header->npd0 = VPE_PLANE_CFG_THREE_PLANES;
    else if (vpe_is_dual_plane_format(vpe_priv->output_ctx.surface.format))
        header->npd0 = VPE_PLANE_CFG_TWO_PLANES;
    else
        header->npd0 = VPE_PLANE_CFG_ONE_PLANE;

    // Populate number of planes for destination 1 if it exists.
    if (cmd_info->num_outputs == 2) {
        if (vpe_is_planar_format(vpe_priv->output_ctx.surface.format))
            header->npd1 = VPE_PLANE_CFG_THREE_PLANES;
        else if (vpe_is_dual_plane_format(vpe_priv->output_ctx.surface.format))
            header->npd1 = VPE_PLANE_CFG_TWO_PLANES;
        else
            header->npd1 = VPE_PLANE_CFG_ONE_PLANE;
    }

    /*
     *  Populate subop.
     *  Note: In the FROD case, num_outputs will be four but sub_op is expected to be x_TO_1
     */
    if (cmd_info->num_inputs == 1)
        header->subop = VPE_PLANE_CFG_SUBOP_1_TO_1;
    else if (cmd_info->num_outputs == 2)
        header->subop = VPE_PLANE_CFG_SUBOP_2_TO_2;
    else
        header->subop = VPE_PLANE_CFG_SUBOP_2_TO_1;
}

static enum VPE_PLANE_CFG_ELEMENT_SIZE vpe_get_element_size(
    enum vpe_surface_pixel_format format, int plane_idx)
{
    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R8:           /* Monochrome 8BPE (R8)*/
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_RGB:   /* Planar RGB 8BPE */
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_8bpc_YCbCr: /* Planar YCbCr 8BPE */
        return VPE_PLANE_CFG_ELEMENT_SIZE_8BPE;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCbCr: /* Semi-Planar 420 8BPE (NV12 + NV12) */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCrCb: /* Semi-Planar 422 8BPE (YUY2)*/
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ALPHA_THRU_LUMA:
        if (plane_idx == 0)
            return VPE_PLANE_CFG_ELEMENT_SIZE_8BPE;
        else
            return VPE_PLANE_CFG_ELEMENT_SIZE_16BPE;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCbCr: /* Semi-Planar 420 (P010 & P016) */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_420_12bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrCb: /* Semi-Planar 422 16BPE (Y210, Y216) */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbCr:
        if (plane_idx == 0)
            return VPE_PLANE_CFG_ELEMENT_SIZE_16BPE;
        else
            return VPE_PLANE_CFG_ELEMENT_SIZE_32BPE;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_R16:               /* Monochrome 16BPE (R16)*/
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB:       /* Planar RGB 16BPE */
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_YCbCr:     /* Planar YCbCr 16BPE */
    case VPE_SURFACE_PIXEL_FORMAT_PLANAR_16bpc_RGB_FLOAT: /* Planar FP16 */
        return VPE_PLANE_CFG_ELEMENT_SIZE_16BPE;
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCrYCb: /* Packed 422 16BPE (Y210, Y216) */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_10bpc_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCrYCb:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_YCbYCr:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CrYCbY:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_422_12bpc_CbYCrY:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616: /* RGB 16BPE */
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_UNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616_SNORM:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616_SNORM:
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_ACrYCb12121212: /* Packed YUV444 16BPE (Y416) */
    case VPE_SURFACE_PIXEL_FORMAT_VIDEO_CrYCbA12121212:
        return VPE_PLANE_CFG_ELEMENT_SIZE_64BPE;
    default:
        break;
    }
    return VPE_PLANE_CFG_ELEMENT_SIZE_32BPE;
}
