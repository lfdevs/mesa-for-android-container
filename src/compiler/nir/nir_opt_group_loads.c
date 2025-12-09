/*
 * Copyright © 2021 Advanced Micro Devices, Inc.
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

/* This is a new block-level load instruction scheduler where loads are grouped
 * according to their indirection level within a basic block. An indirection
 * is when a result of one load is used as a source of another load. The result
 * is that disjoint ALU opcode groups and load (texture) opcode groups are
 * created where each next load group is the next level of indirection.
 * It's done by finding the first and last load with the same indirection
 * level, and moving all unrelated instructions between them after the last
 * load except for load sources, which are moved before the first load.
 * It naturally suits hardware that has limits on texture indirections, but
 * other hardware can benefit too. Only texture, image, and SSBO load and
 * atomic instructions are grouped.
 *
 * There is an option to group only those loads that use the same resource
 * variable. This increases the chance to get more cache hits than if the loads
 * were spread out.
 *
 * The increased register usage is offset by the increase in observed memory
 * bandwidth due to more cache hits (dependent on hw behavior) and thus
 * decrease the subgroup lifetime, which allows registers to be deallocated
 * and reused sooner. In some bandwidth-bound cases, low register usage doesn't
 * benefit at all. Doubling the register usage and using those registers to
 * amplify observed bandwidth can improve performance a lot.
 *
 * It's recommended to run a hw-specific instruction scheduler after this to
 * prevent spilling.
 */

#include "nir.h"
#include "util/u_dynarray.h"

typedef struct {
   bool visited;
   uint32_t instr_index;
   uint32_t indirection_level;
} instr_info;

typedef struct opaque_resource opaque_resource;

static opaque_resource *
get_load_resource(nir_instr *instr)
{
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);

      for (unsigned i = 0; i < tex->num_srcs; i++) {
         switch (tex->src[i].src_type) {
         case nir_tex_src_texture_deref:
         case nir_tex_src_texture_handle:
            return (opaque_resource*)nir_def_instr(tex->src[i].src.ssa);
         default:
            break;
         }
      }

      /* Some drivers that don't support indirect resource indexing lower
       * derefs to the constant texture_index.
       */
      return (opaque_resource*)(uintptr_t)tex->texture_index;
   }

   if (instr->type == nir_instr_type_intrinsic) {
      /* This is also the list of intrinsics that are grouped. */
      switch (nir_instr_as_intrinsic(instr)->intrinsic) {
      /* Image loads. */
      case nir_intrinsic_image_load:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_bindless_image_load:
      case nir_intrinsic_image_sparse_load:
      case nir_intrinsic_image_deref_sparse_load:
      case nir_intrinsic_bindless_image_sparse_load:
      /* Fragment mask loads. (samples_identical also loads it) */
      case nir_intrinsic_image_fragment_mask_load_amd:
      case nir_intrinsic_image_deref_fragment_mask_load_amd:
      case nir_intrinsic_bindless_image_fragment_mask_load_amd:
      case nir_intrinsic_image_samples_identical:
      case nir_intrinsic_image_deref_samples_identical:
      case nir_intrinsic_bindless_image_samples_identical:
      /* Queries */
      case nir_intrinsic_image_size:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_bindless_image_size:
      case nir_intrinsic_image_samples:
      case nir_intrinsic_image_deref_samples:
      case nir_intrinsic_bindless_image_samples:
      case nir_intrinsic_image_levels:
      case nir_intrinsic_image_deref_levels:
      case nir_intrinsic_bindless_image_levels:
      /* Other loads. */
      /* load_ubo is ignored because it's usually cheap. */
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_load_global:
         return (opaque_resource*)nir_def_instr(nir_instr_as_intrinsic(instr)->src[0].ssa);
      default:
         return NULL;
      }
   }

   return NULL;
}

/* Track only those that we want to group. */
static bool
is_grouped_load(nir_instr *instr)
{
   if (instr->type == nir_instr_type_intrinsic &&
       !nir_intrinsic_can_reorder(nir_instr_as_intrinsic(instr)))
      return false;

   return get_load_resource(instr) != NULL;
}

static bool
is_part_of_group(nir_instr *instr, nir_instr *first,
                 uint32_t indirection_level, instr_info *infos)
{
   /* Grouping is done by moving everything else out of the first..last
    * instruction range of the load group corresponding to the given
    * indirection level.
    *
    * We can move anything that's not a grouped load because we are not really
    * moving it. What we are doing is that we are moving grouped loads to
    * the same place by moving everything else between the first and last load
    * out of the way. This doesn't change the order of non-reorderable
    * instructions.
    *
    * If "first" is set, compare against its indirection level, else compared
    * against "indirection_level".
    */
   return is_grouped_load(instr) &&
          infos[instr->index].indirection_level ==
          (first ? infos[first->index].indirection_level : indirection_level);
}

struct check_sources_state {
   instr_info *infos;
   nir_block *block;
   uint32_t first_instr_index;
};

static bool
has_only_sources_less_than(nir_src *src, void *data)
{
   struct check_sources_state *state = (struct check_sources_state *)data;

   /* true if nir_foreach_src should keep going */
   return state->block != nir_def_block(src->ssa) ||
             state->infos[nir_def_instr(src->ssa)->index].instr_index <
             state->first_instr_index;
}

static void
group_loads(nir_instr *first, nir_instr *last, instr_info *infos)
{
   assert(is_grouped_load(first));
   assert(is_grouped_load(last));

   /* Walk the instruction range between the first and last backward, and
    * move those that have no uses within the range after the last one.
    */
   for (nir_instr *instr = nir_instr_prev(last); instr != first;
        instr = nir_instr_prev(instr)) {
      if (is_part_of_group(instr, first, 0, infos))
         continue;

      bool all_uses_after_last = true;
      nir_def *def = nir_instr_def(instr);

      if (def) {
         nir_foreach_use(use, def) {
            if (nir_src_parent_instr(use)->block == instr->block &&
                infos[nir_src_parent_instr(use)->index].instr_index <=
                infos[last->index].instr_index) {
               all_uses_after_last = false;
               break;
            }
         }
      }

      if (all_uses_after_last) {
         nir_instr *move_instr = instr;
         /* Set the iterator to the next instruction because we'll move
          * the current one.
          */
         instr = nir_instr_next(instr);

         /* Move the instruction after the last and update its index to
          * indicate that it's after it.
          */
         nir_instr_move(nir_after_instr(last), move_instr);
         infos[move_instr->index].instr_index =
            infos[last->index].instr_index + 1;
      }
   }

   struct check_sources_state state;
   state.infos = infos;
   state.block = first->block;
   state.first_instr_index = infos[first->index].instr_index;

   /* Walk the instruction range between the first and last forward, and move
    * those that have no sources within the range before the first one.
    */
   for (nir_instr *instr = nir_instr_next(first); instr != last;
        instr = nir_instr_next(instr)) {
      /* Only move instructions without side effects. */
      if (is_part_of_group(instr, first, 0, infos))
         continue;

      if (nir_foreach_src(instr, has_only_sources_less_than, &state)) {
         nir_instr *move_instr = instr;
         /* Set the last instruction because we'll delete the current one. */
         instr = nir_instr_prev(instr);

         /* Move the instruction before the first and update its index
          * to indicate that it's before it.
          */
         nir_instr_move(nir_before_instr(first), move_instr);
         infos[move_instr->index].instr_index =
            infos[first->index].instr_index - 1;
      }
   }
}

static bool
is_pseudo_inst(nir_instr *instr)
{
   /* Other instructions do not usually contribute to the shader binary size. */
   return instr->type != nir_instr_type_alu &&
          instr->type != nir_instr_type_call &&
          instr->type != nir_instr_type_tex &&
          instr->type != nir_instr_type_intrinsic;
}

static void
set_instr_indices(nir_block *block, instr_info *infos)
{
   /* Start with 1 because we'll move instructions before the first one
    * and will want to label it 0.
    */
   unsigned counter = 1;
   nir_instr *last = NULL;

   nir_foreach_instr(instr, block) {
      /* Make sure grouped instructions don't have the same index as pseudo
       * instructions.
       */
      if (last && is_pseudo_inst(last) && is_grouped_load(instr))
         counter++;

      /* Set each instruction's index within the block. */
      infos[instr->index].instr_index = counter;

      /* Only count non-pseudo instructions. */
      if (!is_pseudo_inst(instr))
         counter++;

      last = instr;
   }
}

static void
handle_load_range(nir_instr **first, nir_instr **last, nir_instr *current,
                  unsigned max_distance, instr_info *infos)
{
   assert(!current || !*first ||
          infos[current->index].instr_index >=
          infos[(*first)->index].instr_index);
   if (*first && *last &&
       (!current ||
        infos[current->index].instr_index -
        infos[(*first)->index].instr_index > max_distance)) {
      assert(*first != *last);
      group_loads(*first, *last, infos);
      set_instr_indices((*first)->block, infos);
      *first = NULL;
      *last = NULL;
   }
}

static bool
is_demote(nir_instr *instr)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

      if (intr->intrinsic == nir_intrinsic_terminate ||
          intr->intrinsic == nir_intrinsic_terminate_if ||
          intr->intrinsic == nir_intrinsic_demote ||
          intr->intrinsic == nir_intrinsic_demote_if)
         return true;
   }

   return false;
}

struct indirection_state {
   instr_info *infos;
   nir_block *block;
   unsigned indirections;
};

static unsigned
get_num_indirections(nir_instr *instr, instr_info *infos);

static bool
gather_indirections(nir_src *src, void *data)
{
   struct indirection_state *state = (struct indirection_state *)data;
   nir_instr *instr = nir_def_instr(src->ssa);

   /* We only count indirections within the same block. */
   if (instr->block == state->block) {
      unsigned indirections = get_num_indirections(nir_def_instr(src->ssa),
                                                   state->infos);

      if (instr->type == nir_instr_type_tex || is_grouped_load(instr))
         indirections++;

      state->indirections = MAX2(state->indirections, indirections);
   }

   return true; /* whether nir_foreach_src should keep going */
}

/* Return the number of load indirections within the block. */
static unsigned
get_num_indirections(nir_instr *instr, instr_info *infos)
{
   /* Don't traverse phis because we could end up in an infinite recursion
    * if the phi points to the current block (such as a loop body).
    */
   if (instr->type == nir_instr_type_phi)
      return 0;

   if (infos[instr->index].visited)
      return infos[instr->index].instr_index;

   struct indirection_state state;
   state.infos = infos;
   state.block = instr->block;
   state.indirections = 0;

   nir_foreach_src(instr, gather_indirections, &state);

   infos[instr->index].visited = true;
   infos[instr->index].instr_index = state.indirections;
   return state.indirections;
}

static void
process_block(nir_block *block, nir_load_grouping grouping,
              unsigned max_distance, instr_info *infos)
{
   int max_indirection = -1;
   unsigned num_inst_per_level[256] = { 0 };

   for (unsigned i = 0; i < block->end_ip + 1 - block->start_ip; i++) {
      infos[block->start_ip + i].visited = false;
   }

   /* Count the number of load indirections for each load instruction
    * within this block.
    */
   nir_foreach_instr(instr, block) {
      if (is_grouped_load(instr)) {
         unsigned indirections = get_num_indirections(instr, infos);

         num_inst_per_level[indirections]++;
         infos[instr->index].indirection_level = indirections;

         max_indirection = MAX2(max_indirection, (int)indirections);
      }
   }

   /* Each indirection level is grouped. */
   for (int level = 0; level <= max_indirection; level++) {
      if (num_inst_per_level[level] <= 1)
         continue;

      set_instr_indices(block, infos);

      opaque_resource *resource = NULL;
      nir_instr *first_load = NULL, *last_load = NULL;

      /* Find the first and last instruction that use the same
       * resource and are within a certain distance of each other.
       * If found, group them by moving all movable instructions
       * between them out.
       */
      nir_foreach_instr(current, block) {
         /* Don't group across terminate. */
         if (is_demote(current)) {
            /* Group unconditionally.  */
            handle_load_range(&first_load, &last_load, NULL, 0, infos);
            first_load = NULL;
            last_load = NULL;
            continue;
         }

         /* Only group load instructions with the same indirection level. */
         if (is_part_of_group(current, NULL, level, infos)) {
            opaque_resource *current_resource;

            switch (grouping) {
            case nir_group_all:
               if (!first_load)
                  first_load = current;
               else
                  last_load = current;
               break;

            case nir_group_same_resource_only:
               current_resource = get_load_resource(current);

               if (current_resource) {
                  if (!first_load) {
                     first_load = current;
                     resource = current_resource;
                  } else if (current_resource == resource) {
                     last_load = current;
                  }
               }
            }
         }

         /* Group only if we exceeded the maximum distance. */
         handle_load_range(&first_load, &last_load, current, max_distance,
                           infos);
      }

      /* Group unconditionally.  */
      handle_load_range(&first_load, &last_load, NULL, 0, infos);
   }
}

/* max_distance is the maximum distance between the first and last instruction
 * in a group.
 */
bool
nir_opt_group_loads(nir_shader *shader, nir_load_grouping grouping,
                    unsigned max_distance)
{
   /* Temporary space for instruction info. */
   struct util_dynarray infos_scratch = UTIL_DYNARRAY_INIT;

   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_instr_index);

      unsigned num_instr =
         nir_impl_last_block(impl)->end_ip + 1; /* we might need 1 more */
      instr_info *infos =
         (instr_info*)util_dynarray_resize(&infos_scratch, instr_info,
                                           num_instr);

      nir_foreach_block(block, impl) {
         process_block(block, grouping, max_distance, infos);
      }

      nir_progress(true, impl,
                   nir_metadata_control_flow | nir_metadata_loop_analysis);
   }

   util_dynarray_fini(&infos_scratch);
   return true;
}
