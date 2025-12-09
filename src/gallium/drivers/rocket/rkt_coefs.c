/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "util/u_inlines.h"

#include "rkt_coefs.h"
#include "rkt_ml.h"

struct pipe_resource *
rkt_fill_weights(struct rkt_ml_subgraph *subgraph,
                 const struct pipe_ml_operation *poperation)
{
   struct pipe_context *pcontext = subgraph->base.context;
   unsigned weights_width = poperation->conv.weight_tensor->dims[1];
   unsigned weights_height = poperation->conv.weight_tensor->dims[2];
   unsigned input_channels = poperation->input_tensors[0]->dims[3];
   unsigned input_channels_real = poperation->input_tensors[0]->dims[3];
   unsigned output_channels = poperation->output_tensors[0]->dims[3];
   unsigned output_channels_real = poperation->output_tensors[0]->dims[3];
   unsigned weights_size;
   uint8_t zero_point = poperation->conv.weight_tensor->zero_point;
   struct pipe_transfer *transfer_in, *transfer_out;
   void *map =
      pipe_buffer_map(pcontext, poperation->conv.weight_tensor->resource,
                      PIPE_MAP_READ, &transfer_in);
   uint8_t(*weights_in)[weights_width][weights_height][input_channels] = map;
   struct pipe_resource *rsc;
   uint8_t *weights_out;

   input_channels = MAX2(input_channels, FEATURE_ATOMIC_SIZE);

   output_channels = align(output_channels, 2);
   if (rkt_is_depthwise(poperation))
      output_channels = 1;

   weights_size = weights_width * weights_height * output_channels *
                  align(input_channels, WEIGHT_ATOMIC_SIZE) * 2;

   rsc =
      pipe_buffer_create(pcontext->screen, 0, PIPE_USAGE_DEFAULT, weights_size);
   weights_out = pipe_buffer_map(pcontext, rsc, PIPE_MAP_WRITE, &transfer_out);

   unsigned input_channel_groups = WEIGHT_ATOMIC_SIZE;
   if (rkt_is_depthwise(poperation))
      input_channel_groups *= 2;

   unsigned input_channels_1 =
      DIV_ROUND_UP(input_channels, input_channel_groups);
   unsigned input_channels_2 = MIN2(input_channels, input_channel_groups);

   unsigned n = 0;
   for (int oc1 = 0; oc1 < DIV_ROUND_UP(output_channels, WEIGHT_ATOMIC_SIZE);
        oc1++) {
      for (int ic1 = 0; ic1 < input_channels_1; ic1++) {
         for (int x = 0; x < weights_width; x++) {
            for (int y = 0; y < weights_height; y++) {
               for (int oc2 = 0; oc2 < MIN2(output_channels, WEIGHT_ATOMIC_SIZE);
                    oc2++) {
                  for (int ic2 = 0; ic2 < input_channels_2; ic2++) {
                     unsigned oc = oc1 * WEIGHT_ATOMIC_SIZE + oc2;
                     unsigned ic = ic1 * input_channel_groups + ic2;
                     if (output_channels_real > 2 &&
                         oc >= align(output_channels_real, 2))
                        continue;

                     if (oc >= output_channels_real)
                        weights_out[n++] = 0x0;
                     else if (ic >= input_channels_real) {
                        if (ic2 < 16 || (input_channels_real % 32) > 16)
                           weights_out[n++] =
                              zero_point - 0x80; /* TODO: Why is the blob converting to
                                                    signed? It should be unsigned. */
                     } else
                        weights_out[n++] = weights_in[oc][x][y][ic] -
                                           0x80; /* TODO: Why is the blob converting to
                                                    signed? It should be unsigned. */
                  }
               }
            }
         }
      }
   }

   if (DBG_ENABLED(ROCKET_DBG_DUMP_BOS)) {
      static int task = 0;
      rkt_dump_buffer(weights_out, "weights", 0, task++, 0, weights_size);
   }

   pipe_buffer_unmap(pcontext, transfer_out);

   pipe_buffer_unmap(pcontext, transfer_in);

   return rsc;
}

static int32_t
calculate_bias_correction(struct rkt_ml_subgraph *subgraph,
                          const struct pipe_ml_operation *poperation,
                          unsigned oc, void *map)
{
   unsigned input_channels = poperation->input_tensors[0]->dims[3];
   unsigned input_zero_point = poperation->input_tensors[0]->zero_point;
   unsigned weights_width = poperation->conv.weight_tensor->dims[1];
   unsigned weights_height = poperation->conv.weight_tensor->dims[2];
   unsigned weight_zero_point = poperation->conv.weight_tensor->zero_point;
   uint8_t(*weights)[weights_width][weights_height][input_channels] = map;

   int32_t correction = 0;
   if (rkt_is_depthwise(poperation)) {
      for (unsigned x = 0; x < weights_width; x++) {
         for (unsigned y = 0; y < weights_height; y++) {
            correction += (weights[0][x][y][oc] - weight_zero_point) *
                          (input_zero_point - 0x80);
         }
      }
   } else {
      for (unsigned x = 0; x < weights_width; x++) {
         for (unsigned y = 0; y < weights_height; y++) {
            for (unsigned ic = 0; ic < input_channels; ic++) {
               correction += (weights[oc][x][y][ic] - weight_zero_point) *
                             (input_zero_point - 0x80);
            }
         }
      }
   }

   return correction;
}

struct pipe_resource *
rkt_fill_biases(struct rkt_ml_subgraph *subgraph,
                const struct pipe_ml_operation *poperation,
                unsigned *truncate_bits)
{
   struct pipe_context *pcontext = subgraph->base.context;
   unsigned output_channels = poperation->output_tensors[0]->dims[3];
   unsigned weights_size = poperation->conv.weight_tensor->dims[1];
   struct pipe_transfer *transfer_in, *transfer_out, *transfer_weights;
   int32_t *biases_in =
      pipe_buffer_map(pcontext, poperation->conv.bias_tensor->resource,
                      PIPE_MAP_READ, &transfer_in);
   void *weights =
      pipe_buffer_map(pcontext, poperation->conv.weight_tensor->resource,
                      PIPE_MAP_READ, &transfer_weights);
   struct pipe_resource *rsc;
   uint32_t *biases;

   rsc = pipe_buffer_create(pcontext->screen, 0, PIPE_USAGE_DEFAULT,
                            output_channels * sizeof(uint32_t));
   biases = pipe_buffer_map(pcontext, rsc, PIPE_MAP_WRITE, &transfer_out);

   // DBG("weight_scale %x\n",
   // fui(poperation->conv.weight_tensor->scale));
   /* TODO: Figure out when exactly we need to truncate */
   /* From
    * http://nvdla.org/hw/v1/ias/unit_description.html#convolution-accumulator :
    *
    * The final result of accumulator in CACC is 48bits for INT16 and 34bits for
    * INT8. The bit width between CACC and SDP is 32. For precisions INT8 and
    * INT16, there is a round and saturation operation before sending the result
    * to SDP. The precision of rounding is configured by field CLIP_TRUNCATE in
    * register D_CLIP_CFG. For FP16, the value is just converted from FP48 to
    * FP32.
    */
   if (fui(poperation->conv.weight_tensor->scale) == 0x3a88323f ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c0060de ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c06022d ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c1642e3 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c1e3f51 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c5c8aa8 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c615e93 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c7326a2 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3c783013 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3d1748e6 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3d282992 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3d2e87ae ||
       fui(poperation->conv.weight_tensor->scale) == 0x3d77f5f6 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3a9a5956 ||
       fui(poperation->conv.weight_tensor->scale) == 0x3caebc56)
      *truncate_bits = 1;
   else
      *truncate_bits = 0;

   int32_t max_bias = 0;
   int32_t max_corr = 0;
   unsigned max_num_bits = 0;
   bool retry = true;
   while (retry) {
      for (int oc = 0; oc < output_channels; oc++) {
         int32_t corr =
            calculate_bias_correction(subgraph, poperation, oc, weights);
         biases[oc] = (biases_in[oc] - corr) / (1 << *truncate_bits);

         int64_t max_val =
            (biases_in[oc] - corr + 255 * 255 * weights_size * weights_size) /
            (1 << *truncate_bits);
         unsigned num_bits = ceil(log(abs((int32_t)max_val)) / log(2)) + 1;
         max_bias = MAX2(max_bias, biases[oc]);
         max_corr = MAX2(max_corr, corr);
         max_num_bits = MAX2(max_num_bits, num_bits);

         /* TODO: This doesn't actually work, num_bits doesn't go above 19, and the
          * blob sometimes truncates way below */
         if (num_bits > 32) {
            (*truncate_bits)++;
            retry = true;
         } else
            retry = false;
      }
   }

   if (DBG_ENABLED(ROCKET_DBG_DUMP_BOS)) {
      static int task = 0;
      rkt_dump_buffer((uint8_t *)biases, "biases", 0, task++, 0,
                      output_channels * sizeof(uint32_t));
   }

   pipe_buffer_unmap(pcontext, transfer_out);

   pipe_buffer_unmap(pcontext, transfer_weights);

   pipe_buffer_unmap(pcontext, transfer_in);

   return rsc;
}
