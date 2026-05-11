/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "jay_builder.h"
#include "jay_ir.h"

static void
set_cr0(jay_function *f, jay_cursor cursor, uint32_t *existing, uint32_t desired)
{
   /* Only touch cr0 if we are changing bits */
   if ((*existing) != desired) {
      jay_builder b = jay_init_builder(f, cursor);
      jay_def cr0 = jay_scalar(J_ARF, JAY_ARF_CONTROL);

      jay_XOR(&b, JAY_TYPE_U32, cr0, cr0, (*existing) ^ desired);
      *existing = desired;
   }
}

void
jay_insert_fp_mode(jay_shader *shader, uint32_t api, uint32_t float_sizes)
{
   /* First, work out the global float control mode for the shader */
   uint32_t global = 0x0;

   /* Initially fp16 denorms are flushed-to-zero, handle preserve. */
   if ((api & FLOAT_CONTROLS_DENORM_PRESERVE_FP16) && (float_sizes & 16)) {
      global |= BRW_CR0_FP16_DENORM_PRESERVE;
   }

   /* Initially fp32 denorms are flushed-to-zero, handle preserve.
    *
    * TODO: Optimize this, we have a dispatch bit.
    */
   if ((api & FLOAT_CONTROLS_DENORM_PRESERVE_FP32) && (float_sizes & 32)) {
      global |= BRW_CR0_FP32_DENORM_PRESERVE;
   }

   /* Initially fp64 denorms are flushed to zero, handle preserve. */
   if ((api & FLOAT_CONTROLS_DENORM_PRESERVE_FP64) && (float_sizes & 64)) {
      global |= BRW_CR0_FP64_DENORM_PRESERVE;
   }

   /* By default, we are in round-to-even mode. Note we do not permit setting
    * round mode separately by bitsize but this is ok for current APIs. The
    * Vulkan driver sets roundingModeIndependence = NONE.
    *
    * TODO: Optimize this, there is a command buffer bit for it.
    */
   if (((api & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16) && (float_sizes & 16)) ||
       ((api & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32) && (float_sizes & 32)) ||
       ((api & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64) && (float_sizes & 64))) {
      global |= (BRW_RND_MODE_RTZ << BRW_CR0_RND_MODE_SHIFT);
   }

   uint32_t cr0 = 0;
   jay_function *entrypoint = jay_shader_get_entrypoint(shader);
   set_cr0(entrypoint, jay_before_function(entrypoint), &cr0, global);

   /* Now handle per-instruction deltas to the global mode */
   jay_foreach_function(shader, func) {
      jay_foreach_block(func, block) {
         uint32_t current = cr0;

         jay_foreach_inst_in_block(block, I) {
            uint32_t required = cr0;
            enum jay_rounding_mode round =
               (I->op == JAY_OPCODE_CVT) ? jay_cvt_rounding_mode(I) : JAY_ROUND;

            if (round != JAY_ROUND) {
               required &= ~BRW_CR0_RND_MODE_MASK;
               required |= ((round - JAY_RNE) << BRW_CR0_RND_MODE_SHIFT);
            }

            if (jay_type_is_any_float(I->type)) {
               set_cr0(func, jay_before_inst(I), &current, required);
            }
         }

         /* Restore to global state on block boundaries */
         if (jay_num_successors(block, GPR) > 0) {
            set_cr0(func, jay_after_block(block), &current, cr0);
         }
      }
   }
}
