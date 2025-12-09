/*
 * Copyright © 2020 Intel Corporation
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

#include "util/u_vector.h"
#include "nir.h"
#include "nir_worklist.h"

static bool
combine_all_barriers(nir_intrinsic_instr *a, nir_intrinsic_instr *b, void *_)
{
   nir_intrinsic_set_memory_modes(
      a, nir_intrinsic_memory_modes(a) | nir_intrinsic_memory_modes(b));
   nir_intrinsic_set_memory_semantics(
      a, nir_intrinsic_memory_semantics(a) | nir_intrinsic_memory_semantics(b));
   nir_intrinsic_set_memory_scope(
      a, MAX2(nir_intrinsic_memory_scope(a), nir_intrinsic_memory_scope(b)));
   nir_intrinsic_set_execution_scope(
      a, MAX2(nir_intrinsic_execution_scope(a), nir_intrinsic_execution_scope(b)));
   return true;
}

static bool
nir_opt_combine_barriers_impl(nir_function_impl *impl,
                              nir_combine_barrier_cb combine_cb,
                              void *data)
{
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_intrinsic_instr *prev = NULL;

      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic) {
            prev = NULL;
            continue;
         }

         nir_intrinsic_instr *current = nir_instr_as_intrinsic(instr);
         if (current->intrinsic != nir_intrinsic_barrier) {
            prev = NULL;
            continue;
         }

         if (prev && combine_cb(prev, current, data)) {
            nir_instr_remove(&current->instr);
            progress = true;
         } else {
            prev = current;
         }
      }
   }

   return nir_progress(progress, impl,
                       nir_metadata_control_flow | nir_metadata_live_defs);
}

/* Combine adjacent scoped barriers. */
bool
nir_opt_combine_barriers(nir_shader *shader,
                         nir_combine_barrier_cb combine_cb,
                         void *data)
{
   /* Default to combining everything. Only some backends can do better. */
   if (!combine_cb)
      combine_cb = combine_all_barriers;

   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      if (nir_opt_combine_barriers_impl(impl, combine_cb, data)) {
         progress = true;
      }
   }

   return progress;
}

/** If \p instr is a nir_intrinsic_barrier, returns it, else NULL. */
static nir_intrinsic_instr *
instr_as_barrier(nir_instr *instr)
{
   if (instr && instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      return intrin->intrinsic == nir_intrinsic_barrier ? intrin : NULL;
   }
   return NULL;
}

/**
 * Return true if \p atomic is surrounded by a pattern:
 *
 *    1. Release barrier
 *    2. Atomic operation
 *    3. Acquire barrier
 *
 * where all three have the same mode, both barriers have the same scope,
 * and that scope is \p max_scope or narrower.
 *
 * For simplicity, we require the barriers to have exactly the one mode
 * used by the atomic, so that we don't have to compare many barriers for
 * other side effects they may have.  nir_opt_barrier_modes() can be used
 * to help reduce unnecessary barrier modes.
 */
static bool
is_acquire_release_atomic(nir_intrinsic_instr *atomic, mesa_scope max_scope)
{
   assert(atomic->intrinsic == nir_intrinsic_deref_atomic ||
          atomic->intrinsic == nir_intrinsic_deref_atomic_swap);

   nir_deref_instr *atomic_deref = nir_src_as_deref(atomic->src[0]);

   nir_intrinsic_instr *prev =
      instr_as_barrier(nir_instr_prev(&atomic->instr));
   nir_intrinsic_instr *next =
      instr_as_barrier(nir_instr_next(&atomic->instr));

   if (!prev || !next)
      return false;

   return nir_intrinsic_memory_semantics(prev) == NIR_MEMORY_RELEASE &&
          nir_intrinsic_memory_semantics(next) == NIR_MEMORY_ACQUIRE &&
          nir_intrinsic_memory_modes(prev) == atomic_deref->modes &&
          nir_intrinsic_memory_modes(next) == atomic_deref->modes &&
          nir_intrinsic_memory_scope(prev) <= max_scope &&
          nir_intrinsic_memory_scope(prev) == nir_intrinsic_memory_scope(next);
}

/**
 * Remove redundant barriers between sequences of atomics.
 *
 * Some shaders contain back-to-back atomic accesses in SPIR-V with
 * AcquireRelease semantics.  In NIR, we translate these to a release
 * memory barrier, the atomic, then an acquire memory barrier.
 *
 * This results in a lot of unnecessary memory barriers in the
 * middle of the sequence of atomics:
 *
 *    1a. Release memory barrier
 *    1b. Atomic
 *    1c. Acquire memory barrier
 *    ...
 *    2a. Release memory barrier
 *    2b. Atomic
 *    2c. Acquire memory barrier
 *    ...
 *
 * We pattern match for <release, atomic, acquire> instruction triplets,
 * and when we find back-to-back occurrences of that pattern, we eliminate
 * the barriers in-between the atomics (1c and 2a above):
 *
 *    1. Release memory barrier
 *    2. Atomic
 *    ...
 *    m. Atomic
 *    n. Acquire memory barrier
 *
 * Some requirements:
 * - The atomics' destinations must be unused (so their only effect is
 *   to update the associated memory store)
 * - Matched barriers must impact the atomic's memory mode.
 * - All barriers must have have identical scope no wider than \p max_scope
 *   (beyond that, removing synchronization could be observable).
 *
 * And for simplicity:
 * - Barrier modes must be exactly the mode of the atomics (otherwise we'd
 *   have to take care to preserve side-effects for other modes).
 * - Barriers must appear directly before/after the instruction (easier
 *   pattern matching, and it's what we generate for the SPIR-V construct)
 *
 * Other instructions are allowed to be present between the atomics, so
 * long as they don't affect the relevant memory mode.  Loads/stores or
 * atomics not matching this pattern are not allowed (we stop matching).
 * For example, this allows calculating the value to be used as the next
 * atomic's operand to appear in-between the two.
 */
static bool
nir_opt_acquire_release_barriers_impl(nir_function_impl *impl,
                                      mesa_scope max_scope)
{
   bool progress = false;
   nir_intrinsic_instr *last_atomic = NULL;

   nir_foreach_block(block, impl) {
      last_atomic = NULL;

      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_load_deref:
         case nir_intrinsic_load_deref_block_intel:
         case nir_intrinsic_store_deref:
         case nir_intrinsic_store_deref_block_intel:
            if (last_atomic) {
               /* If there is a load/store of the same mode as our matched
                * atomic, then abandon our pattern match.
                */
               nir_deref_instr *ref = nir_src_as_deref(intrin->src[0]);
               nir_deref_instr *lastdr = nir_src_as_deref(last_atomic->src[0]);
               if (nir_deref_mode_may_be(ref, lastdr->modes))
                  last_atomic = NULL;
            }
            break;

         case nir_intrinsic_deref_atomic:
         case nir_intrinsic_deref_atomic_swap:
            if (nir_def_is_unused(&intrin->def) &&
                is_acquire_release_atomic(intrin, max_scope)) {

               if (!last_atomic) {
                  last_atomic = intrin;
               } else {
                  nir_intrinsic_instr *last_acquire =
                     nir_instr_as_intrinsic(nir_instr_next(&last_atomic->instr));
                  nir_intrinsic_instr *this_release =
                     nir_instr_as_intrinsic(nir_instr_prev(&intrin->instr));
                  assert(last_acquire->intrinsic == nir_intrinsic_barrier);
                  assert(this_release->intrinsic == nir_intrinsic_barrier);

                  /* Verify that this atomic's barrier modes/scopes match
                   * the last atomic's modes/scope.  (Note that we already
                   * verified that each atomic's pair of barriers match
                   * each other, so we can compare against either here.)
                   */
                  if (nir_intrinsic_memory_modes(last_acquire) ==
                      nir_intrinsic_memory_modes(this_release) &&
                      nir_intrinsic_memory_scope(last_acquire) ==
                      nir_intrinsic_memory_scope(this_release)) {
                     progress = true;
                     nir_instr_remove(&last_acquire->instr);
                     nir_instr_remove(&this_release->instr);
                  }

                  /* Regardless of progress, continue matching from here */
                  last_atomic = intrin;
               }
            } else {
               /* Abandon our pattern match, this is another kind of access */
               last_atomic = NULL;
            }
            break;

         default:
            /* Ignore instructions that don't affect this kind of memory */
            break;
         }
      }
   }

   nir_progress(progress, impl, nir_metadata_control_flow |
                                nir_metadata_live_defs);

   return progress;
}

bool
nir_opt_acquire_release_barriers(nir_shader *shader, mesa_scope max_scope)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      progress |= nir_opt_acquire_release_barriers_impl(impl, max_scope);
   }

   return progress;
}

static bool
barrier_happens_before(const nir_instr *a, const nir_instr *b)
{
   if (a->block == b->block)
      return a->index < b->index;

   return nir_block_dominates(a->block, b->block);
}

static bool
nir_opt_barrier_modes_impl(nir_function_impl *impl)
{
   bool progress = false;

   nir_instr_worklist barriers;
   if (!nir_instr_worklist_init(&barriers))
      return false;

   struct u_vector mem_derefs;
   if (!u_vector_init(&mem_derefs, 32, sizeof(struct nir_instr *))) {
      nir_instr_worklist_fini(&barriers);
      return false;
   }

   const unsigned all_memory_modes = nir_var_image |
                                     nir_var_mem_ssbo |
                                     nir_var_mem_shared |
                                     nir_var_mem_global;

   nir_foreach_block_safe(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            if (intrin->intrinsic == nir_intrinsic_barrier)
               nir_instr_worklist_push_tail(&barriers, instr);

         } else if (instr->type == nir_instr_type_deref) {
            nir_deref_instr *deref = nir_instr_as_deref(instr);

            if (nir_deref_mode_may_be(deref, all_memory_modes) ||
                glsl_contains_atomic(deref->type)) {
               nir_deref_instr **tail = u_vector_add(&mem_derefs);
               *tail = deref;
            }
         }
      }
   }

   nir_foreach_instr_in_worklist(instr, &barriers) {
      nir_intrinsic_instr *barrier = nir_instr_as_intrinsic(instr);

      const unsigned barrier_modes = nir_intrinsic_memory_modes(barrier);
      unsigned new_modes = barrier_modes & ~all_memory_modes;

      /* If a barrier dominates all memory accesses for a particular mode (or
       * there are none), then the barrier cannot affect those accesses.  We
       * can drop that mode from the barrier.
       *
       * For each barrier, we look at the list of memory derefs, and see if
       * the barrier fails to dominate the deref.  If so, then there's at
       * least one memory access that may happen before the barrier, so we
       * need to keep the mode.  Any modes not kept are discarded.
       */
      nir_deref_instr **p_deref;
      u_vector_foreach(p_deref, &mem_derefs)
      {
         nir_deref_instr *deref = *p_deref;
         const unsigned atomic_mode =
            glsl_contains_atomic(deref->type) ? nir_var_mem_ssbo : 0;
         const unsigned deref_modes =
            (deref->modes | atomic_mode) & barrier_modes;

         if (deref_modes &&
             !barrier_happens_before(&barrier->instr, &deref->instr))
            new_modes |= deref_modes;
      }

      /* If we don't need all the modes, update the barrier. */
      if (barrier_modes != new_modes) {
         nir_intrinsic_set_memory_modes(barrier, new_modes);
         progress = true;
      }

      /* Shared memory only exists within a workgroup, so synchronizing it
       * beyond workgroup scope is nonsense.
       */
      if (nir_intrinsic_execution_scope(barrier) == SCOPE_NONE &&
          new_modes == nir_var_mem_shared) {
         nir_intrinsic_set_memory_scope(barrier,
                                        MIN2(nir_intrinsic_memory_scope(barrier), SCOPE_WORKGROUP));
         progress = true;
      }
   }

   nir_instr_worklist_fini(&barriers);
   u_vector_finish(&mem_derefs);

   return progress;
}

/**
 * Reduce barriers to remove unnecessary modes and scope.
 *
 * This pass must be called before nir_lower_explicit_io lowers derefs!
 *
 * Many shaders issue full memory barriers, which may need to synchronize
 * access to images, SSBOs, shared local memory, or global memory.  However,
 * many of them only use a subset of those memory types - say, only SSBOs.
 *
 * Shaders may also have patterns such as:
 *
 *    1. shared local memory access
 *    2. barrier with full variable modes
 *    3. more shared local memory access
 *    4. image access
 *
 * In this case, the barrier is needed to ensure synchronization between the
 * various shared memory operations.  Image reads and writes do also exist,
 * but they are all on one side of the barrier, so it is a no-op for image
 * access.  We can drop the image mode from the barrier in this case too.
 *
 * In addition, we can reduce the memory scope of shared-only barriers, as
 * shared local memory only exists within a workgroup.
 */
bool
nir_opt_barrier_modes(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_dominance |
                                    nir_metadata_instr_index);

      bool impl_progress = nir_opt_barrier_modes_impl(impl);
      progress |= nir_progress(impl_progress, impl,
                               nir_metadata_control_flow | nir_metadata_live_defs);
   }

   return progress;
}
