/*
 * Copyright (c) 2019 Andreas Baierl <ichgeh@imkreisrum.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "util/u_math.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "lima_context.h"
#include "lima_parser.h"
#include "lima_texture.h"

#include "lima/ir/gp/codegen.h"
#include "lima/ir/pp/codegen.h"

/* VS CMD stream parser functions */

static void
parse_vs_draw(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   if ((*value1 == 0x00000000) && (*value2 == 0x00000000))
      fprintf(fp, "\t/* ---EMPTY CMD */\n");
   else
      fprintf(fp, "\t/* DRAW: num: %d, index_draw: %s */\n",
              (*value1 & 0xff000000) >> 24 | (*value2 & 0x000000ff) << 8,
              (*value1 & 0x00000001) ? "true" : "false");
}

static void
parse_vs_shader_info(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* SHADER_INFO: prefetch: %d, size: %d */\n",
           (*value1 & 0xfff00000) >> 20,
           (((*value1 & 0x000fffff) >> 10) + 1) << 4);
}

static void
parse_vs_unknown1(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* UNKNOWN_1 */\n");
}

static void
parse_vs_varying_attribute_count(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* VARYING_ATTRIBUTE_COUNT: nr_vary: %d, nr_attr: %d */\n",
           ((*value1 & 0x00ffffff) >> 8) + 1, (*value1 >> 24) + 1);
}

static void
parse_vs_attributes_address(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* ATTRIBUTES_ADDRESS: address: 0x%08x, size: %d */\n",
           *value1, (*value2 & 0x0fffffff) >> 17);
}

static void
parse_vs_varyings_address(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* VARYINGS_ADDRESS: varying info @ 0x%08x, size: %d */\n",
           *value1, (*value2 & 0x0fffffff) >> 17);
}

static void
parse_vs_uniforms_address(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* UNIFORMS_ADDRESS (GP): address: 0x%08x, size: %d */\n",
           *value1, (*value2 & 0x0fffffff) >> 12);
}

static void
parse_vs_shader_address(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* SHADER_ADDRESS (VS): address: 0x%08x, size: %d */\n",
           *value1, (*value2 & 0x0fffffff) >> 12);
}

static void
parse_vs_semaphore(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   if (*value1 == 0x00028000)
      fprintf(fp, "\t/* SEMAPHORE_BEGIN_1 */\n");
   else if (*value1 == 0x00000001)
      fprintf(fp, "\t/* SEMAPHORE_BEGIN_2 */\n");
   else if (*value1 == 0x00000000)
      fprintf(fp, "\t/* SEMAPHORE_END: index_draw disabled */\n");
   else if (*value1 == 0x00018000)
      fprintf(fp, "\t/* SEMAPHORE_END: index_draw enabled */\n");
   else
      fprintf(fp, "\t/* SEMAPHORE - cmd unknown! */\n");
}

static void
parse_vs_unknown2(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* UNKNOWN_2 */\n");
}

static void
parse_vs_continue(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* CONTINUE: at 0x%08x */\n", *value1);
}

void
lima_parse_vs(FILE *fp, uint32_t *data, int size, uint32_t start)
{
   uint32_t *value1;
   uint32_t *value2;

   fprintf(fp, "\n");
   fprintf(fp, "/* ============ VS CMD STREAM BEGIN ============= */\n");
   for (int i = 0; i * 4 < size; i += 2) {
      value1 = &data[i];
      value2 = &data[i + 1];
      fprintf(fp, "/* 0x%08x (0x%08x) */\t0x%08x 0x%08x",
              start + i * 4, i * 4, *value1, *value2);

      if ((*value2 & 0xffff0000) == 0x00000000)
         parse_vs_draw(fp, value1, value2);
      else if ((*value2 & 0xff0000ff) == 0x10000040)
         parse_vs_shader_info(fp, value1, value2);
      else if ((*value2 & 0xff0000ff) == 0x10000041)
         parse_vs_unknown1(fp, value1, value2);
      else if ((*value2 & 0xff0000ff) == 0x10000042)
         parse_vs_varying_attribute_count(fp, value1, value2);
      else if ((*value2 & 0xff0000ff) == 0x20000000)
         parse_vs_attributes_address(fp, value1, value2);
      else if ((*value2 & 0xff0000ff) == 0x20000008)
         parse_vs_varyings_address(fp, value1, value2);
      else if ((*value2 & 0xff000000) == 0x30000000)
         parse_vs_uniforms_address(fp, value1, value2);
      else if ((*value2 & 0xff000000) == 0x40000000)
         parse_vs_shader_address(fp, value1, value2);
      else if ((*value2  & 0xff000000)== 0x50000000)
         parse_vs_semaphore(fp, value1, value2);
      else if ((*value2 & 0xff000000) == 0x60000000)
         parse_vs_unknown2(fp, value1, value2);
      else if ((*value2 & 0xff000000) == 0xf0000000)
         parse_vs_continue(fp, value1, value2);
      else
         fprintf(fp, "\t/* --- unknown cmd --- */\n");
   }
   fprintf(fp, "/* ============ VS CMD STREAM END =============== */\n");
   fprintf(fp, "\n");
}

/* PLBU CMD stream parser functions */

static void
parse_plbu_block_step(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* BLOCK_STEP: shift_min: %d, shift_h: %d, shift_w: %d */\n",
           (*value1 & 0xf0000000) >> 28,
           (*value1 & 0x0fff0000) >> 16,
           *value1 & 0x0000ffff);
}

static void
parse_plbu_tiled_dimensions(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* TILED_DIMENSIONS: tiled_w: %d, tiled_h: %d */\n",
           ((*value1 & 0xff000000) >> 24) + 1,
           ((*value1 & 0x00ffff00) >> 8) + 1);
}

static void
parse_plbu_block_stride(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* BLOCK_STRIDE: block_w: %d */\n", *value1 & 0x000000ff);
}

static void
parse_plbu_array_address(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* ARRAY_ADDRESS: gp_stream: 0x%08x, block_num (block_w * block_h): %d */\n",
           *value1, (*value2 & 0x00ffffff) + 1);
}

static void
parse_plbu_viewport_left(FILE *fp, float *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* VIEWPORT_LEFT: viewport_left: %f */\n", *value1);
}

static void
parse_plbu_viewport_right(FILE *fp, float *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* VIEWPORT_RIGHT: viewport_right: %f */\n", *value1);
}

static void
parse_plbu_viewport_bottom(FILE *fp, float *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* VIEWPORT_BOTTOM: viewport_bottom: %f */\n", *value1);
}

static void
parse_plbu_viewport_top(FILE *fp, float *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* VIEWPORT_TOP: viewport_top: %f */\n", *value1);
}

static void
parse_plbu_semaphore(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   if (*value1 == 0x00010002)
      fprintf(fp, "\t/* ARRAYS_SEMAPHORE_BEGIN */\n");
   else if (*value1 == 0x00010001)
      fprintf(fp, "\t/* ARRAYS_SEMAPHORE_END */\n");
   else
      fprintf(fp, "\t/* SEMAPHORE - cmd unknown! */\n");
}

static void
parse_plbu_primitive_setup(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   if (*value1 == 0x00000200)
      fprintf(fp, "\t/* UNKNOWN_2 (PRIMITIVE_SETUP INIT?) */\n");
   else
      fprintf(fp, "\t/* PRIMITIVE_SETUP: %scull: %d (0x%x), index_size: %d */\n",
              (*value1 & 0x1000) ? "force point size, " : "",
              (*value1 & 0x000f0000) >> 16, (*value1 & 0x000f0000) >> 16,
              (*value1 & 0x00000e00) >> 9);
}

static void
parse_plbu_rsw_vertex_array(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* RSW_VERTEX_ARRAY: rsw: 0x%08x, gl_pos: 0x%08x */\n",
           *value1,
           (*value2 & 0x0fffffff) << 4);
}

static void
parse_plbu_scissors(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   float minx = (*value1 & 0xc0000000) >> 30 | (*value2 & 0x00001fff) << 2;
   float maxx = ((*value2 & 0x0fffe000) >> 13) + 1;
   float miny = *value1 & 0x00003fff;
   float maxy = ((*value1 & 0x3fff8000) >> 15) + 1;

   fprintf(fp, "\t/* SCISSORS: minx: %f, maxx: %f, miny: %f, maxy: %f */\n",
           minx, maxx, miny, maxy);
}

static void
parse_plbu_unknown_1(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* UNKNOWN_1 */\n");
}

static void
parse_plbu_low_prim_size(FILE *fp, float *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* LOW_PRIM_SIZE: size: %f */\n", *value1);
}

static void
parse_plbu_depth_range_near(FILE *fp, float *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* DEPTH_RANG_NEAR: depth_range: %f */\n", *value1);
}

static void
parse_plbu_depth_range_far(FILE *fp, float *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* DEPTH_RANGE_FAR: depth_range: %f */\n", *value1);
}

static void
parse_plbu_indexed_dest(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* INDEXED_DEST: gl_pos: 0x%08x */\n", *value1);
}

static void
parse_plbu_indexed_pt_size(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* INDEXED_PT_SIZE: pt_size: 0x%08x */\n", *value1);
}

static void
parse_plbu_indices(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* INDICES: indices: 0x%08x */\n", *value1);
}

static void
parse_plbu_draw_arrays(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   if ((*value1 == 0x00000000) && (*value2 == 0x00000000)) {
      fprintf(fp, "\t/* ---EMPTY CMD */\n");
      return;
   }

   uint32_t count = (*value1 & 0xff000000) >> 24 | (*value2 & 0x000000ff) << 8;
   uint32_t start = *value1 & 0x00ffffff;
   uint32_t mode = (*value2 & 0x001f0000) >> 16;

   fprintf(fp, "\t/* DRAW_ARRAYS: count: %d, start: %d, mode: %d (0x%x) */\n",
           count, start, mode, mode);
}

static void
parse_plbu_draw_elements(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   uint32_t count = (*value1 & 0xff000000) >> 24 | (*value2 & 0x000000ff) << 8;
   uint32_t start = *value1 & 0x00ffffff;
   uint32_t mode = (*value2 & 0x001f0000) >> 16;

   fprintf(fp, "\t/* DRAW_ELEMENTS: count: %d, start: %d, mode: %d (0x%x) */\n",
           count, start, mode, mode);
}

static void
parse_plbu_continue(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* CONTINUE: continue at 0x%08x */\n", *value1);
}

static void
parse_plbu_end(FILE *fp, uint32_t *value1, uint32_t *value2)
{
   fprintf(fp, "\t/* END (FINISH/FLUSH) */\n");
}

void
lima_parse_plbu(FILE *fp, uint32_t *data, int size, uint32_t start)
{
   uint32_t *value1;
   uint32_t *value2;

   fprintf(fp, "/* ============ PLBU CMD STREAM BEGIN ============= */\n");
   for (int i = 0; i * 4 < size; i += 2) {
      value1 = &data[i];
      value2 = &data[i + 1];
      fprintf(fp, "/* 0x%08x (0x%08x) */\t0x%08x 0x%08x",
              start + i * 4, i * 4, *value1, *value2);

      if ((*value2 & 0xffe00000) == 0x00000000)
         parse_plbu_draw_arrays(fp, value1, value2);
      else if ((*value2 & 0xffe00000) == 0x00200000)
         parse_plbu_draw_elements(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000100)
         parse_plbu_indexed_dest(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000101)
         parse_plbu_indices(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000102)
         parse_plbu_indexed_pt_size(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000105)
         parse_plbu_viewport_bottom(fp, (float *)value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000106)
         parse_plbu_viewport_top(fp, (float *)value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000107)
         parse_plbu_viewport_left(fp, (float *)value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000108)
         parse_plbu_viewport_right(fp, (float *)value1, value2);
      else if ((*value2 & 0xff000fff) == 0x10000109)
         parse_plbu_tiled_dimensions(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x1000010a)
         parse_plbu_unknown_1(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x1000010b) /* also unknown_2 */
         parse_plbu_primitive_setup(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x1000010c)
         parse_plbu_block_step(fp, value1, value2);
      else if ((*value2 & 0xff000fff) == 0x1000010d)
         parse_plbu_low_prim_size(fp, (float *)value1, value2);
      else if ((*value2 & 0xff000fff) == 0x1000010e)
         parse_plbu_depth_range_near(fp, (float *)value1, value2);
      else if ((*value2 & 0xff000fff) == 0x1000010f)
         parse_plbu_depth_range_far(fp, (float *)value1, value2);
      else if ((*value2 & 0xff000000) == 0x28000000)
         parse_plbu_array_address(fp, value1, value2);
      else if ((*value2 & 0xf0000000) == 0x30000000)
         parse_plbu_block_stride(fp, value1, value2);
      else if (*value2 == 0x50000000)
         parse_plbu_end(fp, value1, value2);
      else if ((*value2  & 0xf0000000)== 0x60000000)
         parse_plbu_semaphore(fp, value1, value2);
      else if ((*value2  & 0xf0000000)== 0x70000000)
         parse_plbu_scissors(fp, value1, value2);
      else if ((*value2  & 0xf0000000)== 0x80000000)
         parse_plbu_rsw_vertex_array(fp, value1, value2);
      else if ((*value2  & 0xf0000000)== 0xf0000000)
         parse_plbu_continue(fp, value1, value2);
      else
         fprintf(fp, "\t/* --- unknown cmd --- */\n");
   }
   fprintf(fp, "/* ============ PLBU CMD STREAM END =============== */\n");
   fprintf(fp, "\n");
}

void
lima_parse_shader(FILE *fp, uint32_t *data, int size, bool is_frag)
{
   uint32_t *value = &data[0];

   if (is_frag) {
      uint32_t *bin = value;
      uint32_t offt = 0;
      uint32_t next_instr_length = 0;

      fprintf(fp, "/* ============ FS DISASSEMBLY BEGIN ============== */\n");

      do {
         ppir_codegen_ctrl *ctrl = (ppir_codegen_ctrl *)bin;
         fprintf(fp, "@%6d: ", offt);
         ppir_disassemble_instr(bin, offt, fp);
         bin += ctrl->count;
         offt += ctrl->count;
         next_instr_length = ctrl->next_count;
      } while (next_instr_length);

      fprintf(fp, "/* ============ FS DISASSEMBLY END ================= */\n");
   } else {
      fprintf(fp, "/* ============ VS DISASSEMBLY BEGIN ============== */\n");
      gpir_disassemble_program((gpir_codegen_instr *)value, size / sizeof(gpir_codegen_instr), fp);
      fprintf(fp, "/* ============ VS DISASSEMBLY END ================= */\n");
   }
}

void
lima_parse_render_state(FILE *fp, uint32_t *data, int size, uint32_t start)
{
   fprintf(fp, "/* ============ RSW BEGIN ========================= */\n");
   lima_unpack(fp, data, RENDER_STATE, temp);
   lima_print(fp, RENDER_STATE, temp, 8);
   fprintf(fp, "/* ============ RSW END =========================== */\n");
}

void
lima_parse_texture_descriptor(FILE *fp, uint32_t *data, int size, uint32_t start, uint32_t offset)
{
   fprintf(fp, "/* ============ TEXTURE BEGIN ===================== */\n");
   lima_unpack(fp, data + offset / sizeof(uint32_t), TEXTURE_DESCRIPTOR, temp);
   lima_print(fp, TEXTURE_DESCRIPTOR, temp, 8);
   fprintf(fp, "/* ============ TEXTURE END ======================= */\n");
}
