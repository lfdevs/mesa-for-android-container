/*
 * Copyright © 2025 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef FREEDRENO_LRZ_LAYOUT_H_
#define FREEDRENO_LRZ_LAYOUT_H_

#include <stdint.h>

#include "freedreno_layout.h"

BEGINC;

struct fdl_lrz_layout {
   uint32_t lrz_offset;
   uint32_t lrz_pitch;
   uint32_t lrz_height;
   uint32_t lrz_layer_size;
   uint32_t lrz_buffer_size;
   uint32_t lrz_fc_offset;
   uint32_t lrz_fc_size;
   uint32_t lrz_total_size;
};

void
fdl5_lrz_layout_init(struct fdl_lrz_layout *lrz_layout, uint32_t width,
                     uint32_t height, uint32_t nr_samples);
ENDC;

#ifdef __cplusplus
#include "common/freedreno_lrz.h"

static inline void
fdl6_lrz_get_super_sampled_size(uint32_t *width, uint32_t *height,
                                uint32_t nr_samples)
{
   switch (nr_samples) {
   case 8:
      *height *= 2;
      FALLTHROUGH;
   case 4:
      *width *= 2;
      FALLTHROUGH;
   case 2:
      *height *= 2;
      break;
   default:
      break;
   }
}

template <chip CHIP>
static uint32_t
fdl6_lrz_get_fc_size(uint32_t width, uint32_t height, uint32_t nr_samples,
                     uint32_t array_layers)
{
   fdl6_lrz_get_super_sampled_size(&width, &height, nr_samples);

   unsigned nblocksx = DIV_ROUND_UP(DIV_ROUND_UP(width, 8), 16);
   unsigned nblocksy = DIV_ROUND_UP(DIV_ROUND_UP(height, 8), 4);

   uint32_t lrz_fc_size =
      DIV_ROUND_UP(nblocksx * nblocksy, 8) * array_layers;

   /* Fast-clear buffer cannot be larger than 512 bytes on A6XX and 1024 bytes
    * on A7XX (HW limitation)
    */
   if (lrz_fc_size > fd_lrzfc_layout<CHIP>::FC_SIZE) {
      lrz_fc_size = 0;
   }

   return lrz_fc_size;
}

struct fdl_lrz_fdm_extra_size {
   uint32_t extra_width;
   uint32_t extra_height;
};

/* Get maximum size of the extra tile for VK_QCOM_fragment_density_map_offset,
 * that keeps LRZ fast-clear enabled, if possible.
 */
template <chip CHIP>
static struct fdl_lrz_fdm_extra_size
fdl6_lrz_get_max_fdm_extra_size(const struct fd_dev_info *dev_info,
                                uint32_t width, uint32_t height,
                                uint32_t nr_samples, uint32_t array_layers)
{
   constexpr uint32_t MIN_TILE_SIZE_FOR_FDM_OFFSET = 192;

   if (fdl6_lrz_get_fc_size<CHIP>(width, height, nr_samples, array_layers) == 0) {
      return {dev_info->tile_max_w, dev_info->tile_max_h};
   }

   uint32_t max_extra_size = MIN2(dev_info->tile_max_w, dev_info->tile_max_h);
   uint32_t step = MIN2(dev_info->gmem_align_w, dev_info->gmem_align_h);
   uint32_t min_extra_size = MAX2(step, MIN_TILE_SIZE_FOR_FDM_OFFSET);

   while (max_extra_size > min_extra_size) {
      if (fdl6_lrz_get_fc_size<CHIP>(width + max_extra_size,
                                     height + max_extra_size, nr_samples,
                                     array_layers) != 0) {

         return {util_round_down_npot(max_extra_size, dev_info->gmem_align_w),
                 util_round_down_npot(max_extra_size, dev_info->gmem_align_h)};
      }

      max_extra_size -= step;
   }

   return {dev_info->tile_max_w, dev_info->tile_max_h};
}

template <chip CHIP>
static void
fdl6_lrz_layout_init(struct fdl_lrz_layout *lrz_layout,
                     struct fdl_layout *layout, uint32_t extra_width,
                     uint32_t extra_height,
                     const struct fd_dev_info *dev_info, uint32_t lrz_offset,
                     uint32_t array_layers)
{
   unsigned width = layout->width0 + extra_width;
   unsigned height = layout->height0 + extra_height;
   fdl6_lrz_get_super_sampled_size(&width, &height, layout->nr_samples);

   unsigned lrz_pitch = align(DIV_ROUND_UP(width, 8), 32);
   unsigned lrz_height = align(DIV_ROUND_UP(height, 8), 32);

   lrz_layout->lrz_offset = lrz_offset;
   lrz_layout->lrz_height = lrz_height;
   lrz_layout->lrz_pitch = lrz_pitch;
   lrz_layout->lrz_layer_size = lrz_pitch * lrz_height * sizeof(uint16_t);
   lrz_layout->lrz_buffer_size = lrz_layout->lrz_layer_size * array_layers;

   /* Fast-clear buffer is 1bit/block */
   lrz_layout->lrz_fc_size = fdl6_lrz_get_fc_size<CHIP>(
      layout->width0 + extra_width, layout->height0 + extra_height,
      layout->nr_samples, array_layers);

   if (!dev_info->props.enable_lrz_fast_clear) {
      lrz_layout->lrz_fc_size = 0;
   }

   /* Allocate 2 LRZ buffers for double-buffering on a7xx. */
   uint32_t lrz_size = lrz_layout->lrz_buffer_size *
      (dev_info->chip >= 7 ? 2 : 1);

   if (dev_info->props.enable_lrz_fast_clear ||
       dev_info->props.has_lrz_dir_tracking) {
      lrz_layout->lrz_fc_offset =
         lrz_layout->lrz_offset + lrz_size;
      lrz_size += sizeof(fd_lrzfc_layout<CHIP>);
   }

   lrz_layout->lrz_total_size = lrz_size;

   uint32_t lrz_clear_height = lrz_layout->lrz_height * array_layers;
   if (((lrz_clear_height - 1) >> 14) > 0) {
      /* For simplicity bail out if LRZ cannot be cleared in one go. */
      lrz_layout->lrz_height = 0;
      lrz_layout->lrz_total_size = 0;
   }
}
#endif

#endif
