/*
 * Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/* For each output slot, gather which input components are used to compute it.
 * Component-wise ALU instructions must be scalar.
 */

#include "nir_builder.h"
#include "util/hash_table.h"
#include "util/u_dynarray.h"
#include "util/u_memory.h"

static void
accum_deps(BITSET_WORD *dst, BITSET_WORD *src, unsigned num_bitset_words)
{
   __bitset_or(dst, dst, src, num_bitset_words);
}

typedef struct {
   BITSET_WORD **instr_deps;
   BITSET_WORD *tmp;
   unsigned num_bitset_words;
} foreach_src_data;

static bool
accum_src_deps(nir_src *src, void *opaque)
{
   foreach_src_data *data = (foreach_src_data *)opaque;
   nir_instr *src_instr = nir_def_instr(src->ssa);

   if (src_instr->type == nir_instr_type_load_const ||
       src_instr->type == nir_instr_type_undef)
      return true;

   nir_instr *dst_instr = nir_src_parent_instr(src);
   accum_deps(data->instr_deps[dst_instr->index],
              data->instr_deps[src_instr->index], data->num_bitset_words);
   return true;
}

typedef struct {
   nir_block *start_block; /* the first block of the loop */
   nir_block *exit_block;  /* the first block after the loop */
   bool header_phi_changed;
} loop_entry;

static loop_entry *
get_current_loop(struct util_dynarray *loop_stack)
{
   assert(util_dynarray_num_elements(loop_stack, loop_entry));
   return util_dynarray_last_ptr(loop_stack, loop_entry);
}

/* For each output slot, gather which instructions are used to compute it.
 * The result is that each output slot will have the list of all instructions
 * that must execute to compute that output.
 *
 * If there are memory operations that affect other memory operations, those
 * dependencies are not gathered.
 *
 * Required:
 * - The shader must be in LCSSA form.
 *
 * Recommended:
 * - IO intrinsics and component-wise ALU instructions should be scalar, and
 *   vecN opcodes should have their components copy-propagated. If not,
 *   the results will have false dependencies.
 *
 * Algorithm:
 * - For each instruction, compute a bitset of instruction indices whose
 *   results are needed to compute the result of the instruction. The final
 *   bitset is the instruction index OR'd with bitsets of all its sources and
 *   also all if-conditions used to enter the block, recursively.
 * - Since every instruction inherits instruction bitsets from its sources,
 *   every instruction contains the list of all instructions that must execute
 *   before the instruction can execute.
 * - At the end, output stores contain the list of instructions that must
 *   execute to compute their results. This may be any subset of instructions
 *   from the shader, including all instructions.
 *
 * Control flow notes:
 * - There is a stack of "if" conditions for entered ifs.
 * - The dependencies of instructions are the union of dependencies of all
 *   their sources and all if conditions on the if-condition stack.
 * - For each continue, all loop-header phis receive the dependencies of all
 *   if-conditions on the if-condition stack at the continue.
 * - For each break, all loop-exit phis receive the dependencies of all
 *   if-conditions on the if-condition stack at the break.
 * - If there is any change to loop-header phis while iterating over a loop,
 *   we iterate over the loop again after the current iteration is finished.
 */
void
nir_gather_output_dependencies(nir_shader *nir, nir_output_deps *deps)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_metadata_require(impl, nir_metadata_instr_index);
   unsigned num_instr = nir_impl_last_block(impl)->end_ip;

   /* Allocate bitsets of instruction->instruction dependencies. */
   unsigned num_bitset_words = BITSET_WORDS(num_instr);
   BITSET_WORD **instr_deps = rzalloc_array(NULL, BITSET_WORD*, num_instr);
   void *mem_ctx = instr_deps;
   for (unsigned i = 0; i < num_instr; i++)
      instr_deps[i] = rzalloc_array(mem_ctx, BITSET_WORD, num_bitset_words);

   /* Allocate bitsets of instruction->output dependencies. */
   BITSET_WORD **out_deps = rzalloc_array(mem_ctx, BITSET_WORD*,
                                          NUM_TOTAL_VARYING_SLOTS);

   /* Allocate stacks. */
   struct util_dynarray loop_stack, if_cond_stack;
   util_dynarray_init(&loop_stack, mem_ctx);
   util_dynarray_init(&if_cond_stack, mem_ctx);

   /* Gather dependencies of every instruction.
    * Dependencies of each instruction are OR'd dependencies of its sources and
    * control flow conditions.
    */
   nir_foreach_block(block, impl) {
      nir_cf_node *parent_cf = block->cf_node.parent;
      bool is_loop_first_block = parent_cf->type == nir_cf_node_loop &&
                                 block == nir_cf_node_cf_tree_first(parent_cf);
      if (is_loop_first_block) {
         loop_entry loop = {
            .start_block = block,
            .exit_block = nir_cf_node_cf_tree_next(parent_cf),
         };
         util_dynarray_append(&loop_stack, loop);
      }

      if (parent_cf->type == nir_cf_node_if &&
          block == nir_if_first_then_block(nir_cf_node_as_if(parent_cf))) {
         util_dynarray_append(&if_cond_stack,
                              nir_cf_node_as_if(parent_cf)->condition.ssa);
      }

   loop_again:
      nir_foreach_instr(instr, block) {
         /* Add self as a dependency. */
         BITSET_WORD *this_instr_deps = instr_deps[instr->index];
         BITSET_SET(this_instr_deps, instr->index);

         /* Add sources as dependencies. */
         nir_foreach_src(instr, accum_src_deps,
                         &(foreach_src_data){instr_deps, NULL, num_bitset_words});

         /* Add parent if-conditions as dependencies.
          *
          * Note that phis with sources inside conditional blocks don't need
          * this because the phi sources already contain if-conditions.
          */
         util_dynarray_foreach(&if_cond_stack, nir_def *, cond) {
            accum_deps(this_instr_deps,
                       instr_deps[nir_def_instr(*cond)->index],
                       num_bitset_words);
         }

         /* Gather the current instruction. */
         switch (instr->type) {
         case nir_instr_type_jump:
            switch (nir_instr_as_jump(instr)->type) {
            case nir_jump_continue:
            case nir_jump_break: {
               loop_entry *loop = get_current_loop(&loop_stack);
               /* Iterate over all loop-header phis (for continue) or all
                * loop-exit phis (for break).
                *
                * Assumption: Only the loop-start block can have loop-header
                * phis.
                */
               bool is_continue =
                  nir_instr_as_jump(instr)->type == nir_jump_continue;
               nir_block *iter_block =
                  is_continue ? loop->start_block : loop->exit_block;
               assert(iter_block);

               nir_foreach_phi(phi, iter_block) {
                  /* We need to track whether any header phi of the current
                   * loop has changed because we need to walk such loops
                   * again. Use the bitset bitcount to determine whether
                   * any instruction has been added to header phis as
                   * a dependency.
                   */
                  unsigned old_count = 0;
                  if (is_continue) {
                     old_count = __bitset_count(instr_deps[phi->instr.index],
                                                num_bitset_words);
                  }

                  /* Add dependencies of all if-conditions affecting the
                   * jump statement to phis at the loop header / exit.
                   */
                  util_dynarray_foreach(&if_cond_stack, nir_def *, cond) {
                     accum_deps(instr_deps[phi->instr.index],
                                instr_deps[nir_def_instr(*cond)->index],
                                num_bitset_words);
                  }

                  if (is_continue &&
                      old_count != __bitset_count(instr_deps[phi->instr.index],
                                     num_bitset_words))
                     loop->header_phi_changed = true;
               }
               break;
            }
            default:
               UNREACHABLE("unexpected jump type");
            }
            break;

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            switch (intr->intrinsic) {
            case nir_intrinsic_store_output:
            case nir_intrinsic_store_per_vertex_output:
            case nir_intrinsic_store_per_primitive_output:
            case nir_intrinsic_store_per_view_output: {
               /* The write mask must be contiguous starting from x. */
               ASSERTED unsigned writemask = nir_intrinsic_write_mask(intr);
               assert(writemask == BITFIELD_MASK(util_bitcount(writemask)));

               nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
               assert(sem.num_slots >= 1);

               for (unsigned i = 0; i < sem.num_slots; i++) {
                  unsigned slot = sem.location + i;
                  if (!out_deps[slot]) {
                     out_deps[slot] = rzalloc_array(mem_ctx, BITSET_WORD,
                                                    num_bitset_words);
                  }
                  accum_deps(out_deps[slot], this_instr_deps, num_bitset_words);
               }
               break;
            }

            default:
               break;
            }
            break;
         }

         default:
            break;
         }
      }

      if (parent_cf->type == nir_cf_node_if &&
          block == nir_if_last_else_block(nir_cf_node_as_if(parent_cf))) {
         /* Add the current if stack to the phis after the if node because
          * this can happen:
          *
          *    a = load_const true
          *    b = load_const false
          *    if (cond) {
          *    } else {
          *    }
          *    c = phi a, b
          *
          * c depends on cond, but doesn't use any defs from then/else blocks.
          */
         nir_foreach_phi(phi, nir_cf_node_cf_tree_next(parent_cf)) {
            util_dynarray_foreach(&if_cond_stack, nir_def *, cond) {
               accum_deps(instr_deps[phi->instr.index],
                          instr_deps[nir_def_instr(*cond)->index],
                          num_bitset_words);
            }
         }

         assert(util_dynarray_num_elements(&if_cond_stack, nir_def *));
         (void)util_dynarray_pop_ptr(&if_cond_stack, nir_def *);
      }

      if (parent_cf->type == nir_cf_node_loop &&
          block == nir_cf_node_cf_tree_last(parent_cf)) {
         assert(util_dynarray_num_elements(&loop_stack, loop_entry));
         loop_entry *loop = get_current_loop(&loop_stack);

         /* Check if any loop header phis would be changed by iterating over
          * the loop again.
          */
         nir_foreach_phi(phi, loop->start_block) {
            unsigned old_count = __bitset_count(instr_deps[phi->instr.index],
                                                num_bitset_words);
            nir_foreach_src(&phi->instr, accum_src_deps,
                            &(foreach_src_data){instr_deps, NULL, num_bitset_words});
            if (old_count != __bitset_count(instr_deps[phi->instr.index],
                                            num_bitset_words)) {
               loop->header_phi_changed = true;
               break;
            }
         }

         if (loop->header_phi_changed) {
            loop->header_phi_changed = false;
            /* Iterate over the loop again: */
            is_loop_first_block = true;
            block = loop->start_block;
            assert(block);
            goto loop_again;
         }

         (void)util_dynarray_pop_ptr(&loop_stack, loop_entry);
      }
   }

   /* Gather instructions that affect each output from bitsets. */
   memset(deps, 0, sizeof(*deps));

   for (unsigned i = 0; i < NUM_TOTAL_VARYING_SLOTS; i++) {
      if (!out_deps[i])
         continue;

      unsigned total = __bitset_count(out_deps[i], num_bitset_words);
      unsigned added = 0;
      deps->output[i].num_instr = total;
      deps->output[i].instr_list = malloc(total * sizeof(nir_instr*));

      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (BITSET_TEST(out_deps[i], instr->index)) {
               assert(added < total);
               deps->output[i].instr_list[added++] = instr;
            }
         }
      }
      assert(added == total);
   }

   ralloc_free(mem_ctx);
}

void
nir_free_output_dependencies(nir_output_deps *deps)
{
   for (unsigned i = 0; i < ARRAY_SIZE(deps->output); i++) {
      assert(!!deps->output[i].instr_list == !!deps->output[i].num_instr);
      free(deps->output[i].instr_list);
   }
}

static unsigned
get_slot_index(nir_intrinsic_instr *intr, unsigned slot_offset)
{
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   return (sem.location + slot_offset) * 8 + nir_intrinsic_component(intr) * 2 +
          sem.high_16bits;
}

/* For each output slot, gather which inputs are used to compute it.
 * The shader must be in LCSSA form.
 *
 * If there are memory operations that affect other memory operations, those
 * dependencies are not gathered.
 */
void
nir_gather_input_to_output_dependencies(nir_shader *nir,
                                        nir_input_to_output_deps *out_deps)
{
   nir_output_deps deps;
   nir_gather_output_dependencies(nir, &deps);

   memset(out_deps, 0, sizeof(*out_deps));

   for (unsigned out = 0; out < ARRAY_SIZE(deps.output); out++) {
      unsigned num_instr = deps.output[out].num_instr;

      if (!num_instr)
         continue;

      out_deps->output[out].defined = true;

      for (unsigned i = 0; i < num_instr; i++) {
         nir_instr *instr = deps.output[out].instr_list[i];
         if (instr->type == nir_instr_type_tex) {
            if (!nir_tex_instr_is_query(nir_instr_as_tex(instr)))
               out_deps->output[out].uses_image_reads = true;
         }
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         switch (intr->intrinsic) {
         case nir_intrinsic_load_input:
         case nir_intrinsic_load_input_vertex:
         case nir_intrinsic_load_per_vertex_input:
         case nir_intrinsic_load_per_primitive_input:
         case nir_intrinsic_load_interpolated_input: {
            nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
            assert(intr->def.num_components == 1);
            assert(sem.num_slots >= 1);

            for (unsigned index = 0; index < sem.num_slots; index++) {
               unsigned slot = get_slot_index(intr, index);
               BITSET_SET(out_deps->output[out].inputs, slot);
            }
            break;
         }
         default: {
            const char *name = nir_intrinsic_infos[intr->intrinsic].name;

            if (strstr(name, "load_ssbo") || strstr(name, "ssbo_atomic"))
               out_deps->output[out].uses_ssbo_reads = true;

            if (strstr(name, "image") &&
                (strstr(name, "load") || strstr(name, "atomic")))
               out_deps->output[out].uses_image_reads = true;
            break;
         }
         }
      }
   }

   nir_free_output_dependencies(&deps);
}

void
nir_print_input_to_output_deps(nir_input_to_output_deps *deps,
                               nir_shader *nir, FILE *f)
{
   for (unsigned i = 0; i < NUM_TOTAL_VARYING_SLOTS; i++) {
      if (!deps->output[i].defined)
         continue;

      fprintf(f, "%s(->%s): %s =",
              _mesa_shader_stage_to_abbrev(nir->info.stage),
              nir->info.next_stage != MESA_SHADER_NONE ?
                 _mesa_shader_stage_to_abbrev(nir->info.next_stage) :
                 "NONE",
              gl_varying_slot_name_for_stage(i, nir->info.stage));

      unsigned in;
      BITSET_FOREACH_SET(in, deps->output[i].inputs, NUM_TOTAL_VARYING_SLOTS * 8) {
         fprintf(f, " %u.%c%s", in / 8, "xyzw"[(in % 8) / 2], in % 2 ? ".hi" : "");
      }
      fprintf(f, "%s%s",
              deps->output[i].uses_ssbo_reads ? " (ssbo read)" : "",
              deps->output[i].uses_image_reads ? " (image read)" : "");
      fprintf(f, "\n");
   }
}

/* Gather 3 disjoint sets:
 * - the set of input components only used to compute outputs for the clipper
 *   (those that are only used to compute the position and clip outputs)
 * - the set of input components only used to compute all other outputs
 * - the set of input components that are used to compute BOTH outputs for
 *   the clipper and all other outputs
 *
 * If there are memory operations that affect other memory operations, those
 * dependencies are not gathered.
 *
 * The shader must be in LCSSA form.
 *
 * Patch outputs are not gathered because shaders feeding the clipper don't
 * have patch outputs.
 */
void
nir_gather_output_clipper_var_groups(nir_shader *nir,
                                     nir_output_clipper_var_groups *groups)
{
   nir_input_to_output_deps *deps = calloc(1, sizeof(*deps));
   nir_gather_input_to_output_dependencies(nir, deps);

   uint32_t clipper_outputs = VARYING_BIT_POS |
                              VARYING_BIT_CLIP_VERTEX |
                              VARYING_BIT_CLIP_DIST0 |
                              VARYING_BIT_CLIP_DIST1 |
                              VARYING_BIT_CULL_DIST0 |
                              VARYING_BIT_CULL_DIST1;

   /* OR-reduce the per-output sets. */
   memset(groups, 0, sizeof(*groups));

   u_foreach_bit(i, clipper_outputs) {
      if (deps->output[i].defined) {
         BITSET_OR(groups->pos_only, groups->pos_only,
                   deps->output[i].inputs);
      }
   }

   for (unsigned i = 0; i < NUM_TOTAL_VARYING_SLOTS; i++) {
      if (deps->output[i].defined &&
          (i >= 32 || !(clipper_outputs & BITFIELD_BIT(i)))) {
         BITSET_OR(groups->var_only, groups->var_only,
                   deps->output[i].inputs);
      }
   }

   /* Compute the intersection of the above and make them disjoint. */
   BITSET_AND(groups->both, groups->pos_only, groups->var_only);
   BITSET_ANDNOT(groups->pos_only, groups->pos_only, groups->both);
   BITSET_ANDNOT(groups->var_only, groups->var_only, groups->both);
   free(deps);
}
