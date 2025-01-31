/* Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "libintel_shaders.h"
#include "dev/intel_wa.h"

static void end_generated_draws(global void *dst_ptr,
                                uint32_t item_idx,
                                uint32_t draw_id, uint32_t draw_count,
                                uint32_t ring_count, uint32_t max_draw_count,
                                uint32_t flags, uint32_t _3dprim_size_B,
                                uint64_t gen_addr, uint64_t end_addr)
{
   bool indirect_count = (flags & ANV_GENERATED_FLAG_COUNT) != 0;
   bool ring_mode = (flags & ANV_GENERATED_FLAG_RING_MODE) != 0;
   /* We can have an indirect draw count = 0. */
   uint32_t last_draw_id = draw_count == 0 ? 0 : (min(draw_count, max_draw_count) - 1);
   global void *jump_dst = draw_count == 0 ? dst_ptr : (dst_ptr + _3dprim_size_B);

   if (ring_mode) {
      if (draw_id == last_draw_id) {
         /* Exit the ring buffer to the next user commands */
         genX(write_MI_BATCH_BUFFER_START)(jump_dst, end_addr);
      } else if (item_idx == (ring_count - 1)) {
         /* Jump back to the generation shader to generate mode draws */
         genX(write_MI_BATCH_BUFFER_START)(jump_dst, gen_addr);
      }
   } else {
      if (draw_id == last_draw_id && draw_count < max_draw_count) {
         /* Skip forward to the end of the generated draws */
         genX(write_MI_BATCH_BUFFER_START)(jump_dst, end_addr);
      }
   }
}

void
genX(libanv_write_draw)(global void *dst_base,
                        global void *wa_insts_ptr,
                        global void *indirect_base,
                        global void *draw_id_base,
                        uint32_t indirect_stride,
                        global uint32_t *_draw_count,
                        uint32_t draw_base,
                        uint32_t instance_multiplier,
                        uint32_t max_draw_count,
                        uint32_t flags,
                        uint32_t mocs,
                        uint32_t _3dprim_size_B,
                        uint32_t ring_count,
                        uint64_t gen_addr,
                        uint64_t end_addr,
                        uint32_t item_idx)
{
   uint32_t draw_id = draw_base + item_idx;
   uint32_t draw_count = *_draw_count;
   global void *dst_ptr = dst_base + item_idx * _3dprim_size_B;
   global void *indirect_ptr = indirect_base + draw_id * indirect_stride;
   global void *draw_id_ptr = draw_id_base + item_idx * 4;

   if (draw_id < min(draw_count, max_draw_count)) {
      bool is_indexed = (flags & ANV_GENERATED_FLAG_INDEXED) != 0;
      bool is_predicated = (flags & ANV_GENERATED_FLAG_PREDICATED) != 0;
      bool uses_tbimr = (flags & ANV_GENERATED_FLAG_TBIMR) != 0;
      bool uses_base = (flags & ANV_GENERATED_FLAG_BASE) != 0;
      bool uses_drawid = (flags & ANV_GENERATED_FLAG_DRAWID) != 0;
      uint32_t inst_offset_B = 0;

#if INTEL_WA_16011107343_GFX_VER
      if (flags & ANV_GENERATED_FLAG_WA_16011107343) {
         genX(copy_data)(dst_ptr + inst_offset_B,
                         wa_insts_ptr + inst_offset_B,
                         GENX(3DSTATE_HS_length) * 4);
         inst_offset_B += GENX(3DSTATE_HS_length) * 4;
      }
#endif

#if INTEL_WA_22018402687_GFX_VER
      if (flags & ANV_GENERATED_FLAG_WA_22018402687) {
         genX(copy_data)(dst_ptr + inst_offset_B,
                         wa_insts_ptr + inst_offset_B,
                         GENX(3DSTATE_DS_length) * 4);
         inst_offset_B += GENX(3DSTATE_DS_length) * 4;
      }
#endif

      genX(write_draw)(dst_ptr + inst_offset_B,
                       indirect_ptr, draw_id_ptr,
                       draw_id, instance_multiplier,
                       is_indexed, is_predicated,
                       uses_tbimr, uses_base, uses_drawid,
                       mocs);
   }

   end_generated_draws(dst_ptr, item_idx, draw_id, draw_count,
                       ring_count, max_draw_count,
                       flags, _3dprim_size_B,
                       gen_addr, end_addr);
}
