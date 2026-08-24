/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

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

   jay_SEND(b, .sfid = GEN_SFID_UGM, .msg_desc = desc, .srcs = srcs,
            .nr_srcs = load ? 1 : 2, .dst = load ? gpr : jay_null(),
            .type = JAY_TYPE_U32, .ex_desc = ADDRESS_REG);
}

void
jay_lower_spill(jay_function *func)
{
   jay_builder b = jay_init_builder(func, jay_before_function(func));
   signed ugpr_reservation = -1;

   /* We reserved a block of UGPRs for our use */
   for (unsigned i = 0; i < func->shader->partition.nr_blocks[UGPR]; ++i) {
      struct jay_register_block B = func->shader->partition.blocks[UGPR][i];

      if (B.type == JAY_BLOCK_SPILL) {
         ugpr_reservation = B.start_gpr;
      }
   }

   assert(ugpr_reservation >= 0 && "must have reserved something");

   jay_def sp_0 = jay_bare_reg(UGPR, ugpr_reservation);
   jay_def sp =
      jay_bare_regs(UGPR, ugpr_reservation, func->shader->dispatch_width);

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
   jay_AND(&b, JAY_TYPE_U32, sp_0, jay_bare_reg(UGPR, 5), ~BITFIELD_MASK(10));
   jay_SHR(&b, JAY_TYPE_U32, ADDRESS_REG, sp_0, 4);

   /* We use a 32-bit strided stack: SP = scratch + (lane ID * 4) */
   unsigned disp_width = b.shader->dispatch_width;
   jay_LANE_ID_8(&b, jay_bare_regs(UGPR, ugpr_reservation, 4), 0);

   if (disp_width >= 16) {
      jay_LANE_ID_8(&b, jay_bare_regs(UGPR, ugpr_reservation + 4, 4), 8);
   }

   if (disp_width >= 32) {
      jay_ADD(&b, JAY_TYPE_U16, jay_bare_regs(UGPR, ugpr_reservation + 8, 8),
              jay_bare_regs(UGPR, ugpr_reservation, 8), 16);
   }

   jay_def lid = jay_bare_regs(UGPR, ugpr_reservation, disp_width / 2);
   jay_SHL(&b, JAY_TYPE_U16, lid, lid, util_logbase2(4));
   jay_CVT(&b, JAY_TYPE_U32, sp, lid, JAY_TYPE_U16, JAY_ROUND, 0);
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
               jay_MOV(&b, ADDRESS_REG, sp_0);
               jay_MOV(&b, sp_0, b.shader->scratch_size + sp_delta_B);
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
         } else if (address_valid && jay_clobbers_address_reg(I)) {
            /* Shuffles implicitly clobber the address register. Spill it. */
            jay_MOV(&b, sp_0, ADDRESS_REG);
            address_valid = false;
         }
      }

      /* Canonicalize our internal registers at block boundaries */
      if (jay_num_successors(block, GPR) > 0) {
         b.cursor = jay_after_block_logical(block);

         if (!address_valid) {
            jay_MOV(&b, ADDRESS_REG, sp_0);
            jay_MOV(&b, sp_0, b.shader->scratch_size + sp_delta_B);
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
