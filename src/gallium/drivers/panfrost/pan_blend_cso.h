/*
 * Copyright (C) 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
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
   unsigned constant_mask    : 4;
   bool fixed_function       : 1;
   bool fixed_function_float : 1;
   bool enabled              : 1;
   bool load_dest            : 1;
   bool opaque               : 1;
   bool alpha_zero_nop       : 1;
   bool alpha_one_store      : 1;
};

struct panfrost_blend_state {
   struct pipe_blend_state base;
   struct pan_blend_state pan;
   struct pan_blend_info info[PIPE_MAX_COLOR_BUFS];
   uint32_t equation[PIPE_MAX_COLOR_BUFS];
   uint32_t float_equation[PIPE_MAX_COLOR_BUFS];

   /* info.load presented as a bitfield for draw call hot paths */
   unsigned load_dest_mask : PIPE_MAX_COLOR_BUFS;

   /* info.enabled presented as a bitfield for draw call hot paths */
   unsigned enabled_mask : PIPE_MAX_COLOR_BUFS;
};

struct pan_blend_shader_cache {
   uint64_t gpu_id;
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
                                 uint64_t gpu_id, uint32_t gpu_variant,
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
