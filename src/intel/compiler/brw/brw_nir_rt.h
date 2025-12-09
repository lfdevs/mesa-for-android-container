/*
 * Copyright © 2020 Intel Corporation
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

#pragma once

#include "brw_nir.h"
#include "brw_rt.h"

#ifdef __cplusplus
extern "C" {
#endif

void brw_nir_lower_raygen(nir_shader *nir,
                          const struct intel_device_info *devinfo);
void brw_nir_lower_any_hit(nir_shader *nir,
                           const struct intel_device_info *devinfo);
void brw_nir_lower_closest_hit(nir_shader *nir,
                               const struct intel_device_info *devinfo);
void brw_nir_lower_miss(nir_shader *nir,
                        const struct intel_device_info *devinfo);
void brw_nir_lower_callable(nir_shader *nir,
                            const struct intel_device_info *devinfo);
void brw_nir_lower_combined_intersection_any_hit(nir_shader *intersection,
                                                 const nir_shader *any_hit,
                                                 const struct intel_device_info *devinfo);

/* We reserve the first 16B of the stack for callee data pointers */
#define BRW_BTD_STACK_RESUME_BSR_ADDR_OFFSET 0
#define BRW_BTD_STACK_CALL_DATA_PTR_OFFSET 8
#define BRW_BTD_STACK_CALLEE_DATA_SIZE 16

/* We require the stack to be 8B aligned at the start of a shader */
#define BRW_BTD_STACK_ALIGN 8

struct brw_nir_lower_shader_calls_state {
   const struct intel_device_info *devinfo;
   struct brw_bs_prog_key *key;
};

bool brw_nir_lower_ray_queries(nir_shader *shader,
                               const struct intel_device_info *devinfo);

bool brw_nir_lower_shader_returns(nir_shader *shader);

bool brw_nir_lower_shader_calls(nir_shader *shader,
                                struct brw_nir_lower_shader_calls_state *state);

bool brw_nir_lower_rt_intrinsics_pre_trace(nir_shader *nir);

bool brw_nir_lower_rt_intrinsics(nir_shader *shader,
                                 const struct brw_base_prog_key *key,
                                 const struct intel_device_info *devinfo);
bool brw_nir_lower_intersection_shader(nir_shader *intersection,
                                       const nir_shader *any_hit,
                                       const struct intel_device_info *devinfo);

nir_shader *
brw_nir_create_raygen_trampoline(const struct brw_compiler *compiler,
                                 void *mem_ctx);
nir_shader *
brw_nir_create_trivial_return_shader(const struct brw_compiler *compiler,
                                     void *mem_ctx);
nir_shader *
brw_nir_create_null_ahs_shader(const struct brw_compiler *compiler,
                               void *mem_ctx);

static inline nir_def *
brw_nir_build_vec3_mat_mult_col_major(nir_builder *b, nir_def *vec,
                                      nir_def *matrix[], bool translation)
{
   nir_def *result_components[3] = {
      nir_channel(b, matrix[3], 0),
      nir_channel(b, matrix[3], 1),
      nir_channel(b, matrix[3], 2),
   };
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         nir_def *v = nir_fmul(b, nir_channels(b, vec, 1 << j), nir_channels(b, matrix[j], 1 << i));
         result_components[i] = (translation || j) ? nir_fadd(b, result_components[i], v) : v;
      }
   }
   return nir_vec(b, result_components, 3);
}

#ifdef __cplusplus
}
#endif
