/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PVR_SHADER_FACTORY_H
#define PVR_SHADER_FACTORY_H

#include <stdint.h>

/* Occlusion query availability writes. */
enum pvr_query_availability_write_pool_const {
   PVR_QUERY_AVAILABILITY_WRITE_INDEX_COUNT,
   PVR_QUERY_AVAILABILITY_WRITE_COUNT,
};

/* Copy query pool results. */
enum pvr_copy_query_pool_const {
   PVR_COPY_QUERY_POOL_RESULTS_INDEX_COUNT,
   PVR_COPY_QUERY_POOL_RESULTS_BASE_ADDRESS_LOW,
   PVR_COPY_QUERY_POOL_RESULTS_BASE_ADDRESS_HIGH,
   PVR_COPY_QUERY_POOL_RESULTS_DEST_STRIDE,
   PVR_COPY_QUERY_POOL_RESULTS_PARTIAL_RESULT_FLAG,
   PVR_COPY_QUERY_POOL_RESULTS_64_BIT_FLAG,
   PVR_COPY_QUERY_POOL_RESULTS_WITH_AVAILABILITY_FLAG,
   PVR_COPY_QUERY_POOL_RESULTS_COUNT,
};

/* Reset query pool. */
enum pvr_reset_query_pool_pool_const {
   PVR_RESET_QUERY_POOL_INDEX_COUNT,
   PVR_RESET_QUERY_POOL_COUNT,
};

/* ClearAttachments. */
enum pvr_clear_attachment_const {
   PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_0,
   PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_1,
   PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_2,
   PVR_CLEAR_ATTACHMENT_CONST_COMPONENT_3,
   PVR_CLEAR_ATTACHMENT_CONST_TILE_BUFFER_UPPER,
   PVR_CLEAR_ATTACHMENT_CONST_TILE_BUFFER_LOWER,
   PVR_CLEAR_ATTACHMENT_CONST_COUNT,
};

#define PVR_CLEAR_ATTACHMENT_DEST_ID_UNUSED (~0U)

/* 8 + 8 = 16 <- 1 Dword, 8 offsets, to registers/tile buffers
 * 7 + 7 = 14 <- 2 Dwords, 7 offsets, to registers/tile buffers
 * 6 + 6 = 12 <- 3 Dwords, 6 offsets, to registers/tile buffers
 * 5 + 5 = 10 <- 4 Dwords, 5 offsets, to registers/tile buffers
 */
#define PVR_CLEAR_ATTACHMENT_PROGRAM_COUNT 52

/* This defines the max theoretic number of clear attachment programs. In cases
 * where the dword count goes past the number of on-chip on-tile-buffer targets
 * there are unused elements. There are 4 versions for clearing 1..4 dwords, 8
 * versions for clearing offsets 0..7 and 2 versions for clearing either on
 * chip or in memory calculated as 4 * 8 * 2 = 64.
 */
#define PVR_CLEAR_ATTACHMENT_PROGRAM_COUNT_WITH_HOLES 64

#define PVR_CLEAR_ATTACHMENT_PROGRAM_DWORDS_SHIFT (4U)
#define PVR_CLEAR_ATTACHMENT_PROGRAM_DWORDS_SETMASK (0x30U)
#define PVR_CLEAR_ATTACHMENT_PROGRAM_OFFSET_SHIFT (1U)
#define PVR_CLEAR_ATTACHMENT_PROGRAM_OFFSET_SETMASK (0x0EU)
#define PVR_CLEAR_ATTACHMENT_PROGRAM_DEST_SHIFT (0U)
#define PVR_CLEAR_ATTACHMENT_PROGRAM_DEST_SETMASK (0x01U)

static inline uint32_t
pvr_get_clear_attachment_program_index(uint32_t dwords,
                                       uint32_t offset,
                                       uint8_t tile_buffer)
{
   return (((dwords - 1) << PVR_CLEAR_ATTACHMENT_PROGRAM_DWORDS_SHIFT) |
           (offset << PVR_CLEAR_ATTACHMENT_PROGRAM_OFFSET_SHIFT) |
           (tile_buffer << PVR_CLEAR_ATTACHMENT_PROGRAM_DEST_SHIFT)) &
          0x3f;
}

#endif /* PVR_SHADER_FACTORY_H */
