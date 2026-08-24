/*
 * Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 *
 * Ethos-U85 Scheduler - Converted from Vela's ethos_u85.cpp
 */

#include "ethosu_sched_u85.h"
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include "util/macros.h"
#include "ethosu_ml.h"

#define ACC_DEPTH_GRANULE_U85 16
#define OFM_SPLIT_DEPTH       16
#define CB_SLOTS              6
#define BRICK_ELEMENTS        16

/* Helper structure for block configuration search */
struct find_config_common {
   struct ethosu_block ofm_block_max;
   struct ethosu_block ublock;
   struct ethosu_block granule;
   int acc_bits;
   int ifm_bits;
   int ifm_block_depth;
   bool is_pooling;
};

/* Forward declarations */
static struct ethosu_block calc_ifm_au_size_u85(int ifm_block_depth,
                                                int ifm_bits,
                                                struct ethosu_block ofm_ublock,
                                                int macs);
static bool try_block_config_u85(enum ethosu_operation_type op_type,
                                 struct ethosu_block ofm_block,
                                 struct ethosu_block ifm_block,
                                 struct ethosu_block ifm_shape,
                                 int ifm_bits, int acc_bits,
                                 int ifm_space, int acc_space,
                                 int ifm_au_depth, int num_blocks_in_ram,
                                 bool is_equal_depth_op);

/* Round away from zero */
static int
round_away(int value, int align)
{
   int rem = value % align;
   if (rem == 0) {
      return value;
   } else if (rem < 0) {
      return value - (align + rem);
   }
   return value + (align - rem);
}

/* Round toward zero */
static int
round_zero(int value, int align)
{
   assert(align > 0);
   return value - (value % align);
}

/* Calculate required input size for given output, stride, and kernel */
static int
required_input_size_u85(int value, int stride, int border, int upscale, int rounding)
{
   return (int)ceil((float)((value - 1) * stride + border + rounding) / (float)upscale);
}

/* Get architecture-specific IFM block size */
static struct ethosu_block
get_arch_ifm_block_size_u85(struct ethosu_block ofm_block,
                            struct ethosu_operation *operation,
                            struct ethosu_block au_block,
                            struct ethosu_block subkernel_limit,
                            int upscale, int rounding,
                            int ifm_block_depth)
{
   struct ethosu_block result;
   int dilated_h = operation->kernel.height * operation->kernel.dilation_y -
                   (operation->kernel.dilation_y - 1);
   int dilated_w = operation->kernel.width * operation->kernel.dilation_x -
                   (operation->kernel.dilation_x - 1);

   /* IFM block height */
   int h = required_input_size_u85(ofm_block.height, operation->kernel.stride_y,
                                   MIN2(dilated_h, subkernel_limit.height),
                                   upscale, rounding);
   h = round_away(h, au_block.height);

   /* IFM block width */
   int w = required_input_size_u85(ofm_block.width, operation->kernel.stride_x,
                                   MIN2(dilated_w, subkernel_limit.width),
                                   upscale, rounding);
   w = round_away(w, au_block.width);

   result.height = h;
   result.width = w;
   result.depth = round_away(ifm_block_depth ? ifm_block_depth : ofm_block.depth,
                             au_block.depth);

   return result;
}

/* Fit area by aspect ratio */
static void
fit_area_by_aspect(double aspect, int *width, int *height, int area,
                   struct ethosu_block granule)
{
   double w = sqrt(area / aspect);
   double h = w * aspect;

   if (h < 1.0) {
      w *= h;
      h = 1.0;
   } else if (w < 1.0) {
      h *= w;
      w = 1.0;
   }

   *width = round_zero(MAX2((int)w, granule.width), granule.width);
   *height = round_zero(MAX2((int)h, granule.height), granule.height);
}

/* Find best tile size */
static int
best_tile(int range, int granule, int tile)
{
   assert(range >= 0 && granule > 0 && tile > 0);
   assert((tile % granule) == 0);

   int best = tile;

   if ((range % tile) == 0)
      return best;

   int n_tiles = (range + tile - 1) / tile;
   best = (range + n_tiles - 1) / n_tiles;
   best = round_away(best, granule);

   return best;
}

/* Granular tile calculation */
static int
granular_tile(int range, int granule, int tile)
{
   assert(range >= 0 && granule > 0 && tile > 0);
   assert((tile % granule) == 0 && "tile must be multiple of granule");

   if (range % tile == 0)
      return tile;

   int tiles = range / tile; /* Integer division */

   return MAX2(round_away(range / (tiles + 1), granule), granule);
}

static int
reapportion(int value, int max_limit, int *avail, int required)
{
   assert(required > 0 && value > 0);
   int excess = MIN2(*avail / required, max_limit / value);
   if (excess >= 2) {
      *avail /= excess;
      value *= excess;
   }
   return value;
}

/* Area fitting algorithm for U85 */
static struct ethosu_block
area_fit_u85(struct find_config_common common,
             struct ethosu_block ofm_shape,
             struct ethosu_block ofm_block_limit,
             struct ethosu_block ifm_shape,
             struct ethosu_operation *operation,
             int acc_ram_size, int ifm_ram_size, int macs)
{
   const int acc_elements = (acc_ram_size * 8) / common.acc_bits;
   const int ib_elements = ifm_ram_size;
   const int ub_acc_elements = common.ublock.width * common.ublock.height * ACC_DEPTH_GRANULE_U85;

   assert(ub_acc_elements > 0);

   double aspect = (double)ofm_shape.height / ofm_shape.width;
   /* Prioritise depth only for 1x1 kernels (after dilation) */
   int dilated_w = operation->kernel.dilation_x * operation->kernel.width;
   int dilated_h = operation->kernel.dilation_y * operation->kernel.height;
   bool prioritise_depth = (dilated_w == 1 && dilated_h == 1);

   struct ethosu_block fit_shape = {0};
   double best_metric = DBL_MAX;
   int max_depth = MIN2(MAX2(macs, acc_elements / ub_acc_elements),
                        ofm_block_limit.depth);

   double ofm_area = prioritise_depth ? (ofm_shape.width * ofm_shape.depth) : (ofm_shape.width * ofm_shape.height);

   struct ethosu_block subkernel_max = SUB_KERNEL_MAX;

   for (int depth = ACC_DEPTH_GRANULE_U85; depth <= max_depth;
        depth += ACC_DEPTH_GRANULE_U85) {

      int width = 0, height = 0;
      int fit_acc = acc_elements;
      struct ethosu_block ifm_alloc_unit =
         calc_ifm_au_size_u85(depth, common.ifm_bits, common.ublock, macs);
      int ifm_depth_granule = ifm_alloc_unit.depth;

      /* Round IFM shape to allocation unit */
      struct ethosu_block ifm_rounded;
      ifm_rounded.width = round_away(ifm_shape.width, ifm_alloc_unit.width);
      ifm_rounded.height = round_away(ifm_shape.height, ifm_alloc_unit.height);
      ifm_rounded.depth = round_away(ifm_shape.depth, ifm_alloc_unit.depth);

      int ifm_volume = ifm_rounded.width * ifm_rounded.height * ifm_rounded.depth;
      bool fitted = false;
      int prev_acc_req = -1;
      int retry = 25;

      while (true) {
         fit_area_by_aspect(aspect, &width, &height, fit_acc / depth,
                            common.granule);

         width = MIN2(width, ofm_block_limit.width);
         height = MIN2(height, ofm_block_limit.height);

         /* Regular width tiling is preferred */
         int tmp = width * height;
         if (width < ofm_shape.width) {
            width = best_tile(ofm_shape.width, common.granule.width, width);
         }
         height = MAX2(round_zero(tmp / width, common.granule.height),
                       common.granule.height);

         /* Accumulator fit */
         int acc_required = width * height * depth;
         double ab_ratio = (double)acc_elements / acc_required;

         /* IFM fit */
         struct ethosu_block ofm_block = {width, height, depth};
         struct ethosu_block ifm_req = get_arch_ifm_block_size_u85(
            ofm_block, operation, ifm_alloc_unit, subkernel_max, 1, 0, 0);
         int ib_required = ifm_req.width * ifm_req.height * ifm_req.depth;
         assert(ib_required > 0);
         ib_required = MIN2(ib_required, ifm_volume);
         double ib_ratio = (double)ib_elements / ib_required;

         if (ab_ratio >= 1.0 && ib_ratio >= 1.0) {
            int ifm_used = (depth % ifm_depth_granule) ? (depth % ifm_depth_granule) : ifm_depth_granule;
            double waste = 1.0 + ((double)(ifm_depth_granule - ifm_used) /
                                  ifm_depth_granule) /
                                    10.0;
            double fit = 1.0 + fabs(1.0 - ((double)ofm_block_limit.depth / depth)) / 10.0;
            int other_axis = prioritise_depth ? depth : height;
            double coverage = 1.0 + fabs(1.0 - (ofm_area / (width * other_axis)));
            double metric = coverage * fit * waste;

            if (metric < best_metric) {
               fit_shape.width = width;
               fit_shape.height = height;
               fit_shape.depth = depth;
               best_metric = metric;

               /* Early exit if it covers the entire OFM */
               if (depth >= ofm_shape.depth &&
                   width >= ofm_shape.width && height >= ofm_shape.height) {
                  fit_shape.width = round_away(ofm_shape.width, common.granule.width);
                  fit_shape.height = round_away(ofm_shape.height, common.granule.height);
                  fit_shape.depth = round_away(ofm_shape.depth, common.granule.depth);
                  return fit_shape;
               }
            }
            fitted = true;
            break;
         }

         /* Force scaling if no progress */
         if (acc_required == prev_acc_req) {
            if (--retry <= 0) {
               ib_ratio = 0.9;
            }
         }
         prev_acc_req = acc_required;

         /* Fast reduce if IB requirement not met */
         if ((ib_ratio < 1.0) && (ab_ratio > 1.0)) {
            fit_acc = MIN2(fit_acc, acc_required);
         }

         /* Scale down and retry */
         double ratio = MIN2(ib_ratio, ab_ratio);
         int new_acc = (int)(fit_acc * ratio);
         new_acc = round_away(new_acc, ub_acc_elements);

         /* No scaling progress */
         if (new_acc == fit_acc) {
            new_acc = fit_acc - ub_acc_elements;
         }

         /* No fit possible */
         if (new_acc < ub_acc_elements) {
            break;
         }
         fit_acc = new_acc;
      }

      /* Nothing fitted at this depth */
      if (!fitted) {
         mesa_loge("area_fit_u85: no solution found");
         break;
      }
   }

   return fit_shape;
}

/* Find elementwise block configuration */
static struct ethosu_block
find_elementwise_config_u85(struct ethosu_operation *operation,
                            struct find_config_common common,
                            int cb_ram_size, int ob_ram_size)
{
   struct ethosu_block ofm_shape = operation->ofm.shape;

   /* Pad and round OFM shape */
   ofm_shape.width = round_away(ofm_shape.width, common.granule.width);
   ofm_shape.height = round_away(ofm_shape.height, common.granule.height);
   ofm_shape.depth = round_away(ofm_shape.depth, common.granule.depth);

   struct ethosu_block ofm_block_limit;
   ofm_block_limit.width = MIN2(ofm_shape.width, common.ofm_block_max.width);
   ofm_block_limit.height = MIN2(ofm_shape.height, common.ofm_block_max.height);
   ofm_block_limit.depth = MIN2(ofm_shape.depth, common.ofm_block_max.depth);

   const bool is_scalar = operation->ifm.has_scalar || operation->ifm2.has_scalar;
   const int cb_bricks =
      (cb_ram_size / CB_SLOTS) / (BRICK_ELEMENTS * (common.ifm_bits / 8));
   const int ob_elements = (ob_ram_size * 8) / common.ifm_bits;
   const int ob_width_units = ob_elements / common.granule.depth;
   const int cb_width_units = cb_bricks / common.ublock.height;

   int h_limit = granular_tile(ofm_shape.height, common.granule.height,
                               ofm_block_limit.height);
   int w_required = granular_tile(ofm_shape.width, common.granule.width,
                                  MIN2(ob_width_units, ofm_block_limit.width));
   /* Start with full OB capacity; reapportion() may reduce this */
   int w_limit = ob_width_units;
   int c_limit = reapportion(common.granule.depth, ofm_block_limit.depth,
                             &w_limit, MAX2(w_required, common.granule.width));

   /* Binary elementwise, potentially broadcast */
   if (!is_scalar) {
      /* For now, assume no broadcasting (broadcastMask = 0) */
      unsigned broadcast_mask = 0; /* TODO: Calculate based on IFM shapes */

      /* Broadcast in depth first */
      if (broadcast_mask & 1) {
         c_limit = granular_tile(ofm_shape.depth, common.granule.depth, ofm_block_limit.depth);
         w_limit = cb_width_units;

         /* This is only the depth axis */
         if ((broadcast_mask & 1) == broadcast_mask)
            h_limit = reapportion(common.ublock.height, ofm_block_limit.height, &w_limit,
                                  MAX2(w_required, common.granule.width));
      }
      /* Broadcast in height */
      else if (broadcast_mask & 4) {
         w_limit = cb_width_units;
         c_limit = reapportion(BRICK_ELEMENTS, ofm_block_limit.depth, &w_limit,
                               MAX2(w_required, common.granule.width));
      }
      /* Broadcast in width */
      else if (broadcast_mask & 2) {
         /* No change */
      }
   }

   struct ethosu_block ofm_block;
   ofm_block.height = h_limit;
   ofm_block.width = w_limit;
   ofm_block.depth = c_limit;

   /* Round to granule */
   ofm_block.width = round_away(ofm_block.width, common.granule.width);
   ofm_block.height = round_away(ofm_block.height, common.granule.height);
   ofm_block.depth = round_away(ofm_block.depth, common.granule.depth);

   return ofm_block;
}

/* Find depthwise block configuration */
static struct ethosu_block
find_depthwise_config_u85(struct ethosu_operation *operation,
                          struct find_config_common common,
                          struct ethosu_block *ifm_block_out,
                          int acc_ram_size, int ifm_ram_size, int macs)
{
   struct ethosu_block ofm_shape = operation->ofm.shape;
   struct ethosu_block ifm_shape = operation->ifm.shape;

   /* Pad and round OFM shape */
   ofm_shape.width = round_away(ofm_shape.width, common.granule.width);
   ofm_shape.height = round_away(ofm_shape.height, common.granule.height);
   ofm_shape.depth = round_away(ofm_shape.depth, common.granule.depth);

   struct ethosu_block ofm_block_limit;
   ofm_block_limit.width = MIN2(ofm_shape.width, common.ofm_block_max.width);
   ofm_block_limit.height = MIN2(ofm_shape.height, common.ofm_block_max.height);
   ofm_block_limit.depth = MIN2(ofm_shape.depth, common.ofm_block_max.depth);

   const int acc_elements = (acc_ram_size * 8) / common.acc_bits;
   const int ib_elements = (ifm_ram_size * 8) / common.ifm_bits;

   struct ethosu_block fit_shape = area_fit_u85(common, ofm_shape, ofm_block_limit,
                                                ifm_shape, operation,
                                                acc_ram_size, ifm_ram_size, macs);

   int depth = fit_shape.depth;
   int width = fit_shape.width;
   int height = fit_shape.height;

   struct ethosu_block ifm_alloc_unit;
   struct ethosu_block ifm_req;
   struct ethosu_block subkernel_max = SUB_KERNEL_MAX;
   unsigned force_reduce = 0;

   /* Read-buffering optimization */
   while (true) {
      int ifm_depth_for_au = common.ifm_block_depth ? common.ifm_block_depth : depth;
      ifm_alloc_unit = calc_ifm_au_size_u85(ifm_depth_for_au,
                                            common.ifm_bits,
                                            common.ublock, macs);
      int depth_granule = ifm_alloc_unit.depth;

      struct ethosu_block ofm_block = {width, height, depth};
      ifm_req = get_arch_ifm_block_size_u85(ofm_block, operation, ifm_alloc_unit,
                                            subkernel_max, 1, 0, common.ifm_block_depth);

      int ib_required = ifm_req.width * ifm_req.height * ifm_req.depth;
      assert(ib_required > 0);

      /* Check if we can fit read-buffering for one extra row */
      if (width < ofm_shape.width || height < ofm_shape.height ||
          depth < ofm_shape.depth) {
         struct ethosu_block row_block = {width, common.granule.height, depth};
         struct ethosu_block ifm_row = get_arch_ifm_block_size_u85(
            row_block, operation, ifm_alloc_unit, subkernel_max, 1, 0,
            common.ifm_block_depth);

         int ifm_row_elements = ifm_row.width * ifm_row.height * ifm_row.depth;

         if ((ib_elements - ib_required) < ifm_row_elements) {
            /* Need to reduce block size */

            if ((height != ofm_shape.height || (force_reduce & 2)) &&
                height > common.granule.height) {
               height -= common.granule.height;
            } else if ((depth > width || (force_reduce & 1)) &&
                       depth > depth_granule) {
               depth -= common.granule.depth;
            } else if ((width != ofm_shape.width || (force_reduce & 4)) &&
                       width > common.granule.width) {
               width -= common.granule.width;
            } else if (force_reduce != 7) {
               force_reduce = (force_reduce << 1) | 1;
            } else if (depth > common.granule.depth) {
               depth = MAX2(depth / 2, common.granule.depth);
            } else {
               mesa_loge("Cannot reduce block size further");
               UNREACHABLE("Cannot reduce block size further");
               break;
            }
            continue;
         }
      }
      break;
   }

   struct ethosu_block ofm_block = {width, height, depth};
   *ifm_block_out = ifm_req;

   assert(MIN2(ifm_req.width * ifm_req.height * ifm_req.depth,
               ifm_shape.width * ifm_shape.height * ifm_shape.depth) <= ib_elements);
   assert(ofm_block.width * ofm_block.height * ofm_block.depth <= acc_elements);

   return ofm_block;
}

/* Calculate IFM allocation unit size for U85 */
static struct ethosu_block
calc_ifm_au_size_u85(int ifm_block_depth,
                     int ifm_bits,
                     struct ethosu_block ofm_ublock, int macs)
{
   static const struct ethosu_block ifm_au_table[3][3] = {
      /* Ublock 2x1x16 */
      {{2, 2, 1}, {2, 1, 2}, {1, 1, 4}},
      /* Ublock 4x1x8 */
      {{4, 1, 1}, {2, 1, 2}, {1, 1, 4}},
      /* Ublock 2x2x8 */
      {{2, 2, 1}, {2, 1, 2}, {1, 1, 4}},
   };
   int ifm_depth_bits = ifm_block_depth * ifm_bits;

   /* Determine IFMU index based on depth*bits */
   int ifmu = 0;
   if (ifm_depth_bits > 256) {
      ifmu = 2; /* ifmu3 */
   } else if (ifm_depth_bits > 128) {
      ifmu = 1; /* ifmu2 */
   }

   /* Determine blockIdx from ofm_ublock */
   int block_idx = 0;
   if (ofm_ublock.width == 4 && ofm_ublock.height == 1 && ofm_ublock.depth == 8) {
      block_idx = 1;
   } else if (ofm_ublock.width == 2 && ofm_ublock.height == 2 && ofm_ublock.depth == 8) {
      block_idx = 2;
   }
   /* else block_idx = 0 for 2x1x16 */

   struct ethosu_block result = ifm_au_table[block_idx][ifmu];

   /* Scale depth by 128/ifm_bits */
   result.depth = result.depth * 128 / ifm_bits;

   return result;
}

/* Try block configuration to see if it fits in RAM */
static bool
try_block_config_u85(enum ethosu_operation_type op_type,
                     struct ethosu_block ofm_block,
                     struct ethosu_block ifm_block,
                     struct ethosu_block ifm_shape,
                     int ifm_bits, int acc_bits,
                     int ifm_space, int acc_space,
                     int ifm_au_depth, int num_blocks_in_ram,
                     bool is_equal_depth_op)
{
   assert(acc_bits > 0);
   assert(ifm_bits >= 8 && ifm_bits % 8 == 0);

   /* Elementwise and Resize don't use IB/AB */
   if (op_type == ETHOSU_OPERATION_TYPE_ELTWISE) {
      return true;
   }

   /* IFM space calculation */
   int ifm_align_depth = ifm_au_depth;
   int ifm_block_depth = is_equal_depth_op ? ofm_block.depth : MIN2(ifm_block.depth, ifm_shape.depth);
   ifm_block_depth = round_away(ifm_block_depth, ifm_align_depth);
   int ifm_bytes = ifm_block.width * ifm_block.height * ifm_block_depth *
                   (ifm_bits / 8) * num_blocks_in_ram;

   /* Accumulator space calculation */
   int ofm_block_depth = round_away(ofm_block.depth, ACC_DEPTH_GRANULE_U85);
   int acc_bytes = ((ofm_block.width * ofm_block.height * ofm_block_depth *
                     acc_bits) /
                    8) *
                   num_blocks_in_ram;

   if (ifm_bytes > ifm_space || acc_bytes > acc_space) {
      return false;
   }

   return true;
}

/* Regor's Shape(h,w,d) stores as [d,w,h], so Shape(1,2,16) = h=1,w=2,d=16
 * Mesa's {w,h,d} format, so we need to swap h and w from Regor's parameters
 * Regor Shape(1,2,16) → Mesa {2,1,16}
 * Regor Shape(1,4,8)  → Mesa {4,1,8}
 * Regor Shape(2,2,8)  → Mesa {2,2,8}
 */
static struct ethosu_block arch_ublocks[] = {
   {2, 1, 16}, /* Shape(1,2,16) */
   {4, 1, 8},  /* Shape(1,4,8) */
   {2, 2, 8}}; /* Shape(2,2,8) */

static struct ethosu_block
block_round_away(struct ethosu_block value, struct ethosu_block granule)
{
   struct ethosu_block result;

   result.width = round_away(value.width, granule.width);
   result.height = round_away(value.height, granule.height);
   result.depth = round_away(value.depth, granule.depth);

   return result;
}

static int
block_elements(struct ethosu_block value)
{
   return value.width * value.height * value.depth;
}

static struct ethosu_block
find_ublock(struct ethosu_operation *operation, bool is_part_kernel)
{
   int best_waste = INT_MAX;
   struct ethosu_block best_ublock = {0};
   bool is_pointwise = (operation->type == ETHOSU_OPERATION_TYPE_CONVOLUTION &&
                        operation->kernel.width == 1 && operation->kernel.height == 1);
   bool is_depthwise = operation->conv.depthwise;
   bool is_pooling = (operation->type == ETHOSU_OPERATION_TYPE_POOLING);
   bool is_memory_copy_pooling = is_pooling &&
                                 operation->kernel.width == 1 &&
                                 operation->kernel.height == 1;
   bool is_reduce_sum =
      is_pooling && operation->pooling.type == ETHOSU_POOLING_TYPE_REDUCE_SUM;
   unsigned ifm_bits = 8 << operation->ifm.precision;

   /* For memory-copy pooling (1x1 pooling), always use 2x1x16 ublock
    * to match Vela's behavior and avoid waste-based selection issues */
   if (is_memory_copy_pooling) {
      struct ethosu_block result = {2, 1, 16};
      return result;
   }

   for (int i = 0; i < ARRAY_SIZE(arch_ublocks); i++) {
      const struct ethosu_block ublk = arch_ublocks[i];

      /* Special case for 1x1 convolutions on U85-256:
       * The 2x1x16 microblock is ONLY valid for depth-first mode (not part-kernel).
       * For part-kernel mode, 2x1x16 must be rejected.
       * This matches Vela's logic in ethos_u85.cpp ValidateOfmUBlock().
       */
      if (is_pointwise && !is_part_kernel && ublk.width == 2 && ublk.height == 1 && ublk.depth == 16) {
         /* 2x1x16 is allowed for depth-first 1x1 - don't skip */
      } else if (is_pointwise && is_part_kernel && ublk.width == 2 && ublk.height == 1 && ublk.depth == 16) {
         continue; /* Skip 2x1x16 for part-kernel 1x1 */
      }

      if (ifm_bits == 8) {
         /* Skip {2,1,16} for 8-bit non-depthwise convolutions. */
         if (ublk.width == 2 && ublk.height == 1 && ublk.depth == 16 &&
             operation->type == ETHOSU_OPERATION_TYPE_CONVOLUTION &&
             !is_depthwise)
            continue;

         /* For 8-bit depthwise/pooling, only {2,1,16} is valid. */
         if (!(ublk.width == 2 && ublk.height == 1 && ublk.depth == 16) &&
             (is_depthwise || is_pooling))
            continue;
      } else if (is_reduce_sum) {
         /* For 16/32-bit ReduceSum on U85-256, Regor permits
          * {4,1,8} and {2,2,8}, but not {2,1,16}. */
         if (ublk.width == 2 && ublk.height == 1 && ublk.depth == 16)
            continue;
      }

      /* Minimum waste is better than aspect correct */
      struct ethosu_block tmp = block_round_away(operation->ofm.shape, ublk);
      int waste = block_elements(tmp) - block_elements(operation->ofm.shape);
      DBG("UBLOCK: ublock %ux%ux%u: tmp=%ux%ux%u waste=%d\n",
          ublk.width, ublk.height, ublk.depth,
          tmp.width, tmp.height, tmp.depth, waste);
      bool is_better = (waste < best_waste);
      /* For 1x1 convolutions with equal waste, prefer smaller depth */
      if (!is_better && waste == best_waste && is_pointwise && ublk.depth < best_ublock.depth)
         is_better = true;
      if (is_better) {
         best_ublock = ublk;
         best_waste = waste;
      }
   }

   DBG("UBLOCK: Chosen ublock: width=%u height=%u depth=%u\n", best_ublock.width, best_ublock.height, best_ublock.depth);
   return best_ublock;
}

/* Main entry point for U85 scheduling */
struct ethosu_block_config
find_block_config_u85(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   struct ethosu_block_config config = {0};

   /* Architecture constants for U85-256.
    * Note: on U85, the coefficient buffer (CB) is smaller than the output
    * buffer (OB), which is the opposite of U55/U65. */
   const int macs = 256;                 /* TODO: Get from screen */
   const int ifm_ram_size_bytes = 16384; /* TODO: Get from screen */
   const int acc_ram_size_bytes = 16384; /* TODO: Get from screen */
   const int cb_ram_size_bytes = 1536;   /* TODO: Get from screen */
   const int ob_ram_size_bytes = 2048;   /* TODO: Get from screen */

   /* Operation typing */
   bool is_pooling = (operation->type == ETHOSU_OPERATION_TYPE_POOLING);
   bool is_depthwise = operation->conv.depthwise;
   bool is_elementwise = (operation->type == ETHOSU_OPERATION_TYPE_ELTWISE);
   bool is_convolution = (operation->type == ETHOSU_OPERATION_TYPE_CONVOLUTION);
   bool is_equal_depth_op = is_elementwise || is_pooling || is_depthwise;
   bool is_sparse = operation->conv.weight_sparse;

   /* Determine if we should use kernel-first (part-kernel) scheduling for convolutions */
   bool is_part_kernel = false;
   if (is_convolution) {
      struct ethosu_block ifm_shape = operation->ifm.shape;
      int k = operation->kernel.width * operation->kernel.height;
      int s = is_sparse ? 10 : 5;
      int r = is_sparse ? 4 : 2;
      int k_rnd = (k / s) * s + MAX2(k % s, r);
      double kernel_first_util = ((double)ifm_shape.depth / round_away(ifm_shape.depth, 16)) *
                                 ((double)k / k_rnd);
      double depth_first_util =
         (double)ifm_shape.depth / round_away(ifm_shape.depth,
                                               is_sparse ? 128 : 64);
      is_part_kernel = (kernel_first_util >= depth_first_util);

      DBG("UBLOCK: kernel-first heuristic: k=%d k_rnd=%d ifm_depth=%d\n", k, k_rnd, ifm_shape.depth);
      DBG("UBLOCK:   kernel_first_util=%.3f depth_first_util=%.3f is_part_kernel=%d\n",
          kernel_first_util, depth_first_util, is_part_kernel);
   }

   /* Setup common search parameters */
   struct find_config_common common;
   common.acc_bits = 32; /* TODO: Determine from operation */
   common.ifm_bits = 8 << operation->ifm.precision;

   /* Find microblock */
   struct ethosu_block ofm_ublock = find_ublock(operation, is_part_kernel);

   common.ofm_block_max.width = 128;  /* TODO: Get from screen */
   common.ofm_block_max.height = 128; /* TODO: Get from screen */
   common.ofm_block_max.depth = 1024; /* TODO: Get from screen */
   common.ublock = ofm_ublock;
   common.granule.width = ofm_ublock.width;
   common.granule.height = ofm_ublock.height;
   common.granule.depth = ACC_DEPTH_GRANULE_U85;
   common.ifm_block_depth = 0;
   common.is_pooling = is_pooling;

   /* Handle elementwise operations */
   if (is_elementwise) {
      config.ofm_block = find_elementwise_config_u85(operation, common,
                                                     cb_ram_size_bytes,
                                                     ob_ram_size_bytes);
      config.ifm_block = config.ofm_block;
      config.ofm_ublock = ofm_ublock;
      return config;
   }

   /* Handle depthwise/pooling operations
    * Exception: 1x1 pooling (memory copy) uses full block search instead */
   bool is_memory_copy_pooling = is_pooling &&
                                 operation->kernel.width == 1 &&
                                 operation->kernel.height == 1;

   if ((is_depthwise || is_pooling) && !is_memory_copy_pooling) {
      config.ofm_block = find_depthwise_config_u85(operation, common,
                                                   &config.ifm_block,
                                                   acc_ram_size_bytes,
                                                   ifm_ram_size_bytes, macs);
      config.ofm_ublock = ofm_ublock;
      config.is_partkernel = false;
      return config;
   }

   /* Handle regular convolutions and memory-copy pooling - Full block search */

   struct ethosu_block ofm_shape = operation->ofm.shape;
   struct ethosu_block ifm_shape = operation->ifm.shape;

   int ifm_block_depth = 64;
   if (is_part_kernel) {
      ifm_block_depth = 16;
   } else if (common.ifm_bits == 32 ||
              ((macs == 128 || macs == 256) &&
               ofm_ublock.depth == 16 && !is_sparse)) {
      ifm_block_depth = 32;
   } else if (is_sparse && common.ifm_bits == 8) {
      ifm_block_depth = 128;
   }

   common.ifm_block_depth = ifm_block_depth;

   /* Search space setup */
   struct ethosu_block search_space_step;
   search_space_step.width = MAX2(ofm_ublock.width, common.granule.width);
   search_space_step.height = MAX2(ofm_ublock.height, common.granule.height);
   search_space_step.depth = MAX2(ofm_ublock.depth, common.granule.depth);

   struct ethosu_block search_space_end;
   search_space_end.width = MIN2(ofm_shape.width, common.ofm_block_max.width);
   search_space_end.height = MIN2(ofm_shape.height, common.ofm_block_max.height);
   search_space_end.depth = MIN2(ofm_shape.depth, common.ofm_block_max.depth);

   search_space_end.width = round_away(MAX2(search_space_end.width,
                                            search_space_step.width),
                                       ofm_ublock.width);
   search_space_end.height = round_away(MAX2(search_space_end.height,
                                             search_space_step.height),
                                        ofm_ublock.height);
   search_space_end.depth = round_away(MAX2(search_space_end.depth,
                                            search_space_step.depth),
                                       ofm_ublock.depth);

   float best_cost = FLT_MAX;
   float best_coverage = FLT_MAX;
   int ofm_elements = ofm_shape.width * ofm_shape.height * ofm_shape.depth;

   int depth = MAX2(ofm_ublock.depth, MIN2(search_space_end.depth, OFM_SPLIT_DEPTH));
   int restart_depth = depth;
   if (depth < ofm_shape.depth) {
      depth = round_away(depth, OFM_SPLIT_DEPTH);
   }

   struct ethosu_block ifm_alloc_unit =
      calc_ifm_au_size_u85(ifm_block_depth, common.ifm_bits, ofm_ublock, macs);
   int num_blocks_in_ram = 2;

   /* Block search loop */
   while (depth <= search_space_end.depth) {
      if (is_equal_depth_op) {
         ifm_block_depth = depth;
         struct ethosu_block new_au =
            calc_ifm_au_size_u85(depth, common.ifm_bits, ofm_ublock, macs);
         if (new_au.depth != ifm_alloc_unit.depth ||
             new_au.width != ifm_alloc_unit.width ||
             new_au.height != ifm_alloc_unit.height) {
            ifm_alloc_unit = new_au;
         }
      }

      for (int height = search_space_step.height;
           height <= search_space_end.height;
           height += search_space_step.height) {
         for (int width = search_space_step.width;
              width <= search_space_end.width;
              width += search_space_step.width) {

            struct ethosu_block ofm_block = {width, height, depth};
            struct ethosu_block ifm_block = get_arch_ifm_block_size_u85(
               ofm_block, operation, ifm_alloc_unit, SUB_KERNEL_MAX,
               1, 0, ifm_block_depth);

            /* Test if blocks fit in RAM */
            if (try_block_config_u85(operation->type, ofm_block, ifm_block,
                                     ifm_shape, common.ifm_bits,
                                     common.acc_bits,
                                     ifm_ram_size_bytes, acc_ram_size_bytes,
                                     ifm_alloc_unit.depth, num_blocks_in_ram,
                                     is_equal_depth_op)) {

               /* Calculate cost metric */
               int dilated_h = operation->kernel.height * operation->kernel.dilation_y -
                               (operation->kernel.dilation_y - 1);
               int dilated_w = operation->kernel.width * operation->kernel.dilation_x -
                               (operation->kernel.dilation_x - 1);

               float blocks_w = (float)ofm_shape.width / width;
               float blocks_h = (float)ofm_shape.height / height;
               float blocks_d = (float)ofm_shape.depth / depth;

               int full_blocks_w = (ofm_shape.width + width - 1) / width;
               int full_blocks_h = (ofm_shape.height + height - 1) / height;
               int full_blocks_d = (ofm_shape.depth + depth - 1) / depth;

               float weight_area = is_convolution ? operation->kernel.width * operation->kernel.height : 0;

               int ifm_repeats = ((dilated_w + SUB_KERNEL_MAX.width - 1) /
                                  SUB_KERNEL_MAX.width) *
                                 ((dilated_h + SUB_KERNEL_MAX.height - 1) /
                                  SUB_KERNEL_MAX.height);

               /* Weight fetch */
               float weight_fetch = weight_area * ifm_shape.depth *
                                    full_blocks_w * full_blocks_h;
               if (!is_depthwise) {
                  weight_fetch *= blocks_d * depth;
               }

               /* IFM fetch */
               /* Note: blocks_h and blocks_w operands appear in this specific order to match
                *       Vela's calculation to match its rounding behavior */
               float ifm_fetch = (float)(ifm_block.width * ifm_block.height) *
                                 ifm_shape.depth * ifm_repeats * blocks_h * blocks_w;
               if (!is_equal_depth_op) {
                  ifm_fetch *= full_blocks_d;
               }

               float relative_cost = (ifm_fetch + weight_fetch) / ofm_elements;

               /* Bias for encompassing entire IFM */
               if (ifm_shape.width * ifm_shape.height * ifm_shape.depth <
                   ifm_block.width * ifm_block.height * ifm_block.depth * 2) {
                  relative_cost = relative_cost / 2.0f;
               }

               /* Choose based on minimum cost */
               if (relative_cost <= best_cost) {
                  bool choose_this = false;

                  if (relative_cost == best_cost) {
                     int coverage_w = MIN2(ifm_shape.width, ifm_block.width);
                     int coverage_h = MIN2(ifm_shape.height, ifm_block.height);
                     float coverage = (float)(ifm_shape.width * ifm_shape.height) /
                                      MAX2(coverage_w * coverage_h, 1);

                     if (coverage <= best_coverage && (height <= 4 && width <= 4)) {
                        best_coverage = coverage;
                        choose_this = true;
                     }
                  } else {
                     best_coverage = FLT_MAX;
                     choose_this = true;
                  }

                  if (choose_this) {
                     best_cost = relative_cost;
                     config.ifm_block = ifm_block;
                     config.ifm_block.depth = ifm_block_depth;
                     config.ofm_block = ofm_block;
                  }
               }
            }
         }
      }

      /* Next depth level */
      depth = depth + ofm_ublock.depth;
      if (depth < ofm_shape.depth) {
         depth = round_away(depth, OFM_SPLIT_DEPTH);
      }

      /* Retry with single buffer if nothing found */
      if (depth > search_space_end.depth && best_cost == FLT_MAX &&
          num_blocks_in_ram == 2) {
         num_blocks_in_ram = 1;
         depth = restart_depth;
      }
   }

   config.ofm_ublock = ofm_ublock;
   config.is_partkernel = is_part_kernel;

   return config;
}
