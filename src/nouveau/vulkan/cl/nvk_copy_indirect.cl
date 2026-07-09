/*
 * Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nvk_copy_indirect.h"

#include "nv_push.h"
#include "nv_push_cl90b5.h"

#include "compiler/libcl/libcl_vk.h"

void
nvk_copy_indirect(const __global void* in, uintptr_t in_stride,
                  __global uint32_t* restrict out, uint32_t count)
{
   uint i = get_sub_group_local_id() + cl_group_id.x * 32;
   if (i >= count)
      return;

   in += i * in_stride;
   VkCopyMemoryIndirectCommandKHR cmd = *((__global VkCopyMemoryIndirectCommandKHR*)in);

   uint32_t push_data[NVK_COPY_INDIRECT_CMD_WORDS];
   struct nv_push push;
   struct nv_push *p = &push;
   nv_push_init(p, push_data, ARRAY_SIZE(push_data),
                BITFIELD_BIT(SUBC_NV90B5));

   P_MTHD(p, NV90B5, OFFSET_IN_UPPER);
   P_NV90B5_OFFSET_IN_UPPER(p, cmd.srcAddress >> 32);
   P_NV90B5_OFFSET_IN_LOWER(p, cmd.srcAddress & 0xffffffff);
   P_NV90B5_OFFSET_OUT_UPPER(p, cmd.dstAddress >> 32);
   P_NV90B5_OFFSET_OUT_LOWER(p, cmd.dstAddress & 0xffffffff);

   P_MTHD(p, NV90B5, LINE_LENGTH_IN);
   P_NV90B5_LINE_LENGTH_IN(p, cmd.size);

   P_IMMD_WORD(p, NV90B5, LAUNCH_DMA, {
      .data_transfer_type = DATA_TRANSFER_TYPE_PIPELINED,
      .multi_line_enable = MULTI_LINE_ENABLE_FALSE,
      .flush_enable = FLUSH_ENABLE_TRUE,
      .src_memory_layout = SRC_MEMORY_LAYOUT_PITCH,
      .dst_memory_layout = DST_MEMORY_LAYOUT_PITCH,
      .remap_enable = REMAP_ENABLE_FALSE,
   });

   assert(nv_push_dw_count(p) == NVK_COPY_INDIRECT_CMD_WORDS);
   memcpy(out + i * NVK_COPY_INDIRECT_CMD_WORDS, push_data, sizeof(push_data));
}
