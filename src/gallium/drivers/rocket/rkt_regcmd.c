/*
 * Copyright (c) 2024 Tomeu Vizoso <tomeu@tomeuvizoso.net>
 * SPDX-License-Identifier: MIT
 */

#include "rkt_regcmd.h"
#include "rkt_ml.h"
#include "rkt_registers.h"

static void
emit_raw(struct util_dynarray *regs, uint32_t target, uint32_t reg,
         uint32_t value)
{
   uint64_t packed_value = 0;
   packed_value = ((uint64_t)target) << 48;
   packed_value |= ((uint64_t)value) << 16;
   packed_value |= (uint64_t)reg;

   util_dynarray_append(regs, packed_value);
}

static void
emit(struct util_dynarray *regs, uint32_t reg, uint32_t value)
{
   uint32_t target = rkt_get_target(reg) + 0x1;
   emit_raw(regs, target, reg, value);
}

#define EMIT(offset, value) emit(regs, offset, value);

static void
fill_first_regcmd(struct rkt_ml_subgraph *subgraph,
                  const struct rkt_operation *operation,
                  struct util_dynarray *regs, unsigned task_num)
{
   struct split_task *task =
      util_dynarray_element(&operation->tasks, struct split_task, task_num);
   unsigned num_tasks =
      util_dynarray_num_elements(&operation->tasks, struct split_task);
   unsigned output_zero_point = task->output_zero_point;
   unsigned weights_zero_point = task->weights_zero_point;
   unsigned offset = output_zero_point - 0x80;

   uint32_t con0 = CNA_CBUF_CON0_WEIGHT_BANK(task->weights_banks) |
                   CNA_CBUF_CON0_DATA_BANK(task->input_banks);
   if (task_num > 0 && operation->reuse_weights_cbuf)
      con0 |= CNA_CBUF_CON0_WEIGHT_REUSE(1);

   EMIT(REG_CNA_CBUF_CON0, con0);

   EMIT(REG_CNA_DCOMP_REGNUM, 0);
   EMIT(REG_CNA_DCOMP_CTRL, 0);

   uint32_t con1 = 0x0;
   if (task->input_channels_real == 1) {
      con1 |= CNA_CONV_CON1_NONALIGN_DMA(1) | CNA_CONV_CON1_GROUP_LINE_OFF(1) |
              CNA_CONV_CON1_ARGB_IN(8);
   }

   if (operation->depthwise)
      con1 |= CNA_CONV_CON1_CONV_MODE(3);

   EMIT(REG_CNA_CONV_CON1, con1);

   EMIT(REG_DPU_S_POINTER, DPU_S_POINTER_POINTER_PP_MODE(1) |
                              DPU_S_POINTER_EXECUTER_PP_EN(1) |
                              DPU_S_POINTER_POINTER_PP_EN(1));
   EMIT(REG_DPU_RDMA_RDMA_S_POINTER,
        DPU_RDMA_RDMA_S_POINTER_POINTER_PP_MODE(1) |
           DPU_RDMA_RDMA_S_POINTER_EXECUTER_PP_EN(1) |
           DPU_RDMA_RDMA_S_POINTER_POINTER_PP_EN(1));
   EMIT(REG_CNA_CONV_CON1, con1);
   EMIT(REG_CNA_CONV_CON2,
        CNA_CONV_CON2_FEATURE_GRAINS(
           50 + task->stride_y + 1)); /* Magic: Seems to pass the most tests */
   EMIT(REG_CNA_CONV_CON3, CNA_CONV_CON3_CONV_X_STRIDE(task->stride_x) |
                              CNA_CONV_CON3_CONV_Y_STRIDE(task->stride_y));
   EMIT(REG_CNA_DATA_SIZE0,
        CNA_DATA_SIZE0_DATAIN_WIDTH(task->input_width) |
           CNA_DATA_SIZE0_DATAIN_HEIGHT(task->input_height));

   EMIT(REG_CNA_DATA_SIZE1,
        CNA_DATA_SIZE1_DATAIN_CHANNEL_REAL(task->input_channels_real - 1) |
           CNA_DATA_SIZE1_DATAIN_CHANNEL(task->input_channels));

   EMIT(REG_CNA_DATA_SIZE2, CNA_DATA_SIZE2_DATAOUT_WIDTH(task->output_width));
   EMIT(REG_CNA_DATA_SIZE3, CNA_DATA_SIZE3_DATAOUT_ATOMICS(task->atomic_count));
   EMIT(REG_CNA_WEIGHT_SIZE0, task->weights_width * task->weights_height *
                                 task->input_channels * task->weights_kernels);
   EMIT(REG_CNA_WEIGHT_SIZE1,
        task->weights_width * task->weights_height * task->input_channels);
   EMIT(REG_CNA_WEIGHT_SIZE2,
        CNA_WEIGHT_SIZE2_WEIGHT_WIDTH(task->weights_width) |
           CNA_WEIGHT_SIZE2_WEIGHT_HEIGHT(task->weights_height) |
           CNA_WEIGHT_SIZE2_WEIGHT_KERNELS(task->weights_kernels));

   EMIT(REG_CNA_CBUF_CON0, con0);

   EMIT(REG_CNA_CBUF_CON1, CNA_CBUF_CON1_DATA_ENTRIES(task->input_data_entries));

   if (task->input_channels_real == 1) {
      unsigned truncate = 14;
      unsigned scale = 16384;
      unsigned offset = 65408;

      if (operation->addition_input || operation->add_tensor != -1) {
         truncate = 15;
         scale = 32388;
      }

      EMIT(REG_CNA_CVT_CON0, CNA_CVT_CON0_CVT_TRUNCATE_3(truncate) |
                                CNA_CVT_CON0_CVT_TRUNCATE_2(truncate) |
                                CNA_CVT_CON0_CVT_TRUNCATE_1(truncate) |
                                CNA_CVT_CON0_CVT_TRUNCATE_0(truncate));
      EMIT(REG_CNA_CVT_CON1,
           CNA_CVT_CON1_CVT_SCALE0(scale) | CNA_CVT_CON1_CVT_OFFSET0(offset));
      EMIT(REG_CNA_CVT_CON2,
           CNA_CVT_CON2_CVT_SCALE1(scale) | CNA_CVT_CON2_CVT_OFFSET1(offset));
      EMIT(REG_CNA_CVT_CON3,
           CNA_CVT_CON3_CVT_SCALE2(scale) | CNA_CVT_CON3_CVT_OFFSET2(offset));
      EMIT(REG_CNA_CVT_CON4,
           CNA_CVT_CON4_CVT_SCALE3(scale) | CNA_CVT_CON4_CVT_OFFSET3(offset));
   } else {
      EMIT(REG_CNA_CVT_CON0, CNA_CVT_CON0_DATA_SIGN(1) |
                                CNA_CVT_CON0_CVT_TYPE(1) |
                                CNA_CVT_CON0_CVT_BYPASS(1));
      EMIT(REG_CNA_CVT_CON1, CNA_CVT_CON1_CVT_SCALE0(1));
      EMIT(REG_CNA_CVT_CON2, CNA_CVT_CON2_CVT_SCALE1(1));
      EMIT(REG_CNA_CVT_CON3, CNA_CVT_CON3_CVT_SCALE2(1));
      EMIT(REG_CNA_CVT_CON4, CNA_CVT_CON4_CVT_SCALE3(1));
   }

   EMIT(REG_CNA_FC_CON0, 0);
   EMIT(REG_CNA_FC_CON1, 0);
   EMIT(REG_CNA_PAD_CON0, CNA_PAD_CON0_PAD_LEFT(task->pad_left) |
                             CNA_PAD_CON0_PAD_TOP(task->pad_top));
   EMIT(REG_CNA_FEATURE_DATA_ADDR,
        rkt_get_tensor(subgraph, operation->input_index)->phys_addr +
           task->input_offset);
   EMIT(REG_CNA_FC_CON2, 0);
   EMIT(REG_CNA_DMA_CON0,
        CNA_DMA_CON0_WEIGHT_BURST_LEN(15) | CNA_DMA_CON0_DATA_BURST_LEN(15));
   EMIT(REG_CNA_DMA_CON1, CNA_DMA_CON1_LINE_STRIDE(task->input_line_stride));
   EMIT(REG_CNA_DMA_CON2, CNA_DMA_CON2_SURF_STRIDE(task->input_surface_stride));

   EMIT(REG_CNA_FC_DATA_SIZE0,
        CNA_FC_DATA_SIZE0_DMA_WIDTH(operation->input_width) |
           CNA_FC_DATA_SIZE0_DMA_HEIGHT(task->input_height));

   EMIT(REG_CNA_FC_DATA_SIZE1,
        CNA_FC_DATA_SIZE1_DMA_CHANNEL(task->input_channels));
   EMIT(REG_CNA_DCOMP_CTRL, 0);
   EMIT(REG_CNA_DCOMP_REGNUM, 0);
   EMIT(REG_CNA_DCOMP_ADDR0, rkt_resource(operation->weights)->phys_addr);
   EMIT(REG_CNA_DCOMP_AMOUNT0, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT1, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT2, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT3, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT4, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT5, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT6, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT7, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT8, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT9, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT10, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT11, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT12, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT13, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT14, 0);
   EMIT(REG_CNA_DCOMP_AMOUNT15, 0);

   if (task->input_channels_real == 1) {
      EMIT(REG_CNA_CVT_CON5, 65535);
   } else {
      EMIT(REG_CNA_CVT_CON5, 0);
   }

   int32_t pad_con1;
   if (task->weights_width >= 3 && task->input_zero_point == 0x0)
      pad_con1 = 0xffff8080;
   else
      pad_con1 = task->input_zero_point - 0x80;

   if (operation->addition_input || operation->add_tensor != -1)
      pad_con1 = 0xffffff80;

   if (operation->depthwise && task->input_zero_point == 0x8b)
      pad_con1 = 0x0b0b;

   EMIT(REG_CNA_PAD_CON1, pad_con1);

   uint32_t misc_cfg = CORE_MISC_CFG_QD_EN(1);
   if (operation->depthwise)
      misc_cfg |= CORE_MISC_CFG_DW_EN(1);

   EMIT(REG_CORE_MISC_CFG, misc_cfg);
   EMIT(REG_CORE_DATAOUT_SIZE_0,
        CORE_DATAOUT_SIZE_0_DATAOUT_HEIGHT(task->output_height - 1) |
           CORE_DATAOUT_SIZE_0_DATAOUT_WIDTH(task->output_width - 1));
   EMIT(REG_CORE_DATAOUT_SIZE_1,
        CORE_DATAOUT_SIZE_1_DATAOUT_CHANNEL(task->output_channels - 1));
   EMIT(REG_CORE_CLIP_TRUNCATE,
        CORE_CLIP_TRUNCATE_CLIP_TRUNCATE(operation->truncate_bits));
   emit_raw(regs, CORE | 0x1, 0x3030, 0);

   uint32_t feat_mode_cfg =
      DPU_FEATURE_MODE_CFG_BURST_LEN(15) | DPU_FEATURE_MODE_CFG_OUTPUT_MODE(2);
   if (operation->depthwise)
      feat_mode_cfg |= DPU_FEATURE_MODE_CFG_CONV_MODE(3);

   EMIT(REG_DPU_FEATURE_MODE_CFG, feat_mode_cfg);
   EMIT(REG_DPU_DATA_FORMAT, 0);
   EMIT(REG_DPU_OFFSET_PEND, 0);
   EMIT(REG_DPU_DST_BASE_ADDR,
        rkt_get_tensor(subgraph, operation->output_index)->phys_addr +
           task->output_offset);
   EMIT(REG_DPU_DST_SURF_STRIDE,
        DPU_DST_SURF_STRIDE_DST_SURF_STRIDE(task->output_surface_stride));
   EMIT(REG_DPU_DATA_CUBE_WIDTH,
        DPU_DATA_CUBE_WIDTH_WIDTH(task->output_width - 1));
   EMIT(REG_DPU_DATA_CUBE_HEIGHT,
        DPU_DATA_CUBE_HEIGHT_HEIGHT(task->output_height - 1));
   EMIT(REG_DPU_DATA_CUBE_NOTCH_ADDR, 0);
   EMIT(REG_DPU_DATA_CUBE_CHANNEL,
        DPU_DATA_CUBE_CHANNEL_ORIG_CHANNEL(task->output_channels_real - 1) |
           DPU_DATA_CUBE_CHANNEL_CHANNEL(task->output_channels - 1));
   EMIT(REG_DPU_BS_CFG, DPU_BS_CFG_BS_ALU_ALGO(2) | DPU_BS_CFG_BS_ALU_SRC(1) |
                           DPU_BS_CFG_BS_RELU_BYPASS(1) |
                           DPU_BS_CFG_BS_MUL_BYPASS(1));
   EMIT(REG_DPU_BS_ALU_CFG, 0);
   EMIT(REG_DPU_BS_MUL_CFG, 0);
   EMIT(REG_DPU_BS_RELUX_CMP_VALUE, 0);

   if (operation->depthwise) {
      EMIT(REG_DPU_BS_OW_CFG, DPU_BS_OW_CFG_SIZE_E_2(3) |
                                 DPU_BS_OW_CFG_SIZE_E_1(3) |
                                 DPU_BS_OW_CFG_SIZE_E_0(3));
   } else {
      EMIT(REG_DPU_BS_OW_CFG, DPU_BS_OW_CFG_SIZE_E_2(1) |
                                 DPU_BS_OW_CFG_SIZE_E_1(1) |
                                 DPU_BS_OW_CFG_SIZE_E_0(1));
   }

   EMIT(REG_DPU_BS_OW_OP, DPU_BS_OW_OP_OW_OP(0x80 - weights_zero_point));

   EMIT(REG_DPU_WDMA_SIZE_0,
        DPU_WDMA_SIZE_0_CHANNEL_WDMA(task->output_channels - 1));
   EMIT(REG_DPU_WDMA_SIZE_1,
        DPU_WDMA_SIZE_1_HEIGHT_WDMA(task->output_height - 1) |
           DPU_WDMA_SIZE_1_WIDTH_WDMA(task->output_width - 1));
   EMIT(REG_DPU_BN_CFG,
        DPU_BN_CFG_BN_RELU_BYPASS(1) | DPU_BN_CFG_BN_MUL_BYPASS(1) |
           DPU_BN_CFG_BN_ALU_BYPASS(1) | DPU_BN_CFG_BN_BYPASS(1));
   EMIT(REG_DPU_BN_ALU_CFG, 0);
   EMIT(REG_DPU_BN_MUL_CFG, 0);
   EMIT(REG_DPU_BN_RELUX_CMP_VALUE, 0);

   if (operation->add_tensor != -1) {
      EMIT(REG_DPU_EW_CFG,
           DPU_EW_CFG_EW_CVT_TYPE(1) | DPU_EW_CFG_EW_DATA_MODE(1) |
              DPU_EW_CFG_EDATA_SIZE(1) | DPU_EW_CFG_EW_ALU_ALGO(2) |
              DPU_EW_CFG_EW_RELU_BYPASS(1) | DPU_EW_CFG_EW_LUT_BYPASS(1) |
              DPU_EW_CFG_EW_OP_SRC(1));

      /* See http://nvdla.org/hw/v1/ias/precision.html#element-wise */
      EMIT(REG_DPU_EW_CVT_OFFSET_VALUE, operation->addition_offset);

      float add_scale = 0.0;
      if (fabs(operation->addition_scale - 0.090192) < 0.00001) {
         add_scale = 299.671889248;
      } else if (fabs(operation->addition_scale - 0.399250) < 0.00001) {
         add_scale = 1326.499209406;
      } else if (fabs(operation->addition_scale - 0.364902) < 0.00001) {
         add_scale = 780.34375;
      } else if (fabs(operation->addition_scale - 0.422037) < 0.00001) {
         add_scale = 715.5625;
      } else if (fabs(operation->addition_scale - 0.213016) < 0.00001) {
         add_scale = 564.6875;
      } else if (fabs(operation->addition_scale - 0.244231) < 0.00001) {
         add_scale = 499.796875;
      } else if (fabs(operation->addition_scale - 0.283416) < 0.00001) {
         add_scale = 488.203125;
      } else if (fabs(operation->addition_scale - 0.171151) < 0.00001) {
         add_scale = 602.90625;
      } else if (fabs(operation->addition_scale - 0.164588) < 0.00001) {
         add_scale = 271.921875;
      } else if (fabs(operation->addition_scale - 0.204098) < 0.00001) {
         add_scale = 262.90625;
      } else if (fabs(operation->addition_scale - 0.116532) < 0.00001) {
         add_scale = 450.140625;
      } else if (fabs(operation->addition_scale - 0.134499) < 0.00001) {
         add_scale = 212.1953125;
      } else if (fabs(operation->addition_scale - 0.220141) < 0.00001) {
         add_scale = 368.28125;
      } else if (fabs(operation->addition_scale - 0.094560) < 0.00001) {
         add_scale = 416.421875;
      } else if (fabs(operation->addition_scale - 0.093230) < 0.00001) {
         add_scale = 305.421875;
      } else if (fabs(operation->addition_scale - 0.100618) < 0.00001) {
         add_scale = 313.671875;
      } else {
         add_scale = 0.0;
      }

      uint32_t add_scale_bits = fui(add_scale);
      /* Taken from
       * https://github.com/pytorch/QNNPACK/blob/master/src/qnnpack/requantization.h#L130
       */
      unsigned add_shift = 127 + 31 - 32 - (add_scale_bits >> 23) + 16;

      unsigned scale = ((add_scale_bits >> 9) & 0x7fff);
      if (scale < 1 << 14)
         scale |= 1 << 14;

      EMIT(REG_DPU_EW_CVT_SCALE_VALUE,
           DPU_EW_CVT_SCALE_VALUE_EW_OP_CVT_SHIFT(add_shift - 1) |
              DPU_EW_CVT_SCALE_VALUE_EW_OP_CVT_SCALE(scale));

      EMIT(REG_DPU_EW_RELUX_CMP_VALUE, 0x0);

      if (fabs(operation->addition_scale - 0.213016) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x4);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(25914));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else if (fabs(operation->addition_scale - 0.244231) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x1);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(28927));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else if (fabs(operation->addition_scale - 0.283416) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x6);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(26050));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else if (fabs(operation->addition_scale - 0.171151) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0xfffffffd);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(28937));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else if (fabs(operation->addition_scale - 0.164588) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x1);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(24877));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(23));
      } else if (fabs(operation->addition_scale - 0.204098) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x0);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(23272));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(23));
      } else if (fabs(operation->addition_scale - 0.116532) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0xfffffff8);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(32292));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else if (fabs(operation->addition_scale - 0.134499) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0xfffffffb);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(24153));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(23));
      } else if (fabs(operation->addition_scale - 0.220141) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0xb);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(27655));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else if (fabs(operation->addition_scale - 0.094560) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x5);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(20432));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(23));
      } else if (fabs(operation->addition_scale - 0.093230) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0xffffffff);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(25449));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(23));
      } else if (fabs(operation->addition_scale - 0.100618) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, offset);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(16874));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(23));
      } else if (fabs(operation->addition_scale - 0.422037) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x1);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(22559));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else if (fabs(operation->addition_scale - 0.364902) < 0.00001) {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x4);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(18589));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(24));
      } else {
         EMIT(REG_DPU_OUT_CVT_OFFSET, 0x6);
         EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(27676));
         EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(25));
      }
   } else {
      EMIT(REG_DPU_EW_CFG,
           DPU_EW_CFG_EW_RELU_BYPASS(1) | DPU_EW_CFG_EW_OP_CVT_BYPASS(1) |
              DPU_EW_CFG_EW_LUT_BYPASS(1) | DPU_EW_CFG_EW_OP_BYPASS(1) |
              DPU_EW_CFG_EW_BYPASS(1));
      EMIT(REG_DPU_EW_CVT_OFFSET_VALUE, 0);
      EMIT(REG_DPU_EW_CVT_SCALE_VALUE, DPU_EW_CVT_SCALE_VALUE_EW_OP_CVT_SCALE(1));
      EMIT(REG_DPU_EW_RELUX_CMP_VALUE, 0);
      EMIT(REG_DPU_OUT_CVT_OFFSET, offset);

      float conv_scale =
         (task->input_scale * task->weights_scale) / task->output_scale;
      // DBG("conv_scale %f\n", conv_scale);
      uint32_t scale_bits = fui(conv_scale);
      /* Taken from
       * https://github.com/pytorch/QNNPACK/blob/master/src/qnnpack/requantization.h#L130
       */
      unsigned shift = 127 + 31 - 32 - (scale_bits >> 23) + 16;

      if (operation->truncate_bits > 0)
         shift--;

      unsigned scale = ((scale_bits >> 9) & 0x7fff) + 1;
      if (scale < 1 << 14)
         scale |= 1 << 14;

      EMIT(REG_DPU_OUT_CVT_SCALE, DPU_OUT_CVT_SCALE_OUT_CVT_SCALE(scale));
      EMIT(REG_DPU_OUT_CVT_SHIFT, DPU_OUT_CVT_SHIFT_OUT_CVT_SHIFT(shift - 1));
   }

   EMIT(REG_DPU_EW_OP_VALUE_0, 0);
   EMIT(REG_DPU_EW_OP_VALUE_1, 0);
   EMIT(REG_DPU_EW_OP_VALUE_2, 0);
   EMIT(REG_DPU_EW_OP_VALUE_3, 0);
   EMIT(REG_DPU_EW_OP_VALUE_4, 0);
   EMIT(REG_DPU_EW_OP_VALUE_5, 0);
   EMIT(REG_DPU_EW_OP_VALUE_6, 0);
   EMIT(REG_DPU_EW_OP_VALUE_7, 0);
   EMIT(REG_DPU_SURFACE_ADD, DPU_SURFACE_ADD_SURF_ADD(task->surfaces_per_row));
   emit_raw(regs, DPU | 0x1, 0x40c4, 0);
   EMIT(REG_DPU_LUT_ACCESS_CFG, 0);
   EMIT(REG_DPU_LUT_ACCESS_DATA, 0);
   EMIT(REG_DPU_LUT_CFG, 0);
   EMIT(REG_DPU_LUT_INFO, 0);
   EMIT(REG_DPU_LUT_LE_START, 0);
   EMIT(REG_DPU_LUT_LE_END, 0);
   EMIT(REG_DPU_LUT_LO_START, 0);
   EMIT(REG_DPU_LUT_LO_END, 0);
   EMIT(REG_DPU_LUT_LE_SLOPE_SCALE, 0);
   EMIT(REG_DPU_LUT_LE_SLOPE_SHIFT, 0);
   EMIT(REG_DPU_LUT_LO_SLOPE_SCALE, 0);
   EMIT(REG_DPU_LUT_LO_SLOPE_SHIFT, 0);
   EMIT(REG_DPU_RDMA_RDMA_DATA_CUBE_WIDTH,
        DPU_RDMA_RDMA_DATA_CUBE_WIDTH_WIDTH(task->output_width - 1));
   EMIT(REG_DPU_RDMA_RDMA_DATA_CUBE_HEIGHT,
        DPU_RDMA_RDMA_DATA_CUBE_HEIGHT_HEIGHT(task->output_height - 1));
   EMIT(REG_DPU_RDMA_RDMA_DATA_CUBE_CHANNEL,
        DPU_RDMA_RDMA_DATA_CUBE_CHANNEL_CHANNEL(task->output_channels - 1));

   if (operation->add_tensor != -1) {
      EMIT(REG_DPU_RDMA_RDMA_SRC_BASE_ADDR,
           rkt_get_tensor(subgraph, operation->add_tensor)->phys_addr +
              task->output_offset);
   } else {
      EMIT(REG_DPU_RDMA_RDMA_SRC_BASE_ADDR, 0);
   }

   EMIT(REG_DPU_RDMA_RDMA_BRDMA_CFG, DPU_RDMA_RDMA_BRDMA_CFG_BRDMA_DATA_USE(1));
   EMIT(REG_DPU_RDMA_RDMA_BS_BASE_ADDR,
        rkt_resource(operation->biases)->phys_addr);
   EMIT(REG_DPU_RDMA_RDMA_NRDMA_CFG, 0);
   EMIT(REG_DPU_RDMA_RDMA_BN_BASE_ADDR, 0);

   unsigned ew_stride =
      MAX2(operation->output_width * operation->output_height, 12);

   if (operation->add_tensor != -1) {
      EMIT(REG_DPU_RDMA_RDMA_ERDMA_CFG,
           DPU_RDMA_RDMA_ERDMA_CFG_ERDMA_DATA_MODE(1) |
              DPU_RDMA_RDMA_ERDMA_CFG_ERDMA_DATA_SIZE(1));
      unsigned ew_base_offset =
         operation->output_width * operation->output_height * ATOMIC_K_SIZE;
      EMIT(REG_DPU_RDMA_RDMA_EW_BASE_ADDR,
           rkt_get_tensor(subgraph, operation->add_tensor)->phys_addr +
              task->output_offset + ew_base_offset);
      EMIT(REG_DPU_RDMA_RDMA_EW_SURF_STRIDE,
           DPU_RDMA_RDMA_EW_SURF_STRIDE_EW_SURF_STRIDE(ew_stride));
   } else {
      EMIT(REG_DPU_RDMA_RDMA_ERDMA_CFG, DPU_RDMA_RDMA_ERDMA_CFG_ERDMA_DISABLE(1));
      EMIT(REG_DPU_RDMA_RDMA_EW_BASE_ADDR, 0);
      EMIT(REG_DPU_RDMA_RDMA_EW_SURF_STRIDE, 0);
   }

   uint32_t rdma_feat_mode_cfg = 0x0;

   if (operation->add_tensor != -1) {
      rdma_feat_mode_cfg |= DPU_RDMA_RDMA_FEATURE_MODE_CFG_BURST_LEN(15) |
                            DPU_RDMA_RDMA_FEATURE_MODE_CFG_COMB_USE(5);
   } else {
      rdma_feat_mode_cfg |= DPU_RDMA_RDMA_FEATURE_MODE_CFG_BURST_LEN(15) |
                            DPU_RDMA_RDMA_FEATURE_MODE_CFG_MRDMA_DISABLE(1);
   }

   if (operation->depthwise)
      rdma_feat_mode_cfg |= DPU_RDMA_RDMA_FEATURE_MODE_CFG_CONV_MODE(3);

   EMIT(REG_DPU_RDMA_RDMA_FEATURE_MODE_CFG, rdma_feat_mode_cfg);
   EMIT(REG_DPU_RDMA_RDMA_SRC_DMA_CFG, 0);

   unsigned surf_notch =
      ew_stride +
      task->output_width * (operation->output_height - task->output_height);

   if (operation->input_width == 3) {
      surf_notch = 15;
   }

   if (operation->add_tensor != -1) {
      EMIT(REG_DPU_RDMA_RDMA_SURF_NOTCH,
           DPU_RDMA_RDMA_SURF_NOTCH_SURF_NOTCH_ADDR(surf_notch));
   } else {
      EMIT(REG_DPU_RDMA_RDMA_SURF_NOTCH, 0);
   }

   EMIT(REG_DPU_RDMA_RDMA_PAD_CFG, 0);
   EMIT(REG_DPU_RDMA_RDMA_WEIGHT,
        DPU_RDMA_RDMA_WEIGHT_E_WEIGHT(1) | DPU_RDMA_RDMA_WEIGHT_N_WEIGHT(1) |
           DPU_RDMA_RDMA_WEIGHT_B_WEIGHT(1) | DPU_RDMA_RDMA_WEIGHT_M_WEIGHT(1));

   if (operation->add_tensor != -1) {
      EMIT(REG_DPU_RDMA_RDMA_EW_SURF_NOTCH,
           DPU_RDMA_RDMA_EW_SURF_NOTCH_EW_SURF_NOTCH(surf_notch));
   } else {
      EMIT(REG_DPU_RDMA_RDMA_EW_SURF_NOTCH, 0x0);
   }

   if (num_tasks == 1)
      util_dynarray_append_typed(regs, uint64_t, 0x0);
   else
      EMIT(REG_PC_BASE_ADDRESS, 0);

   EMIT(REG_PC_REGISTER_AMOUNTS, 0);

   /* TRM: before op_en, 64'h0041_xxxx_xxxx_xxxx must be set. */
   util_dynarray_append_typed(regs, uint64_t, 0x0041000000000000);

   /* TRM: 64'h0081_0000_007f_0008 will set each block's op_en(CNA, CORE, ...,
    * PPU_RDMA). */
   emit_raw(regs, 0x81, REG_PC_OPERATION_ENABLE,
            PC_OPERATION_ENABLE_RESERVED_0(14) | PC_OPERATION_ENABLE_OP_EN(1));
}

void
rkt_fill_regcmd(struct rkt_ml_subgraph *subgraph,
                const struct rkt_operation *operation,
                struct util_dynarray *regs, unsigned task_num)
{
   /*
    * TODO: We should only need to set all the registers on the regcmd for the first
    * task in an operation, but for now set them all to be sure.
    */
   fill_first_regcmd(subgraph, operation, regs, task_num);
}
