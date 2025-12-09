/*
 * Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "ethosu_sched.h"

static int
required_input_size(int value, int stride, int border)
{
   return (value - 1) * stride + border;
}

static struct ethosu_block
_get_ifm_blocksize(struct ethosu_operation *operation, struct ethosu_block ofm_block)
{
   struct ethosu_block ifm_block = {0};

   // IFM block height
   int h = required_input_size(ofm_block.height, operation->kernel.stride_y, MIN2(operation->kernel.height, SUB_KERNEL_MAX.height));
   h = align(h, OFM_UBLOCK.height);

   // IFM block width
   int w = required_input_size(ofm_block.width, operation->kernel.stride_x, MIN2(operation->kernel.width, SUB_KERNEL_MAX.width));
   w = align(w, OFM_UBLOCK.width);

   ifm_block.height = h;
   ifm_block.width = w;
   ifm_block.depth = ofm_block.depth;

   return ifm_block;
}

static bool
try_block_config(struct ethosu_operation *operation, struct ethosu_block ofm_block, struct ethosu_block ifm_block, struct ethosu_shram_layout *layout)
{
   int ifm_bytes = ifm_block.width * ifm_block.height * align(ifm_block.depth, 8);
   int ifm_banks = align(DIV_ROUND_UP(ifm_bytes, BANK_SIZE_BYTES) * 2, IFM_GRANULE);
   int lut_bytes = operation->type == ETHOSU_OPERATION_TYPE_ELTWISE ? operation->eltwise.lut_bytes : 0;
   int lut_banks = MAX2(DIV_ROUND_UP(lut_bytes, 1024), SHRAM_RESERVED_END_BANKS);
   int lut_start = SHRAM_TOTAL_BANKS - lut_banks;
   int ifm_end = SHRAM_RESERVED_OUTPUT_BANKS + ifm_banks;
   int ifm2_start = ifm_end;
   int acc_start = lut_start;

   if (operation->type != ETHOSU_OPERATION_TYPE_ELTWISE) {
      int acc_bytes = (ofm_block.width * ofm_block.height * align(ofm_block.depth, 8) * 32) / 8;
      int acc_banks = align(DIV_ROUND_UP(acc_bytes, BANK_SIZE_BYTES) * 2, ACC_GRANULE);
      acc_start -= acc_banks;
   } else {
      int ifm2_banks = ifm_banks; /* TODO: Fix for scalar eltwise */

      if (ifm2_start + ifm2_banks > acc_start)
         return false;

      ifm_end = acc_start;
   }

   if (ifm_end > acc_start)
      return false;

   layout->ib_start = SHRAM_RESERVED_OUTPUT_BANKS;
   layout->ib_start2 = ifm2_start;
   layout->ib_end = ifm_end;
   layout->ab_start = acc_start;
   layout->lut_start = lut_start;

   return true;
}

static struct ethosu_block_config
find_block_config(struct ethosu_operation *operation)
{
   struct ethosu_block_config config = {};
   struct ethosu_block search_space = ARCH_OFM_BLOCK_MAX;
   float ofm_elements = operation->ofm.shape.width * operation->ofm.shape.height * operation->ofm.shape.depth;
   float ifm_elements = operation->ifm.shape.width * operation->ifm.shape.height * operation->ifm.shape.depth;
   bool is_pooling = operation->type == ETHOSU_OPERATION_TYPE_POOLING;
   bool is_depthwise = operation->conv.depthwise;
   bool is_equal_depth = is_pooling || is_depthwise || operation->type == ETHOSU_OPERATION_TYPE_ELTWISE;
   bool is_convolution = operation->type == ETHOSU_OPERATION_TYPE_CONVOLUTION;
   float best_cost = FLT_MAX;
   unsigned best_coverage = UINT_MAX;

   search_space.width = MIN2(search_space.width, operation->ofm.shape.width);
   search_space.height = MIN2(search_space.height, operation->ofm.shape.height);
   search_space.depth = MIN2(search_space.depth, operation->ofm.shape.depth);

   unsigned depth = MAX2(OFM_UBLOCK.depth, MIN2(search_space.depth, ARCH_SPLIT_DEPTH));

   if (depth < operation->ofm.shape.depth) {
      depth = align(depth, ARCH_SPLIT_DEPTH);
   }

   search_space.width = align(search_space.width, OFM_UBLOCK.width);
   search_space.height = align(search_space.height, OFM_UBLOCK.height);
   search_space.depth = align(search_space.depth, OFM_UBLOCK.depth);

   while (depth <= search_space.depth) {
      bool wont_fit[search_space.height + 1][search_space.width + 1];
      memset(wont_fit, 0, sizeof(wont_fit));

      for (unsigned height = OFM_UBLOCK.height; height <= search_space.height; height += OFM_UBLOCK.height) {
         for (unsigned width = OFM_UBLOCK.width; width <= search_space.width; width += OFM_UBLOCK.width) {

            if (wont_fit[height][width])
               continue;

            struct ethosu_block ofm_block = {height, width, depth};
            struct ethosu_block ifm_block = _get_ifm_blocksize(operation, ofm_block);

            if (!is_equal_depth)
               ifm_block.depth = align(MIN2(operation->ifm.shape.depth, operation->conv.part_kernel_first ? 16 : 32), IFM_UBLOCK.depth);

            // Try to fit the blocks in SHRAM
            struct ethosu_shram_layout layout = {0};
            if (try_block_config(operation, ofm_block, ifm_block, &layout)) {

               struct ethosu_block full_blocks = {DIV_ROUND_UP(operation->ofm.shape.width, ofm_block.width),
                                                  DIV_ROUND_UP(operation->ofm.shape.height, ofm_block.height),
                                                  DIV_ROUND_UP(operation->ofm.shape.depth, ofm_block.depth)};
               float blocks[3] = {operation->ofm.shape.width / (float)ofm_block.width,
                                  operation->ofm.shape.height / (float)ofm_block.height,
                                  operation->ofm.shape.depth / (float)ofm_block.depth};

               float weight_area = is_convolution ? operation->kernel.width * operation->kernel.height : 0;
               float weight_fetch = weight_area * operation->ifm.shape.depth * full_blocks.width * full_blocks.height;
               if (!is_depthwise)
                  weight_fetch *= blocks[2] * ofm_block.depth;

               float ifm_fetch = ifm_block.width * ifm_block.height * operation->ifm.shape.depth * blocks[0] * blocks[1];
               if (!is_equal_depth)
                  ifm_fetch *= full_blocks.depth;

               float relative_cost = 0;
               if (operation->type != ETHOSU_OPERATION_TYPE_ELTWISE)
                  relative_cost = (ifm_fetch + weight_fetch) / ofm_elements;
               else
                  relative_cost = ofm_elements / (height * width * depth);

               if (ifm_elements < ifm_block.width * ifm_block.height * ifm_block.depth * 2)
                  relative_cost /= 2.0f;

               if (relative_cost <= best_cost) {
                  bool choose_this = false;

                  if (relative_cost == best_cost) {
                     struct ethosu_block coverage_shape = {
                        MIN2(ifm_block.height, operation->ifm.shape.height),
                        MIN2(ifm_block.width, operation->ifm.shape.width),
                        MIN2(ifm_block.depth, operation->ifm.shape.depth)};
                     float coverage = (float)(operation->ifm.shape.width * operation->ifm.shape.height) /
                                      (float)MAX2(1, coverage_shape.width * coverage_shape.height);

                     if (coverage <= best_coverage && (height <= 4 && width <= 4)) {
                        best_coverage = coverage;
                        choose_this = true;
                     }
                  } else {
                     best_coverage = UINT_MAX;
                     choose_this = true;
                  }

                  if (choose_this) {
                     config.shram_layout = layout;
                     config.ifm_block = ifm_block;
                     config.ofm_block.height = height;
                     config.ofm_block.width = width;
                     config.ofm_block.depth = depth;

                     best_cost = relative_cost;
                  }
               }
            } else {
               wont_fit[height][width] = true;
            }
         }
      }

      depth += OFM_UBLOCK.depth;
      if (depth < operation->ofm.shape.depth) {
         depth = align(depth, ARCH_SPLIT_DEPTH);
      }
   }

   return config;
}

void
ethosu_sched_operation(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   operation->block_config = find_block_config(operation);
}
