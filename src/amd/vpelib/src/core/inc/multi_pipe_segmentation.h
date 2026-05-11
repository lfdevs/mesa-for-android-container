/* Copyright 2025 Advanced Micro Devices, Inc.
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

#pragma once

#include "vpe_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vpe_mps_section {
    uint16_t num_streams;
    int32_t  start_x;
    int32_t  end_x;

    struct vpe_vector *command_vector; // type struct vpe_mps_command
};

struct vpe_mps_command {
    uint16_t num_inputs;
    uint16_t stream_idx[MAX_INPUT_PIPE]; // stream idx for input in vpe_priv->stream_ctx array
    int16_t  mps_idx[MAX_INPUT_PIPE];    // track stream idx in mps struct for each input
    uint16_t
        seg_idx[MAX_INPUT_PIPE]; // track segment idx for each input (segments stores in stream_ctx)
    int32_t start_x[MAX_INPUT_PIPE];   // start_x for each input (segment + bg gen)
    int32_t end_x[MAX_INPUT_PIPE];     // end_x for each input (segment + bg gen)
    bool    is_bg_gen[MAX_INPUT_PIPE]; // is this input all bg
};

struct vpe_mps_stream_info {
    struct stream_ctx *stream_ctx;
    struct vpe_rect    src_rect;
    struct vpe_rect    dst_rect;
};

struct vpe_mps_ctx {
    struct vpe_vector *section_vector; // type struct vpe_mps_section

    uint16_t num_streams;
    uint16_t stream_idx[MAX_INPUT_PIPE];
    uint16_t segment_count[MAX_INPUT_PIPE]; // how many segments each stream has
    struct vpe_vector
        *segment_widths[MAX_INPUT_PIPE];    // type struct vpe_vector<uint32_t> : array containing
                                            // vector of segments widths per stream
};

struct vpe_mps_stream_ctx {
    struct stream_ctx *stream_ctx;
    struct vpe_rect    src_rect;
    struct vpe_rect    dst_rect;
};

// Everything required to run MPS algo
struct vpe_mps_input {
    uint16_t                  num_inputs;
    struct vpe_mps_stream_ctx mps_stream_ctx[MAX_INPUT_PIPE];
    uint32_t                  max_seg_width;
    uint32_t                  recout_width_alignment;
};

/**
 * @brief Check if MPS is possible with the streams passed in
 * @param[in]    vpe_priv        vpe_priv
 * @param[in]    stream_ctx      array of stream_ctx involved in MPS blend
 * @param[in]    num_streams     number of streams in stream_ctx array
 * @return true if possible, false if not
 */
bool vpe_is_mps_possible(struct vpe_priv *vpe_priv, struct stream_ctx **stream_ctx,
    uint16_t num_streams, uint32_t recout_width_alignment);

/**
 * @brief Allocate mps_ctx and initialize it with the stream_idx and num_streams. Run
 * vpe_is_mps_possible beforehand!
 * @param[in]     vpe_priv       vpe_priv
 * @param[in]     stream_ctx     array of stream_ctx involved in MPS blend
 * @param[in]     num_streams    number of streams in stream_ctx array
 * @return VPE_STATUS_OK if successful, error code otherwise
 */
enum vpe_status vpe_init_mps_ctx(
    struct vpe_priv *vpe_priv, struct stream_ctx **stream_ctx, uint16_t num_streams);

/**
 * @brief Clear mps_ctx and NULL mps_parent_stream for non-parents streams involved (mem isn't
 * freed)
 * @param[in]     vpe_priv       vpe_priv
 * @param[in]     mps_ctx        mps_ctx to be cleared
 * @return VPE_STATUS_OK if successful, error code otherwise
 */
void vpe_clear_mps_ctx(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx);

/**
 * @brief Deallocate and free mps_ctx and NULL mps_parent_stream for all streams involved
 * @param[in]     vpe_priv       vpe_priv
 * @param[in]     mps_ctx        mps_ctx to be freed
 * @return VPE_STATUS_OK if successful, error code otherwise
 */
void vpe_free_mps_ctx(struct vpe_priv *vpe_priv, struct vpe_mps_ctx **mps_ctx);

/**
 * @brief Add commands to vpe_priv cmd_vector for this MPS op
 * @param[in]     vpe_priv   vpe_priv
 * @param[in]     mps_ctx    mps_ctx (filled out by vpe_mps_build_mps_ctx)
 * @return VPE_STATUS_OK if successful, error code otherwise
 */
enum vpe_status vpe_fill_mps_blend_cmd_info(struct vpe_priv *vpe_priv, struct vpe_mps_ctx *mps_ctx);

/**
 * @brief Get number of segments required for this MPS involved stream. This must be ran on the
 * parent stream the first time!
 * @param[in]     vpe_priv                vpe_priv
 * @param[in]     stream_ctx              stream_ctx
 * @param[in]     max_seg_width           maximum segment width for this stream
 * @param[in]     recout_width_alignment  recout width alignment
 * @return number of segments required for this stream
 */
uint16_t vpe_mps_get_num_segs(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    uint32_t *max_seg_width, uint32_t recout_width_alignment);

#ifdef __cplusplus
}
#endif
