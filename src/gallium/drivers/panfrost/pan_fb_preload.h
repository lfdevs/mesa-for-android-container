/*
 * Copyright (C) 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_FB_PRELOAD_H
#define __PAN_FB_PRELOAD_H

#include "util/format/u_format.h"
#include "pan_desc.h"
#include "pan_pool.h"
#include "pan_util.h"

struct pan_blend_shader_cache;
struct pan_fb_info;
struct pan_jc;
struct pan_pool;

struct pan_fb_preload_cache {
   uint64_t gpu_id;
   uint32_t gpu_variant;
   struct {
      struct pan_pool *pool;
      struct hash_table *preload;
      pthread_mutex_t lock;
   } shaders;
   struct {
      struct pan_pool *pool;
      struct hash_table *rsds;
      pthread_mutex_t lock;
   } rsds;
   struct pan_blend_shader_cache *blend_shader_cache;
};

#ifdef PAN_ARCH
void GENX(pan_fb_preload_cache_init)(
   struct pan_fb_preload_cache *cache, uint64_t gpu_id, uint32_t gpu_variant,
   struct pan_blend_shader_cache *blend_shader_cache, struct pan_pool *bin_pool,
   struct pan_pool *desc_pool);

void GENX(pan_fb_preload_cache_cleanup)(struct pan_fb_preload_cache *cache);

unsigned GENX(pan_preload_fb)(struct pan_fb_preload_cache *cache,
                              struct pan_pool *desc_pool,
                              struct pan_fb_info *fb, uint64_t tsd,
                              struct pan_ptr *jobs);
#endif

#endif
