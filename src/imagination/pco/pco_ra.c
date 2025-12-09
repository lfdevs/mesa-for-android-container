/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * based in part on asahi driver which is:
 * Copyright 2022 Alyssa Rosenzweig
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_ra.c
 *
 * \brief PCO register allocator.
 */

#include "hwdef/rogue_hw_utils.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/bitset.h"
#include "util/hash_table.h"
#include "util/macros.h"
#include "util/register_allocate.h"
#include "util/sparse_array.h"
#include "util/u_dynarray.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** Live range of an SSA variable. */
struct live_range {
   unsigned start;
   unsigned end;
};

/** Vector override information. */
struct vec_override {
   pco_ref ref;
   unsigned offset;
};

typedef struct _pco_ra_ctx {
   unsigned allocable_temps;
   unsigned allocable_vtxins;
   unsigned allocable_interns;

   unsigned temp_alloc_offset;

   bool spilling_setup;
   pco_ref spill_inst_addr_comps[2];
   pco_ref spill_addr_comps[2];
   pco_ref spill_data;
   pco_ref spill_addr;
   pco_ref spill_addr_data;
   unsigned spilled_temps;

   bool done;
} pco_ra_ctx;

/**
 * \brief Checks if a vec has ssa sources that are referenced more than once.
 *
 * \param[in] vec Vec instruction.
 * \return True if the vec has ssa sources that are referenced more than once.
 */
static bool vec_has_repeated_ssas(pco_instr *vec)
{
   assert(vec->op == PCO_OP_VEC);

   pco_foreach_instr_src_ssa (psrc, vec) {
      pco_foreach_instr_src_ssa_from (psrc_inner, vec, psrc) {
         if (psrc_inner->val == psrc->val)
            return true;
      }
   }

   return false;
}

static void pco_extend_live_range(pco_ref origin,
                                  pco_ref current_ref,
                                  pco_instr *current_instr,
                                  struct hash_table_u64 *overrides,
                                  struct live_range *live_ranges)
{
   pco_foreach_instr_in_func_from (instr, current_instr) {
      pco_foreach_instr_src_ssa (psrc, instr) {
         if (current_ref.val != psrc->val)
            continue;

         pco_foreach_instr_dest_ssa (pdest, instr) {
            struct vec_override *override =
               _mesa_hash_table_u64_search(overrides, pdest->val);

            if (override) {
               live_ranges[origin.val].end =
                  MAX2(live_ranges[origin.val].end,
                       live_ranges[override->ref.val].end);
               break;
            }

            live_ranges[origin.val].end =
               MAX2(live_ranges[origin.val].end, instr->index);
         }

         break;
      }
   }
}

typedef struct _pco_use {
   pco_instr *instr;
   pco_ref *ref;
} pco_use;

static void preproc_vecs(pco_func *func)
{
   unsigned num_ssas = func->next_ssa;

   void *mem_ctx = ralloc_context(NULL);
   BITSET_WORD *multi_use_elems = rzalloc_array_size(mem_ctx,
                                                     sizeof(*multi_use_elems),
                                                     BITSET_WORDS(num_ssas));
   struct hash_table_u64 *elem_uses = _mesa_hash_table_u64_create(mem_ctx);

   bool needs_reindex = false;
   pco_foreach_instr_in_func (instr, func) {
      if (instr->op != PCO_OP_VEC)
         continue;

      pco_foreach_instr_src_ssa (psrc, instr) {
         struct util_dynarray *uses =
            _mesa_hash_table_u64_search(elem_uses, psrc->val);
         if (!uses) {
            uses = rzalloc_size(elem_uses, sizeof(*uses));
            util_dynarray_init(uses, uses);
            _mesa_hash_table_u64_insert(elem_uses, psrc->val, uses);
         }

         if (uses->size > 0)
            BITSET_SET(multi_use_elems, psrc->val);

         pco_use use = {
            .instr = instr,
            .ref = psrc,
         };
         util_dynarray_append(uses, use);
      }
   }

   unsigned b;
   BITSET_FOREACH_SET (b, multi_use_elems, num_ssas) {
      struct util_dynarray *uses = _mesa_hash_table_u64_search(elem_uses, b);

      pco_instr *producer = NULL;
      pco_ref var;
      pco_foreach_instr_in_func (instr, func) {
         pco_foreach_instr_dest_ssa (pdest, instr) {
            if (pdest->val == b) {
               producer = instr;
               var = *pdest;
               break;
            }
         }

         if (producer)
            break;
      }
      assert(producer);

      pco_builder b =
         pco_builder_create(func, pco_cursor_after_instr(producer));

      util_dynarray_foreach (uses, pco_use, use) {
         b.cursor = pco_cursor_before_instr(use->instr);
         if (pco_ref_get_chans(var) <= 4) {
            pco_ref dest = pco_ref_new_ssa_clone(func, var);
            pco_mbyp(&b,
                     dest,
                     var,
                     .exec_cnd = pco_instr_has_exec_cnd(producer)
                                    ? pco_instr_get_exec_cnd(producer)
                                    : PCO_EXEC_CND_E1_ZX,
                     .rpt = pco_ref_get_chans(var));
            *use->ref = dest;
         } else {
            assert(use->instr->op == PCO_OP_VEC && producer->op == PCO_OP_VEC);

            pco_instr *instr =
               pco_instr_create(func,
                                1,
                                use->instr->num_srcs + producer->num_srcs - 1);
            instr->op = PCO_OP_VEC;

            instr->dest[0] = use->instr->dest[0];

            unsigned num_srcs = 0;
            for (unsigned s = 0; s < use->instr->num_srcs; ++s) {
               if (&use->instr->src[s] != use->ref) {
                  instr->src[num_srcs++] = use->instr->src[s];
               } else {
                  for (unsigned t = 0; t < producer->num_srcs; ++t)
                     instr->src[num_srcs++] = producer->src[t];
               }
            }

            pco_instr_set_exec_cnd(instr, pco_instr_get_exec_cnd(use->instr));
            pco_builder_insert_instr(&b, instr);

            pco_instr_delete(use->instr);
            needs_reindex = true;
         }
      }
   }

   ralloc_free(mem_ctx);

   if (needs_reindex)
      pco_index(func->parent_shader, false);
}

typedef struct _pco_copy {
   pco_ref src;
   pco_ref dest;
   bool s1;

   bool done;
} pco_copy;

static inline bool
copy_blocked(pco_copy *copy, unsigned *temp_use_counts, unsigned lowest_temp)
{
   return temp_use_counts[pco_ref_get_temp(copy->dest) - lowest_temp] > 0;
}

static inline void
do_copy(pco_builder *b, enum pco_exec_cnd exec_cnd, pco_copy *copy)
{
   if (copy->s1)
      pco_movs1(b, copy->dest, copy->src, .exec_cnd = exec_cnd);
   else
      pco_mbyp(b, copy->dest, copy->src, .exec_cnd = exec_cnd);
}

static inline void
do_swap(pco_builder *b, enum pco_exec_cnd exec_cnd, pco_copy *copy)
{
   assert(!copy->s1);

   pco_mbyp2(b,
             copy->dest,
             pco_ref_reset_mods(copy->src),
             copy->src,
             copy->dest,
             .exec_cnd = exec_cnd);
}

static void emit_copies(pco_builder *b,
                        struct util_dynarray *copies,
                        enum pco_exec_cnd exec_cnd,
                        unsigned highest_temp,
                        unsigned lowest_temp)
{
   unsigned temp_range = highest_temp - lowest_temp + 1;
   unsigned *temp_use_counts =
      rzalloc_array_size(NULL, sizeof(*temp_use_counts), temp_range);
   pco_copy **temp_writes =
      rzalloc_array_size(NULL, sizeof(*temp_writes), temp_range);

   util_dynarray_foreach (copies, pco_copy, copy) {
      if (pco_ref_is_temp(copy->src))
         ++temp_use_counts[pco_ref_get_temp(copy->src) - lowest_temp];

      temp_writes[pco_ref_get_temp(copy->dest) - lowest_temp] = copy;
   }

   bool progress = true;
   while (progress) {
      progress = false;

      util_dynarray_foreach (copies, pco_copy, copy) {
         if (!copy->done && !copy_blocked(copy, temp_use_counts, lowest_temp)) {
            copy->done = true;
            progress = true;
            do_copy(b, exec_cnd, copy);

            if (pco_ref_is_temp(copy->src))
               --temp_use_counts[pco_ref_get_temp(copy->src) - lowest_temp];

            temp_writes[pco_ref_get_temp(copy->dest) - lowest_temp] = NULL;
         }
      }

      if (progress)
         continue;

      util_dynarray_foreach (copies, pco_copy, copy) {
         if (copy->done)
            continue;

         if (pco_refs_are_equal(copy->src, copy->dest, true)) {
            copy->done = true;
            continue;
         }

         do_swap(b, exec_cnd, copy);
         copy->src = pco_ref_reset_mods(copy->src);

         util_dynarray_foreach (copies, pco_copy, blocking) {
            if (pco_ref_get_temp(blocking->src) >=
                   pco_ref_get_temp(copy->dest) &&
                pco_ref_get_temp(blocking->src) <
                   (pco_ref_get_temp(copy->dest) + 1)) {
               blocking->src = pco_ref_offset(blocking->src,
                                              pco_ref_get_temp(copy->src) -
                                                 pco_ref_get_temp(copy->dest));
            }
         }

         copy->done = true;
      }
   }

   util_dynarray_foreach (copies, pco_copy, copy) {
      assert(copy->done);
   }

   ralloc_free(temp_writes);
   ralloc_free(temp_use_counts);
}

static void setup_spill_base(pco_shader *shader,
                             pco_ref spill_inst_addr_comps[2])
{
   pco_func *entry = pco_entrypoint(shader);
   pco_block *first_block = pco_func_first_block(entry);
   pco_builder b =
      pco_builder_create(entry, pco_cursor_before_block(first_block));

   assert(shader->data.common.spill_info.count > 0);
   unsigned base_addr_lo_idx = shader->data.common.spill_info.start;
   unsigned base_addr_hi_idx = shader->data.common.spill_info.start + 1;
   unsigned block_size_idx = shader->data.common.spill_info.start + 2;

   pco_ref base_addr_lo = pco_ref_hwreg(base_addr_lo_idx, PCO_REG_CLASS_SHARED);
   pco_ref base_addr_hi = pco_ref_hwreg(base_addr_hi_idx, PCO_REG_CLASS_SHARED);
   pco_ref block_size = pco_ref_hwreg(block_size_idx, PCO_REG_CLASS_SHARED);
   pco_ref local_addr_inst_num =
      pco_ref_hwreg(PCO_SR_LOCAL_ADDR_INST_NUM, PCO_REG_CLASS_SPEC);

   pco_imadd64(&b,
               spill_inst_addr_comps[0],
               spill_inst_addr_comps[1],
               block_size,
               local_addr_inst_num,
               base_addr_lo,
               base_addr_hi,
               pco_ref_null());
}

static void spill(unsigned spill_index, pco_func *func, pco_ra_ctx *ctx)
{
   unsigned spill_offset = ctx->spilled_temps++;

   pco_foreach_instr_in_func (instr, func) {
      pco_builder b = pco_builder_create(func, pco_cursor_before_instr(instr));
      pco_foreach_instr_dest_ssa (pdest, instr) {
         if (pdest->val != spill_index)
            continue;

         pco_ref imm_off = pco_ref_imm32(spill_offset);
         pco_movi32(&b, ctx->spill_data, imm_off);
         pco_imadd64(&b,
                     ctx->spill_addr_comps[0],
                     ctx->spill_addr_comps[1],
                     ctx->spill_data,
                     pco_4,
                     ctx->spill_inst_addr_comps[0],
                     ctx->spill_inst_addr_comps[1],
                     pco_ref_null());

         /**/

         *pdest = ctx->spill_data;

         pco_instr *next_instr = pco_next_instr(instr);
         if (next_instr && next_instr->op == PCO_OP_WDF)
            b.cursor = pco_cursor_after_instr(next_instr);
         else
            b.cursor = pco_cursor_after_instr(instr);

         pco_st32(&b,
                  ctx->spill_data,
                  pco_ref_drc(PCO_DRC_0),
                  pco_ref_imm8(1),
                  ctx->spill_addr_data,
                  pco_ref_null());

         pco_wdf(&b, pco_ref_drc(PCO_DRC_0));

         break;
      }

      b.cursor = pco_cursor_before_instr(instr);
      bool load_done = false;
      pco_foreach_instr_src_ssa (pdest, instr) {
         if (pdest->val != spill_index)
            continue;

         if (!load_done) {
            pco_ref imm_off = pco_ref_imm32(spill_offset);
            pco_movi32(&b, ctx->spill_data, imm_off);
            pco_imadd64(&b,
                        ctx->spill_addr_comps[0],
                        ctx->spill_addr_comps[1],
                        ctx->spill_data,
                        pco_4,
                        ctx->spill_inst_addr_comps[0],
                        ctx->spill_inst_addr_comps[1],
                        pco_ref_null());

            pco_ld(&b,
                   ctx->spill_data,
                   pco_ref_drc(PCO_DRC_0),
                   pco_ref_imm8(1),
                   ctx->spill_addr);

            pco_wdf(&b, pco_ref_drc(PCO_DRC_0));

            load_done = true;
         }

         *pdest = ctx->spill_data;
      }
   }

   pco_index(func->parent_shader, false);
}

/**
 * \brief Performs register allocation on a function.
 *
 * \param[in,out] func PCO shader.
 * \param[in] allocable_temps Number of allocatable temp registers.
 * \param[in] allocable_vtxins Number of allocatable vertex input registers.
 * \param[in] allocable_interns Number of allocatable internal registers.
 * \return True if registers were allocated.
 */
static bool pco_ra_func(pco_func *func, pco_ra_ctx *ctx)
{
   /* TODO: support multiple functions and calls. */
   assert(func->type == PCO_FUNC_TYPE_ENTRYPOINT);

   /* TODO: loop lifetime extension.
    * TODO: track successors/predecessors.
    */

   preproc_vecs(func);

   unsigned num_ssas = func->next_ssa;
   unsigned num_vregs = func->next_vreg;
   unsigned num_vars = num_ssas + num_vregs;

   /* Collect used bit sizes. */
   uint8_t used_bits = 0;
   pco_foreach_instr_in_func (instr, func) {
      pco_foreach_instr_dest_ssa (pdest, instr) {
         used_bits |= (1 << pdest->bits);
      }
   }

   /* vregs are always 32x1. */
   if (num_vregs > 0) {
      used_bits |= (1 << PCO_BITS_32);
   }

   /* No registers to allocate. */
   if (!used_bits) {
      ctx->done = true;
      return false;
   }

   /* 64-bit vars should've been lowered by now. */
   assert(!(used_bits & (1 << PCO_BITS_64)));

   /* TODO: support multiple bit sizes. */
   bool only_32bit = used_bits == (1 << PCO_BITS_32);
   assert(only_32bit);

   struct ra_regs *ra_regs =
      ra_alloc_reg_set(func, ctx->allocable_temps, !only_32bit);

   BITSET_WORD *comps =
      rzalloc_array_size(ra_regs, sizeof(*comps), BITSET_WORDS(num_ssas));

   /* Overrides for vector coalescing. */
   struct hash_table_u64 *overrides = _mesa_hash_table_u64_create(ra_regs);
   pco_foreach_instr_in_func_rev (instr, func) {
      if (instr->op != PCO_OP_VEC)
         continue;

      /* Can't override vec ssa sources if they're referenced more than once. */
      if (vec_has_repeated_ssas(instr))
         continue;

      pco_ref dest = instr->dest[0];
      unsigned offset = 0;

      struct vec_override *src_override =
         _mesa_hash_table_u64_search(overrides, dest.val);

      if (src_override) {
         dest = src_override->ref;
         offset += src_override->offset;
      }

      pco_foreach_instr_src (psrc, instr) {
         /* TODO: skip if vector producer is used by multiple things in a way
          * that doesn't allow coalescing. */
         /* TODO: can NIR scalarise things so that the only remaining vectors
          * can be used in this way? */

         if (pco_ref_is_ssa(*psrc)) {
            /* Make sure this hasn't already been overridden somewhere else! */
#if 1
            if (_mesa_hash_table_u64_search(overrides, psrc->val)) {
               BITSET_SET(comps, psrc->val);
               continue;
            }
#else
            assert(!_mesa_hash_table_u64_search(overrides, psrc->val));
#endif

            struct vec_override *src_override =
               rzalloc_size(overrides, sizeof(*src_override));
            src_override->ref = dest;
            src_override->offset = offset;

            _mesa_hash_table_u64_insert(overrides, psrc->val, src_override);
         }

         offset += pco_ref_get_chans(*psrc);
      }
   }

   /* Overrides for vector component uses. */
   pco_foreach_instr_in_func (instr, func) {
      if (instr->op != PCO_OP_COMP)
         continue;

      pco_ref dest = instr->dest[0];
      pco_ref src = instr->src[0];
      unsigned offset = pco_ref_get_imm(instr->src[1]);

      BITSET_SET(comps, dest.val);

      assert(pco_ref_is_ssa(src));
      assert(pco_ref_is_ssa(dest));

      struct vec_override *vec_override =
         _mesa_hash_table_u64_search(overrides, src.val);

      if (vec_override) {
         src = vec_override->ref;
         offset += vec_override->offset;
      }

      struct vec_override *src_override =
         rzalloc_size(overrides, sizeof(*src_override));
      src_override->ref = src;
      src_override->offset = offset;
      _mesa_hash_table_u64_insert(overrides, dest.val, src_override);
   }

   /* Allocate classes. */
   struct hash_table_u64 *ra_classes = _mesa_hash_table_u64_create(ra_regs);
   pco_foreach_instr_in_func (instr, func) {
      pco_foreach_instr_dest_ssa (pdest, instr) {
         unsigned chans = pco_ref_get_chans(*pdest);
         /* TODO: bitset instead of search? */
         if (_mesa_hash_table_u64_search(ra_classes, chans))
            continue;

         /* Skip if collated. */
         if (_mesa_hash_table_u64_search(overrides, pdest->val))
            continue;

         struct ra_class *ra_class = ra_alloc_contig_reg_class(ra_regs, chans);
         _mesa_hash_table_u64_insert(ra_classes, chans, ra_class);
      }
   }

   /* vregs are always 32x1. */
   if (num_vregs > 0) {
      if (!_mesa_hash_table_u64_search(ra_classes, 1)) {
         struct ra_class *ra_class = ra_alloc_contig_reg_class(ra_regs, 1);
         _mesa_hash_table_u64_insert(ra_classes, 1, ra_class);
      }
   }

   /* Assign registers to classes. */
   hash_table_u64_foreach (ra_classes, entry) {
      const unsigned stride = entry.key;
      struct ra_class *ra_class = entry.data;

      for (unsigned t = 0; t < ctx->allocable_temps - (stride - 1); ++t)
         ra_class_add_reg(ra_class, t);
   }

   ra_set_finalize(ra_regs, NULL);

   struct ra_graph *ra_graph = ra_alloc_interference_graph(ra_regs, num_vars);
   ralloc_steal(ra_regs, ra_graph);

   /* Allocate and calculate live ranges. */
   struct live_range *live_ranges =
      rzalloc_array_size(ra_regs, sizeof(*live_ranges), num_vars);

   for (unsigned u = 0; u < num_vars; ++u)
      live_ranges[u].start = ~0U;

   pco_foreach_instr_in_func (instr, func) {
      pco_foreach_instr_dest_ssa (pdest, instr) {
         pco_ref dest = *pdest;
         struct vec_override *override =
            _mesa_hash_table_u64_search(overrides, dest.val);

         if (override)
            dest = override->ref;

         live_ranges[dest.val].start =
            MIN2(live_ranges[dest.val].start, instr->index);

         if (override)
            continue;

         /* Set class if it hasn't already been set up in an override. */
         unsigned chans = pco_ref_get_chans(dest);
         struct ra_class *ra_class =
            _mesa_hash_table_u64_search(ra_classes, chans);
         assert(ra_class);

         ra_set_node_class(ra_graph, dest.val, ra_class);
      }

      pco_foreach_instr_src_ssa (psrc, instr) {
         pco_ref src = *psrc;
         struct vec_override *override =
            _mesa_hash_table_u64_search(overrides, src.val);

         if (override)
            src = override->ref;

         live_ranges[src.val].end =
            MAX2(live_ranges[src.val].end, instr->index);
      }

      pco_foreach_instr_dest_vreg (pdest, instr) {
         pco_ref dest = *pdest;

         /* Place vregs after ssa vars. */
         dest.val += num_ssas;

         live_ranges[dest.val].start =
            MIN2(live_ranges[dest.val].start, instr->index);

         /* Set class if it hasn't already been set up in an override. */
         unsigned chans = pco_ref_get_chans(dest);
         struct ra_class *ra_class =
            _mesa_hash_table_u64_search(ra_classes, chans);
         assert(ra_class);

         ra_set_node_class(ra_graph, dest.val, ra_class);
      }

      pco_foreach_instr_src_vreg (psrc, instr) {
         pco_ref src = *psrc;

         /* Place vregs after ssa vars. */
         src.val += num_ssas;

         live_ranges[src.val].end =
            MAX2(live_ranges[src.val].end, instr->index);
      }
   }

   /* Extend lifetimes of non-overriden vecs that have comp instructions. */
   pco_foreach_instr_in_func (instr, func) {
      if (instr->op != PCO_OP_COMP)
         continue;

      pco_ref dest = instr->dest[0];
      pco_ref src_vec = instr->src[0];

      struct vec_override *vec_override =
         _mesa_hash_table_u64_search(overrides, src_vec.val);

      /* Already taken care of. */
      if (vec_override) {
         assert(live_ranges[src_vec.val].start == ~0U &&
                live_ranges[src_vec.val].end == 0);
         continue;
      }

      pco_extend_live_range(src_vec, dest, instr, overrides, live_ranges);
   }

   /* Extend lifetimes of vars in loops. */
   pco_foreach_loop_in_func (loop, func) {
      pco_block *prologue_block =
         pco_cf_node_as_block(pco_cf_node_head(&loop->prologue));

      pco_block *epilogue_block =
         pco_cf_node_as_block(pco_cf_node_tail(&loop->epilogue));

      unsigned loop_start_index = pco_first_instr(prologue_block)->index;
      unsigned loop_end_index = pco_last_instr(epilogue_block)->index;

      /* If a var is defined before a loop and stops being used during it,
       * extend its lifetime to the end of the loop.
       */

      for (unsigned var = 0; var < num_vars; ++var) {
         if (live_ranges[var].start < loop_start_index &&
             live_ranges[var].end > loop_start_index &&
             live_ranges[var].end < loop_end_index) {
            live_ranges[var].end = loop_end_index;
         }
      }
   }

   /* If there are instructions left with any unused dests that aren't/couldn't
    * be DCEd (e.g. because of side effects), ensure their range ends are setup
    * to avoid missing overlaps and clobbering regs.
    */
   for (unsigned var = 0; var < num_vars; ++var)
      if (live_ranges[var].start != ~0U && live_ranges[var].end == 0)
         live_ranges[var].end = live_ranges[var].start;

   /* Build interference graph from overlapping live ranges. */
   for (unsigned var0 = 0; var0 < num_vars; ++var0) {
      for (unsigned var1 = var0 + 1; var1 < num_vars; ++var1) {
         /* If the live ranges overlap, the register nodes interfere. */
         if ((live_ranges[var0].start != ~0U && live_ranges[var1].end != ~0U) &&
             !(live_ranges[var0].start >= live_ranges[var1].end ||
               live_ranges[var1].start >= live_ranges[var0].end)) {
            ra_add_node_interference(ra_graph, var0, var1);
         }
      }
   }

   pco_foreach_instr_in_func_rev (vec, func) {
      if (vec->op != PCO_OP_VEC)
         continue;

      pco_foreach_instr_src_ssa (psrc, vec) {
         ra_add_node_interference(ra_graph, vec->dest[0].val, psrc->val);
      }
   }

   /* Make srcs and dests interfere for instructions with repeat > 1. */
   pco_foreach_instr_in_func_rev (instr, func) {
      if (!pco_instr_has_rpt(instr))
         continue;

      if (pco_instr_get_rpt(instr) < 2)
         continue;

      pco_foreach_instr_dest_ssa (pdest, instr) {
         pco_foreach_instr_src_ssa (psrc, instr) {
            ra_add_node_interference(ra_graph, pdest->val, psrc->val);
         }
      }
   }

   bool allocated = ra_allocate(ra_graph);
   bool force_spill = false;
   if (!allocated || force_spill) {
      if (!ctx->spilling_setup) {
         ctx->spill_inst_addr_comps[0] = pco_ref_hwreg(0, PCO_REG_CLASS_TEMP);
         ctx->spill_inst_addr_comps[1] = pco_ref_hwreg(1, PCO_REG_CLASS_TEMP);

         ctx->spill_addr_comps[0] = pco_ref_hwreg(2, PCO_REG_CLASS_TEMP);
         ctx->spill_addr_comps[1] = pco_ref_hwreg(3, PCO_REG_CLASS_TEMP);

         ctx->spill_data = pco_ref_hwreg(4, PCO_REG_CLASS_TEMP);

         ctx->spill_addr = pco_ref_hwreg_vec(2, PCO_REG_CLASS_TEMP, 2);
         ctx->spill_addr_data = pco_ref_hwreg_vec(2, PCO_REG_CLASS_TEMP, 3);

         ctx->allocable_temps -= 5;
         ctx->temp_alloc_offset = 5;

         setup_spill_base(func->parent_shader, ctx->spill_inst_addr_comps);
         ctx->spilling_setup = true;
      }

      unsigned *uses = rzalloc_array_size(ra_regs, sizeof(*uses), num_ssas);
      pco_foreach_instr_in_func (instr, func) {
         pco_foreach_instr_src_ssa (psrc, instr) {
            if (pco_ref_get_chans(*psrc) > 1)
               continue;

            ++uses[psrc->val];
         }
      }

      for (unsigned u = 0; u < num_ssas; ++u)
         ra_set_node_spill_cost(ra_graph, u, (float)uses[u]);

      unsigned spill_index = ra_get_best_spill_node(ra_graph);
      if (spill_index == ~0) {
         fprintf(stderr, "FATAL: Failed to get best spill node.\n");
         abort();
      }

      spill(spill_index, func, ctx);

      ralloc_free(ra_regs);
      return false;
   }

   if (pco_should_print_shader(func->parent_shader) && PCO_DEBUG_PRINT(RA)) {
      printf("RA live ranges:\n");
      for (unsigned u = 0; u < num_vars; ++u)
         printf("  %c%u: %u, %u\n",
                u >= num_ssas ? '$' : '%',
                u >= num_ssas ? u - num_ssas : u,
                live_ranges[u].start,
                live_ranges[u].end);

      if (_mesa_hash_table_u64_num_entries(overrides)) {
         printf("RA overrides:\n");
         hash_table_u64_foreach (overrides, entry) {
            struct vec_override *override = entry.data;
            printf("  %%%" PRIu64 ": ref = ", entry.key);
            pco_print_ref(func->parent_shader, override->ref);
            printf(", offset = %u\n", override->offset);
         }
         printf("\n");
      }
   }

   /* Replace vars with allocated registers. */
   unsigned temps = 0;
   unsigned vtxins = 0;
   unsigned interns = 0;
   pco_foreach_instr_in_func_safe (instr, func) {
#if 1
      if (pco_should_print_shader(func->parent_shader) && PCO_DEBUG_PRINT(RA))
         pco_print_shader(func->parent_shader, stdout, "ra debug");
#endif

      /* Insert movs for scalar components of super vecs. */
      if (instr->op == PCO_OP_VEC) {
         pco_builder b =
            pco_builder_create(func, pco_cursor_before_instr(instr));

         struct vec_override *override =
            _mesa_hash_table_u64_search(overrides, instr->dest[0].val);

         unsigned offset = override ? override->offset : 0;

         unsigned temp_dest_base =
            override ? ra_get_node_reg(ra_graph, override->ref.val)
                     : ra_get_node_reg(ra_graph, instr->dest[0].val);

         struct util_dynarray copies = UTIL_DYNARRAY_INIT;

         unsigned highest_temp = 0;
         unsigned lowest_temp = ~0;

         enum pco_exec_cnd exec_cnd = pco_instr_get_exec_cnd(instr);
         pco_foreach_instr_src (psrc, instr) {
            if (!pco_ref_is_ssa(*psrc) ||
                !_mesa_hash_table_u64_search(overrides, psrc->val) ||
                BITSET_TEST(comps, psrc->val)) {
               unsigned chans = pco_ref_get_chans(*psrc);

               unsigned temp_src_base = ~0U;
               if (pco_ref_is_ssa(*psrc)) {
                  temp_src_base = ra_get_node_reg(ra_graph, psrc->val);

                  struct vec_override *src_override =
                     _mesa_hash_table_u64_search(overrides, psrc->val);
                  if (src_override) {
                     temp_src_base =
                        ra_get_node_reg(ra_graph, src_override->ref.val);
                     temp_src_base += src_override->offset;
                  }
               } else if (pco_ref_is_vreg(*psrc)) {
                  temp_src_base =
                     ra_get_node_reg(ra_graph, psrc->val + num_ssas);
               }

               for (unsigned u = 0; u < chans; ++u) {
                  pco_ref dest =
                     pco_ref_hwreg(temp_dest_base + offset, PCO_REG_CLASS_TEMP);
                  dest = pco_ref_offset(dest, u);
                  dest = pco_ref_offset(dest, ctx->temp_alloc_offset);

                  pco_ref src;
                  if (pco_ref_is_ssa(*psrc) || pco_ref_is_vreg(*psrc))
                     src = pco_ref_hwreg(temp_src_base, PCO_REG_CLASS_TEMP);
                  else
                     src = pco_ref_chans(*psrc, 1);

                  src = pco_ref_offset(src, u);
                  src = pco_ref_offset(src, ctx->temp_alloc_offset);

                  pco_ref_xfer_mods(&src, psrc, false);

                  /* if (!pco_refs_are_equal(src, dest, true)) */ {
                     highest_temp =
                        MAX3(highest_temp,
                             pco_ref_is_temp(src) ? pco_ref_get_temp(src)
                                                  : highest_temp,
                             pco_ref_is_temp(dest) ? pco_ref_get_temp(dest)
                                                   : highest_temp);

                     lowest_temp =
                        MIN3(lowest_temp,
                             pco_ref_is_temp(src) ? pco_ref_get_temp(src)
                                                  : lowest_temp,
                             pco_ref_is_temp(dest) ? pco_ref_get_temp(dest)
                                                   : lowest_temp);
                     pco_copy copy = {
                        .src = src,
                        .dest = dest,
                        .s1 = pco_ref_is_reg(src) &&
                              pco_ref_get_reg_class(src) == PCO_REG_CLASS_SPEC,
                     };

                     /*
                     if (pco_ref_is_reg(src) &&
                         pco_ref_get_reg_class(src) == PCO_REG_CLASS_SPEC) {
                        pco_movs1(&b, dest, src, .exec_cnd = exec_cnd);
                     } else {
                        pco_mbyp(&b, dest, src, .exec_cnd = exec_cnd);
                     }
                     */

                     util_dynarray_append(&copies, copy);
                  }
               }

               temps = MAX2(temps, temp_dest_base + offset + chans);
            }

            offset += pco_ref_get_chans(*psrc);
         }

         /* Emit copies. */
         emit_copies(&b, &copies, exec_cnd, highest_temp, lowest_temp);

         util_dynarray_fini(&copies);

         pco_instr_delete(instr);
         continue;
      } else if (instr->op == PCO_OP_COMP) {
         pco_instr_delete(instr);
         continue;
      }

      pco_foreach_instr_dest_ssa (pdest, instr) {
         struct vec_override *override =
            _mesa_hash_table_u64_search(overrides, pdest->val);

         unsigned val = ra_get_node_reg(ra_graph, pdest->val);
         unsigned dest_temps = val + pco_ref_get_chans(*pdest);
         if (override) {
            val = ra_get_node_reg(ra_graph, override->ref.val);
            dest_temps = val + pco_ref_get_chans(override->ref);
            val += override->offset;
         }

         pdest->type = PCO_REF_TYPE_REG;
         pdest->reg_class = PCO_REG_CLASS_TEMP;
         pdest->val = val + ctx->temp_alloc_offset;
         temps = MAX2(temps, dest_temps + ctx->temp_alloc_offset);
      }

      pco_foreach_instr_src_ssa (psrc, instr) {
         struct vec_override *override =
            _mesa_hash_table_u64_search(overrides, psrc->val);

         unsigned val =
            override
               ? ra_get_node_reg(ra_graph, override->ref.val) + override->offset
               : ra_get_node_reg(ra_graph, psrc->val);

         psrc->type = PCO_REF_TYPE_REG;
         psrc->reg_class = PCO_REG_CLASS_TEMP;
         psrc->val = val + ctx->temp_alloc_offset;
      }

      pco_foreach_instr_dest_vreg (pdest, instr) {
         unsigned val = ra_get_node_reg(ra_graph, pdest->val + num_ssas);
         unsigned dest_temps = val + 1;

         pdest->type = PCO_REF_TYPE_REG;
         pdest->reg_class = PCO_REG_CLASS_TEMP;
         pdest->val = val + ctx->temp_alloc_offset;
         temps = MAX2(temps, dest_temps);
      }

      pco_foreach_instr_src_vreg (psrc, instr) {
         unsigned val = ra_get_node_reg(ra_graph, psrc->val + num_ssas);

         psrc->type = PCO_REF_TYPE_REG;
         psrc->reg_class = PCO_REG_CLASS_TEMP;
         psrc->val = val + ctx->temp_alloc_offset;
      }

      /* Drop no-ops. */
      if (instr->op == PCO_OP_MBYP &&
          (pco_ref_is_ssa(instr->src[0]) || pco_ref_is_vreg(instr->src[0])) &&
          pco_refs_are_equal(instr->src[0], instr->dest[0], true)) {
         pco_instr_delete(instr);
      }
   }

   ralloc_free(ra_regs);

   func->temps = temps;

   if (pco_should_print_shader(func->parent_shader) && PCO_DEBUG_PRINT(RA)) {
      printf(
         "RA allocated %u temps, %u vtxins, %u interns from %u SSA vars, %u vregs.\n",
         temps,
         vtxins,
         interns,
         num_ssas,
         num_vregs);
   }

   ctx->done = true;
   return true;
}

/**
 * \brief Register allocation pass.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_ra(pco_shader *shader)
{
   assert(!shader->is_grouped);

   /* Instruction indices need to be ordered for live ranges. */
   pco_index(shader, false);

   unsigned hw_temps = rogue_get_temps(shader->ctx->dev_info);
   /* TODO:
    * unsigned opt_temps = rogue_get_optimal_temps(shader->ctx->dev_info);
    */

   /* TODO: different number of temps available if preamble/phase change. */
   /* TODO: different number of temps available if barriers are in use. */
   /* TODO: support for internal and vtxin registers. */
   pco_ra_ctx ctx = {
      .allocable_temps = hw_temps,
      .allocable_vtxins = 0,
      .allocable_interns = 0,
   };

   if (shader->stage == MESA_SHADER_COMPUTE) {
      unsigned wg_size = shader->data.cs.workgroup_size[0] *
                         shader->data.cs.workgroup_size[1] *
                         shader->data.cs.workgroup_size[2];
      ctx.allocable_temps =
         rogue_max_wg_temps(shader->ctx->dev_info,
                            ctx.allocable_temps,
                            wg_size,
                            shader->data.common.uses.barriers);
   }

   /* Perform register allocation for each function. */
   bool progress = false;
   pco_foreach_func_in_shader (func, shader) {
      ctx.done = false;
      while (!ctx.done)
         progress |= pco_ra_func(func, &ctx);

      shader->data.common.temps = MAX2(shader->data.common.temps, func->temps);
   }

   shader->data.common.spilled_temps = ctx.spilled_temps;
   return progress;
}
