/*
 * Copyright © 2016 Red Hat
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
 */

#ifndef ST_NIR_H
#define ST_NIR_H

#include "st_context.h"
#include "compiler/shader_enums.h"
#include "util/perf/cpu_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nir_shader;
struct nir_variable;

typedef struct nir_lower_drawpixels_options {
   gl_state_index16 texcoord_state_tokens[STATE_LENGTH];
   gl_state_index16 scale_state_tokens[STATE_LENGTH];
   gl_state_index16 bias_state_tokens[STATE_LENGTH];
   unsigned drawpix_sampler;
   unsigned pixelmap_sampler;
   bool pixel_maps : 1;
   bool scale_and_bias : 1;
} nir_lower_drawpixels_options;

bool st_nir_lower_alpha_test(struct nir_shader *shader, enum compare_func func,
                             bool alpha_to_one,
                             const gl_state_index16 *alpha_ref_state_tokens,
                             struct gl_program_parameter_list *paramList,
                             bool packed_driver_uniform_storage);

bool st_nir_lower_drawpixels(struct nir_shader *shader,
                             const nir_lower_drawpixels_options *options,
                             struct gl_program_parameter_list *paramList,
                             bool packed_driver_uniform_storage);

bool st_nir_lower_point_size_mov(struct nir_shader *shader,
                                 const gl_state_index16 *pointsize_state_tokens,
                                 struct gl_program_parameter_list *paramList,
                                 bool packed_driver_uniform_storage);

bool st_nir_lower_builtin(struct nir_shader *shader);
bool st_nir_lower_tex_src_plane(struct nir_shader *shader, unsigned free_slots,
                                unsigned lower_2plane, unsigned lower_3plane);

bool st_nir_lower_wpos_ytransform(struct nir_shader *nir,
                                  struct gl_program *prog,
                                  struct pipe_screen *pscreen);

void st_finalize_nir(struct st_context *st, struct gl_program *prog,
                     struct gl_shader_program *shader_program,
                     struct nir_shader *nir, bool is_before_variants,
                     bool is_draw_shader);

void st_nir_lower_samplers(struct pipe_screen *screen, struct nir_shader *nir,
                           struct gl_shader_program *shader_program,
                           struct gl_program *prog);
void st_nir_lower_uniforms(struct st_context *st, struct nir_shader *nir);

void
st_nir_finish_builtin_nir(struct st_context *st, struct nir_shader *nir);
void *
st_nir_finish_builtin_shader(struct st_context *st,
                             struct nir_shader *nir);

void *
st_nir_make_passthrough_vs(struct st_context *st,
                           const char *shader_name,
                           unsigned num_vars,
                           const unsigned *input_locations,
                           const gl_varying_slot *output_locations,
                           unsigned sysval_mask);

void *
st_nir_make_clearcolor_shader(struct st_context *st);

struct nir_variable *
st_nir_state_variable_create(struct nir_shader *shader,
                             const struct glsl_type *type,
                             struct gl_program_parameter_list *param_list,
                             const gl_state_index16 state[STATE_LENGTH],
                             char *var_name,
                             bool packed_driver_uniform_storage);

bool st_nir_lower_fog(struct nir_shader *s, enum gl_fog_mode fog_mode,
                      struct gl_program_parameter_list *paramList,
                      bool packed_driver_uniform_storage);
bool st_nir_lower_position_invariant(struct nir_shader *s,
                                     struct gl_program_parameter_list *paramList,
                                     bool packed_driver_uniform_storage);

#ifdef __cplusplus
}
#endif

#endif /* ST_NIR_H */
