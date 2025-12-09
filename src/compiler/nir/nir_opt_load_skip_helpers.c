/*
 * Copyright © 2025 Collabora, Ltd.
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

#include "nir.h"
#include "nir_worklist.h"

static bool
instr_never_needs_helpers(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic == nir_intrinsic_store_scratch)
      return false;

   if (nir_intrinsic_has_access(intr) && (nir_intrinsic_access(intr) & ACCESS_INCLUDE_HELPERS))
      return false;

   bool is_store = !nir_intrinsic_infos[intr->intrinsic].has_dest;
   bool is_atomic = nir_intrinsic_has_atomic_op(intr);

   /* Stores and atomics must already disable helper lanes. */
   return is_store || is_atomic;
}

struct helper_state {
   BITSET_WORD *needs_helpers;
   nir_instr_worklist worklist;
   nir_instr_worklist load_instrs;
   nir_opt_load_skip_helpers_options *options;
};

static inline bool
def_needs_helpers(nir_def *def, void *_data)
{
   struct helper_state *hs = _data;
   return BITSET_TEST(hs->needs_helpers, def->index);
}

static inline bool
set_src_needs_helpers(nir_src *src, void *_data)
{
   struct helper_state *hs = _data;
   if (!BITSET_TEST(hs->needs_helpers, src->ssa->index) &&
       !instr_never_needs_helpers(nir_def_instr(src->ssa))) {
      BITSET_SET(hs->needs_helpers, src->ssa->index);
      nir_instr_worklist_push_tail(&hs->worklist, nir_def_instr(src->ssa));
   }
   return true;
}

static inline bool
add_load_to_worklist(struct helper_state *hs, nir_instr *instr)
{
   /* If a load is uniform, we don't want to set skip_helpers because
    * then it might not be uniform if the helpers don't fetch.  Also,
    * for uniform load results, we shouldn't be burning any more
    * memory by executing the helper pixels unless the hardware is
    * really dumb.
    */
   if (hs->options->no_add_divergence && !nir_instr_def(instr)->divergent)
      return false;

   nir_instr_worklist_push_tail(&hs->load_instrs, instr);

   return true;
}

bool
nir_opt_load_skip_helpers(nir_shader *shader, nir_opt_load_skip_helpers_options *options)
{
   /* This is only useful on fragment shaders */
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   /* This only works if functions are inlined */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   struct helper_state hs = {
      .needs_helpers = BITSET_RZALLOC(NULL, impl->ssa_alloc),
      .options = options,
   };
   nir_instr_worklist_init(&hs.worklist);
   nir_instr_worklist_init(&hs.load_instrs);

   /* First, add subgroup ops and anything that might cause side effects */
   nir_foreach_block(block, impl) {
      /* Control-flow is hard.  Given that this is only for load ops, we
       * can afford to be conservative and assume that any control-flow is
       * potentially going to affect helpers.
       */
      nir_if *nif = nir_block_get_following_if(block);
      if (nif != NULL)
         set_src_needs_helpers(&nif->condition, &hs);

      nir_foreach_instr(instr, block) {
         switch (instr->type) {
         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);

            /* Stash texture instructions so we don't have to walk the whole
             * shader again just to set the skip_helpers bit.
             */
            add_load_to_worklist(&hs, instr);

            for (uint32_t i = 0; i < tex->num_srcs; i++) {
               switch (tex->src[i].src_type) {
               case nir_tex_src_coord:
               case nir_tex_src_projector:
                  if (nir_tex_instr_has_implicit_derivative(tex))
                     set_src_needs_helpers(&tex->src[i].src, &hs);
                  break;

               case nir_tex_src_texture_deref:
               case nir_tex_src_sampler_deref:
               case nir_tex_src_texture_offset:
               case nir_tex_src_sampler_offset:
               case nir_tex_src_texture_handle:
               case nir_tex_src_sampler_handle:
               case nir_tex_src_sampler_deref_intrinsic:
               case nir_tex_src_texture_deref_intrinsic:
               case nir_tex_src_backend1:
               case nir_tex_src_backend2:
                  /* Anything which affects which descriptor is used by
                   * the texture instruction is considered a possible
                   * side-effect.  If, for instance, the array index or
                   * bindless handle is wrong, that can cause us to use an
                   * invalid descriptor or fault.  This includes back-end
                   * source types because we don't know what they are.
                   */
                  set_src_needs_helpers(&tex->src[i].src, &hs);
                  break;

               default:
                  break;
               }
            }
            break;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (nir_intrinsic_has_semantic(intr, NIR_INTRINSIC_SUBGROUP)) {
               nir_foreach_src(instr, set_src_needs_helpers, &hs);
            } else if (intr->intrinsic == nir_intrinsic_terminate_if) {
               /* Unlike demote, terminate disables invocations completely.
                * For example, a subgroup operation after terminate should
                * include helpers, but not the invocations that were terminated.
                * So the condition must be correct for helpers too.
                */
               set_src_needs_helpers(&intr->src[0], &hs);
            } else if (instr_never_needs_helpers(instr)) {
               continue;
            } else if (hs.options->intrinsic_cb &&
                       hs.options->intrinsic_cb(intr, hs.options->intrinsic_cb_data) &&
                       add_load_to_worklist(&hs, instr)) {
               switch (intr->intrinsic) {
               case nir_intrinsic_load_global_amd:
                  break;
               default: {
                  /* Even if this load is skipped for helpers, the handle must
                   * still be uniform.
                   */
                  nir_src *io_index_src = nir_get_io_index_src(intr);
                  if (io_index_src != NULL)
                     set_src_needs_helpers(io_index_src, &hs);
                  break;
               }
               }

               /* We don't need to set the offset/address sources as needing
                * helpers if this load is skipped for helpers.
                */
            } else {
               /* All I/O addresses need helpers because getting them wrong
                * may cause a fault.
                */
               nir_src *io_index_src = nir_get_io_index_src(intr);
               if (io_index_src != NULL)
                  set_src_needs_helpers(io_index_src, &hs);
               nir_src *io_offset_src = nir_get_io_offset_src(intr);
               if (io_offset_src != NULL)
                  set_src_needs_helpers(io_offset_src, &hs);
            }
            break;
         }

         default:
            break;
         }
      }
   }

   bool progress = false;

   /* We only need to run the worklist if we have loads */
   if (!nir_instr_worklist_is_empty(&hs.load_instrs)) {
      while (!nir_instr_worklist_is_empty(&hs.worklist)) {
         nir_instr *instr = nir_instr_worklist_pop_head(&hs.worklist);
         assert(nir_foreach_def(instr, def_needs_helpers, &hs));
         nir_foreach_src(instr, set_src_needs_helpers, &hs);
      }

      while (!nir_instr_worklist_is_empty(&hs.load_instrs)) {
         nir_instr *instr = nir_instr_worklist_pop_head(&hs.load_instrs);
         nir_def *def = nir_instr_def(instr);

         if (!def_needs_helpers(def, &hs)) {
            if (instr->type == nir_instr_type_tex) {
               nir_tex_instr *tex = nir_instr_as_tex(instr);
               progress |= !tex->skip_helpers;
               tex->skip_helpers = true;
            } else if (instr->type == nir_instr_type_intrinsic) {
               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
               enum gl_access_qualifier access = nir_intrinsic_access(intr);
               progress |= !(access & ACCESS_SKIP_HELPERS);
               nir_intrinsic_set_access(intr, access | ACCESS_SKIP_HELPERS);
            }
         }
      }
   }

   nir_instr_worklist_fini(&hs.load_instrs);
   nir_instr_worklist_fini(&hs.worklist);
   ralloc_free(hs.needs_helpers);

   return nir_progress(progress, impl, nir_metadata_all);
}
