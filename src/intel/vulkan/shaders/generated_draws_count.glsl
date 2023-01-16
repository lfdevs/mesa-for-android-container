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

/* These 2 bindings will be accessed through A64 messages */
layout(set = 0, binding = 0, std430) buffer Storage0 {
   uint indirect_data[];
};

layout(set = 0, binding = 1, std430) buffer Storage1 {
   uint commands[];
};

/* This data will be provided through push constants. */
layout(set = 0, binding = 2) uniform block {
   uint is_indexed;
   uint is_predicated;
   uint draw_base;
   uint item_count;
   uint draw_count;
   uint instance_multiplier;
   uint indirect_data_stride;
   uint end_addr_ldw;
   uint end_addr_udw;
};

void main()
{
   uint item_idx = uint(gl_FragCoord.y) * 8192 + uint(gl_FragCoord.x);
   uint indirect_data_offset = item_idx * indirect_data_stride / 4;
   uint _3dprim_dw_size = 10;
   uint cmd_idx = item_idx * _3dprim_dw_size;

   /* Loading a VkDrawIndexedIndirectCommand */
   uint index_count    = indirect_data[indirect_data_offset + 0];
   uint instance_count = indirect_data[indirect_data_offset + 1];
   uint first_index    = indirect_data[indirect_data_offset + 2];
   uint vertex_offset  = indirect_data[indirect_data_offset + 3];
   uint first_instance = indirect_data[indirect_data_offset + 4];
   uint draw_id        = draw_base + item_idx;

   if (draw_id < draw_count) {
      if (is_indexed != 0) {
         /* Loading a VkDrawIndexedIndirectCommand */
         uint index_count    = indirect_data[indirect_data_offset + 0];
         uint instance_count = indirect_data[indirect_data_offset + 1] * instance_multiplier;
         uint first_index    = indirect_data[indirect_data_offset + 2];
         uint vertex_offset  = indirect_data[indirect_data_offset + 3];
         uint first_instance = indirect_data[indirect_data_offset + 4];

         commands[cmd_idx + 0] = (3 << 29 |         /* Command Type */
                                  3 << 27 |         /* Command SubType */
                                  3 << 24 |         /* 3D Command Opcode */
                                  1 << 11 |         /* Extended Parameter Enable */
                                  is_predicated << 8 |
                                  8 << 0);          /* DWord Length */
         commands[cmd_idx + 1] = 1 << 8;            /* Indexed */
         commands[cmd_idx + 2] = index_count;       /* Vertex Count Per Instance */
         commands[cmd_idx + 3] = first_index;       /* Start Vertex Location */
         commands[cmd_idx + 4] = instance_count;    /* Instance Count */
         commands[cmd_idx + 5] = first_instance;    /* Start Instance Location */
         commands[cmd_idx + 6] = vertex_offset;     /* Base Vertex Location */
         commands[cmd_idx + 7] = vertex_offset;     /* gl_BaseVertex */
         commands[cmd_idx + 8] = first_instance;    /* gl_BaseInstance */
         commands[cmd_idx + 9] = draw_id;           /* gl_DrawID */
      } else {
         /* Loading a VkDrawIndirectCommand structure */
         uint vertex_count   = indirect_data[indirect_data_offset + 0];
         uint instance_count = indirect_data[indirect_data_offset + 1] * instance_multiplier;
         uint first_vertex   = indirect_data[indirect_data_offset + 2];
         uint first_instance = indirect_data[indirect_data_offset + 3];

         commands[cmd_idx + 0] = (3 << 29 |         /* Command Type */
                                  3 << 27 |         /* Command SubType */
                                  3 << 24 |         /* 3D Command Opcode */
                                  1 << 11 |         /* Extended Parameter Enable */
                                  is_predicated << 8 |
                                  8 << 0);          /* DWord Length */
         commands[cmd_idx + 1] = 0;
         commands[cmd_idx + 2] = vertex_count;      /* Vertex Count Per Instance */
         commands[cmd_idx + 3] = first_vertex;      /* Start Vertex Location */
         commands[cmd_idx + 4] = instance_count;    /* Instance Count */
         commands[cmd_idx + 5] = first_instance;    /* Start Instance Location */
         commands[cmd_idx + 6] = 0;                 /* Base Vertex Location */
         commands[cmd_idx + 7] = first_vertex;      /* gl_BaseVertex */
         commands[cmd_idx + 8] = first_instance;    /* gl_BaseInstance */
         commands[cmd_idx + 9] = draw_id;           /* gl_DrawID */
      }
   } else if (draw_id == draw_count) {
      commands[cmd_idx + 0] = (0  << 29 |        /* Command Type */
                               49 << 23 |        /* MI Command Opcode */
                               1  << 8  |        /* Address Space Indicator (PPGTT) */
                               1  << 0);         /* DWord Length */
      commands[cmd_idx + 1] = end_addr_ldw;
      commands[cmd_idx + 2] = end_addr_udw;
   }
}
