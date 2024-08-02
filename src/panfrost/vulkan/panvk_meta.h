/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_META_H
#define PANVK_META_H

#include "panvk_macros.h"
#include "panvk_mempool.h"

#include "pan_blend.h"
#include "pan_blitter.h"

#define PANVK_META_COPY_BUF2IMG_NUM_FORMATS  12
#define PANVK_META_COPY_IMG2BUF_NUM_FORMATS  12
#define PANVK_META_COPY_IMG2IMG_NUM_FORMATS  14
#define PANVK_META_COPY_NUM_TEX_TYPES        5
#define PANVK_META_COPY_BUF2BUF_NUM_BLKSIZES 5

static inline unsigned
panvk_meta_copy_tex_type(unsigned dim, bool isarray)
{
   assert(dim > 0 && dim <= 3);
   assert(dim < 3 || !isarray);
   return (((dim - 1) << 1) | (isarray ? 1 : 0));
}

struct panvk_meta {
   struct panvk_pool bin_pool;
   struct panvk_pool desc_pool;

   /* Access to the blitter pools are protected by the blitter
    * shader/rsd locks. They can't be merged with other binary/desc
    * pools unless we patch pan_blitter.c to external pool locks.
    */
   struct {
      struct panvk_pool bin_pool;
      struct panvk_pool desc_pool;
      struct pan_blitter_cache cache;
   } blitter;

   struct pan_blend_shader_cache blend_shader_cache;

   struct {
      struct {
         mali_ptr shader;
         struct pan_shader_info shader_info;
      } color[3]; /* 3 base types */
   } clear_attachment;

   struct {
      struct {
         mali_ptr rsd;
      } buf2img[PANVK_META_COPY_BUF2IMG_NUM_FORMATS];
      struct {
         mali_ptr rsd;
      } img2buf[PANVK_META_COPY_NUM_TEX_TYPES]
               [PANVK_META_COPY_IMG2BUF_NUM_FORMATS];
      struct {
         mali_ptr rsd;
      } img2img[2][PANVK_META_COPY_NUM_TEX_TYPES]
               [PANVK_META_COPY_IMG2IMG_NUM_FORMATS];
      struct {
         mali_ptr rsd;
      } buf2buf[PANVK_META_COPY_BUF2BUF_NUM_BLKSIZES];
      struct {
         mali_ptr rsd;
      } fillbuf;
   } copy;

   struct {
      mali_ptr rsd;
   } desc_copy;
};

#if PAN_ARCH

#if PAN_ARCH <= 7
struct panvk_descriptor_state;
struct panvk_shader;
struct panvk_shader_desc_state;

struct panfrost_ptr panvk_per_arch(meta_get_copy_desc_job)(
   struct panvk_device *dev, struct pan_pool *desc_pool,
   const struct panvk_shader *shader,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_state *shader_desc_state,
   uint32_t attrib_buf_idx_offset);
#endif

void panvk_per_arch(meta_init)(struct panvk_device *dev);

void panvk_per_arch(meta_cleanup)(struct panvk_device *dev);

mali_ptr panvk_per_arch(meta_emit_viewport)(struct pan_pool *pool,
                                            uint16_t minx, uint16_t miny,
                                            uint16_t maxx, uint16_t maxy);

void panvk_per_arch(meta_clear_init)(struct panvk_device *dev);

void panvk_per_arch(meta_blit_init)(struct panvk_device *dev);

void panvk_per_arch(meta_blit_cleanup)(struct panvk_device *dev);

void panvk_per_arch(meta_copy_init)(struct panvk_device *dev);

void panvk_per_arch(meta_desc_copy_init)(struct panvk_device *dev);
#endif

#endif
