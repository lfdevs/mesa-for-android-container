/*
 * Copyright (C) 2025 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __PAN_NIR_H__
#define __PAN_NIR_H__

#include "nir.h"
#include "pan_compiler.h"

struct util_format_description;

bool pan_nir_lower_zs_store(nir_shader *nir);
bool pan_nir_lower_store_component(nir_shader *shader);

bool pan_nir_lower_vertex_id(nir_shader *shader);

bool pan_nir_lower_image_ms(nir_shader *shader);

bool pan_nir_lower_frag_coord_zw(nir_shader *shader);
bool pan_nir_lower_noperspective_vs(nir_shader *shader);
bool pan_nir_lower_noperspective_fs(nir_shader *shader);

bool pan_nir_lower_helper_invocation(nir_shader *shader);
bool pan_nir_lower_sample_pos(nir_shader *shader);
bool pan_nir_lower_xfb(nir_shader *nir);

bool pan_nir_lower_image_index(nir_shader *shader,
                               unsigned vs_img_attrib_offset);

void pan_nir_lower_texture_early(nir_shader *nir, unsigned gpu_id);
void pan_nir_lower_texture_late(nir_shader *nir, unsigned gpu_id);

nir_alu_type
pan_unpacked_type_for_format(const struct util_format_description *desc);

bool pan_nir_lower_framebuffer(nir_shader *shader,
                               const enum pipe_format *rt_fmts,
                               uint8_t raw_fmt_mask,
                               unsigned blend_shader_nr_samples,
                               bool broken_ld_special);

uint32_t pan_nir_collect_noperspective_varyings_fs(nir_shader *s);

/* Specify the mediump lowering behavior for pan_nir_collect_varyings */
enum pan_mediump_vary {
   /* Always assign a 32-bit format to mediump varyings */
   PAN_MEDIUMP_VARY_32BIT,
   /* Assign a 16-bit format to varyings with smooth interpolation, and a
    * 32-bit format to varyings with flat interpolation */
   PAN_MEDIUMP_VARY_SMOOTH_16BIT,
};

void pan_nir_collect_varyings(nir_shader *s, struct pan_shader_info *info,
                              enum pan_mediump_vary mediump);

#endif /* __PAN_NIR_H__ */
