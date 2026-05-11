/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "util/u_inlines.h"

#include "ethosu_device.h"
#include "ethosu_lower.h"
#include "ethosu_coefs.h"
#include "ethosu_ml.h"
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
needed_total_padding(int input_size, int stride, int filter_size)
{
   if (input_size % stride == 0)
      return MAX2(filter_size - stride, 0);

   return MAX2(filter_size - (input_size % stride), 0);
}

static void
set_feature_map_strides(struct ethosu_feature_map *fm, bool is_nhcwb16)
{
   unsigned elem_size = 1;

   if (is_nhcwb16) {
      fm->stride.x = 16 * elem_size;
      fm->stride.c = fm->stride.x * fm->shape.width;
      fm->stride.y = elem_size * fm->shape.width * align(fm->shape.depth, 16);
   } else {
      fm->stride.c = elem_size;
      fm->stride.x = fm->shape.depth * fm->stride.c;
      fm->stride.y = fm->shape.width * fm->stride.x;
   }
}

static void
set_feature_map(struct ethosu_subgraph *subgraph,
                struct pipe_tensor *tensor,
                struct ethosu_feature_map *fm)
{
   fm->tensor = ethosu_find_tensor(subgraph, tensor->index);
   fm->shape.height = tensor->dims[1];
   fm->shape.width = tensor->dims[2];
   fm->shape.depth = tensor->dims[3];
   fm->zero_point = tensor->zero_point;
   fm->scale = tensor->scale;
   fm->is_signed = tensor->is_signed;
   fm->precision = log2(tensor->type_size);

   set_feature_map_strides(fm, fm->tensor->layout == ETHOSU_LAYOUT_NHCWB16);
}

static void
set_feature_maps(struct ethosu_subgraph *subgraph,
                 struct pipe_tensor *input_tensor,
                 struct pipe_tensor *output_tensor,
                 struct ethosu_operation *operation)
{
   set_feature_map(subgraph, input_tensor, &operation->ifm);
   set_feature_map(subgraph, output_tensor, &operation->ofm);
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

static unsigned
ethosu_allocate_feature_map(struct ethosu_subgraph *subgraph, struct ethosu_tensor *tensor)
{
   unsigned size;

   if (tensor->layout == ETHOSU_LAYOUT_NHWC) {
      size = tensor->shape.width * tensor->shape.height * tensor->shape.depth;
   } else if (tensor->layout == ETHOSU_LAYOUT_NHCWB16) {
      size = tensor->shape.width * tensor->shape.height * align(tensor->shape.depth, 16);
   } else {
      assert(0 && "Unsupported layout");
      size = 0; // This should never happen
   }
   size *= tensor->type_size;

   assert(tensor);

   if (tensor->size > 0)
      return tensor->offset;

   tensor->offset = subgraph->io_used;
   tensor->size = size;
   subgraph->io_used += ALIGN_POT(size, 16);

   return tensor->offset;
}

static void
allocate_feature_maps(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   operation->ofm.tiles.addresses[0] = ethosu_allocate_feature_map(subgraph, operation->ofm.tensor);
   operation->ofm.tiles.height_0 = operation->ofm.shape.height;
   operation->ofm.tiles.height_1 = operation->ofm.shape.height;
   operation->ofm.tiles.width_0 = operation->ofm.shape.width;

   operation->ifm.tiles.addresses[0] = ethosu_allocate_feature_map(subgraph, operation->ifm.tensor);
   operation->ifm.tiles.height_0 = operation->ifm.shape.height;
   operation->ifm.tiles.height_1 = operation->ifm.shape.height;
   operation->ifm.tiles.width_0 = operation->ifm.shape.width;
}

static void
operation_set_defaults(struct ethosu_operation *operation)
{
   memset(operation, 0, sizeof(*operation));

   operation->kernel.height = 1;
   operation->kernel.width = 1;
   operation->kernel.stride_y = 1;
   operation->kernel.stride_x = 1;
   operation->kernel.dilation_y = 1;
   operation->kernel.dilation_x = 1;
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
lower_conv_common(struct ethosu_subgraph *subgraph,
                  const struct pipe_ml_operation *poperation,
                  struct pipe_tensor *input_tensor,
                  struct pipe_tensor *weight,
                  int32_t *bias_data,
                  struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_CONVOLUTION;

   set_feature_maps(subgraph, input_tensor, poperation->output_tensors[0], operation);

   /* Per-channel quantization support */
   unsigned num_channels = poperation->output_tensors[0]->dims[3];

   if (weight->scales != NULL) {
      operation->kernel.scales = malloc(num_channels * sizeof(float));
      memcpy(operation->kernel.scales, weight->scales, num_channels * sizeof(float));
   }

   if (weight->zero_points != NULL) {
      operation->kernel.zero_points = malloc(num_channels * sizeof(int));
      memcpy(operation->kernel.zero_points, weight->zero_points, num_channels * sizeof(int));
   }

   allocate_feature_maps(subgraph, operation);

   ethosu_sched_operation(subgraph, operation);
   fill_coefs(subgraph, operation, bias_data, weight->data,
              weight->dims[0] * weight->dims[1] * weight->dims[2] * weight->dims[3]);
}

static void
ethosu_lower_fully_connected(struct ethosu_subgraph *subgraph,
                             const struct pipe_ml_operation *poperation,
                             struct pipe_tensor *input_tensor,
                             struct ethosu_operation *operation)
{
   operation->kernel.scale = poperation->fcon.weight_tensor->scale;
   operation->kernel.zero_point = poperation->fcon.weight_tensor->zero_point;
   operation->kernel.is_signed = poperation->fcon.weight_tensor->is_signed;

   lower_conv_common(subgraph, poperation, input_tensor,
                     poperation->fcon.weight_tensor,
                     (int32_t *)poperation->fcon.bias_tensor->data,
                     operation);
}

static void
ethosu_lower_convolution(struct ethosu_subgraph *subgraph,
                         const struct pipe_ml_operation *poperation,
                         struct pipe_tensor *input_tensor,
                         struct ethosu_operation *operation)
{
   int32_t *bias_data = poperation->conv.bias_tensor ? (int32_t *)poperation->conv.bias_tensor->data : NULL;

   operation->conv.depthwise = is_depthwise(poperation);

   operation->kernel.height = poperation->conv.weight_tensor->dims[1];
   operation->kernel.width = poperation->conv.weight_tensor->dims[2];
   operation->kernel.stride_y = poperation->conv.stride_y;
   operation->kernel.stride_x = poperation->conv.stride_x;
   operation->kernel.depthwise = is_depthwise(poperation);
   operation->kernel.scale = poperation->conv.weight_tensor->scale;
   operation->kernel.zero_point = poperation->conv.weight_tensor->zero_point;
   operation->kernel.is_signed = poperation->conv.weight_tensor->is_signed;

   operation->pad.top = poperation->conv.padding_top;
   operation->pad.bottom = poperation->conv.padding_bottom;
   operation->pad.left = poperation->conv.padding_left;
   operation->pad.right = poperation->conv.padding_right;

   lower_conv_common(subgraph, poperation, input_tensor,
                     poperation->conv.weight_tensor,
                     (int32_t *)bias_data,
                     operation);
}

static void
ethosu_lower_pooling(struct ethosu_subgraph *subgraph,
                     const struct pipe_ml_operation *poperation,
                     struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;

   switch (poperation->pooling.type) {
   case PIPE_ML_POOLING_TYPE_MAX:
      operation->pooling.type = ETHOSU_POOLING_TYPE_MAX;
      break;
   case PIPE_ML_POOLING_TYPE_AVG:
      if (ethosu_ml_device(subgraph->base.device)->is_u65 ||
          ((poperation->pooling.filter_height <= 8) &&
           (poperation->pooling.filter_width <= 8)))
         operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;
      else
         operation->pooling.type = ETHOSU_POOLING_TYPE_SUM;
      break;
   default:
      assert(0 && "Unsupported pooling type");
   }

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);

   operation->kernel.height = poperation->pooling.filter_height;
   operation->kernel.width = poperation->pooling.filter_width;
   operation->kernel.stride_y = poperation->pooling.stride_y;
   operation->kernel.stride_x = poperation->pooling.stride_x;

   operation->pad.top = poperation->pooling.padding_top;
   operation->pad.bottom = poperation->pooling.padding_bottom;
   operation->pad.left = poperation->pooling.padding_left;
   operation->pad.right = poperation->pooling.padding_right;

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static double
clamp_sigmoid8(double x)
{
   if (x <= -8.0)
      return 0.0;
   else if (x >= 8.0)
      return 1.0;
   else
      return (1.0 / (1.0 + exp(-x)));
}

static void
ethos_create_lut(struct ethosu_operation *operation, uint8_t *lut, double (*func)(double))
{
   double ifm_scale = operation->ifm.scale;
   double ofm_scale = operation->ofm.scale;
   int zpIn = operation->ifm.zero_point;
   int zpOut = operation->ofm.zero_point;

   int qMin = operation->ifm.is_signed ? -128 : 0;
   int qMax = operation->ifm.is_signed ? 127 : 255;

   for (int x = qMin; x <= qMax; ++x, lut++) {
      double xReal = ifm_scale * (double)(x - zpIn);
      double yReal = func(xReal);
      int lutVal = (int)round((double)zpOut + yReal / ofm_scale);
      lutVal = MIN2(qMax, MAX2(qMin, lutVal));
      *lut = lutVal;
   }
}

// Implementation from Vela and TensorFlow Lite Micro kernel
static int16_t
saturating_left_shift_16(int16_t value, int amount)
{
   int32_t result = value << amount;
   return CLAMP(result, INT16_MIN, INT16_MAX);
}

// Implementation from Vela and TensorFlow Lite Micro kernel
// Similar to ARM instruction SQDMULH.
// Similar to gemmlowp::SaturatingRoundingDoublingHighMul except
// rounding to zero instead of to nearest (SQRDMULH).
static int16_t
saturating_doubling_high_mul_16(int16_t a, int16_t b)
{
   bool overflow = a == b && a == INT16_MIN;
   int32_t a_32 = a;
   int32_t b_32 = b;
   int32_t ab_32 = a_32 * b_32;
   int16_t ab_x2_high16 = (int16_t)(ab_32 / (1 << 15));
   return overflow ? INT16_MAX : ab_x2_high16;
}

static int16_t
saturating_rounding_doubling_high_mul_16(int16_t a, int16_t b)
{
   bool overflow = a == b && a == INT16_MIN;
   int32_t a_32 = a;
   int32_t b_32 = b;
   int32_t ab_32 = a_32 * b_32;
   int16_t nudge = ab_32 >= 0 ? (1 << 14) : (1 - (1 << 14));
   int16_t ab_x2_high16 = ((ab_32 + nudge) / (1 << 15));
   return overflow ? INT16_MAX : ab_x2_high16;
}

static int16_t
rounding_divide_by_pow2_16(int16_t x, int exponent)
{
   const int16_t mask = (1 << exponent) - 1;
   const int16_t remainder = x & mask;
   const int16_t threshold = (mask >> 1) + ((x < 0) ? 1 : 0);
   return (x >> exponent) + ((remainder > threshold) ? 1 : 0);
}

static int16_t
downscale_int32_to_int16_multiplier(int32_t multiplier)
{
   return CLAMP(((multiplier / 32768) + 1) / 2, INT16_MIN, INT16_MAX);
}

static void
ethos_create_hswish_lut(struct ethosu_operation *operation, uint8_t *lut)
{
   const double ifm_scale = operation->ifm.scale;
   const double ofm_scale = operation->ofm.scale;
   const unsigned zpIn = operation->ifm.zero_point;
   const unsigned zpOut = operation->ofm.zero_point;

   const int qMin = operation->ifm.is_signed ? -128 : 0;
   const int qMax = operation->ifm.is_signed ? 127 : 255;

   const double ifmScaleHires = (1.0 / 128.0) * ifm_scale;
   const double reluMultiplier = 3.0 / 32768.0;

   int32_t out_shift;
   int32_t relu_shift;
   int32_t out_scale = ethosu_quantize_scale(ifmScaleHires / ofm_scale, &out_shift, false);
   int32_t relu_scale = ethosu_quantize_scale(ifmScaleHires / reluMultiplier, &relu_shift, false);
   int16_t outScale16 = downscale_int32_to_int16_multiplier(out_scale);
   int16_t reluScale16 = downscale_int32_to_int16_multiplier(relu_scale);
   // convert to left shift-positive notation
   int outShift = 31 - out_shift;
   int reluShift = 31 - relu_shift;

   for (int x = qMin; x <= qMax; ++x, lut++) {
      const int16_t inputValue = (int16_t)(x - zpIn);
      const int16_t inputValueOnHiresInputScale = (int16_t)(inputValue << 7);
      const int16_t inputValueOnPreshiftOutputScale = saturating_rounding_doubling_high_mul_16(inputValueOnHiresInputScale, outScale16);
      int16_t reluValue = inputValueOnHiresInputScale;

      if (reluShift > 0)
         reluValue = saturating_left_shift_16(reluValue, reluShift - 1);

      reluValue = saturating_rounding_doubling_high_mul_16(reluValue, reluScale16);

      if (reluShift > 0)
         reluValue = saturating_left_shift_16(reluValue, 1);

      // Try to get reluShift into the [-31, 0] range
      if (reluShift < -31) {
         reluValue = reluValue >> (-31 - reluShift);
         reluShift = -31;
      }

      if (reluShift < 0)
         reluValue = rounding_divide_by_pow2_16(reluValue, -reluShift);

      reluValue = (int16_t)((reluValue + (1 << 15)) >> 1);

      const int16_t preshiftOutputValue = saturating_doubling_high_mul_16(reluValue, inputValueOnPreshiftOutputScale);

      int16_t outputValue = rounding_divide_by_pow2_16(preshiftOutputValue, -outShift);

      int lutVal = outputValue + zpOut;
      lutVal = MIN2(qMax, MAX2(qMin, lutVal));
      *lut = lutVal;
   }
}

static int32_t
saturating_rounding_doubling_high_mul_32(int32_t a, int32_t b)
{
   bool overflow = a == b && a == INT32_MIN;
   int64_t a_64 = a;
   int64_t b_64 = b;
   int64_t ab_64 = a_64 * b_64;
   int32_t nudge = ab_64 >= 0 ? (1 << 30) : (1 - (1 << 30));
   int32_t ab_x2_high32 = ((ab_64 + nudge) / (1ll << 31));
   return overflow ? INT32_MAX : ab_x2_high32;
}

static int32_t
rounding_divide_by_pow2_32(int32_t x, int exponent)
{
   const int32_t mask = (1 << exponent) - 1;
   const int32_t remainder = x & mask;
   const int32_t threshold = (mask >> 1) + ((x < 0) ? 1 : 0);
   return (x >> exponent) + ((remainder > threshold) ? 1 : 0);
}

// Multiplies int with QuantizedScale with rounding.
static int
multiply_by_quantized_multiplier(int x, int shift, int32_t scale)
{
   // Multiplies x (int32) by QuantizedScale (scale, shift), returns rounded result.
   // Expects the QuantizedScale to be left-shift positive.
   const int leftShift = shift > 0 ? shift : 0;
   const int rightShift = shift < 0 ? -shift : 0;
   const int32_t mul = saturating_rounding_doubling_high_mul_32(x * (1 << leftShift), scale);
   return rounding_divide_by_pow2_32(mul, rightShift);
}

static float
clamp(double d)
{
   return (float)CLAMP(d, -FLT_MAX, FLT_MAX);
}

/* Calculate elementwise Mul OFM QuantizedScale */
static int32_t
elementwise_mul_scale(double inputScale, double input2Scale, double outputScale, int32_t *mul_shift)
{
   // clamp to single-point precision
   float ifm1Scale = clamp(inputScale);
   float ifm2Scale = clamp(input2Scale);
   float outScale = clamp(outputScale);

   float outputRescale = (ifm1Scale * ifm2Scale) / outScale;
   return ethosu_quantize_scale(outputRescale, mul_shift, false);
}

static void
ethos_create_leakyrelu_lut(struct ethosu_operation *operation, uint8_t *lut, float alpha)
{
   const double ifm_scale = operation->ifm.scale;
   const double ofm_scale = operation->ofm.scale;
   const int zpIn = operation->ifm.zero_point;
   const int zpOut = operation->ofm.zero_point;
   const int qMin = operation->ifm.is_signed ? -128 : 0;
   const int qMax = operation->ifm.is_signed ? 127 : 255;
   int64_t scalar = 1;
   int32_t identity_shift;
   int32_t identity_scale = elementwise_mul_scale(ifm_scale, 1.0, ofm_scale, &identity_shift);
   int32_t alpha_shift;
   int32_t alpha_scale = elementwise_mul_scale(ifm_scale, alpha, ofm_scale, &alpha_shift);

   for (int x = qMin; x <= qMax; ++x, lut++) {
      int lutResult;
      if (x < zpIn)
         lutResult = zpOut + multiply_by_quantized_multiplier((int)(scalar * (x - zpIn)), 31 - alpha_shift, alpha_scale);
      else
         lutResult = zpOut + multiply_by_quantized_multiplier((int)(x - zpIn), 31 - identity_shift, identity_scale);

      lutResult = MIN2(qMax, MAX2(qMin, lutResult));
      *lut = lutResult;
   }
}

static void
ethosu_lower_lut_dma(struct ethosu_subgraph *subgraph,
                     const struct pipe_ml_operation *poperation,
                     struct ethosu_operation *pool_operation,
                     struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_DMA;
   operation->dma.address = pool_operation->pooling.lut.address;
   operation->dma.size = LUT8_SIZE;
   operation->dma.dst_region = LUT_REGION;
   operation->dma.dst_address = SHRAM_LUT_BASE(0);
}

static void
ethosu_lower_lut(struct ethosu_subgraph *subgraph,
                 const struct pipe_ml_operation *poperation,
                 struct ethosu_operation *operation, double (*func)(double))
{
   uint8_t lut[LUT8_SIZE];

   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->round_mode = ETHOSU_ROUNDING_NATURAL;
   operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;
   operation->pooling.activation = ETHOSU_POOLING_ACTIVATION_LUT(0);

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);

   ethos_create_lut(operation, lut, func);
   fill_lut(subgraph, operation, lut);

   /* The LUT handles 0 point and scale, so make them equal */
   operation->ofm.zero_point = operation->ifm.zero_point;
   operation->ofm.scale = operation->ifm.scale;

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_hswish(struct ethosu_subgraph *subgraph,
                    const struct pipe_ml_operation *poperation,
                    struct ethosu_operation *operation)
{
   uint8_t lut[LUT8_SIZE];

   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->round_mode = ETHOSU_ROUNDING_NATURAL;
   operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;
   operation->pooling.activation = ETHOSU_POOLING_ACTIVATION_LUT(0);

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);

   ethos_create_hswish_lut(operation, lut);
   fill_lut(subgraph, operation, lut);

   /* The LUT handles 0 point and scale, so make them equal */
   operation->ofm.zero_point = operation->ifm.zero_point;
   operation->ofm.scale = operation->ifm.scale;

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_leakyrelu(struct ethosu_subgraph *subgraph,
                       const struct pipe_ml_operation *poperation,
                       struct ethosu_operation *operation)
{
   uint8_t lut[LUT8_SIZE];

   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->round_mode = ETHOSU_ROUNDING_NATURAL;
   operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;
   operation->pooling.activation = ETHOSU_POOLING_ACTIVATION_LUT(0);

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);

   ethos_create_leakyrelu_lut(operation, lut, poperation->leakyrelu.alpha);
   fill_lut(subgraph, operation, lut);

   /* The LUT handles 0 point and scale, so make them equal */
   operation->ofm.zero_point = operation->ifm.zero_point;
   operation->ofm.scale = operation->ifm.scale;

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_quantize(struct ethosu_subgraph *subgraph,
                      const struct pipe_ml_operation *poperation,
                      struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->round_mode = ETHOSU_ROUNDING_DOUBLE;
   operation->pooling.nop = true;

   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;
   else
      operation->pooling.type = ETHOSU_POOLING_TYPE_SUM;

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_reshape(struct ethosu_subgraph *subgraph,
                     const struct pipe_ml_operation *poperation,
                     struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_NONE;

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);
   operation->ifm.tiles.addresses[0] = ethosu_allocate_feature_map(subgraph, operation->ifm.tensor);
   operation->ofm.tiles.addresses[0] = operation->ifm.tiles.addresses[0];

   operation->ofm.tensor->offset = operation->ifm.tensor->offset;
   operation->ofm.tensor->size = operation->ifm.tensor->size;
   operation->ofm.tensor->layout = operation->ifm.tensor->layout;
}

static void
ethosu_lower_concatenation(struct ethosu_subgraph *subgraph,
                           const struct pipe_ml_operation *poperation,
                           unsigned input_idx,
                           struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;

   if (ethosu_ml_device(subgraph->base.device)->is_u65) {
      operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;
      operation->round_mode = ETHOSU_ROUNDING_NATURAL;
   } else
      operation->pooling.type = ETHOSU_POOLING_TYPE_SUM;
   operation->pooling.nop = true;

   set_feature_maps(subgraph, poperation->input_tensors[input_idx], poperation->output_tensors[0], operation);
   operation->ofm.shape = operation->ifm.shape;
   if (poperation->conc.axis == 1)
      operation->ofm.stride = operation->ifm.stride;

   allocate_feature_maps(subgraph, operation);
   for (unsigned i = 0; i < input_idx; i++) {
      if (operation->ofm.tensor->layout == ETHOSU_LAYOUT_NHWC)
         if (poperation->conc.axis == 1)
            operation->ofm.tiles.addresses[0] += poperation->input_tensors[i]->dims[3] * poperation->input_tensors[i]->dims[2] * poperation->input_tensors[i]->dims[1];
         else
            operation->ofm.tiles.addresses[0] += poperation->input_tensors[i]->dims[3];
      else if (operation->ofm.tensor->layout == ETHOSU_LAYOUT_NHCWB16)
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
   operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);

   operation->upscale = ETHOSU_UPSCALE_NEAREST;

   allocate_feature_maps(subgraph, operation);
   ethosu_sched_operation(subgraph, operation);
}

static void
ethosu_lower_strided_slice(struct ethosu_subgraph *subgraph,
                           const struct pipe_ml_operation *poperation,
                           struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_POOLING;
   operation->pooling.type = ETHOSU_POOLING_TYPE_AVG;

   set_feature_maps(subgraph, poperation->input_tensors[0], poperation->output_tensors[0], operation);
   operation->ifm.shape = operation->ofm.shape;

   allocate_feature_maps(subgraph, operation);

   unsigned augmented_coord[5] = {};
   for (int i = 0; i < poperation->input_tensors[1]->dims[3]; ++i) {
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

static bool
is_sub_shape(struct pipe_tensor *sub, struct pipe_tensor *super)
{
   for (int i = 1; i < 4; i++) {
      if (sub->dims[i] > super->dims[i])
         return false;
   }
   return true;
}

static void
ethosu_lower_eltwise(struct ethosu_subgraph *subgraph,
                     const struct pipe_ml_operation *poperation,
                     struct ethosu_operation *operation)
{
   operation->type = ETHOSU_OPERATION_TYPE_ELTWISE;
   int ifm_idx = 0;
   int ifm2_idx = 1;

   if (!is_sub_shape(poperation->input_tensors[1], poperation->input_tensors[0])) {
      ifm_idx = 1;
      ifm2_idx = 0;
      operation->eltwise.ifm_reversed = true;
   }

   set_feature_maps(subgraph, poperation->input_tensors[ifm_idx], poperation->output_tensors[0], operation);

   set_feature_map(subgraph, poperation->input_tensors[ifm2_idx], &operation->ifm2);

   if (poperation->input_tensors[ifm2_idx]->data) {
      if (operation->ifm2.shape.width == 1 &&
          operation->ifm2.shape.height == 1 &&
          operation->ifm2.shape.depth == 1)
         operation->ifm2.scalar = *poperation->input_tensors[ifm2_idx]->data;
      else {
         size_t size = operation->ifm2.shape.height * operation->ifm2.shape.width *
                       operation->ifm2.shape.depth * poperation->input_tensors[ifm2_idx]->type_size;

         operation->ifm2.region = COEFS_REGION;
         operation->ifm2.tiles.addresses[0] = subgraph->coefs_used;
         subgraph->coefs_used += size;
         subgraph->coefs = realloc(subgraph->coefs, subgraph->coefs_used);
         memcpy(subgraph->coefs + operation->ifm2.tiles.addresses[0],
                poperation->input_tensors[ifm2_idx]->data, size);
      }
   } else {
      operation->ifm2.region = IO_REGION;
      operation->ifm2.tiles.addresses[0] = ethosu_allocate_feature_map(subgraph, operation->ifm2.tensor);
   }
   operation->ifm2.tiles.height_0 = operation->ifm2.shape.height;
   operation->ifm2.tiles.height_1 = operation->ifm2.shape.height;
   operation->ifm2.tiles.width_0 = operation->ifm2.shape.width;

   if (poperation->add.relu)
      operation->eltwise.activation_min = operation->ofm.zero_point;

   allocate_feature_maps(subgraph, operation);
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
   operation->dma.dst_region = SCRATCH_REGION;

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
            if (tensor->shape.depth % 16 == 0) {
               const struct pipe_ml_operation *consumer =
                  ethosu_find_first_consumer(poperations, count, ptensor->index);
               if (consumer && consumer->type != PIPE_ML_OPERATION_TYPE_RESHAPE)
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
      struct ethosu_operation operation;

      operation_set_defaults(&operation);

      switch (poperations[i].type) {

      case PIPE_ML_OPERATION_TYPE_FULLY_CONNECTED: {
         struct pipe_tensor *input_tensor = poperations[i].input_tensors[0];

         ethosu_lower_fully_connected(subgraph, &poperations[i], input_tensor, &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

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
             ethosu_ml_device(subgraph->base.device)->sram_size) {
            struct ethosu_operation dma_operation = {0};
            ethosu_lower_dma(subgraph, &poperations[i], &operation, &dma_operation);

            util_dynarray_append(&subgraph->operations, dma_operation);
         }

         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_ADD: {
         ethosu_lower_eltwise(subgraph, &poperations[i], &operation);
         operation.eltwise.type = ETHOSU_ELTWISE_TYPE_ADD;
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_MUL: {
         ethosu_lower_eltwise(subgraph, &poperations[i], &operation);
         operation.eltwise.type = ETHOSU_ELTWISE_TYPE_MUL;
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_MAXIMUM: {
         ethosu_lower_eltwise(subgraph, &poperations[i], &operation);
         operation.eltwise.type = ETHOSU_ELTWISE_TYPE_MAX;
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_MINIMUM: {
         ethosu_lower_eltwise(subgraph, &poperations[i], &operation);
         operation.eltwise.type = ETHOSU_ELTWISE_TYPE_MIN;
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_POOLING: {
         ethosu_lower_pooling(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_LOGISTIC: {
         ethosu_lower_lut(subgraph, &poperations[i], &operation, clamp_sigmoid8);

         struct ethosu_operation dma_operation = {0};
         ethosu_lower_lut_dma(subgraph, &poperations[i], &operation, &dma_operation);
         util_dynarray_append(&subgraph->operations, dma_operation);

         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_TANH: {
         ethosu_lower_lut(subgraph, &poperations[i], &operation, tanh);

         struct ethosu_operation dma_operation = {0};
         ethosu_lower_lut_dma(subgraph, &poperations[i], &operation, &dma_operation);
         util_dynarray_append(&subgraph->operations, dma_operation);

         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_HSWISH: {
         ethosu_lower_hswish(subgraph, &poperations[i], &operation);

         struct ethosu_operation dma_operation = {0};
         ethosu_lower_lut_dma(subgraph, &poperations[i], &operation, &dma_operation);
         util_dynarray_append(&subgraph->operations, dma_operation);

         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_STRIDED_SLICE: {
         ethosu_lower_strided_slice(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_CONCATENATION: {
         for (int j = poperations[i].input_count - 1; j >= 0; j--) {
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

      case PIPE_ML_OPERATION_TYPE_LEAKY_RELU: {
         ethosu_lower_leakyrelu(subgraph, &poperations[i], &operation);

         struct ethosu_operation dma_operation = {0};
         ethosu_lower_lut_dma(subgraph, &poperations[i], &operation, &dma_operation);
         util_dynarray_append(&subgraph->operations, dma_operation);

         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_QUANTIZE: {
         ethosu_lower_quantize(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      case PIPE_ML_OPERATION_TYPE_RESHAPE: {
         ethosu_lower_reshape(subgraph, &poperations[i], &operation);
         util_dynarray_append(&subgraph->operations, operation);
         break;
      }

      default:
         DBG("poperation->type %d\n", poperations[i].type);
         UNREACHABLE("Unsupported ML operation type");
      }
   }
}
