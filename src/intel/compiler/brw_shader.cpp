/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "brw_cfg.h"
#include "brw_fs.h"
#include "util/macros.h"

bool
fs_inst::is_commutative() const
{
   switch (opcode) {
   case BRW_OPCODE_AND:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_XOR:
   case BRW_OPCODE_ADD:
   case BRW_OPCODE_ADD3:
   case SHADER_OPCODE_MULH:
      return true;

   case BRW_OPCODE_MUL:
      /* Integer multiplication of dword and word sources is not actually
       * commutative. The DW source must be first.
       */
      return !brw_type_is_int(src[0].type) ||
             brw_type_size_bits(src[0].type) == brw_type_size_bits(src[1].type);

   case BRW_OPCODE_SEL:
      /* MIN and MAX are commutative. */
      if (conditional_mod == BRW_CONDITIONAL_GE ||
          conditional_mod == BRW_CONDITIONAL_L) {
         return true;
      }
      FALLTHROUGH;
   default:
      return false;
   }
}

bool
fs_inst::is_3src(const struct brw_compiler *compiler) const
{
   return ::is_3src(&compiler->isa, opcode);
}

bool
fs_inst::is_math() const
{
   return (opcode == SHADER_OPCODE_RCP ||
           opcode == SHADER_OPCODE_RSQ ||
           opcode == SHADER_OPCODE_SQRT ||
           opcode == SHADER_OPCODE_EXP2 ||
           opcode == SHADER_OPCODE_LOG2 ||
           opcode == SHADER_OPCODE_SIN ||
           opcode == SHADER_OPCODE_COS ||
           opcode == SHADER_OPCODE_INT_QUOTIENT ||
           opcode == SHADER_OPCODE_INT_REMAINDER ||
           opcode == SHADER_OPCODE_POW);
}

bool
fs_inst::is_control_flow_begin() const
{
   switch (opcode) {
   case BRW_OPCODE_DO:
   case BRW_OPCODE_IF:
   case BRW_OPCODE_ELSE:
      return true;
   default:
      return false;
   }
}

bool
fs_inst::is_control_flow_end() const
{
   switch (opcode) {
   case BRW_OPCODE_ELSE:
   case BRW_OPCODE_WHILE:
   case BRW_OPCODE_ENDIF:
      return true;
   default:
      return false;
   }
}

bool
fs_inst::is_control_flow() const
{
   switch (opcode) {
   case BRW_OPCODE_DO:
   case BRW_OPCODE_WHILE:
   case BRW_OPCODE_IF:
   case BRW_OPCODE_ELSE:
   case BRW_OPCODE_ENDIF:
   case BRW_OPCODE_BREAK:
   case BRW_OPCODE_CONTINUE:
      return true;
   default:
      return false;
   }
}

bool
fs_inst::uses_indirect_addressing() const
{
   switch (opcode) {
   case SHADER_OPCODE_BROADCAST:
   case SHADER_OPCODE_CLUSTER_BROADCAST:
   case SHADER_OPCODE_MOV_INDIRECT:
      return true;
   default:
      return false;
   }
}

bool
fs_inst::can_do_saturate() const
{
   switch (opcode) {
   case BRW_OPCODE_ADD:
   case BRW_OPCODE_ADD3:
   case BRW_OPCODE_ASR:
   case BRW_OPCODE_AVG:
   case BRW_OPCODE_CSEL:
   case BRW_OPCODE_DP2:
   case BRW_OPCODE_DP3:
   case BRW_OPCODE_DP4:
   case BRW_OPCODE_DPH:
   case BRW_OPCODE_DP4A:
   case BRW_OPCODE_LINE:
   case BRW_OPCODE_LRP:
   case BRW_OPCODE_MAC:
   case BRW_OPCODE_MAD:
   case BRW_OPCODE_MATH:
   case BRW_OPCODE_MOV:
   case BRW_OPCODE_MUL:
   case SHADER_OPCODE_MULH:
   case BRW_OPCODE_PLN:
   case BRW_OPCODE_RNDD:
   case BRW_OPCODE_RNDE:
   case BRW_OPCODE_RNDU:
   case BRW_OPCODE_RNDZ:
   case BRW_OPCODE_SEL:
   case BRW_OPCODE_SHL:
   case BRW_OPCODE_SHR:
   case SHADER_OPCODE_COS:
   case SHADER_OPCODE_EXP2:
   case SHADER_OPCODE_LOG2:
   case SHADER_OPCODE_POW:
   case SHADER_OPCODE_RCP:
   case SHADER_OPCODE_RSQ:
   case SHADER_OPCODE_SIN:
   case SHADER_OPCODE_SQRT:
      return true;
   default:
      return false;
   }
}

bool
fs_inst::reads_accumulator_implicitly() const
{
   switch (opcode) {
   case BRW_OPCODE_MAC:
   case BRW_OPCODE_MACH:
      return true;
   default:
      return false;
   }
}

bool
fs_inst::writes_accumulator_implicitly(const struct intel_device_info *devinfo) const
{
   return writes_accumulator ||
          (eot && intel_needs_workaround(devinfo, 14010017096));
}

bool
fs_inst::has_side_effects() const
{
   switch (opcode) {
   case SHADER_OPCODE_SEND:
   case SHADER_OPCODE_SEND_GATHER:
      return send_has_side_effects;

   case BRW_OPCODE_SYNC:
   case SHADER_OPCODE_MEMORY_STORE_LOGICAL:
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
   case SHADER_OPCODE_MEMORY_FENCE:
   case SHADER_OPCODE_INTERLOCK:
   case SHADER_OPCODE_URB_WRITE_LOGICAL:
   case FS_OPCODE_FB_WRITE_LOGICAL:
   case SHADER_OPCODE_BARRIER:
   case SHADER_OPCODE_RND_MODE:
   case SHADER_OPCODE_FLOAT_CONTROL_MODE:
   case FS_OPCODE_SCHEDULING_FENCE:
   case SHADER_OPCODE_BTD_SPAWN_LOGICAL:
   case SHADER_OPCODE_BTD_RETIRE_LOGICAL:
   case RT_OPCODE_TRACE_RAY_LOGICAL:
      return true;
   default:
      return eot;
   }
}

bool
fs_inst::is_volatile() const
{
   return opcode == SHADER_OPCODE_MEMORY_LOAD_LOGICAL ||
          ((opcode == SHADER_OPCODE_SEND ||
            opcode == SHADER_OPCODE_SEND_GATHER) && send_is_volatile);
}

#ifndef NDEBUG
static bool
inst_is_in_block(const bblock_t *block, const fs_inst *inst)
{
   const exec_node *n = inst;

   /* Find the tail sentinel. If the tail sentinel is the sentinel from the
    * list header in the bblock_t, then this instruction is in that basic
    * block.
    */
   while (!n->is_tail_sentinel())
      n = n->get_next();

   return n == &block->instructions.tail_sentinel;
}
#endif

static void
adjust_later_block_ips(bblock_t *start_block, int ip_adjustment)
{
   for (bblock_t *block_iter = start_block->next();
        block_iter;
        block_iter = block_iter->next()) {
      block_iter->start_ip += ip_adjustment;
      block_iter->end_ip += ip_adjustment;
   }
}

void
fs_inst::insert_after(bblock_t *block, fs_inst *inst)
{
   assert(this != inst);
   assert(block->end_ip_delta == 0);

   if (!this->is_head_sentinel())
      assert(inst_is_in_block(block, this) || !"Instruction not in block");

   block->end_ip++;

   adjust_later_block_ips(block, 1);

   exec_node::insert_after(inst);
}

void
fs_inst::insert_before(bblock_t *block, fs_inst *inst)
{
   assert(this != inst);
   assert(block->end_ip_delta == 0);

   if (!this->is_tail_sentinel())
      assert(inst_is_in_block(block, this) || !"Instruction not in block");

   block->end_ip++;

   adjust_later_block_ips(block, 1);

   exec_node::insert_before(inst);
}

void
fs_inst::remove(bblock_t *block, bool defer_later_block_ip_updates)
{
   assert(inst_is_in_block(block, this) || !"Instruction not in block");

   if (exec_list_is_singular(&block->instructions)) {
      this->opcode = BRW_OPCODE_NOP;
      this->resize_sources(0);
      this->dst = brw_reg();
      this->size_written = 0;
      return;
   }

   if (defer_later_block_ip_updates) {
      block->end_ip_delta--;
   } else {
      assert(block->end_ip_delta == 0);
      adjust_later_block_ips(block, -1);
   }

   if (block->start_ip == block->end_ip) {
      if (block->end_ip_delta != 0) {
         adjust_later_block_ips(block, block->end_ip_delta);
         block->end_ip_delta = 0;
      }

      block->cfg->remove_block(block);
   } else {
      block->end_ip--;
   }

   exec_node::remove();
}
