/*
 * Copyright © 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nak_private.h"
#include "nir_builder.h"
#include "util/hash_table.h"

struct remat_ctx {
   struct hash_table *remap;
   nir_block *block;
   nir_builder b;
};

static bool
rematerialize_load_const(nir_src *src, void *_ctx)
{
   struct remat_ctx *ctx = _ctx;

   if (!nir_src_is_const(*src))
      return true;

   struct hash_entry *entry = _mesa_hash_table_search(ctx->remap, src->ssa);
   if (entry != NULL) {
      nir_src_rewrite(src, entry->data);
      return true;
   }

   nir_load_const_instr *old_lc =
      nir_def_as_load_const(src->ssa);

   nir_load_const_instr *new_lc =
      nir_instr_as_load_const(nir_instr_clone(ctx->b.shader, &old_lc->instr));
   nir_builder_instr_insert(&ctx->b, &new_lc->instr);

   _mesa_hash_table_insert(ctx->remap, &old_lc->def, &new_lc->def);
   nir_src_rewrite(src, &new_lc->def);

   return true;
}

static bool
rematerialize_load_const_impl(nir_function_impl *impl)
{
   bool progress = false;

   struct remat_ctx ctx = {
      .remap = _mesa_pointer_hash_table_create(NULL),
      .b = nir_builder_create(impl),
   };

   nir_foreach_block(block, impl) {
      _mesa_hash_table_clear(ctx.remap, NULL);
      ctx.block = block;

      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_phi)
            continue;

         ctx.b.cursor = nir_before_instr(instr);
         nir_foreach_src(instr, rematerialize_load_const, &ctx);
      }

      ctx.b.cursor = nir_after_block_before_jump(block);

      for (unsigned i = 0; i < ARRAY_SIZE(block->successors); i++) {
         nir_block *succ = block->successors[i];
         if (succ == NULL)
            continue;

         nir_foreach_instr(instr, succ) {
            if (instr->type != nir_instr_type_phi)
               break;

            nir_phi_instr *phi = nir_instr_as_phi(instr);
            nir_foreach_phi_src(src, phi) {
               if (src->pred != block)
                  continue;

               rematerialize_load_const(&src->src, &ctx);
            }
         }
      }
   }

   _mesa_hash_table_destroy(ctx.remap, NULL);

   return nir_progress(progress, impl, nir_metadata_control_flow |
                                       nir_metadata_divergence);
}

bool
nak_nir_rematerialize_load_const(nir_shader *nir)
{
   bool progress = false;

   nir_foreach_function_impl(impl, nir)
      progress |= rematerialize_load_const_impl(impl);

   return progress;
}
