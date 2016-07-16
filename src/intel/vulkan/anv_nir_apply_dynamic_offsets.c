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
#include "nir/nir_builder.h"

static void
apply_dynamic_offsets_block(nir_block *block, nir_builder *b,
                            const struct anv_pipeline_layout *layout,
                            bool add_bounds_checks,
                            uint32_t indices_start)
{
   struct anv_descriptor_set_layout *set_layout;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      unsigned block_idx_src;
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ssbo:
         block_idx_src = 0;
         break;
      case nir_intrinsic_store_ssbo:
         block_idx_src = 1;
         break;
      default:
         continue; /* the loop */
      }

      nir_instr *res_instr = intrin->src[block_idx_src].ssa->parent_instr;
      assert(res_instr->type == nir_instr_type_intrinsic);
      nir_intrinsic_instr *res_intrin = nir_instr_as_intrinsic(res_instr);
      assert(res_intrin->intrinsic == nir_intrinsic_vulkan_resource_index);

      unsigned set = res_intrin->const_index[0];
      unsigned binding = res_intrin->const_index[1];

      set_layout = layout->set[set].layout;
      if (set_layout->binding[binding].dynamic_offset_index < 0)
         continue;

      b->cursor = nir_before_instr(&intrin->instr);

      /* First, we need to generate the uniform load for the buffer offset */
      uint32_t index = layout->set[set].dynamic_offset_start +
                       set_layout->binding[binding].dynamic_offset_index;
      uint32_t array_size = set_layout->binding[binding].array_size;

      nir_intrinsic_instr *offset_load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_uniform);
      offset_load->num_components = 2;
      nir_intrinsic_set_base(offset_load, indices_start + index * 8);
      nir_intrinsic_set_range(offset_load, array_size * 8);
      offset_load->src[0] = nir_src_for_ssa(nir_imul(b, res_intrin->src[0].ssa,
                                                     nir_imm_int(b, 8)));

      nir_ssa_dest_init(&offset_load->instr, &offset_load->dest, 2, 32, NULL);
      nir_builder_instr_insert(b, &offset_load->instr);

      nir_src *offset_src = nir_get_io_offset_src(intrin);
      nir_ssa_def *old_offset = nir_ssa_for_src(b, *offset_src, 1);
      nir_ssa_def *new_offset = nir_iadd(b, old_offset, &offset_load->dest.ssa);
      nir_instr_rewrite_src(&intrin->instr, offset_src,
                            nir_src_for_ssa(new_offset));

      if (!add_bounds_checks)
         continue;

      /* In order to avoid out-of-bounds access, we predicate */
      nir_ssa_def *pred = nir_uge(b, nir_channel(b, &offset_load->dest.ssa, 1),
                                  old_offset);
      nir_if *if_stmt = nir_if_create(b->shader);
      if_stmt->condition = nir_src_for_ssa(pred);
      nir_cf_node_insert(b->cursor, &if_stmt->cf_node);

      nir_instr_remove(&intrin->instr);
      nir_instr_insert_after_cf_list(&if_stmt->then_list, &intrin->instr);

      if (intrin->intrinsic != nir_intrinsic_store_ssbo) {
         /* It's a load, we need a phi node */
         nir_phi_instr *phi = nir_phi_instr_create(b->shader);
         nir_ssa_dest_init(&phi->instr, &phi->dest,
                           intrin->num_components,
                           intrin->dest.ssa.bit_size, NULL);

         nir_phi_src *src1 = ralloc(phi, nir_phi_src);
         struct exec_node *tnode = exec_list_get_tail(&if_stmt->then_list);
         src1->pred = exec_node_data(nir_block, tnode, cf_node.node);
         src1->src = nir_src_for_ssa(&intrin->dest.ssa);
         exec_list_push_tail(&phi->srcs, &src1->node);

         b->cursor = nir_after_cf_list(&if_stmt->else_list);
         nir_const_value zero_val = { .u32 = { 0, 0, 0, 0 } };
         nir_ssa_def *zero = nir_build_imm(b, intrin->num_components,
                                           intrin->dest.ssa.bit_size, zero_val);

         nir_phi_src *src2 = ralloc(phi, nir_phi_src);
         struct exec_node *enode = exec_list_get_tail(&if_stmt->else_list);
         src2->pred = exec_node_data(nir_block, enode, cf_node.node);
         src2->src = nir_src_for_ssa(zero);
         exec_list_push_tail(&phi->srcs, &src2->node);

         assert(intrin->dest.is_ssa);
         nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                  nir_src_for_ssa(&phi->dest.ssa));

         nir_instr_insert_after_cf(&if_stmt->cf_node, &phi->instr);
      }
   }
}

void
anv_nir_apply_dynamic_offsets(struct anv_pipeline *pipeline,
                              nir_shader *shader,
                              struct brw_stage_prog_data *prog_data)
{
   const struct anv_pipeline_layout *layout = pipeline->layout;
   if (!layout || !layout->stage[shader->stage].has_dynamic_offsets)
      return;

   const bool add_bounds_checks = pipeline->device->robust_buffer_access;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_builder builder;
      nir_builder_init(&builder, function->impl);

      nir_foreach_block(block, function->impl) {
         apply_dynamic_offsets_block(block, &builder, pipeline->layout,
                                     add_bounds_checks, shader->num_uniforms);
      }

      nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                            nir_metadata_dominance);
   }

   struct anv_push_constants *null_data = NULL;
   for (unsigned i = 0; i < MAX_DYNAMIC_BUFFERS; i++) {
      prog_data->param[i * 2 + shader->num_uniforms / 4] =
         (const union gl_constant_value *)&null_data->dynamic[i].offset;
      prog_data->param[i * 2 + 1 + shader->num_uniforms / 4] =
         (const union gl_constant_value *)&null_data->dynamic[i].range;
   }

   shader->num_uniforms += MAX_DYNAMIC_BUFFERS * 8;
}
