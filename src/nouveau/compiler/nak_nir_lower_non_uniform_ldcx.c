/*
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"
#include "nir_vla.h"

static void
lower_ldcx_to_global(nir_builder *b, nir_intrinsic_instr *load)
{
   assert(load->intrinsic == nir_intrinsic_ldcx_nv);

   b->cursor = nir_before_instr(&load->instr);

   nir_def *handle = load->src[0].ssa;
   assert(handle->bit_size == 64);
   nir_def *offset = load->src[1].ssa;
   assert(offset->bit_size == 32);

   /* Base address shifted by 4 is the bottom 45 bits */
   nir_def *addr_4_lo = nir_unpack_64_2x32_split_x(b, handle);
   nir_def *addr_4_hi = nir_iand_imm(b, nir_unpack_64_2x32_split_y(b, handle),
                                        BITFIELD_MASK(13));
   nir_def *addr_4 = nir_pack_64_2x32_split(b, addr_4_lo, addr_4_hi);
   nir_def *addr =  nir_ishl_imm(b, addr_4, 4);

   /* Size shifted by 2 is the top 19 bits */
   nir_def *size = nir_unpack_64_2x32_split_y(b, handle);
   size = nir_ishl_imm(b, nir_ushr_imm(b, size, 13), 4);

   /* At this point we can assume the offset is aligned so we only need a
    * simple less-than check here.
    */
   nir_def *zero = nir_imm_zero(b, load->def.num_components,
                                   load->def.bit_size);
   nir_def *val;
   nir_push_if(b, nir_ilt(b, offset, size));
   {
      val = nir_build_load_global_constant(b,
         load->def.num_components, load->def.bit_size,
         nir_iadd(b, addr, nir_u2u64(b, offset)),
         .align_mul = nir_intrinsic_align_mul(load),
         .align_offset = nir_intrinsic_align_offset(load));
   }
   nir_pop_if(b, NULL);
   val = nir_if_phi(b, val, zero);

   nir_def_replace(&load->def, val);
}

struct non_uniform_section {
   nir_block *pred;
   nir_block *succ;
   struct hash_table *live_handles;
};

static void
non_uniform_section_init(struct non_uniform_section *nus,
                         nir_block *pred, nir_block *succ)
{
   *nus = (struct non_uniform_section) {
      .pred = pred,
      .succ = succ,
      .live_handles = _mesa_pointer_hash_table_create(NULL),
   };
}

static void
non_uniform_section_finish(struct non_uniform_section *nus)
{
   _mesa_hash_table_destroy(nus->live_handles, NULL);
}

static void
add_live_handle(nir_def *handle, struct non_uniform_section *nus)
{
   uint32_t hash = _mesa_hash_pointer(handle);
   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(nus->live_handles, hash, handle);
   if (entry == NULL) {
      _mesa_hash_table_insert_pre_hashed(nus->live_handles, hash, handle,
                                         (void *)(uintptr_t)1);
   } else {
      entry->data = (void *)(((uintptr_t)entry->data) + 1);
   }
}

static bool
def_needs_hoist(nir_def *def, nir_block *target)
{
   return def->parent_instr->block->index > target->index;
}

static bool
can_hoist_def(nir_def *def, nir_block *target)
{
   if (!def_needs_hoist(def, target))
      return true;

   nir_instr *instr = def->parent_instr;
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      for (uint8_t i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (!can_hoist_def(alu->src[i].src.ssa, target))
            return false;
      }
      return true;
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_ldc_nv &&
          intrin->intrinsic != nir_intrinsic_ldcx_nv)
         return false;

      for (uint8_t i = 0; i < 2; i++) {
         if (!can_hoist_def(intrin->src[i].ssa, target))
            return false;
      }

      return true;
   }

   case nir_instr_type_load_const:
      return true;

   default:
      return false;
   }
}

static bool
hoist_def(nir_def *def, nir_block *target)
{
   if (!def_needs_hoist(def, target))
      return false;

   nir_instr *instr = def->parent_instr;
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      for (uint8_t i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
         hoist_def(alu->src[i].src.ssa, target);
      break;
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_ldc_nv &&
          intrin->intrinsic != nir_intrinsic_ldcx_nv)
         break;

      for (uint8_t i = 0; i < 2; i++)
         hoist_def(intrin->src[i].ssa, target);
      break;
   }

   case nir_instr_type_load_const:
      break;

   default:
      unreachable("Cannot hoist instruction");
   }

   nir_instr_remove(instr);
   nir_instr_insert(nir_after_block(target), instr);

   return true;
}

static bool
try_hoist_ldcx_handles_block(nir_block *block, struct non_uniform_section *nus)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu: {
         /* ALU instructions may directly consume cbuf values so mark
          * any ldcx_nv sources as also used.
          */
         nir_alu_instr *alu = nir_instr_as_alu(instr);
         for (uint8_t i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
            nir_instr *src_instr = alu->src[i].src.ssa->parent_instr;
            if (src_instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *src_intrin =
               nir_instr_as_intrinsic(src_instr);
            if (src_intrin->intrinsic != nir_intrinsic_ldcx_nv)
               continue;

            if (nir_intrinsic_access(src_intrin) & ACCESS_NON_UNIFORM)
               continue;

            /* If it really is a uniform handle, it should have been
             * hoistd by now.
             */
            nir_def *handle = src_intrin->src[0].ssa;
            if (def_needs_hoist(handle, nus->pred))
               continue;

            add_live_handle(handle, nus);
         }
         break;
      }

      case nir_instr_type_intrinsic: {
         nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
         if (load->intrinsic != nir_intrinsic_ldcx_nv)
            break;

         if (nir_intrinsic_access(load) & ACCESS_NON_UNIFORM)
            break;

         nir_def *handle = load->src[0].ssa;
         if (can_hoist_def(handle, nus->pred)) {
            progress |= hoist_def(handle, nus->pred);
            add_live_handle(handle, nus);
         }
         break;
      }

      default:
         break;
      }
   }

   return progress;
}

struct handle {
   nir_def *handle;
   uint64_t sort_key;
};

static int
cmp_handles(const void *_a, const void *_b)
{
   const struct handle *a = _a, *b = _b;

   if (a->sort_key < b->sort_key)
      return -1;
   else if (a->sort_key > b->sort_key)
      return 1;
   else
      return 0;
}

static bool
sort_and_mark_live_handles(nir_builder *b, struct non_uniform_section *nus)
{
   /* There are 63 uniform registers and each handle takes 2 */
   static const unsigned max_live_handles = 31;

   const unsigned num_handles = nus->live_handles->entries;
   if (num_handles == 0)
      return false;

   NIR_VLA(struct handle, handles, num_handles);

   unsigned i = 0;
   hash_table_foreach(nus->live_handles, entry) {
      nir_def *handle = (nir_def *)entry->key;
      uint64_t uses = (uintptr_t)entry->data;

      handles[i++] = (struct handle) {
         .handle = handle,
         .sort_key = (uses << 32) | handle->index,
      };
   }
   _mesa_hash_table_clear(nus->live_handles, NULL);

   qsort(handles, num_handles, sizeof(*handles), cmp_handles);

   b->cursor = nir_after_block(nus->pred);
   for (unsigned i = 0; i < num_handles && i < max_live_handles; i++) {
      nir_def *handle = handles[i].handle;
      if (handle->divergent)
         handle = nir_as_uniform(b, handles[i].handle);
      nir_pin_cx_handle_nv(b, handle);

      _mesa_hash_table_insert(nus->live_handles, handle, handle);
      _mesa_hash_table_insert(nus->live_handles, handles[i].handle, handle);
   }

   b->cursor = nir_before_block_after_phis(nus->succ);
   for (unsigned i = 0; i < num_handles && i < max_live_handles; i++)
      nir_unpin_cx_handle_nv(b, handles[i].handle);

   return true;
}

static bool
try_remat_ldcx_alu_use(nir_builder *b, nir_alu_instr *alu, uint8_t src_idx,
                       struct non_uniform_section *nus)
{
   nir_instr *src_instr = alu->src[src_idx].src.ssa->parent_instr;
   if (src_instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *load = nir_instr_as_intrinsic(src_instr);
   if (load->intrinsic != nir_intrinsic_ldcx_nv)
      return false;

   /* If it's already in the same block, there's nothing to do */
   if (load->instr.block == alu->instr.block)
      return false;

   nir_def *handle = load->src[0].ssa;

   /* Only constant offset can be used directly by ALU ops */
   if (!nir_src_is_const(load->src[1]))
      return false;

   struct hash_entry *entry =
      _mesa_hash_table_search(nus->live_handles, handle);
   if (entry == NULL)
      return false;

   nir_intrinsic_instr *new_load =
      nir_instr_as_intrinsic(nir_instr_clone(b->shader, &load->instr));
   nir_instr_insert(nir_before_instr(&alu->instr), &new_load->instr);

   nir_foreach_use_safe(use, &load->def) {
      if (nir_src_parent_instr(use)->type == nir_instr_type_alu &&
          nir_src_parent_instr(use)->block == alu->instr.block)
         nir_src_rewrite(use, &new_load->def);
   }

   return true;
}

static bool
lower_ldcx_block(nir_builder *b, nir_block *block,
                 struct non_uniform_section *nus)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu: {
         nir_alu_instr *alu = nir_instr_as_alu(instr);
         if (nus != NULL) {
            /* In non-uniform control-flow, NAK can't copy-prop cbuf values
             * across blocks.  Re-materialize cbufs that are used by ALU
             * instructions in the same block.
             */
            for (uint8_t i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
               progress |= try_remat_ldcx_alu_use(b, alu, i, nus);
         }
         break;
      }
      case nir_instr_type_intrinsic: {
         nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
         if (load->intrinsic != nir_intrinsic_ldcx_nv)
            break;

         nir_def *handle = load->src[0].ssa;
         if (nus == NULL) {
            /* Uniform control-flow */
            if (nir_intrinsic_access(load) & ACCESS_NON_UNIFORM) {
               lower_ldcx_to_global(b, load);
               progress = true;
            } else if (handle->divergent) {
               b->cursor = nir_before_instr(&load->instr);
               nir_def *ugpr = nir_as_uniform(b, handle);
               nir_src_rewrite(&load->src[0], ugpr);
               progress = true;
            }
         } else {
            struct hash_entry *entry =
               _mesa_hash_table_search(nus->live_handles, handle);
            if (entry != NULL) {
               nir_def *ugpr = entry->data;
               assert(!ugpr->divergent);
               nir_src_rewrite(&load->src[0], ugpr);
            } else {
               lower_ldcx_to_global(b, load);
            }
            progress = true;
         }
         break;
      }

      default:
         break;
      }
   }

   return progress;
}

static bool
lower_non_uniform_cf_node(nir_builder *b, nir_cf_node *node,
                          nir_block *pred, nir_block *succ)
{
   bool progress = false;

   struct non_uniform_section nus;
   non_uniform_section_init(&nus, pred, succ);

   /* This also marks any uniform handles it finds as live */
   nir_foreach_block_in_cf_node(block, node)
      progress |= try_hoist_ldcx_handles_block(block, &nus);

   progress |= sort_and_mark_live_handles(b, &nus);

   nir_foreach_block_in_cf_node_safe(block, node)
      progress |= lower_ldcx_block(b, block, &nus);

   non_uniform_section_finish(&nus);

   return progress;
}

static bool
lower_cf_list(nir_builder *b, struct exec_list *cf_list)
{
   bool progress = false;

   nir_block *block = NULL;
   foreach_list_typed_safe(nir_cf_node, node, node, cf_list) {
      switch (node->type) {
      case nir_cf_node_block:
         block = nir_cf_node_as_block(node);
         progress |= lower_ldcx_block(b, block, NULL);
         continue;

      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         if (nir_src_is_divergent(&nif->condition)) {
            nir_block *succ = nir_cf_node_as_block(nir_cf_node_next(node));
            progress |= lower_non_uniform_cf_node(b, node, block, succ);
         } else {
            progress |= lower_cf_list(b, &nif->then_list);
            progress |= lower_cf_list(b, &nif->else_list);
         }
         break;
      }

      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);
         if (nir_loop_is_divergent(loop)) {
            nir_block *succ = nir_cf_node_as_block(nir_cf_node_next(node));
            progress |= lower_non_uniform_cf_node(b, node, block, succ);
         } else {
            progress |= lower_cf_list(b, &loop->body);
            progress |= lower_cf_list(b, &loop->continue_list);
         }
         break;
      }

      default:
         unreachable("Unknown CF node type");
      }
   }

   return progress;
}

bool
nak_nir_lower_non_uniform_ldcx(nir_shader *nir)
{
   /* Real functions are going to make hash of this */
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_create(impl);

   /* We use block indices to determine when something is a predecessor */
   nir_metadata_require(impl, nir_metadata_block_index);

   if (lower_cf_list(&b, &impl->body)) {
      nir_metadata_preserve(impl, nir_metadata_none);
      return true;
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
      return false;
   }
}
