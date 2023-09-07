/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "nir.h"
#include "nir_builder.h"
#include "lvp_lower_vulkan_resource.h"

static bool
lower_vulkan_resource_index(const nir_instr *instr, const void *data_cb)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_vulkan_resource_index:
      case nir_intrinsic_vulkan_resource_reindex:
      case nir_intrinsic_load_vulkan_descriptor:
      case nir_intrinsic_get_ssbo_size:
      case nir_intrinsic_image_deref_sparse_load:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_samples:
         return true;
      default:
         return false;
      }
   }
   if (instr->type == nir_instr_type_tex) {
      return true;
   }
   return false;
}

static nir_ssa_def *lower_vri_intrin_vri(struct nir_builder *b,
                                           nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   unsigned desc_set_idx = nir_intrinsic_desc_set(intrin);
   unsigned binding_idx = nir_intrinsic_binding(intrin);
   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(data_cb, desc_set_idx, binding_idx);

   return nir_vec3(b, nir_imm_int(b, desc_set_idx + 1),
                   nir_iadd_imm(b, intrin->src[0].ssa, binding->descriptor_index),
                   nir_imm_int(b, 0));
}

static nir_ssa_def *lower_vri_intrin_vrri(struct nir_builder *b,
                                          nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_ssa_def *old_index = nir_ssa_for_src(b, intrin->src[0], 3);
   nir_ssa_def *delta = nir_ssa_for_src(b, intrin->src[1], 1);
   return nir_vec3(b, nir_channel(b, old_index, 0),
                   nir_iadd(b, nir_channel(b, old_index, 1), delta),
                   nir_channel(b, old_index, 2));
}

static nir_ssa_def *lower_vri_intrin_lvd(struct nir_builder *b,
                                         nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return nir_ssa_for_src(b, intrin->src[0], 3);
}

static nir_ssa_def *
vulkan_resource_from_deref(nir_builder *b, nir_deref_instr *deref, const struct lvp_pipeline_layout *layout)
{
   nir_ssa_def *index = nir_imm_int(b, 0);

   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);
      unsigned array_size = MAX2(glsl_get_aoa_size(deref->type), 1);

      index = nir_iadd(b, index, nir_imul_imm(b, deref->arr.index.ssa, array_size));

      deref = nir_deref_instr_parent(deref);
   }

   nir_variable *var = deref->var;

   uint32_t binding_base = get_binding_layout(layout, var->data.descriptor_set, var->data.binding)->descriptor_index;

   return nir_vec3(b, nir_imm_int(b, var->data.descriptor_set + 1),
                   nir_iadd_imm(b, index, binding_base),
                   nir_imm_int(b, 0));
}

static void lower_vri_instr_tex(struct nir_builder *b,
                                nir_tex_instr *tex, void *data_cb)
{
   struct lvp_pipeline_layout *layout = data_cb;

   for (unsigned i = 0; i < tex->num_srcs; i++) {
      nir_deref_instr *deref;
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_deref:
         tex->src[i].src_type = nir_tex_src_texture_handle;
         deref = nir_src_as_deref(tex->src[i].src);
         break;
      case nir_tex_src_sampler_deref:
         tex->src[i].src_type = nir_tex_src_sampler_handle;
         deref = nir_src_as_deref(tex->src[i].src);
         break;
      default:
         continue;
      }

      nir_ssa_def *resource = vulkan_resource_from_deref(b, deref, layout);
      nir_instr_rewrite_src_ssa(&tex->instr, &tex->src[i].src, resource);
   }
}

static void
lower_image_intrinsic(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      void *data_cb)
{
   const struct lvp_pipeline_layout *layout = data_cb;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   nir_ssa_def *resource = vulkan_resource_from_deref(b, deref, layout);
   nir_rewrite_image_intrinsic(intrin, resource, true);
}

static bool
lower_load_ubo(nir_builder *b, nir_instr *instr, void *data_cb)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_ubo)
      return false;

   nir_binding binding = nir_chase_binding(intrin->src[0]);
   /* If binding.success=false, then this is a variable pointer, which we don't support with
    * VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK.
    */
   if (!binding.success)
      return false;

   const struct lvp_descriptor_set_binding_layout *bind_layout =
      get_binding_layout(data_cb, binding.desc_set, binding.binding);
   if (bind_layout->type != VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
      return false;

   b->cursor = nir_before_instr(instr);

   nir_instr_rewrite_src(instr, &intrin->src[0], nir_src_for_ssa(nir_imm_int(b, binding.desc_set + 1)));

   nir_ssa_def *offset = nir_iadd_imm(b, intrin->src[1].ssa, bind_layout->uniform_block_offset);
   nir_instr_rewrite_src(instr, &intrin->src[1], nir_src_for_ssa(offset));

   return true;
}

static nir_ssa_def *lower_vri_instr(struct nir_builder *b,
                                    nir_instr *instr, void *data_cb)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_vulkan_resource_index:
         return lower_vri_intrin_vri(b, instr, data_cb);

      case nir_intrinsic_vulkan_resource_reindex:
         return lower_vri_intrin_vrri(b, instr, data_cb);

      case nir_intrinsic_load_vulkan_descriptor:
         return lower_vri_intrin_lvd(b, instr, data_cb);

      case nir_intrinsic_get_ssbo_size: {
         /* Ignore the offset component. */
         b->cursor = nir_before_instr(instr);
         nir_ssa_def *resource = nir_ssa_for_src(b, intrin->src[0], 2);
         nir_instr_rewrite_src(&intrin->instr, &intrin->src[0],
                               nir_src_for_ssa(resource));
         return NULL;
      }
      case nir_intrinsic_image_deref_sparse_load:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_samples:
         b->cursor = nir_before_instr(instr);
         lower_image_intrinsic(b, intrin, data_cb);
         return NULL;

      default:
         return NULL;
      }
   }

   if (instr->type == nir_instr_type_tex) {
      b->cursor = nir_before_instr(instr);
      lower_vri_instr_tex(b, nir_instr_as_tex(instr), data_cb);
   }

   return NULL;
}

void lvp_lower_pipeline_layout(const struct lvp_device *device,
                               struct lvp_pipeline_layout *layout,
                               nir_shader *shader)
{
   nir_shader_instructions_pass(shader, lower_load_ubo, nir_metadata_block_index | nir_metadata_dominance, layout);
   nir_shader_lower_instructions(shader, lower_vulkan_resource_index, lower_vri_instr, layout);
}
