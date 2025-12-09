/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_cf.c
 *
 * \brief PCO control-flow passes.
 */

#include "pco.h"
#include "pco_builder.h"
#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"

#include <assert.h>
#include <stdbool.h>

static pco_ref emc_ref(pco_func *func, pco_builder *b)
{
   if (pco_ref_is_null(func->emc)) {
      /* Allocate and initialize the emc. */
      func->emc = pco_ref_new_vreg(func);

      pco_cndst(b,
                pco_ref_pred(PCO_PRED_PE),
                func->emc,
                pco_zero,
                pco_ref_imm8(1),
                .exec_cnd = PCO_EXEC_CND_EX_ZX,
                .cnd = PCO_CND_ALWAYS);
   }

   return func->emc;
}

static inline pco_block *cf_section_create(pco_func *func,
                                           pco_cf_node *parent_cf_node,
                                           struct list_head *cf_node_list,
                                           enum pco_cf_node_flag flag)
{
   assert(flag == PCO_CF_NODE_FLAG_PROLOGUE ||
          flag == PCO_CF_NODE_FLAG_INTERLOGUE ||
          flag == PCO_CF_NODE_FLAG_EPILOGUE);

   pco_block *block = pco_block_create(func);
   block->cf_node.parent = parent_cf_node;
   block->cf_node.flag = flag;
   list_addtail(&block->cf_node.link, cf_node_list);

   return block;
}

static inline bool body_has_non_preds(struct list_head *body)
{
   if (list_is_empty(body))
      return false;

   pco_block *block = pco_cf_node_as_block(pco_cf_node_head(body));
   pco_instr *last_instr = NULL;
   pco_foreach_instr_in_block (instr, block) {
      /* Make sure there are no instructions that read/write predicates. */
      pco_foreach_instr_src (psrc, instr) {
         if (pco_ref_is_pred(*psrc))
            return true;
      }

      pco_foreach_instr_dest (pdest, instr) {
         if (pco_ref_is_pred(*pdest))
            return true;
      }

      if (!pco_instr_has_exec_cnd(instr))
         return true;

      if (!pco_instr_has_default_exec(instr))
         return true;

      last_instr = instr;
   }

   assert(last_instr);
   return last_instr->op == PCO_OP_BR;
}

static inline bool can_pred_exec(pco_if *pif)
{
   if (PCO_DEBUG(NO_PRED_CF))
      return false;

   /* Skip if there is any nesting. */
   if (list_length(&pif->then_body) > 1 || list_length(&pif->else_body) > 1)
      return false;

   /* Skip if then/else blocks end with a branch, or contain non-predicatable
    * instructions.
    * Note: WDFs can't be predicated and won't be inserted until the scheduling
    * pass which comes after this one, but we don't have to worry about it as if
    * there are no outstanding data fences it'll simply NOP.
    */
   if (body_has_non_preds(&pif->then_body) ||
       body_has_non_preds(&pif->else_body))
      return false;

   return true;
}

static inline void set_body_exec_cnd(struct list_head *body,
                                     enum pco_exec_cnd exec_cnd)
{
   assert(!list_is_empty(body));

   pco_block *block = pco_cf_node_as_block(pco_cf_node_head(body));
   pco_foreach_instr_in_block (instr, block) {
      pco_instr_set_exec_cnd(instr, exec_cnd);
   }
}

static inline void
lower_if_pred_exec(pco_if *pif, pco_func *func, bool has_else, bool invert_cond)
{
   pco_block *prologue = cf_section_create(func,
                                           &pif->cf_node,
                                           &pif->prologue,
                                           PCO_CF_NODE_FLAG_PROLOGUE);

   /* Setup the prologue. */
   pco_builder b = pco_builder_create(func, pco_cursor_after_block(prologue));

   /* TODO: see if the cond producer can set p0 directly. */
   pco_tstz(&b,
            pco_ref_null(),
            pco_ref_pred(PCO_PRED_P0),
            pif->cond,
            .tst_type_main = PCO_TST_TYPE_MAIN_U32);

   set_body_exec_cnd(&pif->then_body,
                     invert_cond ? PCO_EXEC_CND_E1_Z1 : PCO_EXEC_CND_E1_Z0);

   if (has_else) {
      set_body_exec_cnd(&pif->else_body,
                        invert_cond ? PCO_EXEC_CND_E1_Z0 : PCO_EXEC_CND_E1_Z1);
   }
}

static inline void
lower_if_cond_exec(pco_if *pif, pco_func *func, bool has_else, bool invert_cond)
{
   pco_block *prologue = cf_section_create(func,
                                           &pif->cf_node,
                                           &pif->prologue,
                                           PCO_CF_NODE_FLAG_PROLOGUE);

   pco_block *interlogue = has_else
                              ? cf_section_create(func,
                                                  &pif->cf_node,
                                                  &pif->interlogue,
                                                  PCO_CF_NODE_FLAG_INTERLOGUE)
                              : NULL;

   pco_block *epilogue = cf_section_create(func,
                                           &pif->cf_node,
                                           &pif->epilogue,
                                           PCO_CF_NODE_FLAG_EPILOGUE);

   /* Setup the prologue. */
   pco_builder b = pco_builder_create(func, pco_cursor_after_block(prologue));
   pco_ref emc = emc_ref(func, &b);

   /* TODO: see if the cond producer can set p0 directly. */
   pco_tstz(&b,
            pco_ref_null(),
            pco_ref_pred(PCO_PRED_P0),
            pif->cond,
            .tst_type_main = PCO_TST_TYPE_MAIN_U32);

   pco_cndst(&b,
             pco_ref_pred(PCO_PRED_PE),
             emc,
             emc,
             pco_ref_imm8(1),
             .exec_cnd = PCO_EXEC_CND_EX_ZX,
             .cnd = invert_cond ? PCO_CND_P0_TRUE : PCO_CND_P0_FALSE);

   pco_br(&b,
          has_else ? &interlogue->cf_node : &epilogue->cf_node,
          .branch_cnd = PCO_BRANCH_CND_ALLINST);

   /* Setup the interlogue (if needed). */
   if (has_else) {
      b.cursor = pco_cursor_after_block(interlogue);

      pco_cndef(&b,
                pco_ref_pred(PCO_PRED_PE),
                emc,
                emc,
                pco_ref_imm8(1),
                .exec_cnd = PCO_EXEC_CND_EX_ZX,
                .cnd = PCO_CND_ALWAYS);

      pco_br(&b, &epilogue->cf_node, .branch_cnd = PCO_BRANCH_CND_ALLINST);
   }

   /* Setup the epilogue. */
   b.cursor = pco_cursor_after_block(epilogue);

   pco_cndend(&b,
              pco_ref_pred(PCO_PRED_PE),
              emc,
              emc,
              pco_ref_imm8(1),
              .exec_cnd = PCO_EXEC_CND_EX_ZX);
}

static inline void lower_if(pco_if *pif, pco_func *func)
{
   assert(!pco_ref_is_null(pif->cond));
   assert(list_is_empty(&pif->prologue));
   assert(list_is_empty(&pif->interlogue));
   assert(list_is_empty(&pif->epilogue));

   bool has_then = !list_is_empty(&pif->then_body);
   bool has_else = !list_is_empty(&pif->else_body);
   assert(has_then || has_else);

   /* If we only have an else body, invert the condition and bodies. */
   bool invert_cond = false;
   if (!has_then && has_else) {
      struct list_head temp;
      memcpy(&temp, &pif->then_body, sizeof(pif->then_body));
      memcpy(&pif->then_body, &pif->else_body, sizeof(pif->else_body));
      memcpy(&pif->else_body, &pif->then_body, sizeof(pif->then_body));
      invert_cond = true;

      has_then = true;
      has_else = false;
   }

   assert(has_then);

   if (pif->pred_exec)
      lower_if_pred_exec(pif, func, has_else, invert_cond);
   else
      lower_if_cond_exec(pif, func, has_else, invert_cond);

   pif->cond = pco_ref_null();
}

static inline void lower_loop(pco_loop *loop, pco_func *func)
{
   assert(list_is_empty(&loop->prologue));
   assert(list_is_empty(&loop->interlogue));
   assert(list_is_empty(&loop->epilogue));

   pco_block *prologue = cf_section_create(func,
                                           &loop->cf_node,
                                           &loop->prologue,
                                           PCO_CF_NODE_FLAG_PROLOGUE);

   pco_block *interlogue = cf_section_create(func,
                                             &loop->cf_node,
                                             &loop->interlogue,
                                             PCO_CF_NODE_FLAG_INTERLOGUE);

   pco_block *epilogue = cf_section_create(func,
                                           &loop->cf_node,
                                           &loop->epilogue,
                                           PCO_CF_NODE_FLAG_EPILOGUE);

   /* Setup the prologue. */
   pco_builder b = pco_builder_create(func, pco_cursor_after_block(prologue));
   pco_ref emc = emc_ref(func, &b);

   pco_cndst(&b,
             pco_ref_pred(PCO_PRED_PE),
             emc,
             emc,
             pco_ref_imm8(2), /* TODO: make this a define */
             .exec_cnd = PCO_EXEC_CND_EX_ZX,
             .cnd = PCO_CND_ALWAYS);

   pco_br(&b, &epilogue->cf_node, .branch_cnd = PCO_BRANCH_CND_ALLINST);

   /* Setup the interlogue. */
   b.cursor = pco_cursor_after_block(interlogue);

   pco_cndend(&b,
              pco_ref_pred(PCO_PRED_PE),
              emc,
              emc,
              pco_ref_imm8(1), /* TODO: make this a define */
              .exec_cnd = PCO_EXEC_CND_EX_ZX);

   pco_cndst(&b,
             pco_ref_pred(PCO_PRED_PE),
             emc,
             emc,
             pco_ref_imm8(1), /* TODO: make this a define */
             .exec_cnd = PCO_EXEC_CND_EX_ZX,
             .cnd = PCO_CND_ALWAYS);

   /* Setup the epilogue. */
   b.cursor = pco_cursor_after_block(epilogue);

   pco_cndlt(&b,
             pco_ref_pred(PCO_PRED_PE),
             emc,
             pco_ref_pred(PCO_PRED_P0),
             emc,
             pco_ref_imm8(2), /* TODO: make this a define */
             .exec_cnd = PCO_EXEC_CND_EX_ZX,
             .cnd = PCO_CND_ALWAYS);

   pco_br(&b,
          pco_cf_node_head(&loop->body),
          .exec_cnd = PCO_EXEC_CND_E1_Z1,
          .branch_cnd = PCO_BRANCH_CND_EXEC_COND);
}

static inline void lower_break_continue(pco_instr *instr,
                                        pco_func *func,
                                        pco_if *pif,
                                        pco_loop *loop,
                                        unsigned loop_nestings,
                                        bool is_continue)
{
   pco_builder b = pco_builder_create(func, pco_cursor_before_instr(instr));
   pco_ref emc = emc_ref(func, &b);
   enum pco_exec_cnd exec_cnd = pco_instr_get_exec_cnd(instr);

   pco_ref val = pco_ref_new_ssa32(func);
   pco_movi32(&b,
              val,
              pco_ref_imm32(loop_nestings + (is_continue ? 1 : 2)),
              .exec_cnd = exec_cnd);

   enum pco_cnd cnd;
   switch (exec_cnd) {
   case PCO_CC_E1_ZX:
      assert(!pif || !pif->pred_exec);
      cnd = PCO_CND_ALWAYS;
      break;

   case PCO_CC_E1_Z1:
      assert(!pif || pif->pred_exec);
      cnd = PCO_CND_P0_TRUE;
      break;

   case PCO_CC_E1_Z0:
      assert(!pif || pif->pred_exec);
      cnd = PCO_CND_P0_FALSE;
      break;

   default:
      UNREACHABLE("");
   }

   pco_cndsm(&b,
             pco_ref_pred(PCO_PRED_PE),
             emc,
             emc,
             val,
             .exec_cnd = PCO_EXEC_CND_EX_ZX,
             .cnd = cnd);

   pco_instr_delete(instr);
}

static inline bool pco_lower_cf(pco_func *func)
{
   bool progress = false;

   void *mem_ctx = ralloc_context(NULL);
   unsigned loop_nestings = 0;
   struct util_dynarray loop_nestings_stack;
   struct util_dynarray pif_stack;
   struct util_dynarray loop_stack;

   util_dynarray_init(&loop_nestings_stack, mem_ctx);
   util_dynarray_init(&pif_stack, mem_ctx);
   util_dynarray_init(&loop_stack, mem_ctx);

   pco_foreach_cf_node_in_func_structured (cf_node, cf_node_completed, func) {
      /* Handle the end of an if/loop. */
      if (cf_node_completed) {
         switch (cf_node_completed->type) {
         case PCO_CF_NODE_TYPE_IF: {
            pco_if *pif = pco_cf_node_as_if(cf_node_completed);

            ASSERTED pco_if *last_pif = util_dynarray_pop(&pif_stack, pco_if *);
            assert(pif == last_pif);

            if (!pif->pred_exec)
               --loop_nestings;

            break;
         }

         case PCO_CF_NODE_TYPE_LOOP: {
            ASSERTED pco_loop *loop = pco_cf_node_as_loop(cf_node_completed);
            ASSERTED pco_loop *last_loop =
               util_dynarray_pop(&loop_stack, pco_loop *);
            assert(loop == last_loop);

            assert(loop_nestings == 0);
            loop_nestings = util_dynarray_pop(&loop_nestings_stack, unsigned);
            break;
         }

         default:
            break;
         }
      }

      /* Handle the start of an if/loop, or lower break/continue for blocks. */
      switch (cf_node->type) {
      case PCO_CF_NODE_TYPE_IF: {
         pco_if *pif = pco_cf_node_as_if(cf_node);
         pif->pred_exec = can_pred_exec(pif);
         util_dynarray_append(&pif_stack, pif);

         if (!pif->pred_exec)
            ++loop_nestings;

         lower_if(pif, func);
         progress = true;

         break;
      }

      case PCO_CF_NODE_TYPE_LOOP: {
         util_dynarray_append(&loop_nestings_stack, loop_nestings);
         loop_nestings = 0;

         pco_loop *loop = pco_cf_node_as_loop(cf_node);
         util_dynarray_append(&loop_stack, loop);

         lower_loop(loop, func);
         progress = true;

         break;
      }

      case PCO_CF_NODE_TYPE_BLOCK: {
         pco_block *block = pco_cf_node_as_block(cf_node);
         pco_foreach_instr_in_block_safe (instr, block) {
            if (instr->op != PCO_OP_BREAK && instr->op != PCO_OP_CONTINUE)
               continue;

            /* This has to be the last instruction in the block. */
            assert(instr == pco_last_instr(block));

            pco_if *current_pif = NULL;
            if (instr->parent_block->cf_node.parent->type ==
                PCO_CF_NODE_TYPE_IF) {
               current_pif = util_dynarray_top(&pif_stack, pco_if *);
               assert(current_pif ==
                      pco_cf_node_as_if(instr->parent_block->cf_node.parent));
            }

            pco_loop *current_loop = util_dynarray_top(&loop_stack, pco_loop *);

            lower_break_continue(instr,
                                 func,
                                 current_pif,
                                 current_loop,
                                 loop_nestings,
                                 instr->op == PCO_OP_CONTINUE);

            progress = true;
         }

         break;
      }

      default:
         break;
      }
   }

   assert(!util_dynarray_num_elements(&loop_stack, pco_loop *));
   assert(!util_dynarray_num_elements(&pif_stack, pco_if *));
   assert(!util_dynarray_num_elements(&loop_nestings_stack, unsigned));
   assert(loop_nestings == 0);

   ralloc_free(mem_ctx);

   return progress;
}

/**
 * \brief Control-flow pass.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_cf(pco_shader *shader)
{
   bool progress = false;

   pco_foreach_func_in_shader (func, shader) {
      progress |= pco_lower_cf(func);
   }

   return progress;
}
