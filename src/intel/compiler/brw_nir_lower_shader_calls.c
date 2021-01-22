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

#include "brw_nir_rt.h"
#include "brw_nir_rt_builder.h"
#include "nir_phi_builder.h"

/** Insert the appropriate return instruction at the end of the shader */
void
brw_nir_lower_shader_returns(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   /* Reserve scratch space at the start of the shader's per-thread scratch
    * space for the return BINDLESS_SHADER_RECORD address and data payload.
    * When a shader is called, the calling shader will write the return BSR
    * address in this region of the callee's scratch space.
    *
    * We could also put it at the end of the caller's scratch space.  However,
    * doing this way means that a shader never accesses its caller's scratch
    * space unless given an explicit pointer (such as for ray payloads).  It
    * also makes computing the address easier given that we want to apply an
    * alignment to the scratch offset to ensure we can make alignment
    * assumptions in the called shader.
    *
    * This isn't needed for ray-gen shaders because they end the thread and
    * never return to the calling trampoline shader.
    */
   assert(shader->scratch_size == 0);
   if (shader->info.stage != MESA_SHADER_RAYGEN)
      shader->scratch_size = BRW_BTD_STACK_CALLEE_DATA_SIZE;

   nir_builder b;
   nir_builder_init(&b, impl);

   set_foreach(impl->end_block->predecessors, block_entry) {
      struct nir_block *block = (void *)block_entry->key;
      b.cursor = nir_after_block_before_jump(block);

      switch (shader->info.stage) {
      case MESA_SHADER_RAYGEN:
         /* A raygen shader is always the root of the shader call tree.  When
          * it ends, we retire the bindless stack ID and no further shaders
          * will be executed.
          */
         brw_nir_btd_retire(&b);
         break;

      case MESA_SHADER_ANY_HIT:
         /* The default action of an any-hit shader is to accept the ray
          * intersection.
          */
         nir_accept_ray_intersection(&b);
         break;

      case MESA_SHADER_CALLABLE:
      case MESA_SHADER_MISS:
      case MESA_SHADER_CLOSEST_HIT:
         /* Callable, miss, and closest-hit shaders don't take any special
          * action at the end.  They simply return back to the previous shader
          * in the call stack.
          */
         brw_nir_btd_return(&b);
         break;

      case MESA_SHADER_INTERSECTION:
         /* This will be handled by brw_nir_lower_intersection_shader */
         break;

      default:
         unreachable("Invalid callable shader stage");
      }

      assert(impl->end_block->predecessors->entries == 1);
      break;
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

static bool
move_system_values_to_top(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   bool progress = false;
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         /* These intrinsics not only can't be re-materialized but aren't
          * preserved when moving to the continuation shader.  We have to move
          * them to the top to ensure they get spilled as needed.
          */
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         switch (intrin->intrinsic) {
         case nir_intrinsic_load_shader_record_ptr:
         case nir_intrinsic_load_btd_local_arg_addr_intel:
            nir_instr_remove(instr);
            nir_instr_insert(nir_before_cf_list(&impl->body), instr);
            progress = true;
            break;

         default:
            break;
         }
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

static bool
instr_is_shader_call(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return intrin->intrinsic == nir_intrinsic_trace_ray ||
          intrin->intrinsic == nir_intrinsic_report_ray_intersection ||
          intrin->intrinsic == nir_intrinsic_execute_callable;
}

struct bitset {
   BITSET_WORD *set;
   unsigned size;
};

static struct bitset
bitset_create(void *mem_ctx, unsigned size)
{
   return (struct bitset) {
      .set = rzalloc_array(mem_ctx, BITSET_WORD, BITSET_WORDS(size)),
      .size = size,
   };
}

static bool
src_is_in_bitset(nir_src *src, void *_set)
{
   struct bitset *set = _set;
   assert(src->is_ssa);

   /* Any SSA values which were added after we generated liveness information
    * are things generated by this pass and, while most of it is arithmetic
    * which we could re-materialize, we don't need to because it's only used
    * for a single load/store and so shouldn't cross any shader calls.
    */
   if (src->ssa->index >= set->size)
      return false;

   return BITSET_TEST(set->set, src->ssa->index);
}

static void
add_ssa_def_to_bitset(nir_ssa_def *def, struct bitset *set)
{
   if (def->index >= set->size)
      return;

   BITSET_SET(set->set, def->index);
}

static bool
can_remat_instr(nir_instr *instr, struct bitset *remat)
{
   /* Set of all values which are trivially re-materializable and we shouldn't
    * ever spill them.  This includes:
    *
    *   - Undef values
    *   - Constants
    *   - Uniforms (UBO or push constant)
    *   - ALU combinations of any of the above
    *   - Derefs which are either complete or casts of any of the above
    *
    * Because this pass rewrites things in-order and phis are always turned
    * into register writes, We can use "is it SSA?" to answer the question
    * "can my source be re-materialized?".
    */
   switch (instr->type) {
   case nir_instr_type_alu:
      if (!nir_instr_as_alu(instr)->dest.dest.is_ssa)
         return false;

      return nir_foreach_src(instr, src_is_in_bitset, remat);

   case nir_instr_type_deref:
      return nir_foreach_src(instr, src_is_in_bitset, remat);

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_push_constant:
         /* These intrinsics don't need to be spilled as long as they don't
          * depend on any spilled values.
          */
         return nir_foreach_src(instr, src_is_in_bitset, remat);

      case nir_intrinsic_load_scratch_base_ptr:
      case nir_intrinsic_load_ray_launch_id:
      case nir_intrinsic_load_btd_dss_id_intel:
      case nir_intrinsic_load_btd_global_arg_addr_intel:
      case nir_intrinsic_load_btd_resume_sbt_addr_intel:
      case nir_intrinsic_load_ray_base_mem_addr_intel:
      case nir_intrinsic_load_ray_hw_stack_size_intel:
      case nir_intrinsic_load_ray_sw_stack_size_intel:
      case nir_intrinsic_load_ray_num_dss_rt_stacks_intel:
      case nir_intrinsic_load_ray_hit_sbt_addr_intel:
      case nir_intrinsic_load_ray_hit_sbt_stride_intel:
      case nir_intrinsic_load_ray_miss_sbt_addr_intel:
      case nir_intrinsic_load_ray_miss_sbt_stride_intel:
      case nir_intrinsic_load_callable_sbt_addr_intel:
      case nir_intrinsic_load_callable_sbt_stride_intel:
         /* Notably missing from the above list is btd_local_arg_addr_intel.
          * This is because the resume shader will have a different local
          * argument pointer because it has a different BSR.  Any access of
          * the original shader's local arguments needs to be preserved so
          * that pointer has to be saved on the stack.
          *
          * TODO: There may be some system values we want to avoid
          *       re-materializing as well but we have to be very careful
          *       to ensure that it's a system value which cannot change
          *       across a shader call.
          */
         return true;

      default:
         return false;
      }
   }

   case nir_instr_type_ssa_undef:
   case nir_instr_type_load_const:
      return true;

   default:
      return false;
   }
}

static bool
can_remat_ssa_def(nir_ssa_def *def, struct bitset *remat)
{
   return can_remat_instr(def->parent_instr, remat);
}

static nir_ssa_def *
remat_ssa_def(nir_builder *b, nir_ssa_def *def)
{
   nir_instr *clone = nir_instr_clone(b->shader, def->parent_instr);
   nir_builder_instr_insert(b, clone);
   return nir_instr_ssa_def(clone);
}

struct pbv_array {
   struct nir_phi_builder_value **arr;
   unsigned len;
};

static struct nir_phi_builder_value *
get_phi_builder_value_for_def(nir_ssa_def *def,
                              struct pbv_array *pbv_arr)
{
   if (def->index >= pbv_arr->len)
      return NULL;

   return pbv_arr->arr[def->index];
}

static nir_ssa_def *
get_phi_builder_def_for_src(nir_src *src, struct pbv_array *pbv_arr,
                            nir_block *block)
{
   assert(src->is_ssa);

   struct nir_phi_builder_value *pbv =
      get_phi_builder_value_for_def(src->ssa, pbv_arr);
   if (pbv == NULL)
      return NULL;

   return nir_phi_builder_value_get_block_def(pbv, block);
}

static bool
rewrite_instr_src_from_phi_builder(nir_src *src, void *_pbv_arr)
{
   nir_block *block;
   if (src->parent_instr->type == nir_instr_type_phi) {
      nir_phi_src *phi_src = exec_node_data(nir_phi_src, src, src);
      block = phi_src->pred;
   } else {
      block = src->parent_instr->block;
   }

   nir_ssa_def *new_def = get_phi_builder_def_for_src(src, _pbv_arr, block);
   if (new_def != NULL)
      nir_instr_rewrite_src(src->parent_instr, src, nir_src_for_ssa(new_def));
   return true;
}

static void
spill_ssa_defs_and_lower_shader_calls(nir_shader *shader, uint32_t num_calls,
                                      uint32_t first_resume_sbt_idx)
{
   /* TODO: If a SSA def is filled more than once, we probably want to just
    *       spill it at the LCM of the fill sites so we avoid unnecessary
    *       extra spills
    *
    * TODO: If a SSA def is defined outside a loop but live through some call
    *       inside the loop, we probably want to spill outside the loop.  We
    *       may also want to fill outside the loop if it's not used in the
    *       loop.
    *
    * TODO: Right now, we only re-materialize things if their immediate
    *       sources are things which we filled.  We probably want to expand
    *       that to re-materialize things whose sources are things we can
    *       re-materialize from things we filled.  We may want some DAG depth
    *       heuristic on this.
    */

   /* This happens per-shader rather than per-impl because we mess with
    * nir_shader::scratch_size.
    */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_metadata_require(impl, nir_metadata_live_ssa_defs |
                              nir_metadata_dominance |
                              nir_metadata_block_index);

   void *mem_ctx = ralloc_context(shader);

   const unsigned num_ssa_defs = impl->ssa_alloc;
   const unsigned live_words = BITSET_WORDS(num_ssa_defs);
   struct bitset trivial_remat = bitset_create(mem_ctx, num_ssa_defs);

   /* Array of all live SSA defs which are spill candidates */
   nir_ssa_def **spill_defs =
      rzalloc_array(mem_ctx, nir_ssa_def *, num_ssa_defs);

   /* For each spill candidate, an array of every time it's defined by a fill,
    * indexed by call instruction index.
    */
   nir_ssa_def ***fill_defs =
      rzalloc_array(mem_ctx, nir_ssa_def **, num_ssa_defs);

   /* For each call instruction, the liveness set at the call */
   const BITSET_WORD **call_live =
      rzalloc_array(mem_ctx, const BITSET_WORD *, num_calls);

   /* For each call instruction, the block index of the block it lives in */
   uint32_t *call_block_indices = rzalloc_array(mem_ctx, uint32_t, num_calls);

   /* Walk the call instructions and fetch the liveness set and block index
    * for each one.  We need to do this before we start modifying the shader
    * so that liveness doesn't complain that it's been invalidated.  Don't
    * worry, we'll be very careful with our live sets. :-)
    */
   unsigned call_idx = 0;
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (!instr_is_shader_call(instr))
            continue;

         call_block_indices[call_idx] = block->index;

         /* The objective here is to preserve values around shader call
          * instructions.  Therefore, we use the live set after the
          * instruction as the set of things we want to preserve.  Because
          * none of our shader call intrinsics return anything, we don't have
          * to worry about spilling over a return value.
          *
          * TODO: This isn't quite true for report_intersection.
          */
         call_live[call_idx] =
            nir_get_live_ssa_defs(nir_after_instr(instr), mem_ctx);

         call_idx++;
      }
   }

   nir_builder before, after;
   nir_builder_init(&before, impl);
   nir_builder_init(&after, impl);

   call_idx = 0;
   unsigned max_scratch_size = shader->scratch_size;
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         nir_ssa_def *def = nir_instr_ssa_def(instr);
         if (def != NULL) {
            if (can_remat_ssa_def(def, &trivial_remat)) {
               add_ssa_def_to_bitset(def, &trivial_remat);
            } else {
               spill_defs[def->index] = def;
            }
         }

         if (!instr_is_shader_call(instr))
            continue;

         const BITSET_WORD *live = call_live[call_idx];

         /* Make a copy of trivial_remat that we'll update as we crawl through
          * the live SSA defs and unspill them.
          */
         struct bitset remat = bitset_create(mem_ctx, num_ssa_defs);
         memcpy(remat.set, trivial_remat.set, live_words * sizeof(BITSET_WORD));

         /* Before the two builders are always separated by the call
          * instruction, it won't break anything to have two of them.
          */
         before.cursor = nir_before_instr(instr);
         after.cursor = nir_after_instr(instr);

         unsigned offset = shader->scratch_size;
         for (unsigned w = 0; w < live_words; w++) {
            BITSET_WORD spill_mask = live[w] & ~trivial_remat.set[w];
            while (spill_mask) {
               int i = u_bit_scan(&spill_mask);
               assert(i >= 0);
               unsigned index = w * BITSET_WORDBITS + i;
               assert(index < num_ssa_defs);

               nir_ssa_def *def = spill_defs[index];
               if (can_remat_ssa_def(def, &remat)) {
                  /* If this SSA def is re-materializable or based on other
                   * things we've already spilled, re-materialize it rather
                   * than spilling and filling.  Anything which is trivially
                   * re-materializable won't even get here because we take
                   * those into account in spill_mask above.
                   */
                  def = remat_ssa_def(&after, def);
               } else {
                  bool is_bool = def->bit_size == 1;
                  if (is_bool)
                     def = nir_b2b32(&before, def);

                  const unsigned comp_size = def->bit_size / 8;
                  offset = ALIGN(offset, comp_size);

                  brw_nir_rt_store_scratch(&before, offset, comp_size, def, ~0);

                  def = brw_nir_rt_load_scratch(&after, offset, comp_size,
                                                def->num_components,
                                                def->bit_size);
                  if (is_bool)
                     def = nir_b2b1(&after, def);

                  offset += def->num_components * comp_size;
               }

               /* Mark this SSA def as available in the remat set so that, if
                * some other SSA def we need is computed based on it, we can
                * just re-compute instead of fetching from memory.
                */
               BITSET_SET(remat.set, index);

               /* For now, we just make a note of this new SSA def.  We'll
                * fix things up with the phi builder as a second pass.
                */
               if (fill_defs[index] == NULL) {
                  fill_defs[index] =
                     rzalloc_array(mem_ctx, nir_ssa_def *, num_calls);
               }
               fill_defs[index][call_idx] = def;
            }
         }

         nir_builder *b = &before;

         offset = ALIGN(offset, BRW_BTD_STACK_ALIGN);
         max_scratch_size = MAX2(max_scratch_size, offset);

         /* First thing on the called shader's stack is the resume address
          * followed by a pointer to the payload.
          */
         nir_intrinsic_instr *call = nir_instr_as_intrinsic(instr);
         nir_ssa_def *resume_record_addr =
            nir_iadd_imm(b, nir_load_btd_resume_sbt_addr_intel(b),
                         (first_resume_sbt_idx + call_idx) *
                         BRW_BTD_RESUME_SBT_STRIDE);
         /* By the time we get here, any remaining shader/function memory
          * pointers have been lowered to SSA values.
          */
         assert(nir_get_shader_call_payload_src(call)->is_ssa);
         nir_ssa_def *payload_addr =
            nir_get_shader_call_payload_src(call)->ssa;
         brw_nir_rt_store_scratch(b, offset, BRW_BTD_STACK_ALIGN,
                                  nir_vec2(b, resume_record_addr, payload_addr),
                                  0xf /* write_mask */);

         nir_btd_stack_push_intel(b, offset);

         /* Lower to the _intel intrinsic */
         switch (call->intrinsic) {
         case nir_intrinsic_trace_ray: {
            nir_ssa_def *as_addr = call->src[0].ssa;
            nir_ssa_def *ray_flags = call->src[1].ssa;
            /* From the SPIR-V spec:
             *
             *    "Only the 8 least-significant bits of Cull Mask are used by
             *    this instruction - other bits are ignored.
             *
             *    Only the 4 least-significant bits of SBT Offset and SBT
             *    Stride are used by this instruction - other bits are
             *    ignored.
             *
             *    Only the 16 least-significant bits of Miss Index are used by
             *    this instruction - other bits are ignored."
             */
            nir_ssa_def *cull_mask = nir_iand_imm(b, call->src[2].ssa, 0xff);
            nir_ssa_def *sbt_offset = nir_iand_imm(b, call->src[3].ssa, 0xf);
            nir_ssa_def *sbt_stride = nir_iand_imm(b, call->src[4].ssa, 0xf);
            nir_ssa_def *miss_index = nir_iand_imm(b, call->src[5].ssa, 0xffff);
            nir_ssa_def *ray_orig = call->src[6].ssa;
            nir_ssa_def *ray_t_min = call->src[7].ssa;
            nir_ssa_def *ray_dir = call->src[8].ssa;
            nir_ssa_def *ray_t_max = call->src[9].ssa;

            /* The hardware packet takes the address to the root node in the
             * acceleration structure, not the acceleration structure itself.
             * To find that, we have to read the root node offset from the
             * acceleration structure which is the first QWord.
             */
            nir_ssa_def *root_node_ptr =
               nir_iadd(b, as_addr, nir_load_global(b, as_addr, 256, 1, 64));

            /* The hardware packet requires an address to the first element of
             * the hit SBT.
             *
             * In order to calculate this, we must multiply the "SBT Offset"
             * provided to OpTraceRay by the SBT stride provided for the hit
             * SBT in the call to vkCmdTraceRay() and add that to the base
             * address of the hit SBT.  This stride is not to be confused with
             * the "SBT Stride" provided to OpTraceRay which is in units of
             * this stride.  It's a rather terrible overload of the word
             * "stride".  The hardware docs calls the SPIR-V stride value the
             * "shader index multiplier" which is a much more sane name.
             */
            nir_ssa_def *hit_sbt_stride_B =
               nir_load_ray_hit_sbt_stride_intel(b);
            nir_ssa_def *hit_sbt_offset_B =
               nir_umul_32x16(b, sbt_offset, nir_u2u32(b, hit_sbt_stride_B));
            nir_ssa_def *hit_sbt_addr =
               nir_iadd(b, nir_load_ray_hit_sbt_addr_intel(b),
                           nir_u2u64(b, hit_sbt_offset_B));

            /* The hardware packet takes an address to the miss BSR. */
            nir_ssa_def *miss_sbt_stride_B =
               nir_load_ray_miss_sbt_stride_intel(b);
            nir_ssa_def *miss_sbt_offset_B =
               nir_umul_32x16(b, miss_index, nir_u2u32(b, miss_sbt_stride_B));
            nir_ssa_def *miss_sbt_addr =
               nir_iadd(b, nir_load_ray_miss_sbt_addr_intel(b),
                           nir_u2u64(b, miss_sbt_offset_B));

            struct brw_nir_rt_mem_ray_defs ray_defs = {
               .root_node_ptr = root_node_ptr,
               .ray_flags = nir_u2u16(b, ray_flags),
               .ray_mask = cull_mask,
               .hit_group_sr_base_ptr = hit_sbt_addr,
               .hit_group_sr_stride = nir_u2u16(b, hit_sbt_stride_B),
               .miss_sr_ptr = miss_sbt_addr,
               .orig = ray_orig,
               .t_near = ray_t_min,
               .dir = ray_dir,
               .t_far = ray_t_max,
               .shader_index_multiplier = sbt_stride,
            };
            brw_nir_rt_store_mem_ray(b, &ray_defs, BRW_RT_BVH_LEVEL_WORLD);
            nir_trace_ray_initial_intel(b);
            break;
         }

         case nir_intrinsic_report_ray_intersection:
            unreachable("Any-hit shaders must be inlined");

         case nir_intrinsic_execute_callable: {
            nir_ssa_def *sbt_offset32 =
               nir_imul(b, call->src[0].ssa,
                        nir_u2u32(b, nir_load_callable_sbt_stride_intel(b)));
            nir_ssa_def *sbt_addr =
               nir_iadd(b, nir_load_callable_sbt_addr_intel(b),
                           nir_u2u64(b, sbt_offset32));
            brw_nir_btd_spawn(b, sbt_addr);
            break;
         }

         default:
            unreachable("Invalid shader call instruction");
         }

         brw_nir_btd_resume(b, call_idx, offset);

         nir_instr_remove(&call->instr);

         call_idx++;
      }
   }
   assert(call_idx == num_calls);
   shader->scratch_size = max_scratch_size;

   struct nir_phi_builder *pb = nir_phi_builder_create(impl);
   struct pbv_array pbv_arr = {
      .arr = rzalloc_array(mem_ctx, struct nir_phi_builder_value *,
                           num_ssa_defs),
      .len = num_ssa_defs,
   };

   const unsigned block_words = BITSET_WORDS(impl->num_blocks);
   BITSET_WORD *def_blocks = ralloc_array(mem_ctx, BITSET_WORD, block_words);

   /* Go through and set up phi builder values for each spillable value which
    * we ever needed to spill at any point.
    */
   for (unsigned index = 0; index < num_ssa_defs; index++) {
      if (fill_defs[index] == NULL)
         continue;

      nir_ssa_def *def = spill_defs[index];

      memset(def_blocks, 0, block_words * sizeof(BITSET_WORD));
      BITSET_SET(def_blocks, def->parent_instr->block->index);
      for (unsigned call_idx = 0; call_idx < num_calls; call_idx++) {
         if (fill_defs[index][call_idx] != NULL)
            BITSET_SET(def_blocks, call_block_indices[call_idx]);
      }

      pbv_arr.arr[index] = nir_phi_builder_add_value(pb, def->num_components,
                                                     def->bit_size, def_blocks);
   }

   /* Walk the shader one more time and rewrite SSA defs as needed using the
    * phi builder.
    */
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         nir_ssa_def *def = nir_instr_ssa_def(instr);
         if (def != NULL) {
            struct nir_phi_builder_value *pbv =
               get_phi_builder_value_for_def(def, &pbv_arr);
            if (pbv != NULL)
               nir_phi_builder_value_set_block_def(pbv, block, def);
         }

         if (instr->type == nir_instr_type_phi)
            continue;

         nir_foreach_src(instr, rewrite_instr_src_from_phi_builder, &pbv_arr);

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *resume = nir_instr_as_intrinsic(instr);
         if (resume->intrinsic != nir_intrinsic_btd_resume_intel)
            continue;

         call_idx = nir_intrinsic_base(resume);

         /* Technically, this is the wrong place to add the fill defs to the
          * phi builder values because we haven't seen any of the load_scratch
          * instructions for this call yet.  However, we know based on how we
          * emitted them that no value ever gets used until after the load
          * instruction has been emitted so this should be safe.  If we ever
          * fail validation due this it likely means a bug in our spilling
          * code and not the phi re-construction code here.
          */
         for (unsigned index = 0; index < num_ssa_defs; index++) {
            if (fill_defs[index] && fill_defs[index][call_idx]) {
               nir_phi_builder_value_set_block_def(pbv_arr.arr[index], block,
                                                   fill_defs[index][call_idx]);
            }
         }
      }

      nir_if *following_if = nir_block_get_following_if(block);
      if (following_if) {
         nir_ssa_def *new_def =
            get_phi_builder_def_for_src(&following_if->condition,
                                        &pbv_arr, block);
         if (new_def != NULL)
            nir_if_rewrite_condition(following_if, nir_src_for_ssa(new_def));
      }

      /* Handle phi sources that source from this block.  We have to do this
       * as a separate pass because the phi builder assumes that uses and
       * defs are processed in an order that respects dominance.  When we have
       * loops, a phi source may be a back-edge so we have to handle it as if
       * it were one of the last instructions in the predecessor block.
       */
      nir_foreach_phi_src_leaving_block(block,
                                        rewrite_instr_src_from_phi_builder,
                                        &pbv_arr);
   }

   nir_phi_builder_finish(pb);

   ralloc_free(mem_ctx);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

static nir_instr *
find_resume_instr(nir_function_impl *impl, unsigned call_idx)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *resume = nir_instr_as_intrinsic(instr);
         if (resume->intrinsic != nir_intrinsic_btd_resume_intel)
            continue;

         if (nir_intrinsic_base(resume) == call_idx)
            return &resume->instr;
      }
   }
   unreachable("Couldn't find resume instruction");
}

/* Walk the CF tree and duplicate the contents of every loop, one half runs on
 * resume and the other half is for any post-resume loop iterations.  We are
 * careful in our duplication to ensure that resume_instr is in the resume
 * half of the loop though a copy of resume_instr will remain in the other
 * half as well in case the same shader call happens twice.
 */
static bool
duplicate_loop_bodies(nir_function_impl *impl, nir_instr *resume_instr)
{
   nir_register *resume_reg = NULL;
   for (nir_cf_node *node = resume_instr->block->cf_node.parent;
        node->type != nir_cf_node_function; node = node->parent) {
      if (node->type != nir_cf_node_loop)
         continue;

      nir_loop *loop = nir_cf_node_as_loop(node);

      if (resume_reg == NULL) {
         /* We only create resume_reg if we encounter a loop.  This way we can
          * avoid re-validating the shader and calling ssa_to_regs in the case
          * where it's just if-ladders.
          */
         resume_reg = nir_local_reg_create(impl);
         resume_reg->num_components = 1;
         resume_reg->bit_size = 1;

         nir_builder b;
         nir_builder_init(&b, impl);

         /* Initialize resume to true */
         b.cursor = nir_before_cf_list(&impl->body);
         nir_store_reg(&b, resume_reg, nir_imm_true(&b), 1);

         /* Set resume to false right after the resume instruction */
         b.cursor = nir_after_instr(resume_instr);
         nir_store_reg(&b, resume_reg, nir_imm_false(&b), 1);
      }

      /* Before we go any further, make sure that everything which exits the
       * loop or continues around to the top of the loop does so through
       * registers.  We're about to duplicate the loop body and we'll have
       * serious trouble if we don't do this.
       */
      nir_convert_loop_to_lcssa(loop);
      nir_lower_phis_to_regs_block(nir_loop_first_block(loop));
      nir_lower_phis_to_regs_block(
         nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node)));

      nir_cf_list cf_list;
      nir_cf_list_extract(&cf_list, &loop->body);

      nir_if *_if = nir_if_create(impl->function->shader);
      _if->condition = nir_src_for_reg(resume_reg);
      nir_cf_node_insert(nir_after_cf_list(&loop->body), &_if->cf_node);

      nir_cf_list clone;
      nir_cf_list_clone(&clone, &cf_list, &loop->cf_node, NULL);

      /* Insert the clone in the else and the original in the then so that
       * the resume_instr remains valid even after the duplication.
       */
      nir_cf_reinsert(&cf_list, nir_before_cf_list(&_if->then_list));
      nir_cf_reinsert(&clone, nir_before_cf_list(&_if->else_list));
   }

   if (resume_reg != NULL)
      nir_metadata_preserve(impl, nir_metadata_none);

   return resume_reg != NULL;
}

static bool
cf_node_contains_instr(nir_cf_node *node, nir_instr *instr)
{
   for (nir_cf_node *n = &instr->block->cf_node; n != NULL; n = n->parent) {
      if (n == node)
         return true;
   }

   return false;
}

static void
rewrite_phis_to_pred(nir_block *block, nir_block *pred)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi = nir_instr_as_phi(instr);

      ASSERTED bool found = false;
      nir_foreach_phi_src(phi_src, phi) {
         if (phi_src->pred == pred) {
            found = true;
            assert(phi_src->src.is_ssa);
            nir_ssa_def_rewrite_uses(&phi->dest.ssa, phi_src->src);
            break;
         }
      }
      assert(found);
   }
}

/** Flattens if ladders leading up to a resume
 *
 * Given a resume_instr, this function flattens any if ladders leading to the
 * resume instruction and deletes any code that cannot be encountered on a
 * direct path to the resume instruction.  This way we get, for the most part,
 * straight-line control-flow up to the resume instruction.
 *
 * While we do this flattening, we also move any code which is in the remat
 * set up to the top of the function or to the top of the resume portion of
 * the current loop.  We don't worry about control-flow as we do this because
 * phis will never be in the remat set (see can_remat_instr) and so nothing
 * control-dependent will ever need to be re-materialized.  It is possible
 * that this algorithm will preserve too many instructions by moving them to
 * the top but we leave that for DCE to clean up.  Any code not in the remat
 * set is deleted because it's either unused in the continuation or else
 * unspilled from a previous continuation and the unspill code is after the
 * resume instruction.
 *
 * If, for instance, we have something like this:
 *
 *    // block 0
 *    if (cond1) {
 *       // block 1
 *    } else {
 *       // block 2
 *       if (cond2) {
 *          // block 3
 *          resume;
 *          if (cond3) {
 *             // block 4
 *          }
 *       } else {
 *          // block 5
 *       }
 *    }
 *
 * then we know, because we know the resume instruction had to be encoutered,
 * that cond1 = false and cond2 = true and we lower as follows:
 *
 *    // block 0
 *    // block 2
 *    // block 3
 *    resume;
 *    if (cond3) {
 *       // block 4
 *    }
 *
 * As you can see, the code in blocks 1 and 5 was removed because there is no
 * path from the start of the shader to the resume instruction which execute
 * blocks 1 or 5.  Any remat code from blocks 0, 2, and 3 is preserved and
 * moved to the top.  If the resume instruction is inside a loop then we know
 * a priori that it is of the form
 *
 *    loop {
 *       if (resume) {
 *          // Contents containing resume_instr
 *       } else {
 *          // Second copy of contents
 *       }
 *    }
 *
 * In this case, we only descend into the first half of the loop.  The second
 * half is left alone as that portion is only ever executed after the resume
 * instruction.
 */
static bool
flatten_resume_if_ladder(nir_function_impl *impl,
                         nir_instr *cursor,
                         struct exec_list *child_list,
                         bool child_list_contains_cursor,
                         nir_instr *resume_instr,
                         struct bitset *remat)
{
   nir_shader *shader = impl->function->shader;
   nir_cf_list cf_list;

   /* If our child list contains the cursor instruction then we start out
    * before the cursor instruction.  We need to know this so that we can skip
    * moving instructions which are already before the cursor.
    */
   bool before_cursor = child_list_contains_cursor;

   nir_cf_node *resume_node = NULL;
   foreach_list_typed_safe(nir_cf_node, child, node, child_list) {
      switch (child->type) {
      case nir_cf_node_block: {
         nir_block *block = nir_cf_node_as_block(child);
         nir_foreach_instr_safe(instr, block) {
            if (instr == cursor) {
               assert(nir_cf_node_is_first(&block->cf_node));
               assert(before_cursor);
               before_cursor = false;
               continue;
            }

            if (instr == resume_instr)
               goto found_resume;

            if (!before_cursor && can_remat_instr(instr, remat)) {
               nir_instr_remove(instr);
               nir_instr_insert(nir_before_instr(cursor), instr);

               nir_ssa_def *def = nir_instr_ssa_def(instr);
               BITSET_SET(remat->set, def->index);
            }
         }
         break;
      }

      case nir_cf_node_if: {
         assert(!before_cursor);
         nir_if *_if = nir_cf_node_as_if(child);
         if (flatten_resume_if_ladder(impl, cursor, &_if->then_list,
                                      false, resume_instr, remat)) {
            resume_node = child;
            rewrite_phis_to_pred(nir_cf_node_as_block(nir_cf_node_next(child)),
                                 nir_if_last_then_block(_if));
            goto found_resume;
         }

         if (flatten_resume_if_ladder(impl, cursor, &_if->else_list,
                                      false, resume_instr, remat)) {
            resume_node = child;
            rewrite_phis_to_pred(nir_cf_node_as_block(nir_cf_node_next(child)),
                                 nir_if_last_else_block(_if));
            goto found_resume;
         }
         break;
      }

      case nir_cf_node_loop: {
         assert(!before_cursor);
         nir_loop *loop = nir_cf_node_as_loop(child);

         if (cf_node_contains_instr(&loop->cf_node, resume_instr)) {
            /* Thanks to our loop body duplication pass, every level of loop
             * containing the resume instruction contains exactly three nodes:
             * two blocks and an if.  We don't want to lower away this if
             * because it's the resume selection if.  The resume half is
             * always the then_list so that's what we want to flatten.
             */
            nir_block *header = nir_loop_first_block(loop);
            nir_if *_if = nir_cf_node_as_if(nir_cf_node_next(&header->cf_node));

            /* We want to place anything re-materialized from inside the loop
             * at the top of the resume half of the loop.
             */
            nir_instr *loop_cursor =
               &nir_intrinsic_instr_create(shader, nir_intrinsic_nop)->instr;
            nir_instr_insert(nir_before_cf_list(&_if->then_list), loop_cursor);

            ASSERTED bool found =
               flatten_resume_if_ladder(impl, loop_cursor, &_if->then_list,
                                        true, resume_instr, remat);
            assert(found);
            resume_node = child;
            goto found_resume;
         } else {
            ASSERTED bool found =
               flatten_resume_if_ladder(impl, cursor, &loop->body,
                                        false, resume_instr, remat);
            assert(!found);
         }
         break;
      }

      case nir_cf_node_function:
         unreachable("Unsupported CF node type");
      }
   }
   assert(!before_cursor);

   /* If we got here, we didn't find the resume node or instruction. */
   return false;

found_resume:
   /* If we got here then we found either the resume node or the resume
    * instruction in this CF list.
    */
   if (resume_node) {
      /* If the resume instruction is buried in side one of our children CF
       * nodes, resume_node now points to that child.
       */
      if (resume_node->type == nir_cf_node_if) {
         /* Thanks to the recursive call, all of the interesting contents of
          * resume_node have been copied before the cursor.  We just need to
          * copy the stuff after resume_node.
          */
         nir_cf_extract(&cf_list, nir_after_cf_node(resume_node),
                                  nir_after_cf_list(child_list));
      } else {
         /* The loop contains its own cursor and still has useful stuff in it.
          * We want to move everything after and including the loop to before
          * the cursor.
          */
         assert(resume_node->type == nir_cf_node_loop);
         nir_cf_extract(&cf_list, nir_before_cf_node(resume_node),
                                  nir_after_cf_list(child_list));
      }
   } else {
      /* If we found the resume instruction in one of our blocks, grab
       * everything after it in the entire list (not just the one block), and
       * place it before the cursor instr.
       */
      nir_cf_extract(&cf_list, nir_after_instr(resume_instr),
                               nir_after_cf_list(child_list));
   }
   nir_cf_reinsert(&cf_list, nir_before_instr(cursor));

   if (!resume_node) {
      /* We want the resume to be the first "interesting" instruction */
      nir_instr_remove(resume_instr);
      nir_instr_insert(nir_before_cf_list(&impl->body), resume_instr);
   }

   /* We've copied everything interesting out of this CF list to before the
    * cursor.  Delete everything else.
    */
   if (child_list_contains_cursor) {
      nir_cf_extract(&cf_list, nir_after_instr(cursor),
                               nir_after_cf_list(child_list));
   } else {
      nir_cf_list_extract(&cf_list, child_list);
   }
   nir_cf_delete(&cf_list);

   return true;
}

static nir_instr *
lower_resume(nir_shader *shader, int call_idx)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_instr *resume_instr = find_resume_instr(impl, call_idx);

   if (duplicate_loop_bodies(impl, resume_instr)) {
      nir_validate_shader(shader, "after duplicate_loop_bodies in "
                                  "brw_nir_lower_shader_calls");
      /* If we duplicated the bodies of any loops, run regs_to_ssa to get rid
       * of all those pesky registers we just added.
       */
      NIR_PASS_V(shader, nir_lower_regs_to_ssa);
   }

   /* Re-index nir_ssa_def::index.  We don't care about actual liveness in
    * this pass but, so we can use the same helpers as the spilling pass, we
    * need to make sure that live_index is something sane.  It's used
    * constantly for determining if an SSA value has been added since the
    * start of the pass.
    */
   nir_index_ssa_defs(impl);

   void *mem_ctx = ralloc_context(shader);

   /* Used to track which things may have been assumed to be re-materialized
    * by the spilling pass and which we shouldn't delete.
    */
   struct bitset remat = bitset_create(mem_ctx, impl->ssa_alloc);

   /* Create a nop instruction to use as a cursor as we extract and re-insert
    * stuff into the CFG.
    */
   nir_instr *cursor =
      &nir_intrinsic_instr_create(shader, nir_intrinsic_nop)->instr;
   nir_instr_insert(nir_before_cf_list(&impl->body), cursor);

   ASSERTED bool found =
      flatten_resume_if_ladder(impl, cursor, &impl->body,
                               true, resume_instr, &remat);
   assert(found);

   ralloc_free(mem_ctx);

   nir_validate_shader(shader, "after flatten_resume_if_ladder in "
                               "brw_nir_lower_shader_calls");

   nir_metadata_preserve(impl, nir_metadata_none);

   return resume_instr;
}

static void
replace_resume_with_halt(nir_shader *shader, nir_instr *keep)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block_safe(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr == keep)
            continue;

         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *resume = nir_instr_as_intrinsic(instr);
         if (resume->intrinsic != nir_intrinsic_btd_resume_intel)
            continue;

         /* If this is some other resume, then we've kicked off a ray or
          * bindless thread and we don't want to go any further in this
          * shader.  Insert a halt so that NIR will delete any instructions
          * dominated by this call instruction including the scratch_load
          * instructions we inserted.
          */
         nir_cf_list cf_list;
         nir_cf_extract(&cf_list, nir_after_instr(&resume->instr),
                                  nir_after_block(block));
         nir_cf_delete(&cf_list);
         b.cursor = nir_instr_remove(&resume->instr);
         nir_jump(&b, nir_jump_halt);
         break;
      }
   }
}


/** Lower shader call instructions to the Intel bindless dispatch model
 *
 * Shader calls on Intel work by splitting the calling shader into an initial
 * shader and a series of "resume" shaders.  When the shader is first invoked,
 * it is the initial shader which is executed.  At any point in the initial
 * shader or any one of the resume shaders, a shader call operation may be
 * performed.  The possible shader call operations are:
 *
 *  - trace_ray
 *  - report_ray_intersection
 *  - execute_callable
 *
 * When a shader call operation is performed, we push all live values to the
 * stack, place a return address as the first QWord of the callee's stack
 * space, call into the BTD or RT hardware to execute shader call operation,
 * and then kill the current shader.  Once the operation we invoked is
 * complete, a callee shader will read the return address off the stack, and
 * do a BTD spawn to invoke one of our resume shaders.  The resume shader
 * pops the contents off the stack and picks up where the calling shader left
 * off.
 *
 * Stack management is done via two intel-specific intrinsics.  The
 * btd_stack_push_intel intrinsic increments the stack pointer by adding the
 * specified byte offset to the stack offset in the per-invocation hotzone.
 * The btd_resume_intel intrinsic marks the beginning of a resume shader and
 * it decrements the stack.  It also contains a call_idx parameter (in the
 * BASE index slot) which identifies which resume operation it is and is
 * invaluable for identifying resume points while lowering resume shaders.
 */
bool
brw_nir_lower_shader_calls(nir_shader *shader,
                           uint32_t first_resume_sbt_idx,
                           nir_shader ***resume_shaders_out,
                           uint32_t *num_resume_shaders_out,
                           void *mem_ctx)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_builder b;
   nir_builder_init(&b, impl);

   int num_calls = 0;
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr_is_shader_call(instr))
            num_calls++;
      }
   }

   if (num_calls == 0) {
      nir_shader_preserve_all_metadata(shader);
      *num_resume_shaders_out = 0;
      return false;
   }

   /* Some intrinsics not only can't be re-materialized but aren't preserved
    * when moving to the continuation shader.  We have to move them to the top
    * to ensure they get spilled as needed.
    */
   {
      bool progress = false;
      NIR_PASS(progress, shader, move_system_values_to_top);
      if (progress)
         NIR_PASS(progress, shader, nir_opt_cse);
   }

   NIR_PASS_V(shader, spill_ssa_defs_and_lower_shader_calls,
              num_calls, first_resume_sbt_idx);

   /* Make N copies of our shader */
   nir_shader **resume_shaders = ralloc_array(mem_ctx, nir_shader *, num_calls);
   for (unsigned i = 0; i < num_calls; i++)
      resume_shaders[i] = nir_shader_clone(mem_ctx, shader);

   replace_resume_with_halt(shader, NULL);
   for (unsigned i = 0; i < num_calls; i++) {
      nir_instr *resume_instr = lower_resume(resume_shaders[i], i);
      replace_resume_with_halt(resume_shaders[i], resume_instr);
   }

   *resume_shaders_out = resume_shaders;
   *num_resume_shaders_out = num_calls;

   return true;
}

/** Creates a trivial return shader
 *
 * This is a callable shader that doesn't really do anything.  It just loads
 * the resume address from the stack and does a return.
 */
nir_shader *
brw_nir_create_trivial_return_shader(const struct brw_compiler *compiler,
                                     void *mem_ctx)
{
   const nir_shader_compiler_options *nir_options =
      compiler->glsl_compiler_options[MESA_SHADER_CALLABLE].NirOptions;

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_CALLABLE,
                                                  nir_options,
                                                  "RT Trivial Return");
   ralloc_steal(mem_ctx, b.shader);
   nir_shader *nir = b.shader;

   NIR_PASS_V(nir, brw_nir_lower_shader_returns);

   return nir;
}
