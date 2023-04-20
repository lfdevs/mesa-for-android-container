/*
 * Copyright © 2022 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#version 450
#extension GL_ARB_gpu_shader_int64 : enable
#extension GL_GOOGLE_include_directive : enable

#include "common_generated_draws.glsl"

void main()
{
   bool is_indexed = (flags & ANV_GENERATED_FLAG_INDEXED) != 0;
   bool is_predicated = (flags & ANV_GENERATED_FLAG_PREDICATED) != 0;
   bool uses_base = (flags & ANV_GENERATED_FLAG_BASE) != 0;
   bool uses_drawid = (flags & ANV_GENERATED_FLAG_DRAWID) != 0;
   uint mocs = (flags >> 8) & 0xff;
   uint _3dprim_dw_size = (flags >> 16) & 0xff;
   uint item_idx = uint(gl_FragCoord.y) * 8192 + uint(gl_FragCoord.x);
   uint indirect_data_offset = item_idx * indirect_data_stride / 4;
   uint cmd_idx = item_idx * _3dprim_dw_size;
   uint draw_id = draw_base + item_idx;

   if (draw_id < draw_count) {
      if (is_indexed) {
         /* Loading a VkDrawIndexedIndirectCommand */
         uint index_count    = indirect_data[indirect_data_offset + 0];
         uint instance_count = indirect_data[indirect_data_offset + 1] * instance_multiplier;
         uint first_index    = indirect_data[indirect_data_offset + 2];
         uint vertex_offset  = indirect_data[indirect_data_offset + 3];
         uint first_instance = indirect_data[indirect_data_offset + 4];

         if (uses_base || uses_drawid) {
            uint state_vertex_len =
               1 + (uses_base ? 4 : 0) + (uses_drawid ? 4 : 0);
            commands[cmd_idx] =
               (3  << 29 |                    /* Command Type */
                3  << 27 |                    /* Command SubType */
                0  << 24 |                    /* 3D Command Opcode */
                8  << 16 |                    /* 3D Command Sub Opcode */
                (state_vertex_len - 2) << 0); /* DWord Length */
            cmd_idx += 1;
            if (uses_base) {
               uint64_t indirect_draw_data_addr =
                  indirect_data_addr + item_idx * indirect_data_stride + 12;
               write_VERTEX_BUFFER_STATE(cmd_idx,
                                         mocs,
                                         31,
                                         indirect_draw_data_addr,
                                         8);
               cmd_idx += 4;
            }
            if (uses_drawid) {
               uint64_t draw_idx_addr = draw_id_addr + 4 * item_idx;
               draw_ids[draw_id] = draw_id;
               write_VERTEX_BUFFER_STATE(cmd_idx,
                                         mocs,
                                         32,
                                         draw_idx_addr,
                                         4);
               cmd_idx += 4;
            }
         }
         write_3DPRIMITIVE(cmd_idx,
                           is_predicated,
                           is_indexed,
                           index_count,
                           first_index,
                           instance_count,
                           first_instance,
                           vertex_offset);
      } else {
         /* Loading a VkDrawIndirectCommand structure */
         uint vertex_count   = indirect_data[indirect_data_offset + 0];
         uint instance_count = indirect_data[indirect_data_offset + 1] * instance_multiplier;
         uint first_vertex   = indirect_data[indirect_data_offset + 2];
         uint first_instance = indirect_data[indirect_data_offset + 3];

         if (uses_base || uses_drawid) {
            uint state_vertex_len =
               1 + (uses_base ? 4 : 0) + (uses_drawid ? 4 : 0);
            commands[cmd_idx] =
               (3  << 29 |                    /* Command Type */
                3  << 27 |                    /* Command SubType */
                0  << 24 |                    /* 3D Command Opcode */
                8  << 16 |                    /* 3D Command Sub Opcode */
                (state_vertex_len - 2) << 0); /* DWord Length */
            cmd_idx += 1;
            if (uses_base) {
               uint64_t indirect_draw_data_addr =
                  indirect_data_addr + item_idx * indirect_data_stride + 8;
               write_VERTEX_BUFFER_STATE(cmd_idx,
                                         mocs,
                                         31,
                                         indirect_draw_data_addr,
                                         8);
               cmd_idx += 4;
            }
            if (uses_drawid) {
               uint64_t draw_idx_addr = draw_id_addr + 4 * item_idx;
               draw_ids[draw_id] = draw_id;
               write_VERTEX_BUFFER_STATE(cmd_idx,
                                         mocs,
                                         32,
                                         draw_idx_addr,
                                         4);
               cmd_idx += 4;
            }
         }
         write_3DPRIMITIVE(cmd_idx,
                           is_predicated,
                           is_indexed,
                           vertex_count,
                           first_vertex,
                           instance_count,
                           first_instance,
                           0 /* base_vertex_location */);
      }
   } else if (draw_id == draw_count && draw_id < max_draw_count) {
      /* Only write a jump forward in the batch if we have fewer elements than
       * the max draw count.
       */
      write_MI_BATCH_BUFFER_START(cmd_idx, end_addr);
   }
}
