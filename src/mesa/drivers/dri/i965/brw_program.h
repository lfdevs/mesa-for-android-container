/*
 * Copyright © 2011 Intel Corporation
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

#ifndef BRW_PROGRAM_H
#define BRW_PROGRAM_H

#include "brw_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

struct brw_context;

void brw_setup_tex_for_precompile(struct brw_context *brw,
                                  struct brw_sampler_prog_key_data *tex,
                                  struct gl_program *prog);

void brw_populate_sampler_prog_key_data(struct gl_context *ctx,
				        const struct gl_program *prog,
                                        unsigned sampler_count,
				        struct brw_sampler_prog_key_data *key);
bool brw_debug_recompile_sampler_key(struct brw_context *brw,
                                     const struct brw_sampler_prog_key_data *old_key,
                                     const struct brw_sampler_prog_key_data *key);
void brw_add_texrect_params(struct gl_program *prog);

void
brw_mark_surface_used(struct brw_stage_prog_data *prog_data,
                      unsigned surf_index);

void
brw_stage_prog_data_free(const void *prog_data);

void
brw_dump_ir(const char *stage, struct gl_shader_program *shader_prog,
            struct gl_shader *shader, struct gl_program *prog);

void brw_upload_tcs_prog(struct brw_context *brw,
                         uint64_t per_vertex_slots, uint32_t per_patch_slots);
void brw_upload_tes_prog(struct brw_context *brw,
                         uint64_t per_vertex_slots, uint32_t per_patch_slots);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
