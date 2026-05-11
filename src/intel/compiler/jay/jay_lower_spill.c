/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/brw/brw_eu_defines.h"
#include "jay_builder.h"
#include "jay_builder_opcodes.h"
#include "jay_ir.h"
#include "jay_opcodes.h"
#include "jay_private.h"

/* We reserve an address register for spilling by ABI */
#define ADDRESS_REG jay_bare_reg(J_ADDRESS, 2)

static void
insert_spill_fill(jay_builder *b,
                  jay_def mem,
                  jay_def gpr,
                  jay_def sp,
                  bool load,
                  unsigned *sp_delta_B)
{
   assert(mem.file == MEM && gpr.file != MEM);

   unsigned offs_B = mem.reg * 4;
   unsigned mem_reg_B = offs_B * b->shader->dispatch_width;

   /* The stack pointer needs to be offset to the desired offset */
   signed sp_adjust_B = mem_reg_B - (*sp_delta_B);
   if (sp_adjust_B) {
      jay_ADD(b, JAY_TYPE_U32, sp, sp, sp_adjust_B);
      *sp_delta_B = mem_reg_B;
   }

   const struct intel_device_info *devinfo = b->shader->devinfo;
   unsigned cache = load ? LSC_CACHE(devinfo, LOAD, L1STATE_L3MOCS) :
                           LSC_CACHE(devinfo, STORE, L1STATE_L3MOCS);
   uint32_t desc = lsc_msg_desc(devinfo, load ? LSC_OP_LOAD : LSC_OP_STORE,
                                LSC_ADDR_SURFTYPE_SS, LSC_ADDR_SIZE_A32,
                                LSC_DATA_SIZE_D32, 1, false, cache);
   jay_def srcs[] = { sp, gpr };

   jay_SEND(b, .sfid = BRW_SFID_UGM, .msg_desc = desc, .srcs = srcs,
            .nr_srcs = load ? 1 : 2, .dst = load ? gpr : jay_null(),
            .type = JAY_TYPE_U32, .ex_desc = ADDRESS_REG);
}

void
jay_lower_spill(jay_function *func)
{
   jay_builder b = jay_init_builder(func, jay_before_function(func));

   /* We reserve the top UGPRs for spilling by ABI */
   unsigned ugpr_reservation = func->shader->num_regs[UGPR];
   assert(util_is_aligned(ugpr_reservation + 1, func->shader->dispatch_width));

   jay_def surf = jay_bare_reg(UGPR, ugpr_reservation);
   jay_def sp = jay_bare_reg(UGPR, ugpr_reservation + 1);
   sp.num_values_m1 = func->shader->dispatch_width - 1;

   /* Calculate how much stack space we need */
   unsigned nr_mem = 0;
   jay_foreach_inst_in_func(func, block, I) {
      if (I->op == JAY_OPCODE_MOV && jay_is_send_like(I)) {
         jay_def mem = I->dst.file == MEM ? I->dst : I->src[0];
         nr_mem = MAX2(nr_mem, mem.reg + 1);
      }
   }

   assert(nr_mem > 0);

   /* We burn the address & stack pointer registers for all spills/fills in a
    * shader. Preinitialize at the top using a scratch register.
    *
    * TODO: Need ABI for multi-function.
    */
   assert(func->is_entrypoint);
   jay_AND(&b, JAY_TYPE_U32, surf, jay_bare_reg(UGPR, 5), ~BITFIELD_MASK(10));
   jay_SHR(&b, JAY_TYPE_U32, ADDRESS_REG, surf, 4);

   /* We use a 32-bit strided stack: SP = scratch + (lane ID * 4) */
   jay_def tmp2 = jay_bare_reg(GPR, func->shader->partition.base2);
   jay_LANE_ID_8(&b, tmp2);
   for (unsigned i = 8; i < b.shader->dispatch_width; i *= 2) {
      jay_LANE_ID_EXPAND(&b, tmp2, tmp2, i);
   }

   jay_SHL(&b, JAY_TYPE_U16, tmp2, tmp2, util_logbase2(4));
   jay_CVT(&b, JAY_TYPE_U32, sp, tmp2, JAY_TYPE_U16, JAY_ROUND, 0);
   if (b.shader->scratch_size) {
      jay_ADD(&b, JAY_TYPE_U32, sp, sp, b.shader->scratch_size);
   }

   jay_foreach_block(func, block) {
      /* We offset the stack pointer locally within a block to form offsets. By
       * contract keep it in its canonical (unoffset) form at block boundaries.
       */
      unsigned sp_delta_B = 0;
      bool address_valid = true;

      jay_foreach_inst_in_block_safe(block, I) {
         b.cursor = jay_before_inst(I);

         if (I->op == JAY_OPCODE_MOV && jay_is_send_like(I)) {
            if (!address_valid) {
               jay_SHR(&b, JAY_TYPE_U32, ADDRESS_REG, surf, 4);
               address_valid = true;
            }

            if (I->dst.file == MEM) {
               insert_spill_fill(&b, I->dst, I->src[0], sp, false, &sp_delta_B);
               func->shader->spills++;
            } else {
               insert_spill_fill(&b, I->src[0], I->dst, sp, true, &sp_delta_B);
               func->shader->fills++;
            }

            jay_remove_instruction(I);
         } else if (I->op == JAY_OPCODE_SHUFFLE) {
            /* Shuffles implicitly clobber the address register so we'll need to
             * rematerialize the surface state (but be lazy).
             */
            address_valid = false;
         }
      }

      /* Canonicalize our internal registers at block boundaries */
      if (jay_num_successors(block, GPR) > 0) {
         if (!address_valid) {
            jay_SHR(&b, JAY_TYPE_U32, ADDRESS_REG, surf, 4);
         }

         if (sp_delta_B > 0) {
            jay_ADD(&b, JAY_TYPE_U32, sp, sp, -sp_delta_B);
         }
      }
   }

   /* Note this is bogus with recursion, but recursion is not supported on any
    * current graphics/compute API.
    */
   func->shader->scratch_size += func->shader->dispatch_width * nr_mem * 4;
}
