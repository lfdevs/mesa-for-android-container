/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PVR_STATIC_SHADERS_H
#define PVR_STATIC_SHADERS_H

#include <stdint.h>

/* TODO: Remove this once compiler is integrated. */
#define PVR_INVALID_INST (~0)

struct pvr_static_buffer {
   uint32_t dst_idx;
   uint32_t value;
};

struct pvr_shader_factory_info {
   uint32_t temps_required;
   uint32_t const_shared_regs;
   uint32_t coeff_regs;
   uint32_t input_regs;
   uint32_t explicit_const_start_offset;
   uint32_t code_size;
   const uint8_t *const shader_code;
   uint32_t const_calc_prog_inst_bytes;
   uint32_t sec_temp_regs;
   const uint8_t *const_calc_program;
   uint32_t coeff_update_prog_start;
   uint32_t coeff_update_temp_regs;
   const uint32_t *driver_const_location_map;
   uint32_t num_driver_consts;
   const struct pvr_static_buffer *static_const_buffer;
   uint32_t num_static_const;
};

static const uint8_t availability_query_write_shader[144] = { 0 };

static const uint32_t availability_query_write_location_map[1] = {
   0,
};

static const struct pvr_static_buffer
   availability_query_write_static_consts[3] = {
      { 0, 0 },
      { 0, 0 },
      { 0, 0 },
   };

static const struct pvr_shader_factory_info availability_query_write_info = {
   0,
   0,
   0,
   0,
   0,
   0,
   availability_query_write_shader,
   0,
   0,
   NULL,
   PVR_INVALID_INST,
   0,
   availability_query_write_location_map,
   0,
   availability_query_write_static_consts,
   0,
};

static const uint8_t copy_query_results_shader[384] = { 0 };

static const uint32_t copy_query_results_location_map[7] = {
   0, 0, 0, 0, 0, 0, 0,
};

static const struct pvr_static_buffer copy_query_results_static_consts[2] = {
   { 0, 0 },
   { 0, 0 },
};

static const struct pvr_shader_factory_info copy_query_results_info = {
   0,
   0,
   0,
   0,
   0,
   0,
   copy_query_results_shader,
   0,
   0,
   NULL,
   PVR_INVALID_INST,
   0,
   copy_query_results_location_map,
   0,
   copy_query_results_static_consts,
   0,
};

static const uint8_t reset_query_shader_code[136] = { 0 };

static const uint32_t reset_query_location_map[1] = {
   0,
};

static const struct pvr_static_buffer reset_query_static_consts[2] = {
   { 0, 0 },
   { 0, 0 },
};

static const struct pvr_shader_factory_info reset_query_info = {
   0,
   0,
   0,
   0,
   0,
   0,
   reset_query_shader_code,
   0,
   0,
   NULL,
   PVR_INVALID_INST,
   0,
   reset_query_location_map,
   0,
   reset_query_static_consts,
   0,
};

static const struct pvr_shader_factory_info
   *const copy_query_results_collection[1] = {
      &copy_query_results_info,
   };

static const struct pvr_shader_factory_info *const reset_query_collection[1] = {
   &reset_query_info,
};

#endif /* PVR_STATIC_SHADERS_H */
