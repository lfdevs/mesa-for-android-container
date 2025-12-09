/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "ethosu_lower.h"
#include "ethosu_coefs.h"
#include "ethosu_sched.h"

static bool
is_depthwise(const struct pipe_ml_operation *poperation)
{
   unsigned input_channels = poperation->input_tensors[0]->dims[3];
   unsigned output_channels = poperation->output_tensors[0]->dims[3];

   return poperation->conv.depthwise && input_channels > 1 &&
          output_channels > 1;
}

static unsigned
needed_total_padding(unsigned input_size, unsigned stride, unsigned filter_size)
{
   if (input_size % stride == 0)
      return MAX2(filter_size - stride, 0);

   return MAX2(filter_size - (input_size % stride), 0);
}

static bool
ethosu_is_part_kernel_first(struct ethosu_operation *operation)
{
   // Determine which block traversal strategy has better DPU utilization
   unsigned kernel_size = operation->kernel.height * operation->kernel.width;
   unsigned depth = operation->ifm.shape.depth;
   float depth_utilization = (float)depth / ethosu_round_up_to_multiple(depth, 32);
   float part_kernel_utilization = ((float)depth / ethosu_round_up_to_multiple(depth, 8));
   part_kernel_utilization *= (float)kernel_size / ethosu_round_up_to_multiple(kernel_size, 4);

   if (operation->type != ETHOSU_OPERATION_TYPE_CONVOLUTION)
      return false;

   if (operation->kernel.depthwise)
      return false;

   // Part-kernel first is always better for ifm depths <= 8
   if (part_kernel_utilization >= depth_utilization || depth <= 8)
      return true;

   return false;
}

static void
set_feature_maps(struct pipe_tensor *input_tensor,
                 struct pipe_tensor *output_tensor,
                 struct ethosu_operation *operation)
{
   operation->ifm.tensor_idx = input_tensor->index;
   operation->ifm.shape.height = input_tensor->dims[1];
   operation->ifm.shape.width = input_tensor->dims[2];
   operation->ifm.shape.depth = input_tensor->dims[3];
   operation->ifm.zero_point = input_tensor->zero_point;
   operation->ifm.scale = input_tensor->scale;
   operation->ifm.is_signed = input_tensor->is_signed;

   operation->ofm.tensor_idx = output_tensor->index;
   operation->ofm.shape.height = output_tensor->dims[1];
   operation->ofm.shape.width = output_tensor->dims[2];
   operation->ofm.shape.depth = output_tensor->dims[3];
   operation->ofm.zero_point = output_tensor->zero_point;
   operation->ofm.scale = output_tensor->scale;
   operation->ofm.is_signed = output_tensor->is_signed;
}

static const struct pipe_ml_operation *
ethosu_find_first_consumer(const struct pipe_ml_operation *poperations,
                           unsigned count,
                           unsigned tensor_index)
{
   for (unsigned i = 0; i < count; i++) {
      const struct pipe_ml_operation *poperation = &poperations[i];
      for (unsigned j = 0; j < poperation->input_count; j++)
         if (poperation->input_tensors[j]->index == tensor_index)
            return poperation;
   }

   return NULL;
}

static void
allocate_feature_maps(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   ethosu_allocate_feature_map(subgraph, &operation->ifm);
   operation->ifm.tiles.height_0 = operation->ifm.shape.height;
   operation->ifm.tiles.height_1 = operation->ifm.shape.height;
   operation->ifm.tiles.width_0 = operation->ifm.shape.width;

   ethosu_allocate_feature_map(subgraph, &operation->ofm);
   operation->ofm.tiles.height_0 = operation->ofm.shape.height;
   operation->ofm.tiles.height_1 = operation->ofm.shape.height;
   operation->ofm.tiles.width_0 = operation->ofm.shape.width;
}

static const struct pipe_ml_operation *
ethosu_find_first_producer(const struct pipe_ml_operation *poperations, unsigned count,
                           unsigned tensor_index)
{
   for (unsigned i = 0; i < count; i++) {
      const struct pipe_ml_operation *poperation = &poperations[i];

      for (unsigned j = 0; j < poperation->output_count; j++) {
         if (poperation->output_tensors[j]->index == tensor_index)
            return poperation;
      }
   }

   return NULL;
}

static void
ethosu_lower_convolution(struct ethosu_subgraph *subgraph,
                         const struct pipe_ml_operation *poperation,
                         struct pipe_tensor *input_tensor,
                         struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_CONVOLUTION;

   operation->conv.depthwise = is_depthwise(poperation);
   // operation->padding_same = poperation->conv.padding_same;
   // operation->stride = poperation->conv.stride_x;

   set_feature_maps(input_tensor, poperation->output_tensors[0], operation);

   operation->kernel.height = poperation->conv.weight_tensor->dims[1];
   operation->kernel.width = poperation->conv.weight_tensor->dims[2];
   operation->kernel.stride_y = poperation->conv.stride_y;
   operation->kernel.stride_x = poperation->conv.stride_x;
   operation->kernel.dilation_y = 1;
   operation->kernel.dilation_x = 1;
   operation->kernel.depthwise = is_depthwise(poperation);
   operation->kernel.scale = poperation->conv.weight_tensor->scale;
   operation->kernel.zero_point = poperation->conv.weight_tensor->zero_point;
   operation->kernel.is_signed = poperation->conv.weight_tensor->is_signed;

   operation->conv.part_kernel_first = ethosu_is_part_kernel_first(operation);

   if (poperation->conv.padding_same) {
      unsigned vert = needed_total_padding(input_tensor->dims[1], poperation->conv.stride_y, poperation->conv.weight_tensor->dims[1]);
      unsigned horiz = needed_total_padding(input_tensor->dims[2], poperation->conv.stride_x, poperation->conv.weight_tensor->dims[2]);

      operation->pad.top = vert / 2;
      operation->pad.left = horiz / 2;
      operation->pad.bottom = (vert + 1) / 2;
      operation->pad.right = (horiz + 1) / 2;
   } else {
      operation->pad.top = 0;
      operation->pad.left = 0;
      operation->pad.bottom = 0;
      operation->pad.right = 0;
   }

   allocate_feature_maps(subgraph, operation);

   ethosu_sched_operation(subgraph, operation);
   fill_coefs(subgraph, operation, poperation->conv.bias_tensor->resource, poperation->conv.weight_tensor->resource);
}

static void
ethosu_lower_pooling(struct ethosu_subgraph *subgraph,
                     const struct pipe_ml_operation *poperation,
                     struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->pooling.avg = poperation->pooling.type == PIPE_ML_POOLING_TYPE_AVG;

   set_feature_maps(poperation->input_tensors[0], poperation->output_tensors[0], operation);

   operation->kernel.height = poperation->pooling.filter_height;
   operation->kernel.width = poperation->pooling.filter_width;
   operation->kernel.stride_y = poperation->pooling.stride_y;
   operation->kernel.stride_x = poperation->pooling.stride_x;
   operation->kernel.dilation_y = 1;
   operation->kernel.dilation_x = 1;

   if (poperation->pooling.padding_same) {
      unsigned vert = needed_total_padding(operation->ifm.shape.height, poperation->pooling.stride_y, poperation->pooling.filter_height);
      unsigned horiz = needed_total_padding(operation->ifm.shape.width, poperation->pooling.stride_x, poperation->pooling.filter_width);

      operation->pad.top = vert / 2;
      operation->pad.left = horiz / 2;
      operation->pad.bottom = (vert + 1) / 2;
      operation->pad.right = (horiz + 1) / 2;
   } else {
      operation->pad.top = 0;
      operation->pad.left = 0;
      operation->pad.bottom = 0;
      operation->pad.right = 0;
   }

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_concatenation(struct ethosu_subgraph *subgraph,
                           const struct pipe_ml_operation *poperation,
                           unsigned input_idx,
                           struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->pooling.avg = true;

   set_feature_maps(poperation->input_tensors[input_idx], poperation->output_tensors[0], operation);
   operation->ofm.shape.depth = operation->ifm.shape.depth;

   operation->round_mode = ETHOSU_ROUNDING_NATURAL;

   operation->kernel.height = 1;
   operation->kernel.width = 1;
   operation->kernel.stride_y = 1;
   operation->kernel.stride_x = 1;
   operation->kernel.dilation_y = 1;
   operation->kernel.dilation_x = 1;

   allocate_feature_maps(subgraph, operation);
   for (unsigned i = 0; i < input_idx; i++) {
      struct ethosu_tensor *tensor = ethosu_find_tensor(subgraph, operation->ofm.tensor_idx);

      if (tensor->layout == ETHOSU_LAYOUT_NHWC)
         operation->ofm.tiles.addresses[0] += poperation->input_tensors[i]->dims[3];
      else if (tensor->layout == ETHOSU_LAYOUT_NHCWB16)
         operation->ofm.tiles.addresses[0] += poperation->input_tensors[i]->dims[2] * align(poperation->input_tensors[i]->dims[3], 16);
      else
         assert(0 && "Unsupported layout");
   }

   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_resize(struct ethosu_subgraph *subgraph,
                    const struct pipe_ml_operation *poperation,
                    struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->pooling.avg = true;

   set_feature_maps(poperation->input_tensors[0], poperation->output_tensors[0], operation);
   operation->ifm.zero_point = 0;
   operation->ofm.zero_point = 0;

   operation->kernel.height = 1;
   operation->kernel.width = 1;
   operation->kernel.stride_y = 1;
   operation->kernel.stride_x = 1;
   operation->kernel.dilation_y = 1;
   operation->kernel.dilation_x = 1;

   operation->upscale = true;

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_strided_slice(struct ethosu_subgraph *subgraph,
                           const struct pipe_ml_operation *poperation,
                           struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->pooling.avg = true;

   set_feature_maps(poperation->input_tensors[0], poperation->output_tensors[0], operation);
   operation->ifm.shape = operation->ofm.shape;
   operation->ifm.zero_point = 0;
   operation->ofm.zero_point = 0;

   operation->kernel.height = 1;
   operation->kernel.width = 1;
   operation->kernel.stride_y = 1;
   operation->kernel.stride_x = 1;
   operation->kernel.dilation_y = 1;
   operation->kernel.dilation_x = 1;

   allocate_feature_maps(subgraph, operation);

   unsigned augmented_coord[5];
   augmented_coord[0] = 0;
   for (int i = 0; i < 4; ++i) {
      augmented_coord[i + 1] = poperation->slice.begin[i];
   }

   unsigned augmented_strides[5];
   augmented_strides[0] = operation->ifm.shape.depth * operation->ifm.shape.width * operation->ifm.shape.height;
   augmented_strides[1] = 1;
   augmented_strides[2] = operation->ifm.shape.depth * operation->ifm.shape.width;
   augmented_strides[3] = operation->ifm.shape.depth;
   augmented_strides[4] = 1;

   unsigned address_offset = 0;
   for (int i = 0; i < 5; ++i)
      address_offset += augmented_coord[i] * augmented_strides[i];

   operation->ifm.tiles.addresses[0] += address_offset;

   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_add(struct ethosu_subgraph *subgraph,
                 const struct pipe_ml_operation *poperation,
                 struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_ELTWISE;

   set_feature_maps(poperation->input_tensors[0], poperation->output_tensors[0], operation);

   operation->ifm2.tensor_idx = poperation->input_tensors[1]->index;
   operation->ifm2.shape.height = poperation->input_tensors[1]->dims[1];
   operation->ifm2.shape.width = poperation->input_tensors[1]->dims[2];
   operation->ifm2.shape.depth = poperation->input_tensors[1]->dims[3];
   operation->ifm2.zero_point = poperation->input_tensors[1]->zero_point;
   operation->ifm2.scale = poperation->input_tensors[1]->scale;
   operation->ifm2.is_signed = poperation->input_tensors[1]->is_signed;

   operation->kernel.height = 1;
   operation->kernel.width = 1;
   operation->kernel.stride_y = 1;
   operation->kernel.stride_x = 1;
   operation->kernel.dilation_y = 1;
   operation->kernel.dilation_x = 1;

   allocate_feature_maps(subgraph, operation);

   ethosu_allocate_feature_map(subgraph, &operation->ifm2);
   operation->ifm2.tiles.height_0 = operation->ifm2.shape.height;
   operation->ifm2.tiles.height_1 = operation->ifm2.shape.height;
   operation->ifm2.tiles.width_0 = operation->ifm2.shape.width;

   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_dma(struct ethosu_subgraph *subgraph,
                 const struct pipe_ml_operation *poperation,
                 struct ethosu_operation *conv_operation,
                 struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_DMA;

   operation->dma.address = conv_operation->conv.scales.address;
   operation->dma.size = conv_operation->conv.scales.size + conv_operation->conv.weights.size;

   conv_operation->conv.scales.region = SCRATCH_REGION;
   conv_operation->conv.scales.address = 0;

   conv_operation->conv.weights.region = SCRATCH_REGION;
   conv_operation->conv.weights.address = conv_operation->conv.scales.size;
}

static void
register_tensors(struct ethosu_subgraph *subgraph,
                 const struct pipe_ml_operation *poperations,
                 unsigned count)
{
   for (unsigned i = 0; i < count; i++) {
      const struct pipe_ml_operation *poperation = &poperations[i];

      for (unsigned j = 0; j < poperation->input_count; j++) {
         struct pipe_tensor *ptensor = poperation->input_tensors[j];
         ethosu_register_tensor(subgraph, ptensor);
      }

      for (unsigned j = 0; j < poperation->output_count; j++) {
         struct pipe_tensor *ptensor = poperation->output_tensors[j];
         ethosu_register_tensor(subgraph, ptensor);

         if (!DBG_ENABLED(ETHOSU_DBG_DISABLE_NHCWB16)) {
            struct ethosu_tensor *tensor = ethosu_find_tensor(subgraph, ptensor->index);
            if (tensor->shape.depth % 16 == 0 &&
                ethosu_find_first_consumer(poperations, count, ptensor->index)) {
               tensor->layout = ETHOSU_LAYOUT_NHCWB16;
            }
         }
      }
   }
}

void
ethosu_lower_graph(struct ethosu_subgraph *subgraph,
                   const struct pipe_ml_operation *poperations, unsigned count)
{
   register_tensors(subgraph, poperations, count);

   /* Lower */
   for (int i = 0; i < count; i++) {
      struct ethosu_operation operation = {0};

      switch (poperations[i].type) {

      case PIPE_ML_OPERATION_TYPE_CONVOLUTION: {
         struct pipe_tensor *input_tensor = poperations[i].input_tensors[0];
         const struct pipe_ml_operation *producer = ethosu_find_first_producer(poperations, count, input_tensor->index);
         bool padded_input = producer && producer->type == PIPE_ML_OPERATION_TYPE_PAD;

         if (padded_input) {
            input_tensor = producer->input_tensors[0];
         }

         ethosu_lower_convolution(subgraph, &poperations[i], input_tensor, &operation);

         if (padded_input) {
            operation.pad.top = 1;
            operation.pad.left = 1;
         }

         if (operation.conv.scales.size + operation.conv.weights.size <=
             ethosu_screen(subgraph->base.context->screen)->info.sram_size) {
            struct ethosu_operation dma_operation = {0};
            ethosu_lower_dma(subgraph, &poperations[i], &operation, &dma_operation);

            util_dynarray_append(&subgraph->operations, dma_operation);
         }

         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_ADD: {
         ethosu_lower_add(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_POOLING: {
         ethosu_lower_pooling(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_STRIDED_SLICE: {
         ethosu_lower_strided_slice(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_CONCATENATION: {
         for (int j = 0; j < poperations[i].input_count; j++) {
            ethosu_lower_concatenation(subgraph, &poperations[i], j, &operation);
            util_dynarray_append(&subgraph->operations, operation);
         }
         break;
      }

      case PIPE_ML_OPERATION_TYPE_RESIZE: {
         ethosu_lower_resize(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_PAD: {
         // Just ignore the pad operation for now, as it will be handled by its consumers
         break;
      }

      default:
         DBG("poperation->type %d\n", poperations[i].type);
         UNREACHABLE("Unsupported ML operation type");
      }
   }
}