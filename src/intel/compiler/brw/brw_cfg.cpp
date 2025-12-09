/*
 * Copyright © 2012 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "brw_cfg.h"
#include "util/u_dynarray.h"
#include "brw_shader.h"

/** @file
 *
 * Walks the shader instructions generated and creates a set of basic
 * blocks with successor/predecessor edges connecting them.
 */

static bblock_t *
pop_stack(brw_exec_list *list)
{
   bblock_link *link = (bblock_link *)list->get_tail();
   bblock_t *block = link->block;
   link->link.remove();

   return block;
}

static brw_exec_node *
link(void *mem_ctx, bblock_t *block, enum bblock_link_kind kind)
{
   bblock_link *l = new(mem_ctx) bblock_link(block, kind);
   return &l->link;
}

void
push_stack(brw_exec_list *list, void *mem_ctx, bblock_t *block)
{
   /* The kind of the link is immaterial, but we need to provide one since
    * this is (ab)using the edge data structure in order to implement a stack.
    */
   list->push_tail(link(mem_ctx, block, bblock_link_logical));
}

bblock_t::bblock_t(cfg_t *cfg) :
   cfg(cfg), num_instructions(0), num(0)
{
   instructions.make_empty();
   parents.make_empty();
   children.make_empty();
}

void
bblock_t::add_successor(void *mem_ctx, bblock_t *successor,
                        enum bblock_link_kind kind)
{
   successor->parents.push_tail(::link(mem_ctx, this, kind));
   children.push_tail(::link(mem_ctx, successor, kind));
}

void
bblock_t::insert_before(brw_inst *inst, brw_exec_node *ref)
{
   assert(inst != ref);
   assert(!inst->block || inst->block == this);

   num_instructions++;
   cfg->total_instructions++;

   ref->brw_exec_node::insert_before(inst);
   inst->block = this;
}

void
bblock_t::remove(brw_inst *inst)
{
   if (brw_exec_list_is_singular(&instructions)) {
      inst = brw_transform_inst(*cfg->s, inst, BRW_OPCODE_NOP);
      inst->dst = brw_reg();
      inst->size_written = 0;
      return;
   }

   assert(num_instructions > 0);
   assert(cfg->total_instructions > 0);
   num_instructions--;
   cfg->total_instructions--;

   inst->brw_exec_node::remove();
   inst->block = NULL;

   if (num_instructions == 0)
      cfg->remove_block(this);
}

static void
append_inst(bblock_t *block, brw_inst *inst)
{
   assert(inst->block == NULL);
   inst->block = block;
   block->instructions.push_tail(inst);
   block->num_instructions++;
   block->cfg->total_instructions++;
}

cfg_t::cfg_t(brw_shader *s, brw_exec_list *instructions) :
   s(s), total_instructions(0)
{
   mem_ctx = ralloc_context(NULL);
   block_list.make_empty();
   blocks = NULL;
   num_blocks = 0;

   bblock_t *cur = NULL;
   int ip = 0;

   bblock_t *entry = new_block();
   bblock_t *cur_if = NULL;    /**< BB ending with IF. */
   bblock_t *cur_else = NULL;  /**< BB ending with ELSE. */
   bblock_t *cur_do = NULL;    /**< BB starting with DO. */
   bblock_t *cur_while = NULL; /**< BB immediately following WHILE. */
   brw_exec_list if_stack, else_stack, do_stack, while_stack;
   bblock_t *next;

   set_next_block(&cur, entry, ip);

   brw_foreach_in_list_safe(brw_inst, inst, instructions) {
      /* set_next_block wants the post-incremented ip */
      ip++;

      inst->brw_exec_node::remove();

      switch (inst->opcode) {
      case SHADER_OPCODE_FLOW:
         append_inst(cur, inst);
         next = new_block();
         cur->add_successor(mem_ctx, next, bblock_link_logical);
         set_next_block(&cur, next, ip);
         break;

      case BRW_OPCODE_IF:
         append_inst(cur, inst);

	 /* Push our information onto a stack so we can recover from
	  * nested ifs.
	  */
         push_stack(&if_stack, mem_ctx, cur_if);
         push_stack(&else_stack, mem_ctx, cur_else);

	 cur_if = cur;
	 cur_else = NULL;

	 /* Set up our immediately following block, full of "then"
	  * instructions.
	  */
	 next = new_block();
         cur_if->add_successor(mem_ctx, next, bblock_link_logical);

	 set_next_block(&cur, next, ip);
	 break;

      case BRW_OPCODE_ELSE:
         append_inst(cur, inst);

         cur_else = cur;

	 next = new_block();
         assert(cur_if != NULL);
         cur_if->add_successor(mem_ctx, next, bblock_link_logical);
         cur_else->add_successor(mem_ctx, next, bblock_link_physical);

	 set_next_block(&cur, next, ip);
	 break;

      case BRW_OPCODE_ENDIF: {
         bblock_t *cur_endif;

         if (cur->instructions.is_empty()) {
            /* New block was just created; use it. */
            cur_endif = cur;
         } else {
            cur_endif = new_block();

            cur->add_successor(mem_ctx, cur_endif, bblock_link_logical);

            set_next_block(&cur, cur_endif, ip - 1);
         }

         append_inst(cur, inst);

         if (cur_else) {
            cur_else->add_successor(mem_ctx, cur_endif, bblock_link_logical);
         } else {
            assert(cur_if != NULL);
            cur_if->add_successor(mem_ctx, cur_endif, bblock_link_logical);
         }

         assert(cur_if->end()->opcode == BRW_OPCODE_IF);
         assert(!cur_else || cur_else->end()->opcode == BRW_OPCODE_ELSE);

	 /* Pop the stack so we're in the previous if/else/endif */
	 cur_if = pop_stack(&if_stack);
	 cur_else = pop_stack(&else_stack);
	 break;
      }
      case BRW_OPCODE_DO:
	 /* Push our information onto a stack so we can recover from
	  * nested loops.
	  */
         push_stack(&do_stack, mem_ctx, cur_do);
         push_stack(&while_stack, mem_ctx, cur_while);

	 /* Set up the block just after the while.  Don't know when exactly
	  * it will start, yet.
	  */
	 cur_while = new_block();

         if (cur->instructions.is_empty()) {
            /* New block was just created; use it. */
            cur_do = cur;
         } else {
            cur_do = new_block();

            cur->add_successor(mem_ctx, cur_do, bblock_link_logical);

            set_next_block(&cur, cur_do, ip - 1);
         }

         append_inst(cur, inst);

         /* Represent divergent execution of the loop as a pair of alternative
          * edges coming out of the DO instruction: For any physical iteration
          * of the loop a given logical thread can either start off enabled
          * (which is represented as the "next" successor), or disabled (if it
          * has reached a non-uniform exit of the loop during a previous
          * iteration, which is represented as the "cur_while" successor).
          *
          * The disabled edge will be taken by the logical thread anytime we
          * arrive at the DO instruction through a back-edge coming from a
          * conditional exit of the loop where divergent control flow started.
          *
          * This guarantees that there is a control-flow path from any
          * divergence point of the loop into the convergence point
          * (immediately past the WHILE instruction) such that it overlaps the
          * whole IP region of divergent control flow (potentially the whole
          * loop) *and* doesn't imply the execution of any instructions part
          * of the loop (since the corresponding execution mask bit will be
          * disabled for a diverging thread).
          *
          * This way we make sure that any variables that are live throughout
          * the region of divergence for an inactive logical thread are also
          * considered to interfere with any other variables assigned by
          * active logical threads within the same physical region of the
          * program, since otherwise we would risk cross-channel data
          * corruption.
          */
         next = new_block();
         cur->add_successor(mem_ctx, next, bblock_link_logical);
         cur->add_successor(mem_ctx, cur_while, bblock_link_physical);
         set_next_block(&cur, next, ip);
	 break;

      case BRW_OPCODE_CONTINUE:
         append_inst(cur, inst);

         /* A conditional CONTINUE may start a region of divergent control
          * flow until the start of the next loop iteration (*not* until the
          * end of the loop which is why the successor is not the top-level
          * divergence point at cur_do).  The live interval of any variable
          * extending through a CONTINUE edge is guaranteed to overlap the
          * whole region of divergent execution, because any variable live-out
          * at the CONTINUE instruction will also be live-in at the top of the
          * loop, and therefore also live-out at the bottom-most point of the
          * loop which is reachable from the top (since a control flow path
          * exists from a definition of the variable through this CONTINUE
          * instruction, the top of the loop, the (reachable) bottom of the
          * loop, the top of the loop again, into a use of the variable).
          */
         assert(cur_do != NULL);
         cur->add_successor(mem_ctx, cur_do->next(), bblock_link_logical);

	 next = new_block();
	 if (inst->predicate)
            cur->add_successor(mem_ctx, next, bblock_link_logical);
         else
            cur->add_successor(mem_ctx, next, bblock_link_physical);

	 set_next_block(&cur, next, ip);
	 break;

      case BRW_OPCODE_BREAK:
         append_inst(cur, inst);

         /* A conditional BREAK instruction may start a region of divergent
          * control flow until the end of the loop if the condition is
          * non-uniform, in which case the loop will execute additional
          * iterations with the present channel disabled.  We model this as a
          * control flow path from the divergence point to the convergence
          * point that overlaps the whole IP range of the loop and skips over
          * the execution of any other instructions part of the loop.
          *
          * See the DO case for additional explanation.
          */
         assert(cur_do != NULL);
         cur->add_successor(mem_ctx, cur_do, bblock_link_physical);
         cur->add_successor(mem_ctx, cur_while, bblock_link_logical);

	 next = new_block();
	 if (inst->predicate)
            cur->add_successor(mem_ctx, next, bblock_link_logical);
         else
            cur->add_successor(mem_ctx, next, bblock_link_physical);

	 set_next_block(&cur, next, ip);
	 break;

      case BRW_OPCODE_WHILE:
         append_inst(cur, inst);

         assert(cur_do != NULL && cur_while != NULL);

         /* A conditional WHILE instruction may start a region of divergent
          * control flow until the end of the loop, just like the BREAK
          * instruction.  See the BREAK case for more details.  OTOH an
          * unconditional WHILE instruction is non-divergent (just like an
          * unconditional CONTINUE), and will necessarily lead to the
          * execution of an additional iteration of the loop for all enabled
          * channels, so we may skip over the divergence point at the top of
          * the loop to keep the CFG as unambiguous as possible.
          */
         if (inst->predicate) {
            cur->add_successor(mem_ctx, cur_do, bblock_link_logical);
         } else {
            cur->add_successor(mem_ctx, cur_do->next(), bblock_link_logical);
         }

	 set_next_block(&cur, cur_while, ip);

	 /* Pop the stack so we're in the previous loop */
	 cur_do = pop_stack(&do_stack);
	 cur_while = pop_stack(&while_stack);
	 break;

      default:
         append_inst(cur, inst);
	 break;
      }
   }

   make_block_array();
}

cfg_t::~cfg_t()
{
   ralloc_free(mem_ctx);
}

void
cfg_t::remove_block(bblock_t *block)
{
   brw_foreach_list_typed_safe (bblock_link, predecessor, link, &block->parents) {
      /* cfg_t::validate checks that predecessor and successor lists are well
       * formed, so it is known that the loop here would find exactly one
       * block. Set old_link_kind to silence "variable used but not set"
       * warnings.
       */
      bblock_link_kind old_link_kind = bblock_link_logical;

      /* Remove block from all of its predecessors' successor lists. */
      brw_foreach_list_typed_safe (bblock_link, successor, link,
                               &predecessor->block->children) {
         if (block == successor->block) {
            old_link_kind = successor->kind;
            successor->link.remove();
            ralloc_free(successor);
            break;
         }
      }

      /* Add removed-block's successors to its predecessors' successor lists. */
      brw_foreach_list_typed (bblock_link, successor, link, &block->children) {
         bool need_to_link = true;
         bblock_link_kind new_link_kind = MAX2(old_link_kind, successor->kind);

         brw_foreach_list_typed_safe (bblock_link, child, link, &predecessor->block->children) {
            /* There is already a link between the two blocks. If the links
             * are the same kind or the link is logical, do nothing. If the
             * existing link is physical and the proposed new link is logical,
             * promote the existing link to logical.
             *
             * This is accomplished by taking the minimum of the existing link
             * kind and the proposed link kind.
             */
            if (child->block == successor->block) {
               child->kind = MIN2(child->kind, new_link_kind);
               need_to_link = false;
               break;
            }
         }

         if (need_to_link) {
            predecessor->block->children.push_tail(link(mem_ctx,
                                                        successor->block,
                                                        new_link_kind));
         }
      }
   }

   brw_foreach_list_typed_safe (bblock_link, successor, link, &block->children) {
      /* cfg_t::validate checks that predecessor and successor lists are well
       * formed, so it is known that the loop here would find exactly one
       * block. Set old_link_kind to silence "variable used but not set"
       * warnings.
       */
      bblock_link_kind old_link_kind = bblock_link_logical;

      /* Remove block from all of its childrens' parents lists. */
      brw_foreach_list_typed_safe (bblock_link, predecessor, link,
                               &successor->block->parents) {
         if (block == predecessor->block) {
            old_link_kind = predecessor->kind;
            predecessor->link.remove();
            ralloc_free(predecessor);
         }
      }

      /* Add removed-block's predecessors to its successors' predecessor lists. */
      brw_foreach_list_typed (bblock_link, predecessor, link, &block->parents) {
         bool need_to_link = true;
         bblock_link_kind new_link_kind = MAX2(old_link_kind, predecessor->kind);

         brw_foreach_list_typed_safe (bblock_link, parent, link, &successor->block->parents) {
            /* There is already a link between the two blocks. If the links
             * are the same kind or the link is logical, do nothing. If the
             * existing link is physical and the proposed new link is logical,
             * promote the existing link to logical.
             *
             * This is accomplished by taking the minimum of the existing link
             * kind and the proposed link kind.
             */
            if (parent->block == predecessor->block) {
               parent->kind = MIN2(parent->kind, new_link_kind);
               need_to_link = false;
               break;
            }
         }

         if (need_to_link) {
            successor->block->parents.push_tail(link(mem_ctx,
                                                     predecessor->block,
                                                     new_link_kind));
         }
      }
   }

   block->link.remove();

   for (int b = block->num; b < this->num_blocks - 1; b++) {
      this->blocks[b] = this->blocks[b + 1];
      this->blocks[b]->num = b;
   }

   this->blocks[this->num_blocks - 1]->num = this->num_blocks - 2;
   this->num_blocks--;
}

bblock_t *
cfg_t::new_block()
{
   bblock_t *block = new(mem_ctx) bblock_t(this);

   return block;
}

void
cfg_t::set_next_block(bblock_t **cur, bblock_t *block, int ip)
{
   block->num = num_blocks++;
   block_list.push_tail(&block->link);
   *cur = block;
}

void
cfg_t::make_block_array()
{
   blocks = ralloc_array(mem_ctx, bblock_t *, num_blocks);

   int i = 0;
   foreach_block (block, this) {
      blocks[i++] = block;
   }
   assert(i == num_blocks);
}

void
cfg_t::dump_cfg()
{
   printf("digraph CFG {\n");
   for (int b = 0; b < num_blocks; b++) {
      bblock_t *block = this->blocks[b];

      brw_foreach_list_typed_safe (bblock_link, child, link, &block->children) {
         printf("\t%d -> %d\n", b, child->block->num);
      }
   }
   printf("}\n");
}

void
brw_calculate_cfg(brw_shader &s)
{
   if (s.cfg)
      return;
   s.cfg = new(s.mem_ctx) cfg_t(&s, &s.instructions);
}

#define cfgv_assert(assertion)                                          \
   {                                                                    \
      if (!(assertion)) {                                               \
         fprintf(stderr, "ASSERT: CFG validation in %s failed!\n", stage_abbrev); \
         fprintf(stderr, "%s:%d: '%s' failed\n", __FILE__, __LINE__, #assertion);  \
         abort();                                                       \
      }                                                                 \
   }

#ifndef NDEBUG
void
cfg_t::validate(const char *stage_abbrev)
{
   unsigned counted_total_instructions = 0;

   foreach_block(block, this) {
      brw_foreach_list_typed(bblock_link, successor, link, &block->children) {
         /* Each successor of a block must have one predecessor link back to
          * the block.
          */
         bool successor_links_back_to_predecessor = false;
         bblock_t *succ_block = successor->block;

         brw_foreach_list_typed(bblock_link, predecessor, link, &succ_block->parents) {
            if (predecessor->block == block) {
               cfgv_assert(!successor_links_back_to_predecessor);
               cfgv_assert(successor->kind == predecessor->kind);
               successor_links_back_to_predecessor = true;
            }
         }

         cfgv_assert(successor_links_back_to_predecessor);

         /* Each successor block must appear only once in the list of
          * successors.
          */
         brw_foreach_list_typed_from(bblock_link, later_successor, link,
                                 &block->children, successor->link.next) {
            cfgv_assert(successor->block != later_successor->block);
         }
      }

      brw_foreach_list_typed(bblock_link, predecessor, link, &block->parents) {
         /* Each predecessor of a block must have one successor link back to
          * the block.
          */
         bool predecessor_links_back_to_successor = false;
         bblock_t *pred_block = predecessor->block;

         brw_foreach_list_typed(bblock_link, successor, link, &pred_block->children) {
            if (successor->block == block) {
               cfgv_assert(!predecessor_links_back_to_successor);
               cfgv_assert(successor->kind == predecessor->kind);
               predecessor_links_back_to_successor = true;
            }
         }

         cfgv_assert(predecessor_links_back_to_successor);

         /* Each precessor block must appear only once in the list of
          * precessors.
          */
         brw_foreach_list_typed_from(bblock_link, later_precessor, link,
                                 &block->parents, predecessor->link.next) {
            cfgv_assert(predecessor->block != later_precessor->block);
         }
      }

      cfgv_assert(!block->instructions.is_empty());

      unsigned num_instructions = 0;
      foreach_inst_in_block(brw_inst, inst, block) {
         cfgv_assert(block == inst->block);
         num_instructions++;
      }
      cfgv_assert(num_instructions == block->num_instructions);

      counted_total_instructions += num_instructions;

      brw_inst *first_inst = block->start();
      if (first_inst->opcode == BRW_OPCODE_DO) {
         /* DO instructions both begin and end a block, so the DO instruction
          * must be the only instruction in the block.
          */
         cfgv_assert(brw_exec_list_is_singular(&block->instructions));

         /* A block starting with DO should have exactly two successors. One
          * is a physical link to the block starting after the WHILE
          * instruction. The other is a logical link to the block starting the
          * body of the loop.
          */
         bblock_t *physical_block = nullptr;
         bblock_t *logical_block = nullptr;

         brw_foreach_list_typed(bblock_link, child, link, &block->children) {
            if (child->kind == bblock_link_physical) {
               cfgv_assert(physical_block == nullptr);
               physical_block = child->block;
            } else {
               cfgv_assert(logical_block == nullptr);
               logical_block = child->block;
            }
         }

         cfgv_assert(logical_block != nullptr);
         cfgv_assert(physical_block != nullptr);

         /* A flow block (block ending with SHADER_OPCODE_FLOW) is
          * used to ensure that the block right after DO is always
          * present even if it doesn't have actual instructions.
          *
          * This way predicated WHILE and CONTINUE don't need to be
          * repaired when adding instructions right after the DO.
          * They will point to the flow block whether is empty or not.
          */
         cfgv_assert(logical_block->end()->opcode == SHADER_OPCODE_FLOW);
      }

      brw_inst *last_inst = block->end();
      if (last_inst->opcode == SHADER_OPCODE_FLOW) {
         /* A flow block only has one successor -- the instruction disappears
          * when generating code.
          */
         cfgv_assert(block->children.length() == 1);
      }
   }

   cfgv_assert(counted_total_instructions == total_instructions);
}
#endif
