/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#ifndef __PAN_BLEND_CSO_H
#define __PAN_BLEND_CSO_H

#include "util/hash_table.h"
#include "nir.h"
#include "pipe/p_state.h"
#include "pan_blend.h"
#include "pan_pool.h"

struct panfrost_bo;
struct panfrost_batch;

struct pan_blend_info {
   unsigned constant_mask : 4;
   bool fixed_function    : 1;
   bool enabled           : 1;
   bool load_dest         : 1;
   bool opaque            : 1;
   bool alpha_zero_nop    : 1;
   bool alpha_one_store   : 1;
};

struct panfrost_blend_state {
   struct pipe_blend_state base;
   struct pan_blend_state pan;
   struct pan_blend_info info[PIPE_MAX_COLOR_BUFS];
   uint32_t equation[PIPE_MAX_COLOR_BUFS];

   /* info.load presented as a bitfield for draw call hot paths */
   unsigned load_dest_mask : PIPE_MAX_COLOR_BUFS;

   /* info.enabled presented as a bitfield for draw call hot paths */
   unsigned enabled_mask : PIPE_MAX_COLOR_BUFS;
};

struct pan_blend_shader_cache {
   unsigned gpu_id;
   uint32_t gpu_variant;
   struct pan_pool *bin_pool;
   struct hash_table *shaders;
   pthread_mutex_t lock;
};

struct pan_blend_shader {
   struct pan_blend_shader_key key;

   uint64_t address;
   unsigned work_reg_count;
};

uint64_t panfrost_get_blend(struct panfrost_batch *batch, unsigned rt);

void pan_blend_shader_cache_init(struct pan_blend_shader_cache *cache,
                                 unsigned gpu_id, uint32_t gpu_variant,
                                 struct pan_pool *bin_pool);

void pan_blend_shader_cache_cleanup(struct pan_blend_shader_cache *cache);

#ifdef PAN_ARCH

/* Take blend_shaders.lock before calling this function and release it when
 * you're done with the shader variant object.
 */
struct pan_blend_shader *GENX(pan_blend_get_shader_locked)(
   struct pan_blend_shader_cache *cache, const struct pan_blend_state *state,
   nir_alu_type src0_type, nir_alu_type src1_type, unsigned rt);

#endif

#endif
