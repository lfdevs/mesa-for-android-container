/*
 * Copyright © 2016 Intel Corporation
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

#include "anv_nir.h"
#include "nir/nir_builder.h"
#include "util/debug.h"

/**
 * This file implements the lowering required for VK_KHR_multiview.
 *
 * When possible, Primitive Replication is used and the shader is modified to
 * make gl_Position an array and fill it with values for each view.
 *
 * Otherwise we implement multiview using instanced rendering.  The number of
 * instances in each draw call is multiplied by the number of views in the
 * subpass.  Then, in the shader, we divide gl_InstanceId by the number of
 * views and use gl_InstanceId % view_count to compute the actual ViewIndex.
 */

struct lower_multiview_state {
   nir_builder builder;

   uint32_t view_mask;

   nir_ssa_def *instance_id;
   nir_ssa_def *view_index;
};

static nir_ssa_def *
build_instance_id(struct lower_multiview_state *state)
{
   assert(state->builder.shader->info.stage == MESA_SHADER_VERTEX);

   if (state->instance_id == NULL) {
      nir_builder *b = &state->builder;

      b->cursor = nir_before_block(nir_start_block(b->impl));

      /* We use instancing for implementing multiview.  The actual instance id
       * is given by dividing instance_id by the number of views in this
       * subpass.
       */
      state->instance_id =
         nir_idiv(b, nir_load_instance_id(b),
                     nir_imm_int(b, util_bitcount(state->view_mask)));
   }

   return state->instance_id;
}

static nir_ssa_def *
build_view_index(struct lower_multiview_state *state)
{
   if (state->view_index == NULL) {
      nir_builder *b = &state->builder;

      b->cursor = nir_before_block(nir_start_block(b->impl));

      assert(state->view_mask != 0);
      if (util_bitcount(state->view_mask) == 1) {
         /* Set the view index directly. */
         state->view_index = nir_imm_int(b, ffs(state->view_mask) - 1);
      } else if (state->builder.shader->info.stage == MESA_SHADER_VERTEX) {
         /* We only support 16 viewports */
         assert((state->view_mask & 0xffff0000) == 0);

         /* We use instancing for implementing multiview.  The compacted view
          * id is given by instance_id % view_count.  We then have to convert
          * that to an actual view id.
          */
         nir_ssa_def *compacted =
            nir_umod(b, nir_load_instance_id(b),
                        nir_imm_int(b, util_bitcount(state->view_mask)));

         if (util_is_power_of_two_or_zero(state->view_mask + 1)) {
            /* If we have a full view mask, then compacted is what we want */
            state->view_index = compacted;
         } else {
            /* Now we define a map from compacted view index to the actual
             * view index that's based on the view_mask.  The map is given by
             * 16 nibbles, each of which is a value from 0 to 15.
             */
            uint64_t remap = 0;
            uint32_t bit, i = 0;
            for_each_bit(bit, state->view_mask) {
               assert(bit < 16);
               remap |= (uint64_t)bit << (i++ * 4);
            }

            nir_ssa_def *shift = nir_imul(b, compacted, nir_imm_int(b, 4));

            /* One of these days, when we have int64 everywhere, this will be
             * easier.
             */
            nir_ssa_def *shifted;
            if (remap <= UINT32_MAX) {
               shifted = nir_ushr(b, nir_imm_int(b, remap), shift);
            } else {
               nir_ssa_def *shifted_low =
                  nir_ushr(b, nir_imm_int(b, remap), shift);
               nir_ssa_def *shifted_high =
                  nir_ushr(b, nir_imm_int(b, remap >> 32),
                              nir_isub(b, shift, nir_imm_int(b, 32)));
               shifted = nir_bcsel(b, nir_ilt(b, shift, nir_imm_int(b, 32)),
                                      shifted_low, shifted_high);
            }
            state->view_index = nir_iand(b, shifted, nir_imm_int(b, 0xf));
         }
      } else {
         const struct glsl_type *type = glsl_int_type();
         if (b->shader->info.stage == MESA_SHADER_TESS_CTRL ||
             b->shader->info.stage == MESA_SHADER_GEOMETRY)
            type = glsl_array_type(type, 1, 0);

         nir_variable *idx_var =
            nir_variable_create(b->shader, nir_var_shader_in,
                                type, "view index");
         idx_var->data.location = VARYING_SLOT_VIEW_INDEX;
         if (b->shader->info.stage == MESA_SHADER_FRAGMENT)
            idx_var->data.interpolation = INTERP_MODE_FLAT;

         nir_deref_instr *deref = nir_build_deref_var(b, idx_var);
         if (glsl_type_is_array(type))
            deref = nir_build_deref_array_imm(b, deref, 0);

         state->view_index = nir_load_deref(b, deref);
      }
   }

   return state->view_index;
}

/* Primitive Replication allows a shader to write different positions for each
 * view in the same execution. If only the position depends on the view, then
 * it is possible to use the feature instead of instancing to implement
 * multiview.
 */
static bool
lower_multiview_with_primitive_replication(nir_shader *shader,
                                           struct anv_graphics_pipeline *pipeline)
{
   if (shader->info.stage == MESA_SHADER_FRAGMENT)
      return false;

   assert(shader->info.stage == MESA_SHADER_VERTEX);

   uint32_t view_mask = pipeline->subpass->view_mask;
   int view_count = util_bitcount(view_mask);
   assert(view_count > 1 && view_count <= MAX_VIEWS_FOR_PRIMITIVE_REPLICATION);

   nir_function_impl *entrypoint = nir_shader_get_entrypoint(shader);

   /* Update position to refer to an array. */
   nir_variable *pos_var = NULL;
   nir_foreach_shader_out_variable(var, shader) {
      if (var->data.location == VARYING_SLOT_POS) {
         assert(var->type == glsl_vec4_type());
         var->type = glsl_array_type(glsl_vec4_type(), view_count, 0);
         var->data.per_view = true;
         pos_var = var;
         break;
      }
   }

   assert(pos_var);

   nir_cf_list body;
   nir_cf_list_extract(&body, &entrypoint->body);

   nir_builder b;
   nir_builder_init(&b, entrypoint);
   b.cursor = nir_after_cf_list(&entrypoint->body);

   /* Fill Layer ID with zero.  Replication will use that as base to apply the
    * RTAI offsets.
    */
   nir_variable *layer_id_out =
      nir_variable_create(shader, nir_var_shader_out,
                          glsl_int_type(), "layer ID");
   layer_id_out->data.location = VARYING_SLOT_LAYER;
   nir_store_var(&b, layer_id_out, nir_imm_zero(&b, 1, 32), 0x1);

   /* Loop Index will go from 0 to view_count. */
   nir_variable *loop_index_var =
      nir_local_variable_create(entrypoint, glsl_uint_type(), "loop_index");
   nir_deref_instr *loop_index_deref = nir_build_deref_var(&b, loop_index_var);
   nir_store_deref(&b, loop_index_deref, nir_imm_int(&b, 0), 1);

   /* Array of view index values that are active in the loop.  Note that the
    * loop index only matches the view index if there are no gaps in the
    * view_mask.
    */
   nir_variable *view_index_var = nir_local_variable_create(
      entrypoint, glsl_array_type(glsl_uint_type(), view_count, 0), "view_index");
   nir_deref_instr *view_index_deref = nir_build_deref_var(&b, view_index_var);
   {
      int array_position = 0;
      uint32_t view_index;
      for_each_bit(view_index, view_mask) {
         nir_store_deref(&b, nir_build_deref_array_imm(&b, view_index_deref, array_position),
                         nir_imm_int(&b, view_index), 1);
         array_position++;
      }
   }

   /* Create the equivalent of
    *
    *    while (true):
    *       if (loop_index >= view_count):
    *          break
    *
    *       view_index = active_indices[loop_index]
    *       pos_deref = &pos[loop_index]
    *
    *       # Placeholder for the body to be reinserted.
    *
    *       loop_index += 1
    *
    * Later both `view_index` and `pos_deref` will be used to rewrite the
    * original shader body.
    */

   nir_loop* loop = nir_push_loop(&b);

   nir_ssa_def *loop_index = nir_load_deref(&b, loop_index_deref);
   nir_ssa_def *cmp = nir_ige(&b, loop_index, nir_imm_int(&b, view_count));
   nir_if *loop_check = nir_push_if(&b, cmp);
   nir_jump(&b, nir_jump_break);
   nir_pop_if(&b, loop_check);

   nir_ssa_def *view_index =
      nir_load_deref(&b, nir_build_deref_array(&b, view_index_deref, loop_index));
   nir_deref_instr *pos_deref =
      nir_build_deref_array(&b, nir_build_deref_var(&b, pos_var), loop_index);

   nir_store_deref(&b, loop_index_deref, nir_iadd_imm(&b, loop_index, 1), 1);
   nir_pop_loop(&b, loop);

   /* Reinsert the body. */
   b.cursor = nir_after_instr(&pos_deref->instr);
   nir_cf_reinsert(&body, b.cursor);

   nir_foreach_block(block, entrypoint) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_load_view_index: {
            assert(intrin->dest.is_ssa);
            nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(view_index));
            break;
         }

         case nir_intrinsic_store_deref: {
            nir_variable *var = nir_intrinsic_get_var(intrin, 0);
            if (var == pos_var) {
               nir_deref_instr *old_deref = nir_src_as_deref(intrin->src[0]);

               nir_instr_rewrite_src(instr, &intrin->src[0],
                                     nir_src_for_ssa(&pos_deref->dest.ssa));

               /* Remove old deref since it has the wrong type. */
               nir_deref_instr_remove_if_unused(old_deref);
            }
            break;
         }

         case nir_intrinsic_load_deref:
            if (nir_intrinsic_get_var(intrin, 0) == pos_var) {
               unreachable("Should have lowered I/O to temporaries "
                           "so no load_deref on position output is expected.");
            }
            break;

         case nir_intrinsic_copy_deref:
            unreachable("Should have lowered copy_derefs at this point");
            break;

         default:
            /* Do nothing. */
            break;
         }
      }
   }

   nir_metadata_preserve(entrypoint, nir_metadata_none);
   return true;
}

bool
anv_nir_lower_multiview(nir_shader *shader,
                        struct anv_graphics_pipeline *pipeline)
{
   assert(shader->info.stage != MESA_SHADER_COMPUTE);
   uint32_t view_mask = pipeline->subpass->view_mask;

   /* If multiview isn't enabled, we have nothing to do. */
   if (view_mask == 0)
      return false;

   if (pipeline->use_primitive_replication)
      return lower_multiview_with_primitive_replication(shader, pipeline);

   struct lower_multiview_state state = {
      .view_mask = view_mask,
   };

   /* This pass assumes a single entrypoint */
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(shader);

   nir_builder_init(&state.builder, entrypoint);

   bool progress = false;
   nir_foreach_block(block, entrypoint) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);

         if (load->intrinsic != nir_intrinsic_load_instance_id &&
             load->intrinsic != nir_intrinsic_load_view_index)
            continue;

         assert(load->dest.is_ssa);

         nir_ssa_def *value;
         if (load->intrinsic == nir_intrinsic_load_instance_id) {
            value = build_instance_id(&state);
         } else {
            assert(load->intrinsic == nir_intrinsic_load_view_index);
            value = build_view_index(&state);
         }

         nir_ssa_def_rewrite_uses(&load->dest.ssa, nir_src_for_ssa(value));

         nir_instr_remove(&load->instr);
         progress = true;
      }
   }

   /* The view index is available in all stages but the instance id is only
    * available in the VS.  If it's not a fragment shader, we need to pass
    * the view index on to the next stage.
    */
   if (shader->info.stage != MESA_SHADER_FRAGMENT) {
      nir_ssa_def *view_index = build_view_index(&state);

      nir_builder *b = &state.builder;

      assert(view_index->parent_instr->block == nir_start_block(entrypoint));
      b->cursor = nir_after_instr(view_index->parent_instr);

      /* Unless there is only one possible view index (that would be set
       * directly), pass it to the next stage. */
      if (util_bitcount(state.view_mask) != 1) {
         nir_variable *view_index_out =
            nir_variable_create(shader, nir_var_shader_out,
                                glsl_int_type(), "view index");
         view_index_out->data.location = VARYING_SLOT_VIEW_INDEX;
         nir_store_var(b, view_index_out, view_index, 0x1);
      }

      nir_variable *layer_id_out =
         nir_variable_create(shader, nir_var_shader_out,
                             glsl_int_type(), "layer ID");
      layer_id_out->data.location = VARYING_SLOT_LAYER;
      nir_store_var(b, layer_id_out, view_index, 0x1);

      progress = true;
   }

   if (progress) {
      nir_metadata_preserve(entrypoint, nir_metadata_block_index |
                                        nir_metadata_dominance);
   }

   return progress;
}

static bool
shader_writes_to_memory(nir_shader *shader)
{
   /* With multiview, we would need to ensure that memory writes happen either
    * once or once per view. Since combination of multiview and memory writes
    * is not expected, we'll just skip this optimization in this case.
    */

   nir_function_impl *entrypoint = nir_shader_get_entrypoint(shader);

   nir_foreach_block(block, entrypoint) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_deref_atomic_add:
         case nir_intrinsic_deref_atomic_imin:
         case nir_intrinsic_deref_atomic_umin:
         case nir_intrinsic_deref_atomic_imax:
         case nir_intrinsic_deref_atomic_umax:
         case nir_intrinsic_deref_atomic_and:
         case nir_intrinsic_deref_atomic_or:
         case nir_intrinsic_deref_atomic_xor:
         case nir_intrinsic_deref_atomic_exchange:
         case nir_intrinsic_deref_atomic_comp_swap:
         case nir_intrinsic_store_ssbo:
         case nir_intrinsic_ssbo_atomic_add:
         case nir_intrinsic_ssbo_atomic_imin:
         case nir_intrinsic_ssbo_atomic_umin:
         case nir_intrinsic_ssbo_atomic_imax:
         case nir_intrinsic_ssbo_atomic_umax:
         case nir_intrinsic_ssbo_atomic_and:
         case nir_intrinsic_ssbo_atomic_or:
         case nir_intrinsic_ssbo_atomic_xor:
         case nir_intrinsic_ssbo_atomic_exchange:
         case nir_intrinsic_ssbo_atomic_comp_swap:
         case nir_intrinsic_store_shared:
         case nir_intrinsic_shared_atomic_add:
         case nir_intrinsic_shared_atomic_imin:
         case nir_intrinsic_shared_atomic_umin:
         case nir_intrinsic_shared_atomic_imax:
         case nir_intrinsic_shared_atomic_umax:
         case nir_intrinsic_shared_atomic_and:
         case nir_intrinsic_shared_atomic_or:
         case nir_intrinsic_shared_atomic_xor:
         case nir_intrinsic_shared_atomic_exchange:
         case nir_intrinsic_shared_atomic_comp_swap:
         case nir_intrinsic_image_deref_store:
         case nir_intrinsic_image_deref_atomic_add:
         case nir_intrinsic_image_deref_atomic_umin:
         case nir_intrinsic_image_deref_atomic_umax:
         case nir_intrinsic_image_deref_atomic_imin:
         case nir_intrinsic_image_deref_atomic_imax:
         case nir_intrinsic_image_deref_atomic_and:
         case nir_intrinsic_image_deref_atomic_or:
         case nir_intrinsic_image_deref_atomic_xor:
         case nir_intrinsic_image_deref_atomic_exchange:
         case nir_intrinsic_image_deref_atomic_comp_swap:
            return true;

         default:
            /* Keep walking. */
            break;
         }
      }
   }

   return false;
}

static bool
shader_uses_view_index(nir_shader *shader)
{
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(shader);

   nir_foreach_block(block, entrypoint) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic == nir_intrinsic_load_view_index)
            return true;
      }
   }

   return false;
}

static bool
shader_only_position_uses_view_index(nir_shader *shader)
{
   nir_shader *shader_no_position = nir_shader_clone(NULL, shader);
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(shader_no_position);

   /* Remove the store position from a cloned shader. */
   nir_foreach_block(block, entrypoint) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *store = nir_instr_as_intrinsic(instr);
         if (store->intrinsic != nir_intrinsic_store_deref)
            continue;

         nir_variable *var = nir_intrinsic_get_var(store, 0);
         if (var->data.location != VARYING_SLOT_POS)
            continue;

         nir_instr_remove(&store->instr);
      }
   }

   /* Clean up shader so unused load_view_index intrinsics are removed. */
   bool progress;
   do {
      progress = false;
      progress |= nir_opt_dead_cf(shader_no_position);

      /* Peephole select will drop if-blocks that have then and else empty,
       * which will remove the usage of an SSA in the condition.
       */
      progress |= nir_opt_peephole_select(shader_no_position, 0, false, false);

      progress |= nir_opt_dce(shader_no_position);
   } while (progress);

   bool uses_view_index = shader_uses_view_index(shader_no_position);

   ralloc_free(shader_no_position);
   return !uses_view_index;
}

bool
anv_check_for_primitive_replication(nir_shader **shaders,
                                    struct anv_graphics_pipeline *pipeline)
{
   assert(pipeline->base.device->info.gen >= 12);

   static int primitive_replication_max_views = -1;
   if (primitive_replication_max_views < 0) {
      /* TODO: Figure out why we are not getting same benefits for larger than
       * 2 views.  For now use Primitive Replication just for the 2-view case
       * by default.
       */
      const unsigned default_max_views = 2;

      primitive_replication_max_views =
         MIN2(MAX_VIEWS_FOR_PRIMITIVE_REPLICATION,
              env_var_as_unsigned("ANV_PRIMITIVE_REPLICATION_MAX_VIEWS",
                                  default_max_views));
   }

   /* TODO: We should be able to support replication at 'geometry' stages
    * later than Vertex.  In that case only the last stage can refer to
    * gl_ViewIndex.
    */
   if (pipeline->active_stages != (VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT)) {
      return false;
   }

   uint32_t view_mask = pipeline->subpass->view_mask;
   int view_count = util_bitcount(view_mask);
   if (view_count == 1 || view_count > primitive_replication_max_views)
      return false;

   bool vs_writes_position = false;
   nir_foreach_shader_out_variable(var, shaders[MESA_SHADER_VERTEX]) {
      if (var->data.location == VARYING_SLOT_POS) {
         vs_writes_position = true;
         break;
      }
   }

   /* Don't bother handling this edge case with Primitive Replication. */
   if (!vs_writes_position)
      return false;

   return !shader_uses_view_index(shaders[MESA_SHADER_FRAGMENT]) &&
          !shader_writes_to_memory(shaders[MESA_SHADER_VERTEX]) &&
          shader_only_position_uses_view_index(shaders[MESA_SHADER_VERTEX]);
}
