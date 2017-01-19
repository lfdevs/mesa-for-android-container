/*
 * Copyright © 2015 Intel Corporation
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
#include "program/prog_parameter.h"
#include "nir/nir_builder.h"

struct apply_pipeline_layout_state {
   nir_shader *shader;
   nir_builder builder;

   struct anv_pipeline_layout *layout;
   bool add_bounds_checks;

   struct {
      BITSET_WORD *used;
      uint8_t *surface_offsets;
      uint8_t *sampler_offsets;
      uint8_t *image_offsets;
   } set[MAX_SETS];
};

static void
add_binding(struct apply_pipeline_layout_state *state,
            uint32_t set, uint32_t binding)
{
   BITSET_SET(state->set[set].used, binding);
}

static void
add_var_binding(struct apply_pipeline_layout_state *state, nir_variable *var)
{
   add_binding(state, var->data.descriptor_set, var->data.binding);
}

static void
get_used_bindings_block(nir_block *block,
                        struct apply_pipeline_layout_state *state)
{
   nir_foreach_instr_safe(instr, block) {
      switch (instr->type) {
      case nir_instr_type_intrinsic: {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         switch (intrin->intrinsic) {
         case nir_intrinsic_vulkan_resource_index:
            add_binding(state, nir_intrinsic_desc_set(intrin),
                        nir_intrinsic_binding(intrin));
            break;

         case nir_intrinsic_image_load:
         case nir_intrinsic_image_store:
         case nir_intrinsic_image_atomic_add:
         case nir_intrinsic_image_atomic_min:
         case nir_intrinsic_image_atomic_max:
         case nir_intrinsic_image_atomic_and:
         case nir_intrinsic_image_atomic_or:
         case nir_intrinsic_image_atomic_xor:
         case nir_intrinsic_image_atomic_exchange:
         case nir_intrinsic_image_atomic_comp_swap:
         case nir_intrinsic_image_size:
         case nir_intrinsic_image_samples:
            add_var_binding(state, intrin->variables[0]->var);
            break;

         default:
            break;
         }
         break;
      }
      case nir_instr_type_tex: {
         nir_tex_instr *tex = nir_instr_as_tex(instr);
         assert(tex->texture);
         add_var_binding(state, tex->texture->var);
         if (tex->sampler)
            add_var_binding(state, tex->sampler->var);
         break;
      }
      default:
         continue;
      }
   }
}

static void
lower_res_index_intrinsic(nir_intrinsic_instr *intrin,
                          struct apply_pipeline_layout_state *state)
{
   nir_builder *b = &state->builder;

   b->cursor = nir_before_instr(&intrin->instr);

   uint32_t set = nir_intrinsic_desc_set(intrin);
   uint32_t binding = nir_intrinsic_binding(intrin);

   uint32_t surface_index = state->set[set].surface_offsets[binding];
   uint32_t array_size =
      state->layout->set[set].layout->binding[binding].array_size;

   nir_ssa_def *block_index = nir_ssa_for_src(b, intrin->src[0], 1);

   if (state->add_bounds_checks)
      block_index = nir_umin(b, block_index, nir_imm_int(b, array_size - 1));

   block_index = nir_iadd(b, nir_imm_int(b, surface_index), block_index);

   assert(intrin->dest.is_ssa);
   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(block_index));
   nir_instr_remove(&intrin->instr);
}

static void
lower_tex_deref(nir_tex_instr *tex, nir_deref_var *deref,
                unsigned *const_index, unsigned array_size,
                nir_tex_src_type src_type,
                struct apply_pipeline_layout_state *state)
{
   nir_builder *b = &state->builder;

   if (deref->deref.child) {
      assert(deref->deref.child->deref_type == nir_deref_type_array);
      nir_deref_array *deref_array = nir_deref_as_array(deref->deref.child);

      if (deref_array->deref_array_type == nir_deref_array_type_indirect) {
         nir_ssa_def *index =
            nir_iadd(b, nir_imm_int(b, deref_array->base_offset),
                        nir_ssa_for_src(b, deref_array->indirect, 1));

         if (state->add_bounds_checks)
            index = nir_umin(b, index, nir_imm_int(b, array_size - 1));

         nir_tex_src *new_srcs = rzalloc_array(tex, nir_tex_src,
                                               tex->num_srcs + 1);

         for (unsigned i = 0; i < tex->num_srcs; i++) {
            new_srcs[i].src_type = tex->src[i].src_type;
            nir_instr_move_src(&tex->instr, &new_srcs[i].src, &tex->src[i].src);
         }

         ralloc_free(tex->src);
         tex->src = new_srcs;

         /* Now we can go ahead and move the source over to being a
          * first-class texture source.
          */
         tex->src[tex->num_srcs].src_type = src_type;
         nir_instr_rewrite_src(&tex->instr, &tex->src[tex->num_srcs].src,
                               nir_src_for_ssa(index));
         tex->num_srcs++;
      } else {
         *const_index += MIN2(deref_array->base_offset, array_size - 1);
      }
   }
}

static void
cleanup_tex_deref(nir_tex_instr *tex, nir_deref_var *deref)
{
   if (deref->deref.child == NULL)
      return;

   nir_deref_array *deref_array = nir_deref_as_array(deref->deref.child);

   if (deref_array->deref_array_type != nir_deref_array_type_indirect)
      return;

   nir_instr_rewrite_src(&tex->instr, &deref_array->indirect, NIR_SRC_INIT);
}

static void
lower_tex(nir_tex_instr *tex, struct apply_pipeline_layout_state *state)
{
   /* No one should have come by and lowered it already */
   assert(tex->texture);

   state->builder.cursor = nir_before_instr(&tex->instr);

   unsigned set = tex->texture->var->data.descriptor_set;
   unsigned binding = tex->texture->var->data.binding;
   unsigned array_size =
      state->layout->set[set].layout->binding[binding].array_size;
   tex->texture_index = state->set[set].surface_offsets[binding];
   lower_tex_deref(tex, tex->texture, &tex->texture_index, array_size,
                   nir_tex_src_texture_offset, state);

   if (tex->sampler) {
      unsigned set = tex->sampler->var->data.descriptor_set;
      unsigned binding = tex->sampler->var->data.binding;
      unsigned array_size =
         state->layout->set[set].layout->binding[binding].array_size;
      tex->sampler_index = state->set[set].sampler_offsets[binding];
      lower_tex_deref(tex, tex->sampler, &tex->sampler_index, array_size,
                      nir_tex_src_sampler_offset, state);
   }

   /* The backend only ever uses this to mark used surfaces.  We don't care
    * about that little optimization so it just needs to be non-zero.
    */
   tex->texture_array_size = 1;

   cleanup_tex_deref(tex, tex->texture);
   if (tex->sampler)
      cleanup_tex_deref(tex, tex->sampler);
   tex->texture = NULL;
   tex->sampler = NULL;
}

static void
apply_pipeline_layout_block(nir_block *block,
                            struct apply_pipeline_layout_state *state)
{
   nir_foreach_instr_safe(instr, block) {
      switch (instr->type) {
      case nir_instr_type_intrinsic: {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic == nir_intrinsic_vulkan_resource_index) {
            lower_res_index_intrinsic(intrin, state);
         }
         break;
      }
      case nir_instr_type_tex:
         lower_tex(nir_instr_as_tex(instr), state);
         break;
      default:
         continue;
      }
   }
}

static void
setup_vec4_uniform_value(const union gl_constant_value **params,
                         const union gl_constant_value *values,
                         unsigned n)
{
   static const gl_constant_value zero = { 0 };

   for (unsigned i = 0; i < n; ++i)
      params[i] = &values[i];

   for (unsigned i = n; i < 4; ++i)
      params[i] = &zero;
}

void
anv_nir_apply_pipeline_layout(struct anv_pipeline *pipeline,
                              nir_shader *shader,
                              struct brw_stage_prog_data *prog_data,
                              struct anv_pipeline_bind_map *map)
{
   struct anv_pipeline_layout *layout = pipeline->layout;

   struct apply_pipeline_layout_state state = {
      .shader = shader,
      .layout = layout,
      .add_bounds_checks = pipeline->device->robust_buffer_access,
   };

   void *mem_ctx = ralloc_context(NULL);

   for (unsigned s = 0; s < layout->num_sets; s++) {
      const unsigned count = layout->set[s].layout->binding_count;
      const unsigned words = BITSET_WORDS(count);
      state.set[s].used = rzalloc_array(mem_ctx, BITSET_WORD, words);
      state.set[s].surface_offsets = rzalloc_array(mem_ctx, uint8_t, count);
      state.set[s].sampler_offsets = rzalloc_array(mem_ctx, uint8_t, count);
      state.set[s].image_offsets = rzalloc_array(mem_ctx, uint8_t, count);
   }

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl)
         get_used_bindings_block(block, &state);
   }

   for (uint32_t set = 0; set < layout->num_sets; set++) {
      struct anv_descriptor_set_layout *set_layout = layout->set[set].layout;

      BITSET_WORD b, _tmp;
      BITSET_FOREACH_SET(b, _tmp, state.set[set].used,
                         set_layout->binding_count) {
         if (set_layout->binding[b].stage[shader->stage].surface_index >= 0)
            map->surface_count += set_layout->binding[b].array_size;
         if (set_layout->binding[b].stage[shader->stage].sampler_index >= 0)
            map->sampler_count += set_layout->binding[b].array_size;
         if (set_layout->binding[b].stage[shader->stage].image_index >= 0)
            map->image_count += set_layout->binding[b].array_size;
      }
   }

   unsigned surface = 0;
   unsigned sampler = 0;
   unsigned image = 0;
   for (uint32_t set = 0; set < layout->num_sets; set++) {
      struct anv_descriptor_set_layout *set_layout = layout->set[set].layout;

      BITSET_WORD b, _tmp;
      BITSET_FOREACH_SET(b, _tmp, state.set[set].used,
                         set_layout->binding_count) {
         unsigned array_size = set_layout->binding[b].array_size;

         if (set_layout->binding[b].stage[shader->stage].surface_index >= 0) {
            state.set[set].surface_offsets[b] = surface;
            for (unsigned i = 0; i < array_size; i++) {
               map->surface_to_descriptor[surface + i].set = set;
               map->surface_to_descriptor[surface + i].binding = b;
               map->surface_to_descriptor[surface + i].index = i;
            }
            surface += array_size;
         }

         if (set_layout->binding[b].stage[shader->stage].sampler_index >= 0) {
            state.set[set].sampler_offsets[b] = sampler;
            for (unsigned i = 0; i < array_size; i++) {
               map->sampler_to_descriptor[sampler + i].set = set;
               map->sampler_to_descriptor[sampler + i].binding = b;
               map->sampler_to_descriptor[sampler + i].index = i;
            }
            sampler += array_size;
         }

         if (set_layout->binding[b].stage[shader->stage].image_index >= 0) {
            state.set[set].image_offsets[b] = image;
            image += array_size;
         }
      }
   }

   nir_foreach_variable(var, &shader->uniforms) {
      if (!glsl_type_is_image(var->interface_type))
         continue;

      enum glsl_sampler_dim dim = glsl_get_sampler_dim(var->interface_type);
      if (dim != GLSL_SAMPLER_DIM_SUBPASS &&
          dim != GLSL_SAMPLER_DIM_SUBPASS_MS)
         continue;

      const uint32_t set = var->data.descriptor_set;
      const uint32_t binding = var->data.binding;
      const uint32_t array_size =
         layout->set[set].layout->binding[binding].array_size;

      if (!BITSET_TEST(state.set[set].used, binding))
         continue;

      struct anv_pipeline_binding *pipe_binding =
         &map->surface_to_descriptor[state.set[set].surface_offsets[binding]];
      for (unsigned i = 0; i < array_size; i++) {
         assert(pipe_binding[i].set == set);
         assert(pipe_binding[i].binding == binding);
         assert(pipe_binding[i].index == i);
         pipe_binding[i].input_attachment_index = var->data.index + i;
      }
   }

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_builder_init(&state.builder, function->impl);
      nir_foreach_block(block, function->impl)
         apply_pipeline_layout_block(block, &state);
      nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                            nir_metadata_dominance);
   }

   if (map->image_count > 0) {
      assert(map->image_count <= MAX_IMAGES);
      nir_foreach_variable(var, &shader->uniforms) {
         if (glsl_type_is_image(var->type) ||
             (glsl_type_is_array(var->type) &&
              glsl_type_is_image(glsl_get_array_element(var->type)))) {
            /* Images are represented as uniform push constants and the actual
             * information required for reading/writing to/from the image is
             * storred in the uniform.
             */
            unsigned set = var->data.descriptor_set;
            unsigned binding = var->data.binding;
            unsigned image_index = state.set[set].image_offsets[binding];

            var->data.driver_location = shader->num_uniforms +
                                        image_index * BRW_IMAGE_PARAM_SIZE * 4;
         }
      }

      struct anv_push_constants *null_data = NULL;
      const gl_constant_value **param =
         prog_data->param + (shader->num_uniforms / 4);
      const struct brw_image_param *image_param = null_data->images;
      for (uint32_t i = 0; i < map->image_count; i++) {
         setup_vec4_uniform_value(param + BRW_IMAGE_PARAM_SURFACE_IDX_OFFSET,
            (const union gl_constant_value *)&image_param->surface_idx, 1);
         setup_vec4_uniform_value(param + BRW_IMAGE_PARAM_OFFSET_OFFSET,
            (const union gl_constant_value *)image_param->offset, 2);
         setup_vec4_uniform_value(param + BRW_IMAGE_PARAM_SIZE_OFFSET,
            (const union gl_constant_value *)image_param->size, 3);
         setup_vec4_uniform_value(param + BRW_IMAGE_PARAM_STRIDE_OFFSET,
            (const union gl_constant_value *)image_param->stride, 4);
         setup_vec4_uniform_value(param + BRW_IMAGE_PARAM_TILING_OFFSET,
            (const union gl_constant_value *)image_param->tiling, 3);
         setup_vec4_uniform_value(param + BRW_IMAGE_PARAM_SWIZZLING_OFFSET,
            (const union gl_constant_value *)image_param->swizzling, 2);

         param += BRW_IMAGE_PARAM_SIZE;
         image_param ++;
      }

      shader->num_uniforms += map->image_count * BRW_IMAGE_PARAM_SIZE * 4;
   }

   ralloc_free(mem_ctx);
}
