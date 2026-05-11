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

#pragma once

#include "vpe_types.h"
#include "plane_desc_writer.h"
#include "vpe_priv.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vpe20_plane_desc_header {
    int32_t nps0;
    int32_t npd0;
    int32_t nps1;
    int32_t npd1;
    int32_t subop;
    int32_t dcomp0;
    int32_t dcomp1;
    uint8_t hist0_dsets;
    uint8_t hist1_dsets;
    int32_t frod;
};

struct vpe20_plane_desc_src {
    uint8_t                      tmz;
    enum vpe_swizzle_mode_values swizzle;
    uint32_t                     base_addr_lo;
    uint32_t                     base_addr_hi;
    uint16_t                     pitch;
    uint16_t                     viewport_x;
    uint16_t                     viewport_y;
    uint16_t                     viewport_w;
    uint16_t                     viewport_h;
    uint8_t                      elem_size;
    enum vpe_scan_direction      scan;
    bool                         comp_mode;
    uint32_t                     meta_base_addr_lo;
    uint32_t                     meta_base_addr_hi;
    uint16_t                     meta_pitch;
    uint8_t                      dcc_ind_blk;
    uint32_t                     format;
};

struct vpe20_plane_desc_dst {
    uint8_t                      tmz;
    enum vpe_swizzle_mode_values swizzle;
    uint32_t                     base_addr_lo;
    uint32_t                     base_addr_hi;
    uint16_t                     pitch;
    uint16_t                     viewport_x;
    uint16_t                     viewport_y;
    uint16_t                     viewport_w;
    uint16_t                     viewport_h;
    uint8_t                      elem_size;
    bool                         comp_mode;
};

void vpe20_construct_plane_desc_writer(struct plane_desc_writer *writer);
/** initialize the plane descriptor writer.
 * Calls right before building any plane descriptor
 *
 * /param   writer               writer instance
 * /param   buf                  points to the current buf,
 * /param   plane_desc_header    header
 */

void vpe20_plane_desc_writer_init(
    struct plane_desc_writer *writer, struct vpe_buf *buf, void *p_header);

/** fill the value to the embedded buffer. */
void vpe20_plane_desc_writer_add_source(
    struct plane_desc_writer *writer, void *p_source, bool is_plane0);

/** fill the value to the embedded buffer. */
void vpe20_plane_desc_writer_add_destination(
    struct plane_desc_writer *writer, void *p_destination, bool is_plane0);

void vpe20_plane_desc_writer_add_meta(struct plane_desc_writer *writer, void *p_source);

/** fill the value to the embedded buffer for histogram collection. */
void vpe20_plane_desc_writer_add_hist_destination(struct plane_desc_writer *writer,
    void *p_destination, uint32_t hist_idx, uint8_t hist_dsets_array[]);

#ifdef __cplusplus
}
#endif
