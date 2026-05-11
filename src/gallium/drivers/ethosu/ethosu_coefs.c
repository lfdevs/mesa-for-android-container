/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "util/u_inlines.h"

#include <assert.h>
#include "ethosu_coefs.h"
#include "ethosu_encode.h"
#include "ethosu_ml.h"
#include "mlw_encode.h"

static void
encode_bias_scale_u65(int64_t bias, int32_t scale, uint32_t shift, uint8_t data[10])
{
   assert(-(1LL << (40 - 1)) <= bias && bias < (1LL << (40 - 1))); // signed 40-bit range
   assert(0 <= scale);                                             // unsigned 32-bit range
   assert(0 <= shift && shift < (1 << 6));                         // unsigned 6-bit range

   data[0] = (bias >> (0 * 8)) & 0xFF;
   data[1] = (bias >> (1 * 8)) & 0xFF;
   data[2] = (bias >> (2 * 8)) & 0xFF;
   data[3] = (bias >> (3 * 8)) & 0xFF;
   data[4] = (bias >> (4 * 8)) & 0xFF;

   data[5] = (scale >> (0 * 8)) & 0xFF;
   data[6] = (scale >> (1 * 8)) & 0xFF;
   data[7] = (scale >> (2 * 8)) & 0xFF;
   data[8] = (scale >> (3 * 8)) & 0xFF;

   data[9] = shift & 0x3F;
}

static void
encode_bias_scale_u85(int64_t bias, int32_t scale, uint32_t shift, uint8_t data[10])
{
   assert(INT32_MIN <= bias && bias <= INT32_MAX); // signed 32-bit range
   assert(0 <= scale);                             // unsigned 31-bit range
   assert(0 <= shift && shift < (1 << 6));         // unsigned 6-bit range

   data[0] = (bias >> (0 * 8)) & 0xFF;
   data[1] = (bias >> (1 * 8)) & 0xFF;
   data[2] = (bias >> (2 * 8)) & 0xFF;
   data[3] = (bias >> (3 * 8)) & 0xFF;

   data[4] = (scale >> (0 * 8)) & 0xFF;
   data[5] = (scale >> (1 * 8)) & 0xFF;
   data[6] = (scale >> (2 * 8)) & 0xFF;
   data[7] = (scale >> (3 * 8)) & 0x7F;

   data[8] = shift & 0x3F;
   data[9] = 0;
}

static void
fill_scale_and_biases(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, uint8_t **scales, long *scales_size, int32_t *biases)
{
   float ifm_scale = operation->ifm.scale;
   float ofm_scale = operation->ofm.scale;
   unsigned idx = 0;

   /* U65 packs 10-byte bias/scale entries contiguously then aligns to 16.
    * U85 scales are read in groups of 16 channels, so pad depth to a
    * 16-channel boundary first, then multiply by 10 bytes per entry. */
   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      *scales_size = align(operation->ofm.shape.depth * 10, 16);
   else
      *scales_size = align(operation->ofm.shape.depth, 16) * 10;

   *scales = malloc(*scales_size);
   memset(*scales, 0, *scales_size);

   for (unsigned i = 0; i < operation->ofm.shape.depth; i++) {
      double kernel_scale = (operation->kernel.scales != NULL) ?
                             operation->kernel.scales[i] : operation->kernel.scale;
      double conv_scale;

      if (!operation->ifm.is_signed) {
         /* UInt8 path: multiply as float first, then cast to double */
         conv_scale = (double)(ifm_scale * kernel_scale) / (double)ofm_scale;
      } else {
         /* Int8 path: cast to double before multiply for higher precision */
         conv_scale = ((double)ifm_scale * (double)kernel_scale) / (double)ofm_scale;
      }

      int32_t shift;
      int scale = ethosu_quantize_scale(conv_scale, &shift, false);

      uint64_t bias = biases ? biases[i] : 0;

      if (ethosu_ml_device(subgraph->base.device)->is_u65)
         encode_bias_scale_u65(
            bias, scale, shift, &(*scales)[idx]);
      else
         encode_bias_scale_u85(
            bias, scale, shift, &(*scales)[idx]);

      /* Saved for NPU_SET_OFM_SCALE emission in the command stream. */
      if (i == 0) {
         operation->conv.scale = scale;
         operation->conv.shift = shift;
      }

      idx += 10;
   }
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
fill_weights(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, uint8_t *weights_in_data, unsigned weight_in_size, uint8_t **weights_out, long *weights_out_size)
{
   ml_reorder_encode_weights(subgraph, operation, weights_in_data, weight_in_size, weights_out, weights_out_size);
}

void
fill_coefs(struct ethosu_subgraph *subgraph,
           struct ethosu_operation *operation,
           int32_t *bias_data,
           uint8_t *weight_data,
           unsigned weight_size)
{
   uint8_t *scales = NULL;
   fill_scale_and_biases(subgraph, operation, &scales, &operation->conv.scales.size, bias_data);

   operation->conv.scales.region = COEFS_REGION;
   operation->conv.scales.address = subgraph->coefs_used;
   subgraph->coefs_used += ALIGN_POT(operation->conv.scales.size, 16);
   subgraph->coefs = realloc(subgraph->coefs, subgraph->coefs_used);
   memcpy(subgraph->coefs + operation->conv.scales.address, scales, operation->conv.scales.size);
   free(scales);

   uint8_t *weights = NULL;
   fill_weights(subgraph, operation, weight_data, weight_size, &weights, &operation->conv.weights.size);

   if (!weights) {
      mesa_loge("fill_weights failed");
      return;
   }

   operation->conv.weights.region = COEFS_REGION;
   operation->conv.weights.address = subgraph->coefs_used;
   subgraph->coefs_used += ALIGN_POT(operation->conv.weights.size, 16);
   subgraph->coefs = realloc(subgraph->coefs, subgraph->coefs_used);
   memcpy(subgraph->coefs + operation->conv.weights.address, weights, operation->conv.weights.size);
   free(weights);
}

#define LUT_SIZE  256

void
fill_lut(struct ethosu_subgraph *subgraph,
         struct ethosu_operation *operation,
         void *lut)
{
   operation->pooling.lut.region = COEFS_REGION;
   operation->pooling.lut.address = subgraph->coefs_used;
   subgraph->coefs_used += LUT_SIZE;
   subgraph->coefs = realloc(subgraph->coefs, subgraph->coefs_used);
   memcpy(subgraph->coefs + operation->pooling.lut.address, lut, LUT_SIZE);
}
