/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <vector>

#include "util/compiler.h"
#include "gen.h"

bool
gen_finish_structured_cf(gen_inst *insts, int num_insts, int final_halt_idx)
{
   /* Any JIPs and UIPs provided by the input are expected as relative
    * byte offsets, and will be respected as-is.  Any remaining JIPs and
    * UIPs will be inferred and filled in as relative byte offsets.
    */

   struct branch_info {
      gen_inst *inst;
      int idx;
      int uip_idx;
      int jip_idx;

      /* For loop headers. */
      int loop_end_idx = -1;

      bool is_loop_header() const { return loop_end_idx != -1; }
   };

   /* Collect information about the control flow instructions and any
    * instruction that are loop headers.  There might be multiple entries
    * for instructions that act as loop header for multiple loops and/or that
    * are control flow instruction themselves (e.g. IF as the loop header).
    */
   std::vector<branch_info> infos;
   for (int idx = 0; idx < num_insts; idx++) {
      gen_inst *inst = &insts[idx];

      switch (inst->opcode) {
      case GEN_OP_WHILE: {
         /* Since there's no DO instruction, the caller must
          * have set the JIP for WHILE insturctions, so that
          * the start of loops can be identified.
          */
         assert(inst->src[0].file == GEN_IMM);
         assert((int32_t)inst->src[0].imm % 16 == 0);
         const int jip_idx = idx + (int32_t)inst->src[0].imm / 16;
         assert(jip_idx <= idx);
         infos.push_back({
            .inst = inst,
            .idx = idx,
            .jip_idx = jip_idx,
         });
         /* Add an entry for the loop header. */
         infos.push_back({
            .inst = &insts[jip_idx],
            .idx = jip_idx,
            .loop_end_idx = idx,
         });
         break;
      }

      case GEN_OP_HALT:
      case GEN_OP_IF:
      case GEN_OP_ELSE:
      case GEN_OP_BREAK:
      case GEN_OP_CONTINUE:
      case GEN_OP_ENDIF:
         infos.push_back({
            .inst = &insts[idx],
            .idx = idx,
         });
         break;

      default:
         /* Skip. */
         continue;
      }
   }

   /* Sort in scope order. */
   std::sort(infos.begin(), infos.end(), [](const auto &a, const auto &b) {
      if (a.idx != b.idx)
         return a.idx < b.idx;
      /* Note the flipped comparison: want to see the largest scope first,
       * since it contains the other.
       */
      return a.loop_end_idx > b.loop_end_idx;
   });

   /* Identify the UIPs and JIPs that can be inferred by the
    * structured control flow.
    */
   std::vector<std::pair<branch_info *, branch_info *>> if_stack;
   std::vector<branch_info *> loop_stack;

   for (unsigned i = 0; i < infos.size(); i++) {
      /* This is a loop header for one or more loops. */
      while (i < infos.size() && infos[i].is_loop_header()) {
         loop_stack.push_back(&infos[i]);
         i++;
      }
      assert(i < infos.size());

      branch_info &info = infos[i];
      const int idx = info.idx;

      switch (info.inst->opcode) {
      case GEN_OP_IF:
         if_stack.push_back({&info, NULL});
         break;

      case GEN_OP_ELSE:
         if_stack.back().second = &info;
         break;

      case GEN_OP_ENDIF: {
         assert(!if_stack.empty());
         auto [if_info, else_info] = if_stack.back();
         if_stack.pop_back();

         if (else_info == NULL) {
            if_info->uip_idx = idx;
            if_info->jip_idx = idx;
         } else {
            if_info->uip_idx = idx;
            if_info->jip_idx = else_info->idx + 1;

            else_info->uip_idx = idx;
            else_info->jip_idx = idx;
         }
         break;
      }

      case GEN_OP_CONTINUE:
      case GEN_OP_BREAK:
         assert(!loop_stack.empty());
         assert(loop_stack.back()->is_loop_header());
         info.uip_idx = loop_stack.back()->loop_end_idx;
         assert(idx < info.uip_idx);
         break;

      case GEN_OP_WHILE:
         assert(!loop_stack.empty());
         assert(loop_stack.back()->loop_end_idx == idx);
         loop_stack.pop_back();
         break;

      case GEN_OP_HALT:
         if (final_halt_idx != -1)
            info.uip_idx = final_halt_idx + 1;
         break;

      default:
         /* Nothing to do. */
         break;
      }
   }

   assert(if_stack.empty());
   assert(loop_stack.empty());

   struct scope {
      int end_idx;
   };

   std::vector<scope> scopes;
   scopes.push_back({-1});

   /* Fill out the remaining JIPs, which are meant to "jump out of the current
    * scope".  Walk backwards keeping track of the scopes.  This make easy to
    * get the innermost end of scope and the innermost end of loop.
    */
   for (int i = infos.size() - 1; i >= 0; i--) {
      branch_info &info = infos[i];
      const int idx = info.idx;

      /* Remove the scope once we walk back to its start. */
      if (info.is_loop_header() || info.inst->opcode == GEN_OP_IF) {
         scopes.pop_back();
         continue;
      }

      switch (info.inst->opcode) {
      case GEN_OP_IF:
         /* Handled above together with loop headers. */
         break;

      case GEN_OP_ELSE:
         /* For instructions before the ELSE in the conditional (i.e. the
          * then-part of the loop), the scope ends here.
          */
         scopes.back().end_idx = idx;
         break;

      case GEN_OP_ENDIF: {
         const int innermost_end_idx = scopes.back().end_idx;

         if (innermost_end_idx != -1)
            info.jip_idx = innermost_end_idx;
         else if (final_halt_idx != -1)
            info.jip_idx = final_halt_idx + 1;
         else
            info.jip_idx = idx + 1;

         scopes.push_back({ idx });
         break;
      }

      case GEN_OP_WHILE:
         scopes.push_back({ idx });
         break;

      case GEN_OP_BREAK:
      case GEN_OP_CONTINUE: {
         const int outer_end_idx = scopes.back().end_idx;
         info.jip_idx = outer_end_idx;

         assert(info.uip_idx != 0);
         assert(info.jip_idx != 0);
         break;
      }

      case GEN_OP_HALT: {
	 /* From the Sandy Bridge PRM (volume 4, part 2, section 8.3.19):
	  *
	  *    "In case of the halt instruction not inside any conditional
	  *     code block, the value of <JIP> and <UIP> should be the
	  *     same. In case of the halt instruction inside conditional code
	  *     block, the <UIP> should be the end of the program, and the
	  *     <JIP> should be end of the most inner conditional code block."
	  */
         const int innermost_end_idx = scopes.back().end_idx;
         if (innermost_end_idx != -1)
            info.jip_idx = innermost_end_idx;
         else
            info.jip_idx = info.uip_idx;
         break;
      }

      default:
         /* Nothing to do. */
         break;
      }
   }

   /* Finally, fill in any JIPs and UIPs not provided by the caller,
    * as relative byte offsets.
    */
   for (auto &info : infos) {
      if (info.is_loop_header())
         continue;

      gen_inst *inst = info.inst;
      const int idx = info.idx;

      switch (info.inst->opcode) {
      case GEN_OP_IF:
      case GEN_OP_ELSE:
      case GEN_OP_BREAK:
      case GEN_OP_CONTINUE:
      case GEN_OP_HALT:
         if (inst->src[1].file == GEN_BAD_FILE) {
            inst->src[1].file = GEN_IMM;
            inst->src[1].type = GEN_TYPE_D;
            inst->src[1].imm = 16 * (info.uip_idx - idx);
         }
         assert((int32_t)inst->src[1].imm > 0);
         FALLTHROUGH;

      case GEN_OP_WHILE:
      case GEN_OP_ENDIF:
         if (inst->src[0].file == GEN_BAD_FILE) {
            inst->src[0].file = GEN_IMM;
            inst->src[0].type = GEN_TYPE_D;
            inst->src[0].imm = 16 * (info.jip_idx - idx);
         }
         break;

      default:
         /* Nothing to do. */
         break;
      }
   }

   return true;
}

