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
#include "vpe20_command.h"
#include "vpe20_plane_desc_writer.h"
#include "reg_helper.h"

void vpe20_construct_plane_desc_writer(struct plane_desc_writer *writer)
{
    writer->init            = vpe20_plane_desc_writer_init;
    writer->add_source      = vpe20_plane_desc_writer_add_source;
    writer->add_destination = vpe20_plane_desc_writer_add_destination;
    writer->add_meta        = vpe20_plane_desc_writer_add_meta;
    writer->add_histo       = vpe20_plane_desc_writer_add_hist_destination;
}

void vpe20_plane_desc_writer_init(
    struct plane_desc_writer *writer, struct vpe_buf *buf, void *p_header)
{
    uint32_t *cmd_space;
    uint64_t  size      = 4;
    struct vpe20_plane_desc_header *header    = (struct vpe20_plane_desc_header *)p_header;

    // For VPE 2.0 all config and plane descriptors gpu address must be 6 bit aligned
    uint64_t aligned_gpu_address =
        (buf->gpu_va + VPE_PLANE_ADDR_ALIGNMENT_MASK) & ~VPE_PLANE_ADDR_ALIGNMENT_MASK;
    uint64_t alignment_offset = aligned_gpu_address - buf->gpu_va;
    buf->gpu_va               = aligned_gpu_address;
    buf->cpu_va               = buf->cpu_va + alignment_offset;

    if (buf->size < alignment_offset) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }
    buf->size -= alignment_offset;

    writer->status      = VPE_STATUS_OK;
    writer->base_cpu_va = buf->cpu_va;
    writer->base_gpu_va = buf->gpu_va;
    writer->buf         = buf;
    writer->num_src     = 0;
    writer->num_dst     = 0;

    /* Buffer does not have enough space to write */
    if (buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }

    cmd_space = (uint32_t *)(uintptr_t)writer->buf->cpu_va;

    *cmd_space++ = VPE_PLANE_CFG_CMD_HEADER(header->subop, header->nps0, header->npd0, header->nps1,
        header->npd1, header->dcomp0, header->dcomp1, header->frod, header->hist0_dsets,
        header->hist1_dsets);

    writer->buf->cpu_va += size;
    writer->buf->gpu_va += size;
    writer->buf->size -= size;
}

/** fill the value to the embedded buffer. */
void vpe20_plane_desc_writer_add_source(
    struct plane_desc_writer *writer, void *p_source, bool is_plane0)
{
    uint32_t *cmd_space, *cmd_start;
    uint32_t  num_wd = is_plane0 ? 6 : 5;
    uint64_t  size   = num_wd * sizeof(uint32_t);
    struct vpe20_plane_desc_src *src    = (struct vpe20_plane_desc_src *)p_source;

    if (writer->status != VPE_STATUS_OK)
        return;

    /* Buffer does not have enough space to write */
    if (writer->buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }
    cmd_start = cmd_space = (uint32_t *)(uintptr_t)writer->buf->cpu_va;

    if (is_plane0) {
        *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_TMZ, src->tmz) |
                       VPEC_FIELD_VALUE(VPE_PLANE_CFG_SWIZZLE_MODE, src->swizzle) |
                       VPEC_FIELD_VALUE(VPE_PLANE_CFG_SCAN_PATTERN, src->scan);
    }

    VPE_ASSERT(!(src->base_addr_lo & 0xFF));

    *cmd_space++ = src->base_addr_lo;
    *cmd_space++ = src->base_addr_hi;

    *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_PITCH, src->pitch - 1) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_ELEMENT_SIZE, src->elem_size);
    *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_X, src->viewport_x) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_Y, src->viewport_y);
    *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_WIDTH, src->viewport_w - 1) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_HEIGHT, src->viewport_h - 1);

    writer->buf->cpu_va += size;
    writer->buf->gpu_va += size;
    writer->buf->size -= size;
}

/** fill the value to the embedded buffer. */
void vpe20_plane_desc_writer_add_destination(
    struct plane_desc_writer *writer, void *p_destination, bool write_header)
{
    uint32_t *cmd_space, *cmd_start;
    uint32_t  num_wd = write_header ? 6 : 5;
    uint64_t  size   = num_wd * sizeof(uint32_t);
    struct vpe20_plane_desc_dst *dst    = (struct vpe20_plane_desc_dst *)p_destination;

    if (writer->status != VPE_STATUS_OK)
        return;

    /* Buffer does not have enough space to write */
    if (writer->buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }

    cmd_start = cmd_space = (uint32_t *)(uintptr_t)writer->buf->cpu_va;

    if (write_header) {
        *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_TMZ, dst->tmz) |
                       VPEC_FIELD_VALUE(VPE_PLANE_CFG_SWIZZLE_MODE, dst->swizzle);        
    }
    writer->num_dst++;

    VPE_ASSERT(!(dst->base_addr_lo & 0xFF));

    *cmd_space++ = dst->base_addr_lo;
    *cmd_space++ = dst->base_addr_hi;

    *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_PITCH, dst->pitch - 1) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_ELEMENT_SIZE, dst->elem_size);

    *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_X, dst->viewport_x) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_Y, dst->viewport_y);
    *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_WIDTH, dst->viewport_w - 1) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_VIEWPORT_HEIGHT, dst->viewport_h - 1);

    writer->buf->cpu_va += size;
    writer->buf->gpu_va += size;
    writer->buf->size -= size;
}

void vpe20_plane_desc_writer_add_meta(struct plane_desc_writer *writer, void *p_source)
{
    uint32_t *cmd_space, *cmd_start;
    uint32_t  num_wd = 3;
    uint64_t  size   = num_wd * sizeof(uint32_t);
    struct vpe20_plane_desc_src *src    = (struct vpe20_plane_desc_src *)p_source;

    if (writer == NULL || src == NULL)
        return;

    if (writer->status != VPE_STATUS_OK)
        return;

    /* Buffer does not have enough space to write */
    if (writer->buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }
    cmd_start = cmd_space = (uint32_t *)(uintptr_t)writer->buf->cpu_va;

    *cmd_space++ = src->meta_base_addr_lo | VPEC_FIELD_VALUE(VPE_PLANE_CFG_META_TMZ, src->tmz);
    *cmd_space++ = src->meta_base_addr_hi;
    *cmd_space++ = VPEC_FIELD_VALUE(VPE_PLANE_CFG_META_PITCH, src->meta_pitch - 1) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_PIXEL_FORMAT, src->format) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_INDEPENDENT_BLOCKS, src->dcc_ind_blk) |
                   VPEC_FIELD_VALUE(VPE_PLANE_CFG_PA, 0);
    writer->buf->cpu_va += size;
    writer->buf->gpu_va += size;
    writer->buf->size -= size;
}

void vpe20_plane_desc_writer_add_hist_destination(struct plane_desc_writer *writer,
    void *p_destination, uint32_t hist_idx, uint8_t hist_dsets_array[])
{
    uint32_t* cmd_space, * cmd_start;
    uint32_t  num_wd = (hist_idx == 0) ? 3 : 2;
    uint64_t  size = num_wd * sizeof(uint32_t);
    struct vpe20_plane_desc_dst *dst    = (struct vpe20_plane_desc_dst *)p_destination;

    if (writer->status != VPE_STATUS_OK)
        return;

    /* Buffer does not have enough space to write */
    if (writer->buf->size < size) {
        writer->status = VPE_STATUS_BUFFER_OVERFLOW;
        return;
    }

    cmd_start = cmd_space = (uint32_t*)(uintptr_t)writer->buf->cpu_va;

    if (hist_idx == 0) {
        *cmd_space++ = 2; // Number of bytes of each data set 2^(8+2) = 1024 
    }                     // equal to 256bins * 4bytes per bin
    writer->num_dst++;
    VPE_ASSERT(!(dst->base_addr_lo & 0xFF));

    *cmd_space++ = dst->base_addr_lo;
    *cmd_space++ = dst->base_addr_hi;

    writer->buf->cpu_va += size;
    writer->buf->gpu_va += size;
    writer->buf->size -= size;
}
