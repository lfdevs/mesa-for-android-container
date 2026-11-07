/*
 * Copyright © 2019 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "ir3.h"

#include "ir3_compiler.h"

/* The maximum number of nop's we may need to insert between two instructions.
 */
#define MAX_NOPS 6

/*
 * Helpers to figure out the necessary delay slots between instructions.  Used
 * both in scheduling pass(es) and the final pass to insert any required nop's
 * so that the shader program is valid.
 *
 * Note that this needs to work both pre and post RA, so we can't assume ssa
 * src iterators work.
 */

/* Return the number of cycles from the start of the instruction until src_n is
 * read.
 */
unsigned
ir3_src_read_delay(struct ir3_compiler *compiler, struct ir3_instruction *instr,
                   unsigned src_n)
{
   /* gat and swz have scalar sources and each source is read in a subsequent
    * cycle.
    */
   if (instr->opc == OPC_GAT || instr->opc == OPC_SWZ) {
      return src_n;
   }

   /* mad instructions consume their last source one or two cycles later
    */
   bool cat3_reads_later = (is_mad(instr->opc) || is_madsh(instr->opc));
   if (cat3_reads_later && src_n == 2) {
      return compiler->delay_slots.cat3_src2_read;
   }

   return 0;
}

/* calculate required # of delay slots between the instruction that
 * assigns a value and the one that consumes
 */
int
_ir3_delayslots(struct ir3_compiler *compiler,
               struct ir3_instruction *assigner,
               struct ir3_instruction *consumer, unsigned n, bool soft)
{
   /* generally don't count false dependencies, since this can just be
    * something like a barrier, or SSBO store.
    */
   if (__is_false_dep(consumer, n))
      return 0;

   /* worst case is cat1-3 (alu) -> cat4/5 needing 6 cycles, normal
    * alu -> alu needs 3 cycles, cat4 -> alu and texture fetch
    * handled with sync bits
    */

   if (is_meta(assigner) || is_meta(consumer))
      return 0;

   if (writes_addr0(assigner) || writes_addr1(assigner)) {
      if (assigner->opc == OPC_MOV)
         return compiler->delay_slots.non_alu;
      else
         return 0;
   }

   if (soft && needs_ss(compiler, assigner, consumer))
      return soft_ss_delay(assigner);

   /* handled via sync flags: */
   if (needs_ss(compiler, assigner, consumer) ||
       is_sy_producer(assigner))
      return 0;

   /* scalar ALU -> scalar ALU depdendencies where the source and destination
    * register sizes match don't require any nops.
    */
   if (is_scalar_alu(assigner, compiler)) {
      assert(is_scalar_alu(consumer, compiler));
      /* If the sizes don't match then we need (ss) and needs_ss() should've
       * returned above.
       */
      assert((assigner->dsts[0]->flags & IR3_REG_HALF) ==
             (consumer->srcs[n]->flags & IR3_REG_HALF));
      return 0;
   }

   /* As far as we know, shader outputs don't need any delay. */
   if (consumer->opc == OPC_END || consumer->opc == OPC_CHMASK)
      return 0;

   /* assigner must be alu: */
   if (is_flow(consumer) || is_sfu(consumer) || is_tex(consumer) ||
       is_mem(consumer)) {
      return compiler->delay_slots.non_alu;
   } else {
      /* In mergedregs mode, there is an extra 2-cycle penalty when half of
       * a full-reg is read as a half-reg or when a half-reg is read as a
       * full-reg.
       */
      bool mismatched_half = (assigner->dsts[0]->flags & IR3_REG_HALF) !=
                             (consumer->srcs[n]->flags & IR3_REG_HALF);
      unsigned penalty = mismatched_half ? 3 : 0;
      return compiler->delay_slots.alu_to_alu + penalty -
             ir3_src_read_delay(compiler, consumer, n);
   }
}
