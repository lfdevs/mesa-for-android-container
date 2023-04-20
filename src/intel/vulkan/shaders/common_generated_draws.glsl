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

#define BITFIELD_BIT(i) (1u << i)

#define ANV_GENERATED_FLAG_INDEXED    BITFIELD_BIT(0)
#define ANV_GENERATED_FLAG_PREDICATED BITFIELD_BIT(1)
#define ANV_GENERATED_FLAG_DRAWID     BITFIELD_BIT(2)
#define ANV_GENERATED_FLAG_BASE       BITFIELD_BIT(3)

/* These 3 bindings will be accessed through A64 messages */
layout(set = 0, binding = 0, std430) buffer Storage0 {
   uint indirect_data[];
};

layout(set = 0, binding = 1, std430) buffer Storage1 {
   uint commands[];
};

layout(set = 0, binding = 2, std430) buffer Storage2 {
   uint draw_ids[];
};

/* This data will be provided through push constants. */
layout(set = 0, binding = 3) uniform block {
   uint64_t draw_id_addr;
   uint64_t indirect_data_addr;
   uint indirect_data_stride;
   uint flags;
   uint draw_base;
   uint draw_count;
   uint max_draw_count;
   uint instance_multiplier;
   uint64_t end_addr;
};

void write_VERTEX_BUFFER_STATE(uint write_offset,
                               uint mocs,
                               uint buffer_idx,
                               uint64_t address,
                               uint size)
{
   commands[write_offset + 0] = (0          << 0  |    /* Buffer Pitch */
                                 0          << 13 |    /* Null Vertex Buffer */
                                 1          << 14 |    /* Address Modify Enable */
                                 mocs       << 16 |    /* MOCS */
                                 buffer_idx << 26);    /* Vertex Buffer Index */
   commands[write_offset + 1]  = uint(address & 0xffffffff);
   commands[write_offset + 2]  = uint(address >> 32);
   commands[write_offset + 3]  = size;
}

void write_3DPRIMITIVE(uint write_offset,
                       bool is_predicated,
                       bool is_indexed,
                       uint vertex_count_per_instance,
                       uint start_vertex_location,
                       uint instance_count,
                       uint start_instance_location,
                       uint base_vertex_location)
{
   commands[write_offset + 0] = (3 << 29 |         /* Command Type */
                                 3 << 27 |         /* Command SubType */
                                 3 << 24 |         /* 3D Command Opcode */
                                 uint(is_predicated) << 8 |
                                 5 << 0);          /* DWord Length */
   commands[write_offset + 1] = uint(is_indexed) << 8;
   commands[write_offset + 2] = vertex_count_per_instance;
   commands[write_offset + 3] = start_vertex_location;
   commands[write_offset + 4] = instance_count;
   commands[write_offset + 5] = start_instance_location;
   commands[write_offset + 6] = base_vertex_location;
}

void write_3DPRIMITIVE_EXTENDED(uint write_offset,
                                bool is_predicated,
                                bool is_indexed,
                                uint vertex_count_per_instance,
                                uint start_vertex_location,
                                uint instance_count,
                                uint start_instance_location,
                                uint base_vertex_location,
                                uint param_base_vertex,
                                uint param_base_instance,
                                uint param_draw_id)
{
   commands[write_offset + 0] = (3 << 29 |         /* Command Type */
                                 3 << 27 |         /* Command SubType */
                                 3 << 24 |         /* 3D Command Opcode */
                                 1 << 11 |         /* Extended Parameter Enable */
                                 uint(is_predicated) << 8 |
                                 8 << 0);          /* DWord Length */
   commands[write_offset + 1] = uint(is_indexed) << 8;
   commands[write_offset + 2] = vertex_count_per_instance;
   commands[write_offset + 3] = start_vertex_location;
   commands[write_offset + 4] = instance_count;
   commands[write_offset + 5] = start_instance_location;
   commands[write_offset + 6] = base_vertex_location;
   commands[write_offset + 7] = param_base_vertex;
   commands[write_offset + 8] = param_base_instance;
   commands[write_offset + 9] = param_draw_id;
}

void write_MI_BATCH_BUFFER_START(uint write_offset,
                                 uint64_t addr)
{
   commands[write_offset + 0] = (0  << 29 | /* Command Type */
                                 49 << 23 | /* MI Command Opcode */
                                 1  << 8  | /* Address Space Indicator (PPGTT) */
                                 1  << 0);  /* DWord Length */
   commands[write_offset + 1] = uint(addr & 0xffffffff);
   commands[write_offset + 2] = uint(addr >> 32);
}
