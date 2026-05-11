/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include "util/macros.h"
#include "util/u_dynarray.h"

#include "ethosu_cmd.h"
#include "ethosu_coefs.h"
#include "ethosu_ml.h"
#include "ethosu_registers.h"
#include "ethosu_sched.h"

#define MAX_OUTSTANDING_DMA_OPS 2
#define MAX_OUTSTANDING_NPU_OPS 2

enum ethosu_op_to_scale {
   OP_NONE = 0,
   OP_A = 1,
   OP_B = 2,
};

enum ethosu_microblock {
   MICROBLOCK_U1X1 = 0,
   MICROBLOCK_U1X2 = 1,
   MICROBLOCK_U1X4 = 2,
   MICROBLOCK_U2X2 = 3,
   MICROBLOCK_U2X4 = 4,
   MICROBLOCK_U4X4 = 5,
   MICROBLOCK_U2X1 = 6, /* U85 elementwise ublock */
};

static void
ethosu_ensure_cmdstream(struct ethosu_subgraph *subgraph)
{
   if ((subgraph->cursor - subgraph->cmdstream) < (subgraph->cmdstream_used - 2))
      return;

   unsigned cur_size = subgraph->cursor - subgraph->cmdstream;
   subgraph->cmdstream = realloc(subgraph->cmdstream, (subgraph->cmdstream_used + 32) * sizeof(*subgraph->cmdstream));
   subgraph->cursor = subgraph->cmdstream + cur_size;
   subgraph->cmdstream_used += 32;
}

/* Check if a CMD0 register value has changed - returns true if should emit */
static bool
ethosu_cmd0_changed(struct ethosu_subgraph *subgraph, uint16_t reg, uint16_t value)
{
   assert(reg < ETHOSU_MAX_REG_INDEX);

   if (subgraph->cmd0_valid[reg] && subgraph->cmd0_state[reg] == value)
      return false;

   subgraph->cmd0_state[reg] = value;
   subgraph->cmd0_valid[reg] = true;
   return true;
}

/* Check if a CMD1 register value has changed - returns true if should emit */
static bool
ethosu_cmd1_changed(struct ethosu_subgraph *subgraph, uint16_t reg, uint64_t value)
{
   assert(reg < ETHOSU_MAX_REG_INDEX);

   if (subgraph->cmd1_valid[reg] && subgraph->cmd1_state[reg] == value)
      return false;

   subgraph->cmd1_state[reg] = value;
   subgraph->cmd1_valid[reg] = true;
   return true;
}

/* Check if this is an operation command (always emit, never deduplicate).
 * NPU_OP_* commands occupy offsets 0x00–0x13: STOP=0, IRQ=1, CONV=2,
 * DEPTHWISE=3, POOL=5, ELEMENTWISE=6, RESIZE=7, DMA_START=16,
 * DMA_WAIT=17, KERNEL_WAIT=18, PMU_MASK=19.
 * Configuration registers (NPU_SET_*) start at 0x100. */
static bool
ethosu_is_op_cmd(uint16_t cmd)
{
   return (cmd <= 0x13);
}

#define EMIT0(cmd, param)                                                               \
   do {                                                                                 \
      uint16_t _value = (param) & 0xFFFF;                                               \
      if (ethosu_is_op_cmd(cmd) || ethosu_cmd0_changed(subgraph, cmd, _value)) {        \
         ethosu_ensure_cmdstream(subgraph);                                             \
         *(subgraph->cursor++) = cmd | ((uint32_t)_value << 16);                        \
         if (DBG_ENABLED(ETHOSU_DBG_MSGS))                                              \
            fprintf(stderr, "emit0(%s, 0x%x);\n", ethosu_get_cmd_name(0, cmd), _value); \
      }                                                                                 \
   } while (0)

#define EMIT1(cmd, param, offset)                                                                                      \
   do {                                                                                                                \
      uint64_t _value = (((uint64_t)(param) & 0xFFFF) << 32) | ((uint64_t)(offset) & 0xFFFFFFFF);                      \
      if (ethosu_cmd1_changed(subgraph, cmd, _value)) {                                                                \
         ethosu_ensure_cmdstream(subgraph);                                                                            \
         *(subgraph->cursor++) = cmd | 0x4000 | (((param) & 0xFFFF) << 16);                                            \
         *(subgraph->cursor++) = (offset) & 0xFFFFFFFF;                                                                \
         if (DBG_ENABLED(ETHOSU_DBG_MSGS))                                                                             \
            fprintf(stderr, "emit1(%s, 0x%x, 0x%x);\n", ethosu_get_cmd_name(1, cmd), (param) & 0xFFFF, (int)(offset)); \
      }                                                                                                                \
   } while (0)

static void
emit_addresses(
   struct ethosu_subgraph *subgraph,
   struct ethosu_feature_map *feature_map,
   uint32_t cmd_base0, uint32_t cmd_base1, uint32_t cmd_base2, uint32_t cmd_base3)
{
   EMIT1(cmd_base0, 0x0, feature_map->tiles.addresses[0]);
   EMIT1(cmd_base1, 0x0, feature_map->tiles.addresses[1]);
   EMIT1(cmd_base2, 0x0, feature_map->tiles.addresses[2]);
   EMIT1(cmd_base3, 0x0, feature_map->tiles.addresses[3]);
}

static void
emit_tiles(
   struct ethosu_subgraph *subgraph,
   struct ethosu_feature_map *feature_map,
   uint32_t cmd_height0, uint32_t cmd_height1, uint32_t cmd_width0)
{
   EMIT0(cmd_height0, feature_map->tiles.height_0 - 1);
   EMIT0(cmd_height1, feature_map->tiles.height_1 - 1);
   EMIT0(cmd_width0, feature_map->tiles.width_0 - 1);
}

static void
emit_strides(
   struct ethosu_subgraph *subgraph,
   struct ethosu_feature_map *feature_map,
   uint32_t cmd_stride_c, uint32_t cmd_stride_y, uint32_t cmd_stride_x)
{
   EMIT1(cmd_stride_y, 0x0, feature_map->stride.y);
   EMIT1(cmd_stride_x, 0x0, feature_map->stride.x);
   EMIT1(cmd_stride_c, 0x0, feature_map->stride.c);
}

static void
emit_ifm(struct ethosu_subgraph *subgraph, struct ethosu_feature_map *feature_map)
{
   EMIT0(NPU_SET_IFM_REGION, IO_REGION);
   emit_addresses(
      subgraph,
      feature_map,
      NPU_SET_IFM_BASE0,
      NPU_SET_IFM_BASE1,
      NPU_SET_IFM_BASE2,
      NPU_SET_IFM_BASE3);

   emit_tiles(
      subgraph, feature_map, NPU_SET_IFM_HEIGHT0_M1, NPU_SET_IFM_HEIGHT1_M1, NPU_SET_IFM_WIDTH0_M1);

   EMIT0(NPU_SET_IFM_DEPTH_M1, feature_map->shape.depth - 1);
   emit_strides(subgraph, feature_map, NPU_SET_IFM_STRIDE_C, NPU_SET_IFM_STRIDE_Y, NPU_SET_IFM_STRIDE_X);
   EMIT0(NPU_SET_IFM_ZERO_POINT, feature_map->zero_point);
}

static void
emit_ifm_precision(struct ethosu_subgraph *subgraph,
                   struct ethosu_feature_map *feature_map,
                   enum ethosu_op_to_scale op_to_scale, uint32_t precision_cmd)
{
   unsigned prec = 0;

   if (feature_map->tensor->layout == ETHOSU_LAYOUT_NHCWB16)
      prec |= NPU_SET_IFM_PRECISION_FORMAT(1);

   prec |= NPU_SET_IFM_PRECISION_PRECISION(feature_map->precision);

   if (feature_map->is_signed)
      prec |= NPU_SET_IFM_PRECISION_ACTIVATION(1); // signed activation

   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      prec |= NPU_SET_IFM_PRECISION_SCALE_MODE(op_to_scale);

   EMIT0(precision_cmd, prec);
}

static void
emit_padding(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_IFM_PAD_TOP, operation->pad.top);
   EMIT0(NPU_SET_IFM_PAD_LEFT, operation->pad.left);
   EMIT0(NPU_SET_IFM_PAD_BOTTOM, operation->pad.bottom);
   EMIT0(NPU_SET_IFM_PAD_RIGHT, operation->pad.right);
}

static void
emit_ofm(struct ethosu_subgraph *subgraph, struct ethosu_feature_map *feature_map)
{
   EMIT0(NPU_SET_OFM_REGION, IO_REGION);
   emit_addresses(
      subgraph,
      feature_map,
      NPU_SET_OFM_BASE0,
      NPU_SET_OFM_BASE1,
      NPU_SET_OFM_BASE2,
      NPU_SET_OFM_BASE3);

   EMIT0(NPU_SET_OFM_HEIGHT_M1, feature_map->shape.height - 1);
   EMIT0(NPU_SET_OFM_WIDTH_M1, feature_map->shape.width - 1);

   if (!ethosu_ml_device(subgraph->base.device)->is_u65)
      EMIT0(NPU_SET_OFM_DEPTH_M1, feature_map->shape.depth - 1);

   emit_tiles(
      subgraph, feature_map, NPU_SET_OFM_HEIGHT0_M1, NPU_SET_OFM_HEIGHT1_M1, NPU_SET_OFM_WIDTH0_M1);

   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      EMIT0(NPU_SET_OFM_DEPTH_M1, feature_map->shape.depth - 1);

   emit_strides(subgraph, feature_map, NPU_SET_OFM_STRIDE_C, NPU_SET_OFM_STRIDE_Y, NPU_SET_OFM_STRIDE_X);
   EMIT0(NPU_SET_OFM_ZERO_POINT, feature_map->zero_point);
}

static void
emit_ofm_precision(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   unsigned prec = 0;

   if (operation->ofm.tensor->layout == ETHOSU_LAYOUT_NHCWB16)
      prec |= NPU_SET_OFM_PRECISION_FORMAT(1);

   prec |= NPU_SET_OFM_PRECISION_PRECISION(operation->ofm.precision);

   if (operation->ofm.is_signed)
      prec |= NPU_SET_OFM_PRECISION_ACTIVATION(1);

   if (operation->type == ETHOSU_OPERATION_TYPE_POOLING ||
       operation->type == ETHOSU_OPERATION_TYPE_ELTWISE) {
      prec |= NPU_SET_OFM_PRECISION_SCALE_MODE(1);
   }

   prec |= NPU_SET_OFM_PRECISION_ROUND_MODE(operation->round_mode);

   EMIT0(NPU_SET_OFM_PRECISION, prec);
}

static void
emit_kernel(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_KERNEL_HEIGHT_M1, operation->kernel.height - 1);
   EMIT0(NPU_SET_KERNEL_WIDTH_M1, operation->kernel.width - 1);
   unsigned stride = (operation->kernel.stride_x - 1) & 1;
   stride |= ((operation->kernel.stride_y - 1) & 1) << 1;
   stride |= ((operation->kernel.stride_x - 1) >> 1) << 6;
   stride |= ((operation->kernel.stride_y - 1) >> 1) << 9;
   stride |= (operation->kernel.dilation_x - 1) << 3;
   stride |= (operation->kernel.dilation_y - 1) << 4;
   stride |= operation->block_config.is_partkernel << 2;
   EMIT0(NPU_SET_KERNEL_STRIDE, stride);
}

static void
emit_weights(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   if (!ethosu_ml_device(subgraph->base.device)->is_u65)
      EMIT0(NPU_SET_WEIGHT_FORMAT, 0x0);

   EMIT0(NPU_SET_WEIGHT_REGION, operation->conv.weights.region);
   EMIT1(NPU_SET_WEIGHT_BASE, 0x0, operation->conv.weights.address);
   EMIT1(NPU_SET_WEIGHT_LENGTH, 0x0, operation->conv.weights.size);
}

static void
emit_biases(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_SCALE_REGION, operation->conv.scales.region);
   EMIT1(NPU_SET_SCALE_BASE, 0x0, operation->conv.scales.address);
   EMIT1(NPU_SET_SCALE_LENGTH, 0x0, operation->conv.scales.size);
}

static void
emit_activation(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   unsigned min = 0;

   if (operation->type == ETHOSU_OPERATION_TYPE_ELTWISE)
      min = operation->eltwise.activation_min;

   if (operation->type == ETHOSU_OPERATION_TYPE_POOLING)
      EMIT0(NPU_SET_ACTIVATION, operation->pooling.activation);
   else
      EMIT0(NPU_SET_ACTIVATION, 0x0);

   if (operation->ofm.is_signed) {
      EMIT0(NPU_SET_ACTIVATION_MIN, 0xff80);
      EMIT0(NPU_SET_ACTIVATION_MAX, 0x7f);
   } else {
      EMIT0(NPU_SET_ACTIVATION_MIN, min);
      EMIT0(NPU_SET_ACTIVATION_MAX, 0xff);
   }
}

static void
emit_block_config(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_OFM_BLK_HEIGHT_M1, operation->block_config.ofm_block.height - 1);
   EMIT0(NPU_SET_OFM_BLK_WIDTH_M1, operation->block_config.ofm_block.width - 1);
   EMIT0(NPU_SET_OFM_BLK_DEPTH_M1, operation->block_config.ofm_block.depth - 1);
}

static void
emit_shram_registers(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_IFM_IB_END, operation->block_config.shram_layout.ib_end);
   EMIT0(NPU_SET_AB_START, operation->block_config.shram_layout.ab_start);

   if (operation->type == ETHOSU_OPERATION_TYPE_ELTWISE)
      EMIT0(NPU_SET_IFM2_IB_START, operation->block_config.shram_layout.ib_start2);

   EMIT0(NPU_SET_ACC_FORMAT, operation->block_config.acc_type);
}

static void
emit_acc_format(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   /* Currently only 8-bit quantized operations are supported, so
    * acc_format=INT_32 (0), acc_input=I8 (0), acc_output=I8 (0).
    * These would need to vary for 16-bit or mixed-precision ops. */
   unsigned acc_format = 0;
   unsigned acc_input = 0;
   unsigned acc_output = 0;
   enum ethosu_microblock block = MICROBLOCK_U1X1;

   switch (operation->block_config.ofm_ublock.height << 4 | operation->block_config.ofm_ublock.width) {
   case 0x11:
      block = MICROBLOCK_U1X1;
      break;
   case 0x12:
      block = MICROBLOCK_U1X2;
      break;
   case 0x14:
      block = MICROBLOCK_U1X4;
      break;
   case 0x21:
      block = MICROBLOCK_U2X1;
      break;
   case 0x22:
      block = MICROBLOCK_U2X2;
      break;
   case 0x24:
      block = MICROBLOCK_U2X4;
      break;
   case 0x44:
      block = MICROBLOCK_U4X4;
      break;
   default:
      assert(false && "Invalid microblock");
   }

   EMIT0(NPU_SET_ACC_FORMAT, NPU_SET_ACC_FORMAT_ACC_FORMAT(acc_format) |
                                NPU_SET_ACC_FORMAT_ACC_INPUT(acc_input) |
                                NPU_SET_ACC_FORMAT_ACC_OUTPUT(acc_output) |
                                NPU_SET_ACC_FORMAT_MICROBLOCK(block));
}

static void
emit_common(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, enum ethosu_op_to_scale op_to_scale)
{
   if (!ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_ifm_precision(subgraph, &operation->ifm, op_to_scale, NPU_SET_IFM_PRECISION);
   emit_ifm(subgraph, &operation->ifm);
   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_ifm_precision(subgraph, &operation->ifm, op_to_scale, NPU_SET_IFM_PRECISION);
   EMIT0(NPU_SET_IFM_UPSCALE, operation->upscale);

   if (operation->type != ETHOSU_OPERATION_TYPE_ELTWISE)
      emit_padding(subgraph, operation);

   if (!ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_ofm_precision(subgraph, operation);

   emit_ofm(subgraph, &operation->ofm);

   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_ofm_precision(subgraph, operation);

   if (operation->type != ETHOSU_OPERATION_TYPE_ELTWISE)
      emit_kernel(subgraph, operation);

   if (operation->type == ETHOSU_OPERATION_TYPE_CONVOLUTION) {
      emit_weights(subgraph, operation);
      emit_biases(subgraph, operation);
   }

   emit_activation(subgraph, operation);
}

static void
emit_convolution(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   if (!ethosu_ml_device(subgraph->base.device)->is_u65)
      EMIT1(NPU_SET_OFM_SCALE, NPU_SET_OFM_SCALE_SHIFT(operation->conv.shift), operation->conv.scale);

   emit_common(subgraph, operation, false);

   emit_block_config(subgraph, operation);
   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_shram_registers(subgraph, operation);
   else
      emit_acc_format(subgraph, operation);
}

static unsigned
quantise_pooling_scale(unsigned nr_kernel_elements, unsigned rescale_bits, int32_t *out_shift)
{
   int k = 0;
   long long N = 0;

   frexp(nr_kernel_elements - 1, &k);
   N = 31 - rescale_bits;
   *out_shift = N + k;

   return ((1LL << (N + k)) + (1LL << k)) / nr_kernel_elements;
}

static unsigned
pooling_emit_ofm_scaling(
   double input1_scale,
   double output_scale,
   unsigned kernel_height,
   unsigned kernel_width,
   int32_t *out_shift)
{
   double rescale = input1_scale / output_scale;
   unsigned rescale_bits = 0;
   unsigned scale;

   if (kernel_height == 1 && kernel_width == 1) {
      if (rescale > 1.0)
         rescale_bits = 32 - __builtin_clz(ceil(rescale)) + 1;
      else if (rescale < 1.0)
         rescale_bits = -(32 - __builtin_clz(ceil(1 / rescale))) - 1;
   }
   scale = quantise_pooling_scale(kernel_height * kernel_width, rescale_bits, out_shift);
   scale = ceil(scale * rescale);
   return scale;
}

static unsigned
sum_emit_ofm_scaling(double input1_scale, double output_scale, unsigned kernel_height, unsigned kernel_width, int32_t *out_shift)
{
   int kernel_elements = kernel_height * kernel_width;
   double rescale = input1_scale / output_scale;
   int rescale_bits = 0;
   int N = 31;
   int exp;

   frexp((double)(kernel_elements - 1), &exp);

   int n = (N - 1) - rescale_bits;
   uint64_t numerator = (1ULL << (n + exp)) + (1ULL << exp);
   uint32_t scale = (uint32_t)ceil(rescale * (double)numerator / kernel_elements);
   int shift = n + exp;

   assert(shift >= 0 && shift < 64);

   *out_shift = shift;
   return scale;
}

static void
emit_pooling(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   unsigned scale;
   int32_t scale_shift;

   emit_common(subgraph, operation, false);

   if (operation->pooling.nop) {
      scale = ethosu_quantize_scale(
         operation->ifm.scale / operation->ofm.scale,
         &scale_shift, true);
      EMIT1(NPU_SET_OFM_SCALE, NPU_SET_OFM_SCALE_SHIFT(scale_shift), scale);
   } else {
      switch (operation->pooling.type) {
      case ETHOSU_POOLING_TYPE_MAX: {
         if (!ethosu_ml_device(subgraph->base.device)->is_u65) {
            EMIT1(NPU_SET_OFM_SCALE, NPU_SET_OFM_SCALE_ROUND_MODE(1), 1);
            break;
         } else
            FALLTHROUGH;
      }
      case ETHOSU_POOLING_TYPE_AVG: {
         scale = pooling_emit_ofm_scaling(
            operation->ifm.scale,
            operation->ofm.scale,
            operation->kernel.height,
            operation->kernel.width,
            &scale_shift);

         EMIT1(NPU_SET_OFM_SCALE, NPU_SET_OFM_SCALE_SHIFT(scale_shift), scale);
         break;
      }
      case ETHOSU_POOLING_TYPE_SUM: {
         scale = sum_emit_ofm_scaling(
            operation->ifm.scale,
            operation->ofm.scale,
            operation->kernel.height,
            operation->kernel.width,
            &scale_shift);

         EMIT1(NPU_SET_OFM_SCALE, NPU_SET_OFM_SCALE_SHIFT(scale_shift) | NPU_SET_OFM_SCALE_ROUND_MODE(1), scale);
         break;
      }
      default:
         UNREACHABLE("Invalid pooling type");
      }
   }

   emit_block_config(subgraph, operation);
   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_shram_registers(subgraph, operation);
   else
      emit_acc_format(subgraph, operation);
}

static void
emit_ifm2_precision(struct ethosu_subgraph *subgraph,
                    struct ethosu_operation *operation,
                    bool has_scalar)
{
   unsigned prec = 0;

   prec |= NPU_SET_IFM2_PRECISION_ACTIVATION_TYPE(operation->ifm2.is_signed);
   prec |= NPU_SET_IFM2_PRECISION_ACTIVATION_PRECISION(operation->ifm2.precision);

   if (operation->ifm2.tensor->layout == ETHOSU_LAYOUT_NHCWB16)
      prec |= NPU_SET_IFM2_PRECISION_ACTIVATION_FORMAT(1);

   /* Vela: scalar → NONE(3), non-scalar → TILE2X2(0) */
   if (has_scalar)
      prec |= NPU_SET_IFM2_PRECISION_ACTIVATION_STORAGE(3);

   EMIT0(NPU_SET_IFM2_PRECISION, prec);
}

static void
emit_ifm2(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, bool has_scalar)
{
   if (has_scalar) {
      if (ethosu_ml_device(subgraph->base.device)->is_u65)
         EMIT0(NPU_SET_IFM2_SCALAR, operation->ifm2.scalar);
      else {
         emit_ifm2_precision(subgraph, operation, true);
         EMIT1(NPU_SET_OP_SCALAR, 0, operation->ifm2.scalar);
      }
   } else {
      EMIT0(NPU_SET_IFM2_REGION, operation->ifm2.region);
      emit_addresses(subgraph, &operation->ifm2, NPU_SET_IFM2_BASE0, NPU_SET_IFM2_BASE1, NPU_SET_IFM2_BASE2, NPU_SET_IFM2_BASE3);
      emit_tiles(subgraph, &operation->ifm2, NPU_SET_IFM2_HEIGHT0_M1, NPU_SET_IFM2_HEIGHT1_M1, NPU_SET_IFM2_WIDTH0_M1);
      emit_strides(subgraph, &operation->ifm2, NPU_SET_IFM2_STRIDE_C, NPU_SET_IFM2_STRIDE_Y, NPU_SET_IFM2_STRIDE_X);
   }
   EMIT0(NPU_SET_IFM2_ZERO_POINT, operation->ifm2.zero_point);
}

static void
emit_ifm_broadcast(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, bool has_scalar)
{
   unsigned ifm_broadcast = 0;

   EMIT0(NPU_SET_IFM_BROADCAST, ifm_broadcast);
}

/*
 * U85 broadcast_mode calculation matching Vela's CalculateBroadcast().
 * Broadcasts shape1 dimensions that are 1 when shape2 is larger.
 * Returns a 4-bit broadcast_mode: H=1, W=2, C=4, or'ed together.
 */
static unsigned
calc_broadcast_mode(struct ethosu_block *shape1, struct ethosu_block *shape2)
{
   unsigned mode = 0;

   if (shape1->height < shape2->height && shape1->height == 1)
      mode |= 1; /* H */
   if (shape1->width < shape2->width && shape1->width == 1)
      mode |= 2; /* W */
   if (shape1->depth < shape2->depth && shape1->depth == 1)
      mode |= 4; /* C */

   return mode;
}

static void
emit_ifm2_broadcast(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation, bool has_scalar)
{
   unsigned ifm2_broadcast = 0;

   if (ethosu_ml_device(subgraph->base.device)->is_u65) {
      ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_OPERAND_ORDER(operation->eltwise.ifm_reversed);

      if (has_scalar) {
         ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_SCALAR(1);
      } else {
         if (operation->ifm.shape.height != operation->ifm2.shape.height)
            ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_HEIGHT__MASK;
         if (operation->ifm.shape.width != operation->ifm2.shape.width)
            ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_WIDTH__MASK;
         if (operation->ifm.shape.depth != operation->ifm2.shape.depth)
            ifm2_broadcast |= NPU_SET_IFM2_BROADCAST_BROADCAST_DEPTH__MASK;
      }
   } else {
      unsigned ifm_mode, ifm2_mode;

      if (has_scalar) {
         ifm_mode = 0;
         ifm2_mode = 8; /* SCALAR */
      } else {
         ifm_mode = calc_broadcast_mode(&operation->ifm.shape, &operation->ifm2.shape);
         ifm2_mode = calc_broadcast_mode(&operation->ifm2.shape, &operation->ifm.shape);
      }

      EMIT0(NPU_SET_IFM_BROADCAST, ifm_mode);
      ifm2_broadcast = ifm2_mode;
   }

   EMIT0(NPU_SET_IFM2_BROADCAST, ifm2_broadcast);
}

/*
 * Advanced elementwise ADD/SUB scaling for different input scales.
 * Based on ethos-u-vela scaling.py advanced_elementwise_add_sub_scale().
 *
 * The smaller input scale operand gets scaled up to match the larger one.
 * OPA_SCALE carries the input scaling, OPB_SCALE is always 0.
 * IFM_PRECISION bits 8-9 (op_to_scale) tells NPU which operand to scale.
 *
 * Formulas from Vela (for 8-bit, input_shift=20):
 *   input_rescale = min_scale * (1 << 20) / (2 * max_scale)
 *   output_rescale = (2 * max_scale) / (output_scale * (1 << 20))
 */
static enum ethosu_op_to_scale
eltwise_emit_ofm_scaling(
   struct ethosu_subgraph *subgraph,
   double input1_scale,
   double input2_scale,
   double output_scale)
{
   double max_input_scale = MAX2(input1_scale, input2_scale);
   double min_input_scale = MIN2(input1_scale, input2_scale);
   unsigned bitdepth = 8;
   uint32_t input_shift = (bitdepth == 8) ? 20 : 15;
   double input_shift_val = (double)(1ULL << input_shift);
   enum ethosu_op_to_scale op_to_scale;
   int32_t opa_scale, opa_shift;
   int32_t ofm_scale, ofm_shift;
   double input_rescale, output_rescale;

   /* Determine which operand to scale (the one with smaller scale) */
   if (input1_scale < input2_scale)
      op_to_scale = OP_A;
   else
      op_to_scale = OP_B;

   /* From Vela simplified_elementwise_add_sub_scale:
    * input1_rescale = input1_scale * (1 << input_shift) / (2 * max_input_scale)
    * output_rescale = (2 * max_input_scale) / (output_scale * (1 << input_shift))
    */
   input_rescale = min_input_scale * input_shift_val / (2.0 * max_input_scale);
   output_rescale = (2.0 * max_input_scale) / (output_scale * input_shift_val);

   opa_scale = ethosu_quantize_scale(input_rescale, &opa_shift, false);
   ofm_scale = ethosu_quantize_scale(output_rescale, &ofm_shift, false);

   if (DBG_ENABLED(ETHOSU_DBG_MSGS)) {
      fprintf(stderr, "ADD advanced scaling: ifm1_scale=%f ifm2_scale=%f ofm_scale=%f\n",
              input1_scale, input2_scale, output_scale);
      fprintf(stderr, "  min=%f max=%f input_rescale=%f output_rescale=%f\n",
              min_input_scale, max_input_scale, input_rescale, output_rescale);
      fprintf(stderr, "  op_to_scale=%d opa_scale=0x%x opa_shift=%d ofm_scale=0x%x ofm_shift=%d\n",
              op_to_scale, opa_scale, opa_shift, ofm_scale, ofm_shift);
   }

   /* OPA_SCALE: input scale value with shift */
   EMIT1(NPU_SET_OPA_SCALE, opa_shift, opa_scale);

   /* OPB_SCALE: always 0 in advanced mode */
   EMIT1(NPU_SET_OPB_SCALE, 0x0, 0x0);

   /* OFM_SCALE: output scale with shift */
   EMIT1(NPU_SET_OFM_SCALE, ofm_shift, ofm_scale);

   return op_to_scale;
}

static void
simplified_elementwise_add_sub_scale(
   double input1_scale,
   double input2_scale,
   double output_scale,
   int32_t input_shift,
   double *out_input1_rescale,
   double *out_input2_rescale,
   int32_t *out_out_scale,
   int32_t *out_out_shift)
{
   double max_input_scale = MAX2(input1_scale, input2_scale);
   double input_shift_val = (double)(1LL << input_shift); /* Use 1LL for large shifts */

   *out_input1_rescale = input1_scale * input_shift_val / (2.0 * max_input_scale);
   *out_input2_rescale = input2_scale * input_shift_val / (2.0 * max_input_scale);

   /*
    * Be careful with division by zero or very small output_scale if output_scale
    * can be zero or close to zero.
    */
   double output_rescale_val;
   if (output_scale == 0.0) {
      /* Handle error or return specific value */
      output_rescale_val = 0.0; /* Or INFINITY, depending on desired behavior */
   } else {
      output_rescale_val = (2.0 * max_input_scale) / (output_scale * input_shift_val);
   }

   *out_out_scale = ethosu_quantize_scale(output_rescale_val, out_out_shift, false);
}

static void
elementwise_min_max_scale(struct ethosu_subgraph *subgraph)
{
   if (!ethosu_ml_device(subgraph->base.device)->is_u65) {
      EMIT1(NPU_SET_OPA_SCALE, 0, 1);
      EMIT1(NPU_SET_OPB_SCALE, 0, 1);

   }
   EMIT1(NPU_SET_OFM_SCALE, 0, 1);
}

static void
elementwise_mul_scale(
   struct ethosu_subgraph *subgraph,
   double input1_scale,
   double input2_scale,
   double output_scale)
{
   double output_rescale;
   int32_t ofm_scale, ofm_shift;

   output_rescale = (input1_scale * input2_scale) / output_scale;
   ofm_scale = ethosu_quantize_scale(output_rescale, &ofm_shift, false);

   /* OFM_SCALE: output scale with shift */
   EMIT1(NPU_SET_OFM_SCALE, ofm_shift, ofm_scale);
}

/*
 * U85 uses "simplified" mode (from Vela simplified_elementwise_add_sub_scale):
 *   Both operands are independently rescaled.  OPA_SCALE and OPB_SCALE each
 *   carry their own rescale value with DBL_RND.  op_to_scale is not used.
 */
static enum ethosu_op_to_scale
eltwise_emit_ofm_scaling_u85(
   struct ethosu_subgraph *subgraph,
   double input1_scale,
   double input2_scale,
   double output_scale)
{
   unsigned bitdepth = 8;
   uint32_t input_shift = (bitdepth == 8) ? 20 : 15;
   double input1_rescale;
   double input2_rescale;
   int32_t ofm_scale, ofm_shift;
   int32_t opa_scale, opa_shift;
   int32_t opb_scale, opb_shift;

   simplified_elementwise_add_sub_scale(
      input1_scale, input2_scale, output_scale, input_shift,
      &input1_rescale, &input2_rescale,
      &ofm_scale, &ofm_shift);

   opa_scale = ethosu_quantize_scale(input1_rescale, &opa_shift, false);
   opb_scale = ethosu_quantize_scale(input2_rescale, &opb_shift, false);

   EMIT1(NPU_SET_OPA_SCALE,
         NPU_SET_OPA_SCALE_SHIFT(opa_shift) | NPU_SET_OPA_SCALE_DBL_RND(input_shift),
         opa_scale);
   EMIT1(NPU_SET_OPB_SCALE,
         NPU_SET_OPB_SCALE_SHIFT(opb_shift) | NPU_SET_OPB_SCALE_DBL_RND(input_shift),
         opb_scale);
   EMIT1(NPU_SET_OFM_SCALE, NPU_SET_OFM_SCALE_SHIFT(ofm_shift), ofm_scale);

   /* On U85, op_to_scale is not encoded into IFM_PRECISION */
   return OP_NONE;
}

static void
emit_eltwise(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   bool has_scalar = operation->ifm2.scalar != 0;
   enum ethosu_op_to_scale op_to_scale = OP_NONE;

   switch (operation->eltwise.type) {
   case ETHOSU_ELTWISE_TYPE_MUL:
      elementwise_mul_scale(subgraph, operation->ifm.scale,
                            operation->ifm2.scale,
                            operation->ofm.scale);
      break;
   case ETHOSU_ELTWISE_TYPE_ADD:
      if (ethosu_ml_device(subgraph->base.device)->is_u65) {
         op_to_scale = eltwise_emit_ofm_scaling(
            subgraph,
            operation->ifm.scale,
            operation->ifm2.scale,
            operation->ofm.scale);
      } else {
         op_to_scale = eltwise_emit_ofm_scaling_u85(
            subgraph,
            operation->ifm.scale,
            operation->ifm2.scale,
            operation->ofm.scale);
      }

      if (operation->eltwise.ifm_reversed) {
         if (op_to_scale == OP_A)
            op_to_scale = OP_B;
         else
            op_to_scale = OP_A;
      }
      break;
   case ETHOSU_ELTWISE_TYPE_MAX:
   case ETHOSU_ELTWISE_TYPE_MIN:
      elementwise_min_max_scale(subgraph);
      break;
   default:
      assert(0);
      break;
   }

   emit_common(subgraph, operation, op_to_scale);

   emit_ifm2(subgraph, operation, has_scalar);

   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_ifm_precision(subgraph, &operation->ifm2, OP_NONE, NPU_SET_IFM2_PRECISION);
   else
      emit_ifm2_precision(subgraph, operation, has_scalar);

   emit_ifm2_broadcast(subgraph, operation, has_scalar);

   emit_block_config(subgraph, operation);
   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      emit_shram_registers(subgraph, operation);
   else
      emit_acc_format(subgraph, operation);
}

static void
emit_dma(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   EMIT0(NPU_SET_DMA0_SRC_REGION, COEFS_REGION);
   EMIT1(NPU_SET_DMA0_SRC, 0x0, operation->dma.address);
   EMIT0(NPU_SET_DMA0_DST_REGION, operation->dma.dst_region);
   EMIT1(NPU_SET_DMA0_DST, 0x0, operation->dma.dst_address);
   EMIT1(NPU_SET_DMA0_LEN, 0x0, operation->dma.size);
}

static void
emit_operation_code(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation)
{
   switch (operation->type) {
   case ETHOSU_OPERATION_TYPE_CONVOLUTION:

      if (operation->conv.depthwise)
         EMIT0(NPU_OP_DEPTHWISE, 0x0);
      else
         EMIT0(NPU_OP_CONV, 0x0);

      break;
   case ETHOSU_OPERATION_TYPE_POOLING:
      EMIT0(NPU_OP_POOL, operation->pooling.type);
      break;
   case ETHOSU_OPERATION_TYPE_ELTWISE:
      EMIT0(NPU_OP_ELEMENTWISE, operation->eltwise.type);
      break;
   case ETHOSU_OPERATION_TYPE_DMA:
      EMIT0(NPU_OP_DMA_START, 0x0);
      break;
   default:
      assert(0);
      break;
   }
}

static void
emit_cmd_waits(struct ethosu_subgraph *subgraph, int npu_waits, int dma_waits)
{
   if (npu_waits >= 0)
      EMIT0(NPU_OP_KERNEL_WAIT, npu_waits);

   if (dma_waits >= 0)
      EMIT0(NPU_OP_DMA_WAIT, dma_waits);
}

static bool
ethosu_intersects_accesses(struct ethosu_address_range *a, struct ethosu_address_range *b)
{
   for (int i = 0; i < MAX_MEMORY_ACCESSES; i++) {
      for (int j = 0; j < MAX_MEMORY_ACCESSES; j++) {
         if (a[i].size == 0 || b[j].size == 0)
            continue;
         if (a[i].region != b[j].region)
            continue;
         if (a[i].address < b[j].address + b[j].size &&
             b[j].address < a[i].address + a[i].size)
            return true;
      }
   }

   return false;
}

static bool
ethosu_operations_conflict(struct ethosu_subgraph *subgraph,
                           struct ethosu_operation *op1, struct ethosu_operation *op2)
{
   /* True dependencies, or write -> read */
   if (ethosu_intersects_accesses(op1->write_accesses, op2->read_accesses))
      return true;

   /* Anti-dependencies, or read -> write */
   if (ethosu_intersects_accesses(op1->read_accesses, op2->write_accesses))
      return true;

   /* Output dependencies, or write -> write */
   if (ethosu_intersects_accesses(op1->write_accesses, op2->write_accesses))
      return true;

   /* read -> read does not cause a conflict */
   return false;
}

static void
get_wait_dependency(struct ethosu_subgraph *subgraph, struct ethosu_operation *operation,
                    struct util_dynarray *outstanding_dma_ops,
                    struct util_dynarray *outstanding_npu_ops,
                    int *npu_waits, int *dma_waits)
{
   unsigned kern_wait = -1;
   unsigned dma_wait = -1;
   struct util_dynarray *outstanding_ops = NULL;

   if (operation->type == ETHOSU_OPERATION_TYPE_DMA) {
      outstanding_ops = outstanding_npu_ops;

      util_dynarray_append(outstanding_dma_ops, operation);

      unsigned dmap_ops = util_dynarray_num_elements(outstanding_dma_ops, struct ethosu_operation *);
      if (dmap_ops > MAX_OUTSTANDING_DMA_OPS)
         (void)util_dynarray_pop(outstanding_dma_ops, struct ethosu_operation *);
   } else {
      outstanding_ops = outstanding_dma_ops;

      util_dynarray_append(outstanding_npu_ops, operation);

      unsigned npu_ops = util_dynarray_num_elements(outstanding_npu_ops, struct ethosu_operation *);
      if (npu_ops > MAX_OUTSTANDING_NPU_OPS)
         (void)util_dynarray_pop(outstanding_npu_ops, struct ethosu_operation *);
   }

   unsigned waits = -1;
   for (int idx = util_dynarray_num_elements(outstanding_ops, struct ethosu_operation *) - 1; idx >= 0; idx--) {
      waits += 1;
      struct ethosu_operation *other_op = *util_dynarray_element(outstanding_ops, struct ethosu_operation *, idx);
      if (other_op == operation)
         continue;
      if (ethosu_operations_conflict(subgraph, other_op, operation)) {
         if (operation->type == ETHOSU_OPERATION_TYPE_DMA)
            kern_wait = waits;
         else
            dma_wait = waits;
         // Current op needs to wait, and after it has waited,
         // outstanding_ops[0..idx] are not outstanding any longer.
         for (int i = 0; i <= idx; i++)
            (void)util_dynarray_pop(outstanding_ops, struct ethosu_operation *);
         break;
      }
   }

   *npu_waits = kern_wait;
   *dma_waits = dma_wait;
}

static void
fill_memory_accesses(struct ethosu_subgraph *subgraph)
{
   util_dynarray_foreach (&subgraph->operations, struct ethosu_operation, operation) {
      switch (operation->type) {
      case ETHOSU_OPERATION_TYPE_NONE:
         break;
      case ETHOSU_OPERATION_TYPE_DMA:
         operation->read_accesses[0].region = COEFS_REGION;
         operation->read_accesses[0].address = operation->dma.address;
         operation->read_accesses[0].size = operation->dma.size;

         operation->write_accesses[0].region = operation->dma.dst_region;
         operation->write_accesses[0].address = 0x0;
         operation->write_accesses[0].size = operation->dma.size;

         break;
      case ETHOSU_OPERATION_TYPE_POOLING:
         if (operation->pooling.activation >= ETHOSU_POOLING_ACTIVATION_LUT(0)) {
            operation->read_accesses[1].region = LUT_REGION;
            operation->read_accesses[1].address = SHRAM_LUT_BASE(operation->pooling.activation & 0xf);
            operation->read_accesses[1].size = LUT8_SIZE;
         }
         operation->read_accesses[0].region = operation->ifm.region;
         operation->read_accesses[0].address = operation->ifm.tiles.addresses[0];
         operation->read_accesses[0].size = operation->ifm.shape.height * operation->ifm.shape.width * operation->ifm.shape.depth;

         operation->write_accesses[0].region = operation->ofm.region;
         operation->write_accesses[0].address = operation->ofm.tiles.addresses[0];
         operation->write_accesses[0].size = operation->ofm.shape.height * operation->ofm.shape.width * operation->ofm.shape.depth;
         break;
      case ETHOSU_OPERATION_TYPE_CONVOLUTION:
         operation->read_accesses[2].region = operation->conv.scales.region;
         operation->read_accesses[2].address = operation->conv.scales.address;
         operation->read_accesses[2].size = operation->conv.scales.size;

         operation->read_accesses[3].region = operation->conv.weights.region;
         operation->read_accesses[3].address = operation->conv.weights.address;
         operation->read_accesses[3].size = operation->conv.weights.size;
         /* fall-through */
      default:
         operation->read_accesses[0].region = IO_REGION;
         operation->read_accesses[0].address = operation->ifm.tiles.addresses[0];
         operation->read_accesses[0].size = operation->ifm.shape.height * operation->ifm.shape.width * operation->ifm.shape.depth;

         operation->read_accesses[1].region = IO_REGION;
         operation->read_accesses[1].address = operation->ifm2.tiles.addresses[0];
         operation->read_accesses[1].size = operation->ifm2.shape.height * operation->ifm2.shape.width * operation->ifm2.shape.depth;

         operation->write_accesses[0].region = IO_REGION;
         operation->write_accesses[0].address = operation->ofm.tiles.addresses[0];
         operation->write_accesses[0].size = operation->ofm.shape.height * operation->ofm.shape.width * operation->ofm.shape.depth;
         break;
      }
   }
}

/* Box structure for spatial overlap detection */
struct box {
   int start_h, start_w, start_d;
   int end_h, end_w, end_d;
};

/* Check if two 1D ranges overlap */
static inline bool
range_overlaps(int start1, int end1, int start2, int end2)
{
   return start1 < end2 && start2 < end1;
}

/* Check if two boxes overlap in 3D space */
static bool
box_overlaps(const struct box *a, const struct box *b)
{
   return range_overlaps(a->start_h, a->end_h, b->start_h, b->end_h) &&
          range_overlaps(a->start_w, a->end_w, b->start_w, b->end_w) &&
          range_overlaps(a->start_d, a->end_d, b->start_d, b->end_d);
}

/* Calculate IFM job shape from OFM block considering kernel properties */
static void
calc_ifm_job_shape(const struct ethosu_block *ofm_block,
                   const struct ethosu_kernel *kernel,
                   int ifm_block_depth,
                   struct ethosu_block *ifm_job)
{
   /* Calculate dilated kernel size */
   int dilated_h = (kernel->height - 1) * kernel->dilation_y + 1;
   int dilated_w = (kernel->width - 1) * kernel->dilation_x + 1;

   /* Calculate required input size */
   int h = (ofm_block->height - 1) * kernel->stride_y + dilated_h;
   int w = (ofm_block->width - 1) * kernel->stride_x + dilated_w;

   ifm_job->height = h;
   ifm_job->width = w;
   ifm_job->depth = ifm_block_depth;
}

/* Get jobs (blocks) from a feature map area
 * area: total work area (feature map dimensions)
 * job_shape: size of each job/block
 * max_jobs: maximum number of jobs to retrieve
 * from_start: if true get first jobs, if false get last jobs
 * jobs: output array to fill
 * Returns: number of jobs added
 */
static int
get_jobs(const struct ethosu_block *area,
         const struct ethosu_block *job_shape,
         int max_jobs,
         bool from_start,
         struct box *jobs)
{
   /* Calculate how many jobs needed in each dimension */
   int job_split_h = (area->height + job_shape->height - 1) / job_shape->height;
   int job_split_w = (area->width + job_shape->width - 1) / job_shape->width;
   int job_split_d = (area->depth + job_shape->depth - 1) / job_shape->depth;

   int total_jobs = job_split_h * job_split_w * job_split_d;

   int first_job = from_start ? 0 : MAX2(0, total_jobs - max_jobs);
   int last_job = from_start ? MIN2(total_jobs, max_jobs) : total_jobs;
   int count = 0;

   /* Iterate jobs in z, x, y order (depth, width, height) */
   for (int i = first_job; i < last_job; i++) {
      int h_idx = i / (job_split_d * job_split_w);
      int w_idx = (i / job_split_d) % job_split_w;
      int d_idx = i % job_split_d;

      jobs[count].start_h = h_idx * job_shape->height;
      jobs[count].start_w = w_idx * job_shape->width;
      jobs[count].start_d = d_idx * job_shape->depth;

      jobs[count].end_h = MIN2(jobs[count].start_h + job_shape->height, area->height);
      jobs[count].end_w = MIN2(jobs[count].start_w + job_shape->width, area->width);
      jobs[count].end_d = MIN2(jobs[count].start_d + job_shape->depth, area->depth);

      count++;
   }

   return count;
}

static unsigned
calc_blockdep(struct ethosu_subgraph *subgraph, struct ethosu_operation *prev_op, struct ethosu_operation *operation)
{
   struct ethosu_ml_device *device = ethosu_ml_device(subgraph->base.device);

   if (!prev_op)
      return 0;

   /* Check if previous OFM matches current IFM (same tensor) */
   int ifm_index = 0;
   if (operation->ifm2.tensor == prev_op->ofm.tensor) {
      ifm_index = 1;
   } else if (operation->ifm.tensor != prev_op->ofm.tensor) {
      if (prev_op->type == ETHOSU_OPERATION_TYPE_NONE)
         return 0;
      else
         /* Previous operation doesn't produce current operation's IFM */
         return device->max_concurrent_blocks;
   }

   const struct ethosu_feature_map *ifm = (ifm_index == 0) ? &operation->ifm : &operation->ifm2;
   const struct ethosu_feature_map *prev_ofm = &prev_op->ofm;

   /* Check if shapes match (no reshape between operations) */
   if (ifm->shape.height != prev_ofm->shape.height ||
       ifm->shape.width != prev_ofm->shape.width ||
       ifm->shape.depth != prev_ofm->shape.depth) {
      /* OFM has been reshaped; overlap calculations don't work */
      return 0;
   }

   /* For operations with 1:1 IFM/OFM block mapping (elementwise, pooling, depthwise),
    * always use BLOCKDEP=0 for safety */
   if (operation->type == ETHOSU_OPERATION_TYPE_ELTWISE ||
       operation->type == ETHOSU_OPERATION_TYPE_POOLING ||
       (operation->type == ETHOSU_OPERATION_TYPE_CONVOLUTION && operation->conv.depthwise) ||
       prev_op->type == ETHOSU_OPERATION_TYPE_ELTWISE ||
       prev_op->type == ETHOSU_OPERATION_TYPE_POOLING ||
       (prev_op->type == ETHOSU_OPERATION_TYPE_CONVOLUTION && prev_op->conv.depthwise))
      return 0;

   /* Calculate block shapes */
   struct ethosu_block prev_block = prev_op->block_config.ofm_block;
   struct ethosu_block curr_ifm_job;
   calc_ifm_job_shape(&operation->block_config.ofm_block,
                      &operation->kernel,
                      operation->block_config.ifm_block.depth,
                      &curr_ifm_job);

   /* Get last jobs from previous operation */
   int max_jobs = device->max_concurrent_blocks;
   assert(max_jobs <= 8);
   struct box last_prev_jobs[8];
   int prev_count = get_jobs(&prev_ofm->shape, &prev_block, max_jobs, false, last_prev_jobs);

   /* Get first jobs from current operation */
   struct box first_curr_jobs[8];
   int curr_count = get_jobs(&ifm->shape, &curr_ifm_job, max_jobs, true, first_curr_jobs);

   /* Find highest blockdep with no overlap between jobs */
   int min_count = MIN2(prev_count, curr_count);
   int prev_last_idx = prev_count - 1;

   for (int blockdep = 0; blockdep < min_count; blockdep++) {
      bool overlaps = false;

      /* Check if any combination of jobs within blockdep range overlaps */
      for (int i = 0; !overlaps && i <= blockdep; i++) {
         for (int j = blockdep - i; !overlaps && i + j <= blockdep; j++) {
            if (box_overlaps(&first_curr_jobs[i], &last_prev_jobs[prev_last_idx - j])) {
               overlaps = true;
            }
         }
      }

      if (overlaps) {
         return blockdep;
      }
   }

   /* No overlap found */
   return min_count;
}

void
ethosu_emit_cmdstream(struct ethosu_subgraph *subgraph)
{
   struct ethosu_operation *prev_op = NULL;
   struct util_dynarray outstanding_dma_ops;
   struct util_dynarray outstanding_npu_ops;
   bool has_op = false;

   util_dynarray_foreach (&subgraph->operations, struct ethosu_operation, operation) {
      if (operation->type != ETHOSU_OPERATION_TYPE_NONE) {
         has_op = true;
         break;
      }
   }
   if (!has_op)
      return;

   outstanding_dma_ops = UTIL_DYNARRAY_INIT;
   outstanding_npu_ops = UTIL_DYNARRAY_INIT;

   subgraph->cmdstream_used = 32;
   subgraph->cmdstream = calloc(subgraph->cmdstream_used, sizeof(*subgraph->cmdstream));
   subgraph->cursor = subgraph->cmdstream;

   fill_memory_accesses(subgraph);

   /* Compile */

   if (ethosu_ml_device(subgraph->base.device)->is_u65)
      EMIT0(NPU_SET_PARALLEL_MODE, 0x0);

   util_dynarray_foreach (&subgraph->operations, struct ethosu_operation, operation) {

      int npu_waits, dma_waits;

      if (operation->type == ETHOSU_OPERATION_TYPE_NONE) {
         prev_op = operation;
         continue;
      }

      get_wait_dependency(subgraph, operation, &outstanding_dma_ops, &outstanding_npu_ops,
                          &npu_waits, &dma_waits);

      switch (operation->type) {
      case ETHOSU_OPERATION_TYPE_CONVOLUTION:
         emit_convolution(subgraph, operation);
         break;
      case ETHOSU_OPERATION_TYPE_POOLING:
         emit_pooling(subgraph, operation);
         break;
      case ETHOSU_OPERATION_TYPE_ELTWISE:
         emit_eltwise(subgraph, operation);
         break;
      case ETHOSU_OPERATION_TYPE_DMA:
         emit_dma(subgraph, operation);
         break;
      default:
         UNREACHABLE("Unknown operation");
         break;
      }

      if (operation->type != ETHOSU_OPERATION_TYPE_DMA) {
         unsigned blockdep = calc_blockdep(subgraph, prev_op, operation);
         EMIT0(NPU_SET_BLOCKDEP, blockdep);

         prev_op = operation;
      }

      emit_cmd_waits(subgraph, npu_waits, dma_waits);
      emit_operation_code(subgraph, operation);
   }

   EMIT0(NPU_OP_STOP, 0xffff);

   util_dynarray_fini(&outstanding_dma_ops);
   util_dynarray_fini(&outstanding_npu_ops);
}
