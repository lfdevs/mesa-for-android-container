/*
 * Copyright (c) 2023-2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "pipe-loader/pipe_loader.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/macros.h"
#include "util/u_inlines.h"

#include "tensorflow/lite/builtin_ops.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/core/c/builtin_op_data.h"

/* TODO: Move to TfLiteAsyncKernel for zero-copy of buffers */

static bool fused_relu6_supported(TfLiteTensor *tensor);

enum teflon_debug_flags {
   TEFLON_DEBUG_VERBOSE = 1 << 1,
};

static const struct debug_named_value teflon_debug_flags[] = {
   {"verbose", TEFLON_DEBUG_VERBOSE, "Verbose logging."},
   DEBUG_NAMED_VALUE_END};

DEBUG_GET_ONCE_FLAGS_OPTION(debug_teflon, "TEFLON_DEBUG", teflon_debug_flags, 0)

static inline void
teflon_debug(const char *format, ...)
{
   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      va_list ap;
      va_start(ap, format);
      _debug_vprintf(format, ap);
      va_end(ap);
   }
}

struct teflon_delegate {
   TfLiteDelegate base;
   struct pipe_loader_device *dev;
   struct pipe_context *context;
   struct pipe_tensor *tensors;
   unsigned tensor_count;
};

struct teflon_subgraph {
   struct pipe_ml_subgraph *base;

   unsigned *input_tensors;
   unsigned input_count;

   unsigned *output_tensors;
   unsigned output_count;
};

static struct pipe_resource *
create_resource(struct pipe_context *context, TfLiteTensor tensor)
{
   unsigned bytes;
   unsigned size = 1;

   for (int i = 0; i < tensor.dims->size; i++)
      size *= tensor.dims->data[i];

   switch (tensor.type) {
   case kTfLiteInt8:
   case kTfLiteUInt8:
      bytes = 1;
      break;
   case kTfLiteInt16:
   case kTfLiteUInt16:
   case kTfLiteFloat16:
      bytes = 2;
      break;
   case kTfLiteInt32:
   case kTfLiteUInt32:
   case kTfLiteFloat32:
      bytes = 4;
      break;
   case kTfLiteInt64:
   case kTfLiteUInt64:
   case kTfLiteFloat64:
   case kTfLiteComplex64:
      bytes = 8;
      break;
   default:
      UNREACHABLE("Unsupported TF type");
   }

   return pipe_buffer_create_with_data(context, 0, PIPE_USAGE_DEFAULT, size * bytes, tensor.data.data);
}

static bool
fill_operation(struct teflon_delegate *delegate, TfLiteContext *tf_context, TfLiteNode *node, TfLiteRegistration *node_registration, struct pipe_ml_operation *operation)
{
   struct pipe_tensor *tensors = delegate->tensors;

   operation->input_count = node->inputs->size;
   operation->input_tensors = calloc(operation->input_count, sizeof(void *));
   for (unsigned i = 0; i < node->inputs->size; i++)
      operation->input_tensors[i] = &tensors[node->inputs->data[i]];

   operation->output_count = node->outputs->size;
   operation->output_tensors = calloc(operation->output_count, sizeof(void *));
   for (unsigned i = 0; i < node->outputs->size; i++)
      operation->output_tensors[i] = &tensors[node->outputs->data[i]];

   switch (node_registration->builtin_code) {
   case kTfLiteBuiltinConv2d:
   case kTfLiteBuiltinDepthwiseConv2d: {
      operation->type = PIPE_ML_OPERATION_TYPE_CONVOLUTION;
      operation->conv.weight_tensor = &tensors[node->inputs->data[1]];
      operation->conv.bias_tensor = &tensors[node->inputs->data[2]];
      if (node_registration->builtin_code == kTfLiteBuiltinConv2d) {
         TfLiteConvParams *params = (TfLiteConvParams *)node->builtin_data;

         assert(params->activation == kTfLiteActNone ||
                params->activation == kTfLiteActRelu ||
                params->activation == kTfLiteActRelu6);
         if (node_registration->version >= 2) {
            operation->conv.dilation_width_factor = params->dilation_width_factor;
            operation->conv.dilation_height_factor = params->dilation_height_factor;
         } else {
            operation->conv.dilation_width_factor = 1;
            operation->conv.dilation_height_factor = 1;
         }
         operation->conv.stride_x = params->stride_width;
         operation->conv.stride_y = params->stride_height;
         operation->conv.padding_same = params->padding == kTfLitePaddingSame;
         operation->conv.depthwise = false;
         operation->conv.relu = params->activation == kTfLiteActRelu ||
                                params->activation == kTfLiteActRelu6;

         if (params->activation == kTfLiteActRelu6)
            return fused_relu6_supported(&tf_context->tensors[node->outputs->data[0]]);

      } else {
         TfLiteDepthwiseConvParams *params = (TfLiteDepthwiseConvParams *)node->builtin_data;

         assert(params->activation == kTfLiteActNone ||
                params->activation == kTfLiteActRelu ||
                params->activation == kTfLiteActRelu6);
         if (node_registration->version >= 2) {
            operation->conv.dilation_width_factor = params->dilation_width_factor;
            operation->conv.dilation_height_factor = params->dilation_height_factor;
         } else {
            operation->conv.dilation_width_factor = 1;
            operation->conv.dilation_height_factor = 1;
         }
         operation->conv.stride_x = params->stride_width;
         operation->conv.stride_y = params->stride_height;
         operation->conv.padding_same = params->padding == kTfLitePaddingSame;
         operation->conv.depthwise = true;
         operation->conv.relu = params->activation == kTfLiteActRelu ||
                                params->activation == kTfLiteActRelu6;

         if (params->activation == kTfLiteActRelu6)
            return fused_relu6_supported(&tf_context->tensors[node->outputs->data[0]]);
      }
      operation->conv.pointwise = operation->conv.weight_tensor->dims[1] == 1 &&
                                  operation->conv.weight_tensor->dims[2] == 1;
      break;
   }
   case kTfLiteBuiltinMaxPool2d:
      operation->pooling.type = PIPE_ML_POOLING_TYPE_MAX;
      __attribute__((fallthrough));
   case kTfLiteBuiltinAveragePool2d: {
      TfLitePoolParams *params = node->builtin_data;

      operation->type = PIPE_ML_OPERATION_TYPE_POOLING;

      /* Skip setting operation->pooling.type for PIPE_ML_POOLING_TYPE_AVG==0 */
      operation->pooling.filter_height = params->filter_height;
      operation->pooling.filter_width = params->filter_width;
      operation->pooling.stride_x = params->stride_width;
      operation->pooling.stride_y = params->stride_height;
      operation->pooling.padding_same = params->padding == kTfLitePaddingSame;
      break;
   }
   case kTfLiteBuiltinAdd:
      operation->type = PIPE_ML_OPERATION_TYPE_ADD;
      break;
   case kTfLiteBuiltinConcatenation: {
      TfLiteConcatenationParams *params = node->builtin_data;

      operation->type = PIPE_ML_OPERATION_TYPE_CONCATENATION;
      operation->conc.axis = params->axis;
      break;
   }
   case kTfLiteBuiltinSplit:
      operation->type = PIPE_ML_OPERATION_TYPE_SPLIT;
      operation->split.axis = tf_context->tensors[node->inputs->data[0]].data.i32[0];
      break;
   case kTfLiteBuiltinPad: {
      int32_t *paddings;

      // Values tensor for non-zero padding not yet implemented
      if (node->inputs->size != 2)
         return false;

      paddings = tf_context->tensors[node->inputs->data[1]].data.data;

      if (tf_context->tensors[node->inputs->data[1]].type != kTfLiteInt32)
         return false;

      if (paddings[0] != 0 ||
          paddings[1] != 0)
         return false;

      operation->type = PIPE_ML_OPERATION_TYPE_PAD;
      operation->pad.before_x = paddings[2];
      operation->pad.after_x = paddings[3];
      operation->pad.before_y = paddings[4];
      operation->pad.after_y = paddings[5];
      operation->pad.before_z = paddings[6];
      operation->pad.after_z = paddings[7];
      break;
   }
   case kTfLiteBuiltinFullyConnected: {
      if (tf_context->tensors[node->inputs->data[0]].type != kTfLiteInt8 &&
          tf_context->tensors[node->inputs->data[0]].type != kTfLiteUInt8)
         return false;

      operation->type = PIPE_ML_OPERATION_TYPE_FULLY_CONNECTED;
      operation->fcon.weight_tensor = &tensors[node->inputs->data[1]];
      operation->fcon.bias_tensor = &tensors[node->inputs->data[2]];
      break;
   }
   case kTfLiteBuiltinReshape: {
      int32_t *shape = tf_context->tensors[node->inputs->data[1]].data.data;

      operation->type = PIPE_ML_OPERATION_TYPE_RESHAPE;
      memcpy(operation->reshape.shape, shape, 4 * sizeof(*operation->reshape.shape));
      break;
   }
   case kTfLiteBuiltinRelu:
      operation->type = PIPE_ML_OPERATION_TYPE_RELU;
      break;
   case kTfLiteBuiltinAbs:
      operation->type = PIPE_ML_OPERATION_TYPE_ABSOLUTE;
      break;
   case kTfLiteBuiltinLogistic:
      operation->type = PIPE_ML_OPERATION_TYPE_LOGISTIC;
      break;
   case kTfLiteBuiltinSub:
      operation->type = PIPE_ML_OPERATION_TYPE_SUBTRACT;
      break;
   case kTfLiteBuiltinTranspose: {
      int32_t *perm = tf_context->tensors[node->inputs->data[1]].data.data;

      operation->type = PIPE_ML_OPERATION_TYPE_TRANSPOSE;
      memcpy(operation->transpose.perm, perm, 4 * sizeof(*operation->transpose.perm));
      break;
   }
   case kTfLiteBuiltinStridedSlice: {
      int *begin = tf_context->tensors[node->inputs->data[1]].data.data;
      int *end = tf_context->tensors[node->inputs->data[2]].data.data;
      int *strides = tf_context->tensors[node->inputs->data[3]].data.data;

      operation->type = PIPE_ML_OPERATION_TYPE_STRIDED_SLICE;
      memcpy(operation->slice.begin, begin, sizeof(operation->slice.begin));
      memcpy(operation->slice.end, end, sizeof(operation->slice.end));
      memcpy(operation->slice.strides, strides, sizeof(operation->slice.strides));

      break;
   }
   case kTfLiteBuiltinResizeNearestNeighbor: {
      operation->type = PIPE_ML_OPERATION_TYPE_RESIZE;
      break;
   }
   default:
      return false;
   }

   return true;
}

static bool
all_scales_equal(const TfLiteAffineQuantization *quant)
{
   float scale = quant->scale->data[0];
   int i;

   for (i = 1; i < quant->scale->size; i++) {
      if (quant->scale->data[i] != scale)
         return false;
   }

   return true;
}

static bool
all_zero_points_equal(const TfLiteAffineQuantization *quant)
{
   int zero_point = quant->zero_point->data[0];
   int i;

   for (i = 1; i < quant->zero_point->size; i++) {
      if (quant->zero_point->data[i] != zero_point)
         return false;
   }

   return true;
}

static void
fill_tensor(struct teflon_delegate *delegate, TfLiteContext *tf_context, struct pipe_tensor *tensor, unsigned index)
{
   struct pipe_context *context = delegate->context;
   TfLiteTensor tf_tensor = tf_context->tensors[index];

   if (tf_tensor.type == kTfLiteNoType)
      return; /* Placeholder tensor */

   if (tf_tensor.data.data)
      tensor->resource = create_resource(context, tf_tensor);

   tensor->index = index;
   for (int out_dim = 0; out_dim < 4; out_dim++) {
      int in_dim = tf_tensor.dims->size - 4 + out_dim;
      if (in_dim >= 0)
         tensor->dims[out_dim] = tf_tensor.dims->data[in_dim];
      else
         tensor->dims[out_dim] = 1;
   }

   if (tf_tensor.quantization.type == kTfLiteAffineQuantization) {
      const TfLiteAffineQuantization *quant = (const TfLiteAffineQuantization *)tf_tensor.quantization.params;
      tensor->scale = quant->scale->data[0];
      tensor->zero_point = quant->zero_point->data[0];

      assert(quant->scale->size == quant->zero_point->size);
      if (quant->scale->size > 1 &&
          (!all_scales_equal(quant) || !all_zero_points_equal(quant))) {
         tensor->scales = calloc(quant->scale->size, sizeof(*tensor->scales));
         memcpy(tensor->scales, quant->scale->data, quant->scale->size * sizeof(*tensor->scales));

         tensor->zero_points = calloc(quant->zero_point->size, sizeof(*tensor->zero_points));
         memcpy(tensor->zero_points, quant->zero_point->data, quant->zero_point->size * sizeof(*tensor->zero_points));
      }
   }

   switch (tf_tensor.type) {
   case kTfLiteInt8:
   case kTfLiteInt16:
   case kTfLiteInt32:
   case kTfLiteInt64:
      tensor->is_signed = true;
      break;
   default:
      tensor->is_signed = false;
   }
}

static void
dump_graph(struct pipe_tensor *tensors, unsigned tensor_count, struct pipe_ml_operation *operations, unsigned operation_count)
{
   teflon_debug("\n");
   teflon_debug("teflon: compiling graph: %d tensors %d operations\n",
                tensor_count, operation_count);

   teflon_debug("%3s %-8s %3s %s %-12s\n", "idx", "scale", "zp", "has_data", "size");
   teflon_debug("=======================================\n");
   for (int i = 0; i < tensor_count; i++) {
      teflon_debug("%3d %6f %3x %-8s %dx%dx%dx%d\n",
                   tensors[i].index,
                   tensors[i].scale,
                   tensors[i].zero_point,
                   tensors[i].resource == NULL ? "no" : "yes",
                   tensors[i].dims[0], tensors[i].dims[1], tensors[i].dims[2], tensors[i].dims[3]);
   }

   teflon_debug("\n");
   teflon_debug("%3s %-6s %25s %25s  %s\n", "idx", "type", "inputs", "outputs", "operation type-specific");
   teflon_debug("================================================================================================\n");
   for (int i = 0; i < operation_count; i++) {
      teflon_debug("%3d ", i);

      switch (operations[i].type) {
      case PIPE_ML_OPERATION_TYPE_ADD:
         teflon_debug("%-6s ", "ADD");
         break;
      case PIPE_ML_OPERATION_TYPE_CONVOLUTION:
         teflon_debug("%-6s ", operations[i].conv.depthwise ? "DWCONV" : "CONV");
         break;
      case PIPE_ML_OPERATION_TYPE_CONCATENATION:
         teflon_debug("%-6s ", "CONCAT");
         break;
      case PIPE_ML_OPERATION_TYPE_POOLING:
         teflon_debug("%-6s ", "POOL");
         break;
      case PIPE_ML_OPERATION_TYPE_SPLIT:
         teflon_debug("%-6s ", "SPLIT");
         break;
      case PIPE_ML_OPERATION_TYPE_PAD:
         teflon_debug("%-6s ", "PAD");
         break;
      case PIPE_ML_OPERATION_TYPE_FULLY_CONNECTED:
         teflon_debug("%-6s ", "FCON");
         break;
      case PIPE_ML_OPERATION_TYPE_RESHAPE:
         teflon_debug("%-6s ", "RESHAPE");
         break;
      case PIPE_ML_OPERATION_TYPE_RELU:
         teflon_debug("%-6s ", "RELU");
         break;
      case PIPE_ML_OPERATION_TYPE_ABSOLUTE:
         teflon_debug("%-6s ", "ABS");
         break;
      case PIPE_ML_OPERATION_TYPE_LOGISTIC:
         teflon_debug("%-6s ", "LOG");
         break;
      case PIPE_ML_OPERATION_TYPE_SUBTRACT:
         teflon_debug("%-6s ", "SUB");
         break;
      case PIPE_ML_OPERATION_TYPE_TRANSPOSE:
         teflon_debug("%-6s ", "TRANSPOSE");
         break;
      case PIPE_ML_OPERATION_TYPE_STRIDED_SLICE:
         teflon_debug("%-6s ", "STRIDED_SLICE");
         break;
      case PIPE_ML_OPERATION_TYPE_RESIZE:
         teflon_debug("%-6s ", "RESIZE");
         break;
      }

      for (unsigned j = 0; j < operations[i].input_count; j++) {
         teflon_debug("%d", operations[i].input_tensors[j]->index);
         if (j < operations[i].input_count - 1)
            teflon_debug(",");
      }

      teflon_debug(" ");

      for (unsigned j = 0; j < operations[i].output_count; j++) {
         teflon_debug("%d", operations[i].output_tensors[j]->index);
         if (j < operations[i].output_count - 1)
            teflon_debug(",");
      }

      teflon_debug("\n");
   }
   teflon_debug("\n");
}

static void
free_operation(struct pipe_ml_operation *operation)
{
   free(operation->input_tensors);
   free(operation->output_tensors);
}

static void *
partition_init(TfLiteContext *tf_context, const char *buffer, size_t length)
{
   const TfLiteDelegateParams *params = (const TfLiteDelegateParams *)buffer;
   struct teflon_delegate *delegate = (struct teflon_delegate *)params->delegate;
   struct pipe_context *context = delegate->context;
   struct pipe_ml_operation operations[params->nodes_to_replace->size];
   long start = 0, end = 0;

   memset(operations, 0, sizeof(operations));

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      start = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
   }

   for (int i = 0; i < params->nodes_to_replace->size; i++) {
      const int node_index = params->nodes_to_replace->data[i];
      TfLiteNode *delegated_node = NULL;
      TfLiteRegistration *delegated_node_registration = NULL;
      tf_context->GetNodeAndRegistration(tf_context, node_index, &delegated_node,
                                         &delegated_node_registration);

      bool ret = fill_operation(delegate, tf_context, delegated_node, delegated_node_registration, &operations[i]);
      assert(ret);
   }

   if (debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)
      dump_graph(delegate->tensors, tf_context->tensors_size, operations, params->nodes_to_replace->size);

   struct pipe_ml_subgraph *subgraph;
   subgraph = context->ml_subgraph_create(context,
                                          operations,
                                          params->nodes_to_replace->size);

   struct teflon_subgraph *tsubgraph = calloc(1, sizeof(*tsubgraph));
   tsubgraph->base = subgraph;

   tsubgraph->input_tensors = malloc(params->input_tensors->size * sizeof(*tsubgraph->input_tensors));
   for (int i = 0; i < params->input_tensors->size; i++) {
      unsigned tensor_idx = params->input_tensors->data[i];
      TfLiteTensor *tensor = &tf_context->tensors[tensor_idx];
      if (tensor->allocation_type == kTfLiteMmapRo)
         continue;
      tsubgraph->input_tensors[tsubgraph->input_count] = tensor_idx;
      tsubgraph->input_count++;
   }

   tsubgraph->output_count = params->output_tensors->size;
   tsubgraph->output_tensors = malloc(params->output_tensors->size * sizeof(*tsubgraph->output_tensors));
   memcpy(tsubgraph->output_tensors, params->output_tensors->data,
          params->output_tensors->size * sizeof(*tsubgraph->output_tensors));

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      end = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
      teflon_debug("teflon: compiled graph, took %ld ms\n", (end - start));
   }

   for (int i = 0; i < params->nodes_to_replace->size; i++) {
      free_operation(&operations[i]);
   }

   return tsubgraph;
}

static TfLiteStatus
partition_prepare(TfLiteContext *context, TfLiteNode *node)
{
   // TODO: If input size has changed, resize input, intermediate and output buffers

   return kTfLiteOk;
}

// De-allocates the per-node-and-Interpreter custom data.
static void
partition_free(TfLiteContext *tf_context, void *buffer)
{
   struct teflon_subgraph *tsubgraph = (struct teflon_subgraph *)buffer;
   struct pipe_ml_subgraph *subgraph = tsubgraph->base;
   struct pipe_context *context = subgraph->context;

   context->ml_subgraph_destroy(context, subgraph);
   free(tsubgraph->input_tensors);
   free(tsubgraph->output_tensors);
   free(tsubgraph);
}

static TfLiteStatus
partition_invoke(TfLiteContext *tf_context, TfLiteNode *node)
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)node->delegate;
   struct teflon_subgraph *tsubgraph = (struct teflon_subgraph *)node->user_data;
   struct pipe_ml_subgraph *subgraph = tsubgraph->base;
   struct pipe_context *context = delegate->context;
   long start = 0, end = 0;

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      start = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
   }

   void **buffers = malloc(tsubgraph->input_count * sizeof(*buffers));
   bool *is_signed = malloc(tsubgraph->input_count * sizeof(*is_signed));
   for (unsigned i = 0; i < tsubgraph->input_count; i++) {
      TfLiteTensor tf_tensor = tf_context->tensors[tsubgraph->input_tensors[i]];

      buffers[i] = tf_tensor.data.data;
      is_signed[i] = tf_tensor.type == kTfLiteInt8 ||
                     tf_tensor.type == kTfLiteInt16 ||
                     tf_tensor.type == kTfLiteInt32 ||
                     tf_tensor.type == kTfLiteInt64;
   }
   context->ml_subgraph_invoke(context, subgraph, tsubgraph->input_count, tsubgraph->input_tensors, buffers, is_signed);
   free(buffers);
   free(is_signed);

   buffers = malloc(tsubgraph->output_count * sizeof(*buffers));
   is_signed = malloc(tsubgraph->output_count * sizeof(*is_signed));
   for (unsigned i = 0; i < tsubgraph->output_count; i++) {
      TfLiteTensor tf_tensor = tf_context->tensors[tsubgraph->output_tensors[i]];

      buffers[i] = tf_tensor.data.data;
      is_signed[i] = tf_tensor.type == kTfLiteInt8 ||
                     tf_tensor.type == kTfLiteInt16 ||
                     tf_tensor.type == kTfLiteInt32 ||
                     tf_tensor.type == kTfLiteInt64;
   }
   context->ml_subgraph_read_output(context, subgraph, tsubgraph->output_count, tsubgraph->output_tensors, buffers, is_signed);
   free(buffers);
   free(is_signed);

   if (unlikely(debug_get_option_debug_teflon() & TEFLON_DEBUG_VERBOSE)) {
      struct timespec time;
      clock_gettime(CLOCK_MONOTONIC, &time);
      end = (long)time.tv_sec * 1000 + (long)time.tv_nsec / 1000000;
      teflon_debug("teflon: invoked graph, took %ld ms\n", (end - start));
   }

   return kTfLiteOk;
}

static const char *
tflite_builtin_op_name(TfLiteBuiltinOperator op)
{
   switch (op) {
   case kTfLiteBuiltinAdd:
      return "ADD";
   case kTfLiteBuiltinAveragePool2d:
      return "AVGPOOL";
   case kTfLiteBuiltinMaxPool2d:
      return "MAXPOOL";
   case kTfLiteBuiltinConv2d:
      return "CONV";
   case kTfLiteBuiltinDepthwiseConv2d:
      return "DWCONV";
   case kTfLiteBuiltinDequantize:
      return "DEQUANT";
   case kTfLiteBuiltinHardSwish:
      return "HSWISH";
   case kTfLiteBuiltinMul:
      return "MUL";
   case kTfLiteBuiltinPad:
      return "PAD";
   case kTfLiteBuiltinQuantize:
      return "QUANT";
   case kTfLiteBuiltinReshape:
      return "RESHAPE";
   case kTfLiteBuiltinSoftmax:
      return "SOFTMAX";
   case kTfLiteBuiltinSqueeze:
      return "SQUEEZE";
   case kTfLiteBuiltinFullyConnected:
      return "FC";
   case kTfLiteBuiltinMean:
      return "MEAN";
   case kTfLiteBuiltinStridedSlice:
      return "STRIDED_SLICE";
   case kTfLiteBuiltinResizeNearestNeighbor:
      return "RESIZE";
   default:
      return "unknown";
   }
}

static const char *
tflite_type_name(TfLiteType type)
{
   switch (type) {
   case kTfLiteNoType:
      return "no";
   case kTfLiteFloat32:
      return "f32";
   case kTfLiteUInt16:
      return "u16";
   case kTfLiteInt16:
      return "i16";
   case kTfLiteUInt32:
      return "u32";
   case kTfLiteInt32:
      return "i32";
   case kTfLiteUInt8:
      return "u8";
   case kTfLiteInt8:
      return "i8";
   default:
      return "??";
   }
}

static const char *
tflite_fused_activation_name(TfLiteFusedActivation activation)
{
   switch (activation) {
   case kTfLiteActRelu:
      return "ReLU";
   case kTfLiteActRelu6:
      return "ReLU6";
   default:
      return "unknown";
   }
}

static bool
fused_relu6_supported(TfLiteTensor *tensor)
{
   TfLiteAffineQuantization *affine;
   int quantized_max;

   switch (tensor->type) {
   case kTfLiteInt8:
      quantized_max = INT8_MAX;
      break;
   case kTfLiteUInt8:
      quantized_max = UINT8_MAX;
      break;
   default:
      return false;
   }

   assert(tensor->quantization.type == kTfLiteAffineQuantization);
   affine = (TfLiteAffineQuantization *)tensor->quantization.params;

   assert(affine->scale->size == affine->zero_point->size);
   for (int i = 0; i < affine->zero_point->size; i++) {
      if ((quantized_max - affine->zero_point->data[i]) * affine->scale->data[i] > 6.0f)
         return false;
   }
   return true;
}

static bool
check_op_support(TfLiteDelegate *tf_delegate, TfLiteContext *tf_context, TfLiteNode *node, TfLiteRegistration *registration)
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)tf_delegate;
   struct pipe_context *context = delegate->context;
   struct pipe_ml_operation operation = {0};
   bool supported = false;

   if (!fill_operation(delegate, tf_context, node, registration, &operation))
      return false;

   supported = context->ml_operation_supported(context, &operation);

   free_operation(&operation);

   return supported;
}

static TfLiteStatus
PrepareDelegate(TfLiteContext *tf_context, TfLiteDelegate *tf_delegate)
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)tf_delegate;
   TfLiteIntArray *plan;
   TfLiteNode *node;
   TF_LITE_ENSURE_STATUS(tf_context->GetExecutionPlan(tf_context, &plan));

   delegate->tensors = calloc(tf_context->tensors_size, sizeof(*delegate->tensors));

   for (int i = 0; i < tf_context->tensors_size; i++)
      fill_tensor(delegate, tf_context, &delegate->tensors[i], i);

   teflon_debug("%3s %7s %3s %-11s %s\n", "idx", "type", "ver", "support", "inputs");
   teflon_debug("================================================================================================\n");

   // Get a list of supported nodes.
   TfLiteIntArray *supported_nodes = malloc(plan->size * sizeof(int) + sizeof(*supported_nodes));
   supported_nodes->size = plan->size;
   unsigned node_count = 0;
   for (int i = 0; i < plan->size; i++) {
      int node_index = plan->data[i];
      bool supported = false;
      TfLiteRegistration *registration;
      TF_LITE_ENSURE_STATUS(tf_context->GetNodeAndRegistration(
         tf_context, node_index, &node, &registration));

      supported = check_op_support(tf_delegate, tf_context, node, registration);

      teflon_debug("%3d %7s v%-2d %-11s in:", node_index,
                   tflite_builtin_op_name(registration->builtin_code),
                   registration->version,
                   supported ? "supported" : "unsupported");
      for (int j = 0; j < node->inputs->size; j++) {
         teflon_debug(" %d(%s)", node->inputs->data[j],
                      tflite_type_name(tf_context->tensors[node->inputs->data[j]].type));
      }
      teflon_debug(" out:");
      for (int j = 0; j < node->outputs->size; j++) {
         teflon_debug(" %d(%s)", node->outputs->data[j],
                      tflite_type_name(tf_context->tensors[node->outputs->data[j]].type));
      }
      if (registration->builtin_code == kTfLiteBuiltinConv2d) {
         TfLiteConvParams *params = (TfLiteConvParams *)node->builtin_data;
         if (params->activation != kTfLiteActNone) {
            teflon_debug(" %s", tflite_fused_activation_name(params->activation));
         }
         if (registration->version >= 2 &&
             (params->dilation_width_factor > 1 || params->dilation_height_factor > 1)) {
            teflon_debug(" dil: %dx%d", params->dilation_width_factor, params->dilation_height_factor);
         }
      }
      if (registration->builtin_code == kTfLiteBuiltinDepthwiseConv2d) {
         TfLiteDepthwiseConvParams *params = (TfLiteDepthwiseConvParams *)node->builtin_data;
         if (params->activation != kTfLiteActNone) {
            teflon_debug(" %s", tflite_fused_activation_name(params->activation));
         }
         if (registration->version >= 2 &&
             (params->dilation_width_factor > 1 || params->dilation_height_factor > 1)) {
            teflon_debug(" dil: %dx%d", params->dilation_width_factor, params->dilation_height_factor);
         }
      }
      teflon_debug("\n");

      if (supported)
         supported_nodes->data[node_count++] = node_index;
   }
   supported_nodes->size = node_count;

   TfLiteRegistration registration;

   registration.init = partition_init;
   registration.free = partition_free;
   registration.prepare = partition_prepare;
   registration.invoke = partition_invoke;

   registration.profiling_string = NULL;
   registration.builtin_code = kTfLiteBuiltinDelegate;
   registration.version = 1;
   registration.registration_external = NULL;
   registration.custom_name = "Teflon Delegate";

   // Replace supported subgraphs.
   TfLiteStatus status = tf_context->ReplaceNodeSubsetsWithDelegateKernels(
      tf_context,
      registration,
      supported_nodes,
      tf_delegate);

   free(supported_nodes);

   return status;
}

static TfLiteStatus
CopyFromBufferHandle(TfLiteContext *context,
                     TfLiteDelegate *delegate,
                     TfLiteBufferHandle buffer_handle,
                     TfLiteTensor *tensor)
{
   return kTfLiteOk;
}

static void
FreeBufferHandle(TfLiteContext *context,
                 TfLiteDelegate *delegate,
                 TfLiteBufferHandle *handle)
{
}

TfLiteDelegate *tflite_plugin_create_delegate(char **options_keys,
                                              char **options_values,
                                              size_t num_options,
                                              void (*report_error)(const char *));

void tflite_plugin_destroy_delegate(TfLiteDelegate *delegate);

static struct pipe_loader_device *
find_accel_device()
{
   struct pipe_loader_device *device = NULL;
   struct pipe_loader_device **devs;

   int n = pipe_loader_accel_probe(NULL, 0);
   devs = (struct pipe_loader_device **)malloc(sizeof(*devs) * n);
   pipe_loader_accel_probe(devs, n);

   for (int i = 0; i < n; i++) {
      if (strstr("rocket", devs[i]->driver_name) ||
          strstr("ethosu", devs[i]->driver_name))
         device = devs[i];
      else
         pipe_loader_release(&devs[i], 1);
   }
   free(devs);

   return device;
}

static struct pipe_loader_device *
find_drm_device()
{
   struct pipe_loader_device *device = NULL;
   struct pipe_loader_device **devs;

   int n = pipe_loader_probe(NULL, 0, false);
   devs = (struct pipe_loader_device **)malloc(sizeof(*devs) * n);
   pipe_loader_probe(devs, n, false);

   for (int i = 0; i < n; i++) {
      if (strstr("etnaviv", devs[i]->driver_name))
         device = devs[i];
      else
         pipe_loader_release(&devs[i], 1);
   }
   free(devs);

   return device;
}

__attribute__((visibility("default"))) TfLiteDelegate *
tflite_plugin_create_delegate(char **options_keys,
                              char **options_values,
                              size_t num_options,
                              void (*report_error)(const char *))
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)calloc(1, sizeof(*delegate));
   struct pipe_screen *screen;
   struct pipe_loader_device **devs;

   delegate->base.flags = kTfLiteDelegateFlagsAllowDynamicTensors | kTfLiteDelegateFlagsRequirePropagatedShapes;
   delegate->base.Prepare = &PrepareDelegate;
   delegate->base.CopyFromBufferHandle = &CopyFromBufferHandle;
   delegate->base.FreeBufferHandle = &FreeBufferHandle;

   int n = pipe_loader_probe(NULL, 0, false);
   devs = (struct pipe_loader_device **)malloc(sizeof(*devs) * n);
   pipe_loader_probe(devs, n, false);

   delegate->dev = find_accel_device();
   if (delegate->dev == NULL)
      delegate->dev = find_drm_device();

   if (delegate->dev == NULL) {
      fprintf(stderr, "Couldn't open kernel device\n");
      return NULL;
   }

   teflon_debug("Teflon delegate: loaded %s driver\n", delegate->dev->driver_name);

   screen = pipe_loader_create_screen(delegate->dev, false);
   delegate->context = screen->context_create(screen, NULL, PIPE_CONTEXT_COMPUTE_ONLY);

   return &delegate->base;
}

__attribute__((visibility("default"))) void
tflite_plugin_destroy_delegate(TfLiteDelegate *tf_delegate)
{
   struct teflon_delegate *delegate = (struct teflon_delegate *)tf_delegate;
   struct pipe_screen *screen;

   if (tf_delegate == NULL) {
      fprintf(stderr, "tflite_plugin_destroy_delegate: NULL delegate!\n");
      return;
   }

   for (int i = 0; i < delegate->tensor_count; i++) {
      free(delegate->tensors[i].scales);
      free(delegate->tensors[i].zero_points);
      pipe_resource_reference(&delegate->tensors[i].resource, NULL);
   }
   free(delegate->tensors);

   screen = delegate->context->screen;
   delegate->context->destroy(delegate->context);
   screen->destroy(screen);
   pipe_loader_release(&delegate->dev, 1);
   free(delegate);
}
