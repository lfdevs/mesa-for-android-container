/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its susidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef FD6_GMEM_CACHE_H
#define FD6_GMEM_CACHE_H

#include <stdint.h>

#include "common/freedreno_dev_info.h"

/* Offset within GMEM of various "non-GMEM" things that GMEM is used to
 * cache.  These offsets differ for gmem vs sysmem rendering (in sysmem
 * mode, the entire GMEM can be used)
 */
struct fd6_gmem_config {
   /* Color/depth CCU cache: */
   uint32_t color_ccu_offset;
   uint32_t depth_ccu_offset;

   /* Vertex attrib cache (a750+): */
   uint32_t vpc_attr_buf_size;
   uint32_t vpc_attr_buf_offset;

   /* Vertex position cache (a8xx+): */
   uint32_t vpc_pos_buf_size;
   uint32_t vpc_pos_buf_offset;
   uint32_t vpc_bv_pos_buf_size;
   uint32_t vpc_bv_pos_buf_offset;

   /* see enum a6xx_ccu_cache_size */
   uint32_t depth_cache_fraction;
   uint32_t color_cache_fraction;
   uint32_t depth_cache_size;
   uint32_t color_cache_size;
};

static inline unsigned
__calc_gmem_cache_offsets(const struct fd_dev_info *info, unsigned offset,
                          struct fd6_gmem_config *config)
{
   unsigned num_ccu = info->num_ccu;

   config->vpc_bv_pos_buf_offset = offset - (num_ccu * config->vpc_bv_pos_buf_size);
   offset = config->vpc_bv_pos_buf_offset;

   config->vpc_attr_buf_offset = offset - (num_ccu * config->vpc_attr_buf_size);
   offset = config->vpc_attr_buf_offset;

   config->vpc_pos_buf_offset = offset - (num_ccu * config->vpc_pos_buf_size);
   offset = config->vpc_pos_buf_offset;

   config->color_ccu_offset = offset - (num_ccu * config->color_cache_size);
   offset = config->color_ccu_offset;

   config->depth_ccu_offset = offset - (num_ccu * config->depth_cache_size);
   offset = config->depth_ccu_offset;

   return offset;
}

static inline unsigned
fd6_calc_gmem_cache_offsets(const struct fd_dev_info *info, unsigned gmemsize_bytes,
                            struct fd6_gmem_config *gmem, struct fd6_gmem_config *sysmem)
{
   uint32_t depth_cache_size =
      info->num_ccu * info->props.sysmem_per_ccu_depth_cache_size;
   uint32_t color_cache_size =
      (info->num_ccu * info->props.sysmem_per_ccu_color_cache_size);
   uint32_t color_cache_size_gmem =
      color_cache_size /
      (1 << info->props.gmem_ccu_color_cache_fraction);
   uint32_t depth_cache_size_gmem =
      depth_cache_size /
      (1 << info->props.gmem_ccu_depth_cache_fraction);

   sysmem->depth_ccu_offset = 0;
   sysmem->color_ccu_offset = sysmem->depth_ccu_offset + depth_cache_size;
   sysmem->depth_cache_fraction =
      info->props.sysmem_ccu_depth_cache_fraction;

   /* TODO we could unify gen7/gen8 setup.. gen7 is a subset.. */
   if (info->chip == 8) {
      gmem->depth_cache_fraction = info->props.gmem_ccu_depth_cache_fraction;
      gmem->depth_cache_size     = info->props.gmem_per_ccu_depth_cache_size;
      gmem->color_cache_fraction = info->props.gmem_ccu_color_cache_fraction;
      gmem->color_cache_size     = info->props.gmem_per_ccu_color_cache_size;
      gmem->vpc_attr_buf_size    = info->props.gmem_vpc_attr_buf_size;
      gmem->vpc_pos_buf_size     = info->props.gmem_vpc_pos_buf_size;
      gmem->vpc_bv_pos_buf_size  = info->props.gmem_vpc_bv_pos_buf_size;

      sysmem->depth_cache_fraction = info->props.sysmem_ccu_depth_cache_fraction;
      sysmem->depth_cache_size     = info->props.sysmem_per_ccu_depth_cache_size;
      sysmem->color_cache_fraction = info->props.sysmem_ccu_color_cache_fraction;
      sysmem->color_cache_size     = info->props.sysmem_per_ccu_color_cache_size;
      sysmem->vpc_attr_buf_size    = info->props.sysmem_vpc_attr_buf_size;
      sysmem->vpc_pos_buf_size     = info->props.sysmem_vpc_pos_buf_size;
      sysmem->vpc_bv_pos_buf_size  = info->props.sysmem_vpc_bv_pos_buf_size;

      __calc_gmem_cache_offsets(info, gmemsize_bytes, sysmem);
      return __calc_gmem_cache_offsets(info, gmemsize_bytes, gmem);
   } else if (info->props.has_gmem_vpc_attr_buf) {
      sysmem->vpc_attr_buf_size = info->props.sysmem_vpc_attr_buf_size;
      sysmem->vpc_attr_buf_offset = sysmem->color_ccu_offset + color_cache_size;

      gmem->vpc_attr_buf_size = info->props.gmem_vpc_attr_buf_size;
      gmem->vpc_attr_buf_offset = gmemsize_bytes -
         (gmem->vpc_attr_buf_size * info->num_ccu);

      gmem->color_ccu_offset = gmem->vpc_attr_buf_offset - color_cache_size_gmem;
      /* Custom resolves can write depth through the CCU. */
      gmem->depth_ccu_offset = gmem->color_ccu_offset - depth_cache_size_gmem;
      gmem->depth_cache_fraction = info->props.gmem_ccu_depth_cache_fraction;

      return gmem->vpc_attr_buf_offset;
   } else {
      gmem->color_ccu_offset = gmemsize_bytes - color_cache_size_gmem;
      /* Custom resolves can write depth through the CCU. */
      gmem->depth_ccu_offset = gmem->color_ccu_offset - depth_cache_size_gmem;
      gmem->depth_cache_fraction = info->props.gmem_ccu_depth_cache_fraction;

      return gmemsize_bytes;
   }
}

#endif /* FD6_GMEM_CACHE_H */
