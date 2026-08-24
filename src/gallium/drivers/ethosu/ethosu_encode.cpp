//
// SPDX-FileCopyrightText: Copyright 2021-2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-FileCopyrightText: Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the License); you may
// not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <assert.h>
#include "ethosu_encode_support.h"
#include "ethosu_ml.h"
#include "ethosu_encode.h"
#include "mlw_encode.h"

extern "C" {

static int32_t
weight_func(int32_t query, ml_source_state_t *state, int16_t *buffer, int32_t size, void *user_arg)
{
   assert(query == MLW_SOURCE_QUERY_WEIGHTS);
   IWeightSource *src = reinterpret_cast<IWeightSource *>(user_arg);
   int source_size = src->Get(buffer, size);
   state->eos = source_size < size;
   return source_size;
}

static int
apply_zero_point_ihwo(const WeightTransformParam *p, int value)
{
   value = (value - int(p->zeroPoints[p->o % p->zeroCount]));
   assert(value >= -255 && value <= 255);
   return value;
}

static int
apply_zero_point_ohwi(const WeightTransformParam *p, int value)
{
   value = (value - int(p->zeroPoints[p->i % p->zeroCount]));
   assert(value >= -255 && value <= 255);
   return value;
}

void
ml_reorder_encode_weights(struct ethosu_subgraph *subgraph,
                          struct ethosu_operation *operation,
                          const uint8_t *input_weights,
                          long input_weights_size,
                          uint8_t **weights,
                          long *weights_size)
{
   struct ethosu_ml_device *device = ethosu_ml_device(subgraph->base.device);
   int bit_depth = 8;
   bool is_sparse = operation->conv.weight_sparse;
   EthosUTraversal traversal;
   struct WeightTransformParam param;
   WeightTransformFunc transform_func = apply_zero_point_ohwi;
   ml_encode_result_t res;
   Point2i stride(operation->kernel.stride_x, operation->kernel.stride_y);
   Point2i dilation(operation->kernel.dilation_x, operation->kernel.dilation_y);
   int ret;

   ml_ethosu_encode_params_t params;
   params.encoder_flags = MLW_ENCODE_FLAG_NONE;
   params.source_buffering_hint = 128 * 1024;
   params.realloc_func = NULL;

   if (weight_cache_lookup(device, operation, input_weights,
                           input_weights_size, weights, weights_size))
      return;

   if (operation->kernel.depthwise) {
      traversal = EthosUTraversal::Depthwise;
      transform_func = apply_zero_point_ihwo;
   } else if (operation->block_config.is_partkernel)
      traversal = EthosUTraversal::PartKernel;
   else
      traversal = EthosUTraversal::DepthFirst;

   int zero_point = (int)operation->kernel.zero_point;
   param.zeroPoints = &zero_point;
   param.zeroCount = 1;

   WeightSourceCommon *source;

   if (device->is_u65) {
      if (operation->kernel.is_signed) {
         source = new EthosUWeightOrdering<int8_t>(1, dilation,
                                                   operation->block_config.ofm_block.depth, bit_depth, device->ofm_ublock.depth,
                                                   device->ifm_ublock.depth, transform_func, &param, traversal);
      } else {
         source = new EthosUWeightOrdering<uint8_t>(1, dilation,
                                                    operation->block_config.ofm_block.depth, bit_depth, device->ofm_ublock.depth,
                                                    device->ifm_ublock.depth, transform_func, &param, traversal);
      }
   } else {
      if (operation->kernel.is_signed) {
         source = new EthosU85WeightOrdering<int8_t>(1, 256, stride, dilation,
                                                     operation->block_config.ofm_block.depth, operation->block_config.ifm_block.depth, bit_depth, operation->block_config.ofm_ublock.depth,
                                                     transform_func, &param, traversal, is_sparse);
      } else {
         source = new EthosU85WeightOrdering<uint8_t>(1, 256, stride, dilation,
                                                      operation->block_config.ofm_block.depth, operation->block_config.ifm_block.depth, bit_depth, operation->block_config.ofm_ublock.depth,
                                                      transform_func, &param, traversal, is_sparse);
      }
   }

   Shape ohwi = {static_cast<int>(operation->ofm.shape.depth),
                 static_cast<int>(operation->kernel.height),
                 static_cast<int>(operation->kernel.width),
                 static_cast<int>(operation->ifm.shape.depth)};
   Shape ohwiStrides;

   int v = 1;
   for (int i = 4 - 1; i >= 0; --i) {
      ohwiStrides[i] = v;
      v *= ohwi[i];
   }

   if (operation->kernel.depthwise)
      SWAP(ohwiStrides[0], ohwiStrides[3]); /* IHWO */

   source->SetSource(input_weights, 0, ohwi, ohwiStrides, 0);

   mle_context_t *ctx = nullptr;
   ret = ml_encode_ethosu_stream(&res, &params, weight_func, source, &ctx);
   mle_destroy_context(ctx);
   if (ret < 0) {
      mesa_loge("mlw encode failed");
      *weights = NULL;
      *weights_size = 0;
      mle_free(&res);
      delete source;
      return;
   }

   *weights = res.encoded_data;
   res.encoded_data = NULL;
   *weights_size = res.encoded_length;
   weight_cache_insert(device, operation, input_weights, input_weights_size,
                       *weights, *weights_size);
   mle_free(&res);
   delete source;
}

} // extern "C"
