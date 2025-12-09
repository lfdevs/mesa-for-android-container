/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "util/u_inlines.h"

#include "mlw_codec/mlw_encode.h"
#include "ethosu_coefs.h"

static void
fill_scale_and_biases(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, uint8_t **scales, long *scales_size, struct pipe_resource *bias_rsrc)
{
   struct pipe_transfer *transfer_in;
   int32_t *biases = pipe_buffer_map(subgraph->base.context, bias_rsrc,
                                     PIPE_MAP_READ, &transfer_in);
   unsigned idx = 0;

   *scales_size = align(operation->ofm.shape.depth * 10, 16);
   *scales = malloc(*scales_size);
   memset(*scales, 0, *scales_size);

   for (unsigned i = 0; i < operation->ofm.shape.depth; i++) {
      uint64_t bias = biases[i];
      double conv_scale = ((double)operation->ifm.scale * (double)operation->kernel.scale) / (double)operation->ofm.scale;
      uint32_t shift;
      int scale = ethosu_quantize_scale(conv_scale, &shift);

      (*scales)[idx++] = (bias >> (0 * 8)) & 0xFF;
      (*scales)[idx++] = (bias >> (1 * 8)) & 0xFF;
      (*scales)[idx++] = (bias >> (2 * 8)) & 0xFF;
      (*scales)[idx++] = (bias >> (3 * 8)) & 0xFF;
      (*scales)[idx++] = (bias >> (4 * 8)) & 0xFF;

      (*scales)[idx++] = (scale >> (0 * 8)) & 0xFF;
      (*scales)[idx++] = (scale >> (1 * 8)) & 0xFF;
      (*scales)[idx++] = (scale >> (2 * 8)) & 0xFF;
      (*scales)[idx++] = (scale >> (3 * 8)) & 0xFF;

      (*scales)[idx++] = shift & 0x3F;
   }

   pipe_buffer_unmap(subgraph->base.context, transfer_in);
}

static void
calculate_weights_strides(struct ethosu_operation *operation, int out_strides[4])
{
   if (operation->kernel.depthwise) {
      out_strides[0] = 1;
      out_strides[1] = operation->ofm.shape.depth * operation->kernel.height;
      out_strides[2] = operation->ofm.shape.depth;
      out_strides[3] = operation->ofm.shape.depth * operation->kernel.width;
   } else {
      out_strides[3] = 1;
      out_strides[2] = out_strides[3] * operation->ifm.shape.depth;
      out_strides[1] = out_strides[2] * operation->kernel.width;
      out_strides[0] = out_strides[1] * operation->kernel.height;
   }
}

static void
fill_weights(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, uint8_t **weights, long *weights_size, struct pipe_resource *weight_rsrc)
{
   int brick_strides[4] = {0};
   unsigned input_channels = operation->ifm.shape.depth;

   if (operation->kernel.depthwise)
      input_channels = 1;

   calculate_weights_strides(operation, brick_strides);

   struct pipe_transfer *transfer_in;
   uint8_t *input_weights_8 = pipe_buffer_map(subgraph->base.context, weight_rsrc,
                                              PIPE_MAP_READ, &transfer_in);
   int16_t *input_weights = malloc(pipe_buffer_size(weight_rsrc) * sizeof(*input_weights));
   for (int i = 0; i < pipe_buffer_size(weight_rsrc); i++) {
      if (operation->kernel.is_signed)
         input_weights[i] = (int8_t)input_weights_8[i] - operation->kernel.zero_point;
      else
         input_weights[i] = input_weights_8[i] - operation->kernel.zero_point;
   }
   pipe_buffer_unmap(subgraph->base.context, transfer_in);

   long padded_size = 0;
   *weights_size = mlw_reorder_encode(
      IFM_UBLOCK.depth,
      OFM_UBLOCK.depth,
      operation->ofm.shape.depth,
      operation->kernel.height,
      operation->kernel.width,
      input_channels,
      brick_strides,
      input_weights,
      operation->block_config.ofm_block.depth,
      operation->kernel.depthwise,
      operation->conv.part_kernel_first,
      8 /* ifm_bitdepth */,
      8 /* decomp_h */,
      8 /* decomp_w */,
      weights,
      &padded_size,
      DBG_ENABLED(ETHOSU_DBG_MSGS));

   free(input_weights);
}

void
fill_coefs(struct ethosu_subgraph *subgraph,
           struct ethosu_operation *operation,
           struct pipe_resource *bias_rsrc,
           struct pipe_resource *weight_rsrc)
{
   uint8_t *scales = NULL;
   fill_scale_and_biases(subgraph, operation, &scales, &operation->conv.scales.size, bias_rsrc);

   operation->conv.scales.region = COEFS_REGION;
   operation->conv.scales.address = subgraph->coefs_used;
   subgraph->coefs_used += ALIGN_POT(operation->conv.scales.size, 16);
   subgraph->coefs = realloc(subgraph->coefs, subgraph->coefs_used);
   memcpy(subgraph->coefs + operation->conv.scales.address, scales, operation->conv.scales.size);
   free(scales);

   uint8_t *weights = NULL;
   fill_weights(subgraph, operation, &weights, &operation->conv.weights.size, weight_rsrc);

   operation->conv.weights.region = COEFS_REGION;
   operation->conv.weights.address = subgraph->coefs_used;
   subgraph->coefs_used += ALIGN_POT(operation->conv.weights.size, 16);
   subgraph->coefs = realloc(subgraph->coefs, subgraph->coefs_used);
   memcpy(subgraph->coefs + operation->conv.weights.address, weights, operation->conv.weights.size);
   free(weights);
}
