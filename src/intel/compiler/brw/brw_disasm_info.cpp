/*
 * Copyright © 2014 Intel Corporation
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
#include "brw_eu.h"
#include "brw_disasm.h"
#include "brw_disasm_info.h"
#include "dev/intel_debug.h"
#include "compiler/nir/nir.h"
#include "util/lut.h"

static bool
is_do_block(struct bblock_t *block)
{
   return block->start()->opcode == BRW_OPCODE_DO;
}

static bool
is_flow_block(struct bblock_t *block)
{
   return block->start()->opcode == SHADER_OPCODE_FLOW;
}

static bool
should_omit_link(struct bblock_t *block,
                 struct bblock_link *link)
{
   return link->kind == bblock_link_physical &&
          (is_do_block(block) || is_do_block(link->block));
}

static void
print_successors_for_disasm(FILE *f, struct bblock_t *block)
{
   brw_foreach_list_typed(struct bblock_link, succ, link,
                          &block->children) {
      if (should_omit_link(block, succ))
         continue;
      if (is_do_block(succ->block) || is_flow_block(succ->block))
         print_successors_for_disasm(f, succ->block);
      else
         fprintf(f, " ->B%d", succ->block->num);
   }
}

static void
print_predecessors_for_disasm(FILE *f, struct bblock_t *block)
{
   brw_foreach_list_typed(struct bblock_link, pred, link,
                          &block->parents) {
      if (should_omit_link(block, pred))
         continue;
      if (is_do_block(pred->block) || is_flow_block(pred->block))
         print_predecessors_for_disasm(f, pred->block);
      else
         fprintf(f, " <-B%d", pred->block->num);
   }
}

void
dump_assembly(void *assembly, int start_offset, int end_offset,
              struct disasm_info *disasm, const unsigned *block_latency, FILE *f)
{
   const struct brw_isa_info *isa = disasm->isa;
   const char *last_annotation_string = NULL;

   void *mem_ctx = ralloc_context(NULL);
   const struct brw_label *root_label =
      brw_label_assembly(isa, assembly, start_offset, end_offset, mem_ctx);

   brw_foreach_list_typed(struct inst_group, group, link, &disasm->group_list) {
      struct brw_exec_node *next_node = brw_exec_node_get_next(&group->link);
      if (brw_exec_node_is_tail_sentinel(next_node))
         break;

      struct inst_group *next =
         brw_exec_node_data(struct inst_group, next_node, link);

      int start_offset = group->offset;
      int end_offset = next->offset;

      if (group->block_start) {
         fprintf(f, "   START B%d", group->block_start->num);
         print_predecessors_for_disasm(f, group->block_start);
         if (block_latency)
            fprintf(f, " (%u cycles)",
                    block_latency[group->block_start->num]);
         fprintf(f, "\n");
      }

      if (last_annotation_string != group->annotation) {
         last_annotation_string = group->annotation;
         if (last_annotation_string)
            fprintf(f, "   %s\n", last_annotation_string);
      }

      brw_disassemble(isa, assembly, start_offset, end_offset,
                      root_label, NULL, f);

      if (group->error) {
         fputs(group->error, f);
      }

      if (group->block_end) {
         fprintf(f, "   END B%d", group->block_end->num);
         print_successors_for_disasm(f, group->block_end);
         fprintf(f, "\n");
      }
   }
   fprintf(f, "\n");

   ralloc_free(mem_ctx);
}

struct disasm_info *
disasm_initialize(const struct brw_isa_info *isa,
                  const struct cfg_t *cfg)
{
   struct disasm_info *disasm = ralloc(NULL, struct disasm_info);
   brw_exec_list_make_empty(&disasm->group_list);
   disasm->isa = isa;
   disasm->cfg = cfg;
   disasm->cur_block = 0;
   disasm->use_tail = false;
   return disasm;
}

struct inst_group *
disasm_new_inst_group(struct disasm_info *disasm, int next_inst_offset)
{
   assert(next_inst_offset >= 0);
   struct inst_group *tail = rzalloc(disasm, struct inst_group);
   tail->offset = next_inst_offset;
   brw_exec_list_push_tail(&disasm->group_list, &tail->link);
   return tail;
}

void
disasm_annotate(struct disasm_info *disasm,
                brw_inst *inst, int offset)
{
   const struct cfg_t *cfg = disasm->cfg;

   struct inst_group *group;
   if (!disasm->use_tail) {
      group = disasm_new_inst_group(disasm, offset);
   } else {
      disasm->use_tail = false;
      group = brw_exec_node_data(struct inst_group,
                             brw_exec_list_get_tail_raw(&disasm->group_list), link);
   }

#ifndef NDEBUG
   if (INTEL_DEBUG(DEBUG_ANNOTATION)) {
      group->annotation = inst->annotation;

      if (group->annotation == NULL && inst->opcode == BRW_OPCODE_BFN)
         group->annotation = util_lut3_to_str[inst->src[3].ud & 0xff];
   }
#endif

   if (inst->opcode == BRW_OPCODE_DO ||
       inst->opcode == SHADER_OPCODE_FLOW) {
      disasm->use_tail = true;
      disasm->cur_block++;
      return;
   }

   if (cfg->blocks[disasm->cur_block]->start() == inst) {
      group->block_start = cfg->blocks[disasm->cur_block];
   }

   if (cfg->blocks[disasm->cur_block]->end() == inst) {
      group->block_end = cfg->blocks[disasm->cur_block];
      disasm->cur_block++;
   }
}

void
disasm_insert_error(struct disasm_info *disasm, int offset,
                    int inst_size, const char *error)
{
   brw_foreach_list_typed(struct inst_group, cur, link, &disasm->group_list) {
      struct brw_exec_node *next_node = brw_exec_node_get_next(&cur->link);
      if (brw_exec_node_is_tail_sentinel(next_node))
         break;

      struct inst_group *next =
         brw_exec_node_data(struct inst_group, next_node, link);

      if (next->offset <= offset)
         continue;

      if (offset + inst_size != next->offset) {
         struct inst_group *new_group = ralloc(disasm, struct inst_group);
         memcpy(new_group, cur, sizeof(struct inst_group));

         cur->error = NULL;
         cur->error_length = 0;
         cur->block_end = NULL;

         new_group->offset = offset + inst_size;
         new_group->block_start = NULL;

         brw_exec_node_insert_after(&cur->link, &new_group->link);
      }

      if (cur->error)
         ralloc_strcat(&cur->error, error);
      else
         cur->error = ralloc_strdup(disasm, error);
      return;
   }
}
