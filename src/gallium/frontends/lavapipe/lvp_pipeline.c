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
#include "vk_pipeline.h"
#include "vk_render_pass.h"
#include "vk_util.h"
#include "glsl_types.h"
#include "util/os_time.h"
#include "spirv/nir_spirv.h"
#include "nir/nir_builder.h"
#include "nir/nir_serialize.h"
#include "lvp_lower_vulkan_resource.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "tgsi/tgsi_from_mesa.h"
#include "nir/nir_xfb_info.h"

#define SPIR_V_MAGIC_NUMBER 0x07230203

#define MAX_DYNAMIC_STATES 72

typedef void (*cso_destroy_func)(struct pipe_context*, void*);

static void
shader_destroy(struct lvp_device *device, struct lvp_shader *shader)
{
   if (!shader->pipeline_nir)
      return;
   gl_shader_stage stage = shader->pipeline_nir->nir->info.stage;
   cso_destroy_func destroy[] = {
      device->queue.ctx->delete_vs_state,
      device->queue.ctx->delete_tcs_state,
      device->queue.ctx->delete_tes_state,
      device->queue.ctx->delete_gs_state,
      device->queue.ctx->delete_fs_state,
      device->queue.ctx->delete_compute_state,
   };
   set_foreach(&shader->inlines.variants, entry) {
      struct lvp_inline_variant *variant = (void*)entry->key;
      destroy[stage](device->queue.ctx, variant->cso);
      free(variant);
   }
   ralloc_free(shader->inlines.variants.table);
   if (shader->shader_cso)
      destroy[stage](device->queue.ctx, shader->shader_cso);
   if (shader->tess_ccw_cso)
      destroy[stage](device->queue.ctx, shader->tess_ccw_cso);

   lvp_pipeline_nir_ref(&shader->pipeline_nir, NULL);
   lvp_pipeline_nir_ref(&shader->tess_ccw, NULL);
}

void
lvp_pipeline_destroy(struct lvp_device *device, struct lvp_pipeline *pipeline)
{
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++)
      shader_destroy(device, &pipeline->shaders[i]);

   if (pipeline->layout)
      vk_pipeline_layout_unref(&device->vk, &pipeline->layout->vk);

   vk_free(&device->vk.alloc, pipeline->state_data);
   vk_object_base_finish(&pipeline->base);
   vk_free(&device->vk.alloc, pipeline);
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyPipeline(
   VkDevice                                    _device,
   VkPipeline                                  _pipeline,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   if (pipeline->used) {
      simple_mtx_lock(&device->queue.pipeline_lock);
      util_dynarray_append(&device->queue.pipeline_destroys, struct lvp_pipeline*, pipeline);
      simple_mtx_unlock(&device->queue.pipeline_lock);
   } else {
      lvp_pipeline_destroy(device, pipeline);
   }
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
      *align = comp_size;
}

static void
set_image_access(struct lvp_shader *shader, struct lvp_pipeline_layout *layout, nir_shader *nir,
                   nir_intrinsic_instr *instr,
                   bool reads, bool writes)
{
   nir_variable *var = nir_intrinsic_get_var(instr, 0);
   /* calculate the variable's offset in the layout */
   uint64_t value = 0;
   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(layout, var->data.descriptor_set, var->data.binding);
   for (unsigned s = 0; s < var->data.descriptor_set; s++) {
     if (layout->vk.set_layouts[s])
        value += get_set_layout(layout, s)->stage[nir->info.stage].image_count;
   }
   value += binding->stage[nir->info.stage].image_index;
   const unsigned size = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1;
   uint64_t mask = BITFIELD64_MASK(MAX2(size, 1)) << value;

   if (reads)
      shader->access.images_read |= mask;
   if (writes)
      shader->access.images_written |= mask;
}

static void
set_buffer_access(struct lvp_shader *shader, struct lvp_pipeline_layout *layout, nir_shader *nir,
                    nir_intrinsic_instr *instr)
{
   nir_variable *var = nir_intrinsic_get_var(instr, 0);
   if (!var) {
      nir_deref_instr *deref = nir_instr_as_deref(instr->src[0].ssa->parent_instr);
      if (deref->modes != nir_var_mem_ssbo)
         return;
      nir_binding b = nir_chase_binding(instr->src[0]);
      var = nir_get_binding_variable(nir, b);
      if (!var)
         return;
   }
   if (var->data.mode != nir_var_mem_ssbo)
      return;
   /* calculate the variable's offset in the layout */
   uint64_t value = 0;
   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(layout, var->data.descriptor_set, var->data.binding);
   for (unsigned s = 0; s < var->data.descriptor_set; s++) {
     if (layout->vk.set_layouts[s])
        value += get_set_layout(layout, s)->stage[nir->info.stage].shader_buffer_count;
   }
   value += binding->stage[nir->info.stage].shader_buffer_index;
   /* Structs have been lowered already, so get_aoa_size is sufficient. */
   const unsigned size = glsl_type_is_array(var->type) ? glsl_get_aoa_size(var->type) : 1;
   uint64_t mask = BITFIELD64_MASK(MAX2(size, 1)) << value;
   shader->access.buffers_written |= mask;
}

static void
scan_intrinsic(struct lvp_shader *shader, struct lvp_pipeline_layout *layout, nir_shader *nir, nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_image_deref_sparse_load:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
      set_image_access(shader, layout, nir, instr, true, false);
      break;
   case nir_intrinsic_image_deref_store:
      set_image_access(shader, layout, nir, instr, false, true);
      break;
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_fadd:
      set_image_access(shader, layout, nir, instr, true, true);
      break;
   case nir_intrinsic_deref_atomic_add:
   case nir_intrinsic_deref_atomic_and:
   case nir_intrinsic_deref_atomic_comp_swap:
   case nir_intrinsic_deref_atomic_exchange:
   case nir_intrinsic_deref_atomic_fadd:
   case nir_intrinsic_deref_atomic_fcomp_swap:
   case nir_intrinsic_deref_atomic_fmax:
   case nir_intrinsic_deref_atomic_fmin:
   case nir_intrinsic_deref_atomic_imax:
   case nir_intrinsic_deref_atomic_imin:
   case nir_intrinsic_deref_atomic_or:
   case nir_intrinsic_deref_atomic_umax:
   case nir_intrinsic_deref_atomic_umin:
   case nir_intrinsic_deref_atomic_xor:
   case nir_intrinsic_store_deref:
      set_buffer_access(shader, layout, nir, instr);
      break;
   default: break;
   }
}

static void
scan_pipeline_info(struct lvp_shader *shader, struct lvp_pipeline_layout *layout, nir_shader *nir)
{
   nir_foreach_function(function, nir) {
      if (function->impl)
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  scan_intrinsic(shader, layout, nir, nir_instr_as_intrinsic(instr));
            }
         }
   }

}

static bool
remove_scoped_barriers_impl(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_scoped_barrier)
      return false;
   if (data) {
      if (nir_intrinsic_memory_scope(intr) == NIR_SCOPE_WORKGROUP ||
          nir_intrinsic_memory_scope(intr) == NIR_SCOPE_DEVICE ||
          nir_intrinsic_memory_scope(intr) == NIR_SCOPE_QUEUE_FAMILY)
         return false;
   }
   nir_instr_remove(instr);
   return true;
}

static bool
remove_scoped_barriers(nir_shader *nir, bool is_compute)
{
   return nir_shader_instructions_pass(nir, remove_scoped_barriers_impl, nir_metadata_dominance, (void*)is_compute);
}

static bool
lower_demote_impl(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic == nir_intrinsic_demote || intr->intrinsic == nir_intrinsic_terminate) {
      intr->intrinsic = nir_intrinsic_discard;
      return true;
   }
   if (intr->intrinsic == nir_intrinsic_demote_if || intr->intrinsic == nir_intrinsic_terminate_if) {
      intr->intrinsic = nir_intrinsic_discard_if;
      return true;
   }
   return false;
}

static bool
lower_demote(nir_shader *nir)
{
   return nir_shader_instructions_pass(nir, lower_demote_impl, nir_metadata_dominance, NULL);
}

static bool
find_tex(const nir_instr *instr, const void *data_cb)
{
   if (instr->type == nir_instr_type_tex)
      return true;
   return false;
}

static nir_ssa_def *
fixup_tex_instr(struct nir_builder *b, nir_instr *instr, void *data_cb)
{
   nir_tex_instr *tex_instr = nir_instr_as_tex(instr);
   unsigned offset = 0;

   int idx = nir_tex_instr_src_index(tex_instr, nir_tex_src_texture_offset);
   if (idx == -1)
      return NULL;

   if (!nir_src_is_const(tex_instr->src[idx].src))
      return NULL;
   offset = nir_src_comp_as_uint(tex_instr->src[idx].src, 0);

   nir_tex_instr_remove_src(tex_instr, idx);
   tex_instr->texture_index += offset;
   return NIR_LOWER_INSTR_PROGRESS;
}

static bool
lvp_nir_fixup_indirect_tex(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader, find_tex, fixup_tex_instr, NULL);
}

static void
optimize(nir_shader *nir)
{
   bool progress = false;
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_lower_flrp, 32|64, true);
      NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_remove_phis);
      bool trivial_continues = false;
      NIR_PASS(trivial_continues, nir, nir_opt_trivial_continues);
      progress |= trivial_continues;
      if (trivial_continues) {
         /* If nir_opt_trivial_continues makes progress, then we need to clean
          * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
          * to make progress.
          */
         NIR_PASS(progress, nir, nir_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
         NIR_PASS(progress, nir, nir_opt_remove_phis);
      }
      NIR_PASS(progress, nir, nir_opt_if, nir_opt_if_aggressive_last_continue | nir_opt_if_optimize_phi_true_false);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_undef);

      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);
      NIR_PASS(progress, nir, lvp_nir_fixup_indirect_tex);
   } while (progress);
}

void
lvp_shader_optimize(nir_shader *nir)
{
   optimize(nir);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, nir_opt_dce);
   nir_sweep(nir);
}

static struct lvp_pipeline_nir *
create_pipeline_nir(nir_shader *nir)
{
   struct lvp_pipeline_nir *pipeline_nir = ralloc(NULL, struct lvp_pipeline_nir);
   pipeline_nir->nir = nir;
   pipeline_nir->ref_cnt = 1;
   return pipeline_nir;
}

static VkResult
compile_spirv(struct lvp_device *pdevice, const VkPipelineShaderStageCreateInfo *sinfo, nir_shader **nir)
{
   gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);
   assert(stage <= MESA_SHADER_COMPUTE && stage != MESA_SHADER_NONE);
   VkResult result;

   const struct spirv_to_nir_options spirv_options = {
      .environment = NIR_SPIRV_VULKAN,
      .caps = {
         .float64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DOUBLES) == 1),
         .int16 = true,
         .int64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_INT64) == 1),
         .tessellation = true,
         .float_controls = true,
         .float32_atomic_add = true,
#if LLVM_VERSION_MAJOR >= 15
         .float32_atomic_min_max = true,
#endif
         .image_ms_array = true,
         .image_read_without_format = true,
         .image_write_without_format = true,
         .storage_image_ms = true,
         .geometry_streams = true,
         .storage_8bit = true,
         .storage_16bit = true,
         .variable_pointers = true,
         .stencil_export = true,
         .post_depth_coverage = true,
         .transform_feedback = true,
         .device_group = true,
         .draw_parameters = true,
         .shader_viewport_index_layer = true,
         .shader_clock = true,
         .multiview = true,
         .physical_storage_buffer_address = true,
         .int64_atomics = true,
         .subgroup_arithmetic = true,
         .subgroup_basic = true,
         .subgroup_ballot = true,
         .subgroup_quad = true,
#if LLVM_VERSION_MAJOR >= 10
         .subgroup_shuffle = true,
#endif
         .subgroup_vote = true,
         .vk_memory_model = true,
         .vk_memory_model_device_scope = true,
         .int8 = true,
         .float16 = true,
         .demote_to_helper_invocation = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_logical,
      .shared_addr_format = nir_address_format_32bit_offset,
   };

   result = vk_pipeline_shader_stage_to_nir(&pdevice->vk, sinfo,
                                            &spirv_options, pdevice->physical_device->drv_options[stage],
                                            NULL, nir);
   return result;
}

static bool
inline_variant_equals(const void *a, const void *b)
{
   const struct lvp_inline_variant *av = a, *bv = b;
   assert(av->mask == bv->mask);
   u_foreach_bit(slot, av->mask) {
      if (memcmp(av->vals[slot], bv->vals[slot], sizeof(av->vals[slot])))
         return false;
   }
   return true;
}

static void
lvp_shader_lower(struct lvp_device *pdevice, nir_shader *nir, struct lvp_shader *shader, struct lvp_pipeline_layout *layout)
{
   if (nir->info.stage != MESA_SHADER_TESS_CTRL)
      NIR_PASS_V(nir, remove_scoped_barriers, nir->info.stage == MESA_SHADER_COMPUTE);

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .frag_coord = true,
      .point_coord = true,
   };
   NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

   struct nir_lower_subgroups_options subgroup_opts = {0};
   subgroup_opts.lower_quad = true;
   subgroup_opts.ballot_components = 1;
   subgroup_opts.ballot_bit_size = 32;
   NIR_PASS_V(nir, nir_lower_subgroups, &subgroup_opts);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      lvp_lower_input_attachments(nir, false);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_is_helper_invocation);
   NIR_PASS_V(nir, lower_demote);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_uniform | nir_var_image, NULL);

   scan_pipeline_info(shader, layout, nir);

   optimize(nir);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   lvp_lower_pipeline_layout(pdevice, layout, nir);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_global,
              nir_address_format_64bit_global);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared, shared_var_info);
      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared, nir_address_format_32bit_offset);
   }

   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);

   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, true);
   }

   // TODO: also optimize the tex srcs. see radeonSI for reference */
   /* Skip if there are potentially conflicting rounding modes */
   struct nir_fold_16bit_tex_image_options fold_16bit_options = {
      .rounding_mode = nir_rounding_mode_undef,
      .fold_tex_dest_types = nir_type_float | nir_type_uint | nir_type_int,
   };
   NIR_PASS_V(nir, nir_fold_16bit_tex_image, &fold_16bit_options);

   lvp_shader_optimize(nir);

   if (nir->info.stage != MESA_SHADER_VERTEX)
      nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, nir->info.stage);
   else {
      nir->num_inputs = util_last_bit64(nir->info.inputs_read);
      nir_foreach_shader_in_variable(var, nir) {
         var->data.driver_location = var->data.location - VERT_ATTRIB_GENERIC0;
      }
   }
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                               nir->info.stage);

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   if (impl->ssa_alloc > 100) //skip for small shaders
      shader->inlines.must_inline = lvp_find_inlinable_uniforms(shader, nir);
   shader->pipeline_nir = create_pipeline_nir(nir);
   if (shader->inlines.can_inline)
      _mesa_set_init(&shader->inlines.variants, NULL, NULL, inline_variant_equals);
}

static VkResult
lvp_shader_compile_to_ir(struct lvp_pipeline *pipeline,
                         const VkPipelineShaderStageCreateInfo *sinfo)
{
   struct lvp_device *pdevice = pipeline->device;
   gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);
   assert(stage <= MESA_SHADER_COMPUTE && stage != MESA_SHADER_NONE);
   struct lvp_shader *shader = &pipeline->shaders[stage];
   nir_shader *nir;
   VkResult result = compile_spirv(pdevice, sinfo, &nir);
   if (result == VK_SUCCESS)
      lvp_shader_lower(pdevice, nir, shader, pipeline->layout);
   return result;
}

static void
merge_tess_info(struct shader_info *tes_info,
                const struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 ||
          tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == 0 ||
          tes_info->tess._primitive_mode == 0 ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;
}

static void
lvp_shader_xfb_init(struct lvp_shader *shader)
{
   nir_xfb_info *xfb_info = shader->pipeline_nir->nir->xfb_info;
   if (xfb_info) {
      uint8_t output_mapping[VARYING_SLOT_TESS_MAX];
      memset(output_mapping, 0, sizeof(output_mapping));

      nir_foreach_shader_out_variable(var, shader->pipeline_nir->nir) {
         unsigned slots = var->data.compact ? DIV_ROUND_UP(glsl_get_length(var->type), 4)
                                            : glsl_count_attribute_slots(var->type, false);
         for (unsigned i = 0; i < slots; i++)
            output_mapping[var->data.location + i] = var->data.driver_location + i;
      }

      shader->stream_output.num_outputs = xfb_info->output_count;
      for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
         if (xfb_info->buffers_written & (1 << i)) {
            shader->stream_output.stride[i] = xfb_info->buffers[i].stride / 4;
         }
      }
      for (unsigned i = 0; i < xfb_info->output_count; i++) {
         shader->stream_output.output[i].output_buffer = xfb_info->outputs[i].buffer;
         shader->stream_output.output[i].dst_offset = xfb_info->outputs[i].offset / 4;
         shader->stream_output.output[i].register_index = output_mapping[xfb_info->outputs[i].location];
         shader->stream_output.output[i].num_components = util_bitcount(xfb_info->outputs[i].component_mask);
         shader->stream_output.output[i].start_component = ffs(xfb_info->outputs[i].component_mask) - 1;
         shader->stream_output.output[i].stream = xfb_info->buffer_to_stream[xfb_info->outputs[i].buffer];
      }

   }
}

static void
lvp_pipeline_xfb_init(struct lvp_pipeline *pipeline)
{
   gl_shader_stage stage = MESA_SHADER_VERTEX;
   if (pipeline->shaders[MESA_SHADER_GEOMETRY].pipeline_nir)
      stage = MESA_SHADER_GEOMETRY;
   else if (pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir)
      stage = MESA_SHADER_TESS_EVAL;
   pipeline->last_vertex = stage;
   lvp_shader_xfb_init(&pipeline->shaders[stage]);
}

void *
lvp_shader_compile_stage(struct lvp_device *device, struct lvp_shader *shader, nir_shader *nir)
{
   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      struct pipe_compute_state shstate = {0};
      shstate.prog = nir;
      shstate.ir_type = PIPE_SHADER_IR_NIR;
      shstate.static_shared_mem = nir->info.shared_size;
      return device->queue.ctx->create_compute_state(device->queue.ctx, &shstate);
   } else {
      struct pipe_shader_state shstate = {0};
      shstate.type = PIPE_SHADER_IR_NIR;
      shstate.ir.nir = nir;
      memcpy(&shstate.stream_output, &shader->stream_output, sizeof(shstate.stream_output));

      switch (nir->info.stage) {
      case MESA_SHADER_FRAGMENT:
         return device->queue.ctx->create_fs_state(device->queue.ctx, &shstate);
      case MESA_SHADER_VERTEX:
         return device->queue.ctx->create_vs_state(device->queue.ctx, &shstate);
      case MESA_SHADER_GEOMETRY:
         return device->queue.ctx->create_gs_state(device->queue.ctx, &shstate);
      case MESA_SHADER_TESS_CTRL:
         return device->queue.ctx->create_tcs_state(device->queue.ctx, &shstate);
      case MESA_SHADER_TESS_EVAL:
         return device->queue.ctx->create_tes_state(device->queue.ctx, &shstate);
      default:
         unreachable("illegal shader");
         break;
      }
   }
   return NULL;
}

void *
lvp_shader_compile(struct lvp_device *device, struct lvp_shader *shader, nir_shader *nir)
{
   device->physical_device->pscreen->finalize_nir(device->physical_device->pscreen, nir);
   return lvp_shader_compile_stage(device, shader, nir);
}

#ifndef NDEBUG
static bool
layouts_equal(const struct lvp_descriptor_set_layout *a, const struct lvp_descriptor_set_layout *b)
{
   const uint8_t *pa = (const uint8_t*)a, *pb = (const uint8_t*)b;
   uint32_t hash_start_offset = sizeof(struct vk_descriptor_set_layout);
   uint32_t binding_offset = offsetof(struct lvp_descriptor_set_layout, binding);
   /* base equal */
   if (memcmp(pa + hash_start_offset, pb + hash_start_offset, binding_offset - hash_start_offset))
      return false;

   /* bindings equal */
   if (a->binding_count != b->binding_count)
      return false;
   size_t binding_size = a->binding_count * sizeof(struct lvp_descriptor_set_binding_layout);
   const struct lvp_descriptor_set_binding_layout *la = a->binding;
   const struct lvp_descriptor_set_binding_layout *lb = b->binding;
   if (memcmp(la, lb, binding_size)) {
      for (unsigned i = 0; i < a->binding_count; i++) {
         if (memcmp(&la[i], &lb[i], offsetof(struct lvp_descriptor_set_binding_layout, immutable_samplers)))
            return false;
      }
   }

   /* immutable sampler equal */
   if (a->immutable_sampler_count != b->immutable_sampler_count)
      return false;
   if (a->immutable_sampler_count) {
      size_t sampler_size = a->immutable_sampler_count * sizeof(struct lvp_sampler *);
      if (memcmp(pa + binding_offset + binding_size, pb + binding_offset + binding_size, sampler_size)) {
         struct lvp_sampler **sa = (struct lvp_sampler **)(pa + binding_offset);
         struct lvp_sampler **sb = (struct lvp_sampler **)(pb + binding_offset);
         for (unsigned i = 0; i < a->immutable_sampler_count; i++) {
            if (memcmp(sa[i], sb[i], sizeof(struct lvp_sampler)))
               return false;
         }
      }
   }
   return true;
}
#endif

static void
merge_layouts(struct vk_device *device, struct lvp_pipeline *dst, struct lvp_pipeline_layout *src)
{
   if (!src)
      return;
   if (dst->layout) {
      /* these must match */
      ASSERTED VkPipelineCreateFlags src_flag = src->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
      ASSERTED VkPipelineCreateFlags dst_flag = dst->layout->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
      assert(src_flag == dst_flag);
   }
   /* always try to reuse existing layout: independent sets bit doesn't guarantee independent sets */
   if (!dst->layout) {
      dst->layout = (struct lvp_pipeline_layout*)vk_pipeline_layout_ref(&src->vk);
      return;
   }
   /* this is a big optimization when hit */
   if (dst->layout == src)
      return;
#ifndef NDEBUG
   /* verify that layouts match */
   const struct lvp_pipeline_layout *smaller = dst->layout->vk.set_count < src->vk.set_count ? dst->layout : src;
   const struct lvp_pipeline_layout *bigger = smaller == dst->layout ? src : dst->layout;
   for (unsigned i = 0; i < smaller->vk.set_count; i++) {
      if (!smaller->vk.set_layouts[i] || !bigger->vk.set_layouts[i] ||
          smaller->vk.set_layouts[i] == bigger->vk.set_layouts[i])
         continue;

      const struct lvp_descriptor_set_layout *smaller_set_layout =
         vk_to_lvp_descriptor_set_layout(smaller->vk.set_layouts[i]);
      const struct lvp_descriptor_set_layout *bigger_set_layout =
         vk_to_lvp_descriptor_set_layout(bigger->vk.set_layouts[i]);

      assert(!smaller_set_layout->binding_count ||
             !bigger_set_layout->binding_count ||
             layouts_equal(smaller_set_layout, bigger_set_layout));
   }
#endif
   /* must be independent sets with different layouts: reallocate to avoid modifying original layout */
   struct lvp_pipeline_layout *old_layout = dst->layout;
   dst->layout = vk_zalloc(&device->alloc, sizeof(struct lvp_pipeline_layout), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   memcpy(dst->layout, old_layout, sizeof(struct lvp_pipeline_layout));
   dst->layout->vk.ref_cnt = 1;
   for (unsigned i = 0; i < dst->layout->vk.set_count; i++) {
      if (dst->layout->vk.set_layouts[i])
         vk_descriptor_set_layout_ref(dst->layout->vk.set_layouts[i]);
   }
   vk_pipeline_layout_unref(device, &old_layout->vk);

   for (unsigned i = 0; i < src->vk.set_count; i++) {
      if (!dst->layout->vk.set_layouts[i]) {
         dst->layout->vk.set_layouts[i] = src->vk.set_layouts[i];
         if (dst->layout->vk.set_layouts[i])
            vk_descriptor_set_layout_ref(src->vk.set_layouts[i]);
      }
   }
   dst->layout->vk.set_count = MAX2(dst->layout->vk.set_count,
                                    src->vk.set_count);
   dst->layout->push_constant_size += src->push_constant_size;
   dst->layout->push_constant_stages |= src->push_constant_stages;
}

static void
copy_shader_sanitized(struct lvp_shader *dst, const struct lvp_shader *src)
{
   *dst = *src;
   dst->pipeline_nir = NULL; //this gets handled later
   dst->tess_ccw = NULL; //this gets handled later
   assert(!dst->shader_cso);
   assert(!dst->tess_ccw_cso);
   if (src->inlines.can_inline)
      _mesa_set_init(&dst->inlines.variants, NULL, NULL, inline_variant_equals);
}

static VkResult
lvp_graphics_pipeline_init(struct lvp_pipeline *pipeline,
                           struct lvp_device *device,
                           struct lvp_pipeline_cache *cache,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   VkResult result;

   const VkGraphicsPipelineLibraryCreateInfoEXT *libinfo = vk_find_struct_const(pCreateInfo,
                                                                                GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT);
   const VkPipelineLibraryCreateInfoKHR *libstate = vk_find_struct_const(pCreateInfo,
                                                                         PIPELINE_LIBRARY_CREATE_INFO_KHR);
   const VkGraphicsPipelineLibraryFlagsEXT layout_stages = VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                                                           VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
   if (libinfo)
      pipeline->stages = libinfo->flags;
   else if (!libstate)
      pipeline->stages = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
                         VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
      pipeline->library = true;

   struct lvp_pipeline_layout *layout = lvp_pipeline_layout_from_handle(pCreateInfo->layout);

   if (!layout || !(layout->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT))
      /* this is a regular pipeline with no partials: directly reuse */
      pipeline->layout = layout ? (void*)vk_pipeline_layout_ref(&layout->vk) : NULL;
   else if (pipeline->stages & layout_stages) {
      if ((pipeline->stages & layout_stages) == layout_stages)
         /* this has all the layout stages: directly reuse */
         pipeline->layout = (void*)vk_pipeline_layout_ref(&layout->vk);
      else {
         /* this is a partial: copy for later merging to avoid modifying another layout */
         merge_layouts(&device->vk, pipeline, layout);
      }
   }

   if (libstate) {
      for (unsigned i = 0; i < libstate->libraryCount; i++) {
         LVP_FROM_HANDLE(lvp_pipeline, p, libstate->pLibraries[i]);
         vk_graphics_pipeline_state_merge(&pipeline->graphics_state,
                                          &p->graphics_state);
         if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
            pipeline->line_smooth = p->line_smooth;
            pipeline->disable_multisample = p->disable_multisample;
            pipeline->line_rectangular = p->line_rectangular;
            memcpy(pipeline->shaders, p->shaders, sizeof(struct lvp_shader) * 4);
            for (unsigned i = 0; i < MESA_SHADER_COMPUTE; i++) {
               if (i == MESA_SHADER_FRAGMENT)
                  continue;
               copy_shader_sanitized(&pipeline->shaders[i], &p->shaders[i]);
            }
         }
         if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
            pipeline->force_min_sample = p->force_min_sample;
            copy_shader_sanitized(&pipeline->shaders[MESA_SHADER_FRAGMENT], &p->shaders[MESA_SHADER_FRAGMENT]);
         }
         if (p->stages & layout_stages) {
            if (!layout || (layout->vk.create_flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT))
               merge_layouts(&device->vk, pipeline, p->layout);
         }
         pipeline->stages |= p->stages;
      }
   }

   result = vk_graphics_pipeline_state_fill(&device->vk,
                                            &pipeline->graphics_state,
                                            pCreateInfo, NULL, NULL, NULL,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT,
                                            &pipeline->state_data);
   if (result != VK_SUCCESS)
      return result;

   assert(pipeline->library || pipeline->stages == (VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                                                    VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |
                                                    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
                                                    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT));

   pipeline->device = device;

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];
      gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);
      if (stage == MESA_SHADER_FRAGMENT) {
         if (!(pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT))
            continue;
      } else {
         if (!(pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT))
            continue;
      }
      result = lvp_shader_compile_to_ir(pipeline, sinfo);
      if (result != VK_SUCCESS)
         goto fail;

      switch (stage) {
      case MESA_SHADER_FRAGMENT:
         if (pipeline->shaders[MESA_SHADER_FRAGMENT].pipeline_nir->nir->info.fs.uses_sample_shading)
            pipeline->force_min_sample = true;
         break;
      default: break;
      }
   }
   if (pCreateInfo->stageCount && pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir) {
      nir_lower_patch_vertices(pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir, pipeline->shaders[MESA_SHADER_TESS_CTRL].pipeline_nir->nir->info.tess.tcs_vertices_out, NULL);
      merge_tess_info(&pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info, &pipeline->shaders[MESA_SHADER_TESS_CTRL].pipeline_nir->nir->info);
      if (BITSET_TEST(pipeline->graphics_state.dynamic,
                      MESA_VK_DYNAMIC_TS_DOMAIN_ORIGIN)) {
         pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw = create_pipeline_nir(nir_shader_clone(NULL, pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir));
         pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw->nir->info.tess.ccw = !pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info.tess.ccw;
      } else if (pipeline->graphics_state.ts->domain_origin == VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT) {
         pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info.tess.ccw = !pipeline->shaders[MESA_SHADER_TESS_EVAL].pipeline_nir->nir->info.tess.ccw;
      }
   }
   if (libstate) {
       for (unsigned i = 0; i < libstate->libraryCount; i++) {
          LVP_FROM_HANDLE(lvp_pipeline, p, libstate->pLibraries[i]);
          if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) {
             if (p->shaders[MESA_SHADER_FRAGMENT].pipeline_nir)
                lvp_pipeline_nir_ref(&pipeline->shaders[MESA_SHADER_FRAGMENT].pipeline_nir, p->shaders[MESA_SHADER_FRAGMENT].pipeline_nir);
          }
          if (p->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
             for (unsigned j = MESA_SHADER_VERTEX; j < MESA_SHADER_FRAGMENT; j++) {
                if (p->shaders[j].pipeline_nir)
                   lvp_pipeline_nir_ref(&pipeline->shaders[j].pipeline_nir, p->shaders[j].pipeline_nir);
             }
             if (p->shaders[MESA_SHADER_TESS_EVAL].tess_ccw)
                lvp_pipeline_nir_ref(&pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw, p->shaders[MESA_SHADER_TESS_EVAL].tess_ccw);
          }
       }
   } else if (pipeline->stages & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT) {
      const struct vk_rasterization_state *rs = pipeline->graphics_state.rs;
      if (rs) {
         /* always draw bresenham if !smooth */
         pipeline->line_smooth = rs->line.mode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
         pipeline->disable_multisample = rs->line.mode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT ||
                                         rs->line.mode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
         pipeline->line_rectangular = rs->line.mode != VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
      } else
         pipeline->line_rectangular = true;
      lvp_pipeline_xfb_init(pipeline);
   }
   if (!libstate && !pipeline->library)
      lvp_pipeline_shaders_compile(pipeline);

   return VK_SUCCESS;

fail:
   for (unsigned i = 0; i < ARRAY_SIZE(pipeline->shaders); i++) {
      lvp_pipeline_nir_ref(&pipeline->shaders[i].pipeline_nir, NULL);
   }
   vk_free(&device->vk.alloc, pipeline->state_data);

   return result;
}

void
lvp_pipeline_shaders_compile(struct lvp_pipeline *pipeline)
{
   if (pipeline->compiled)
      return;
   for (uint32_t i = 0; i < ARRAY_SIZE(pipeline->shaders); i++) {
      if (!pipeline->shaders[i].pipeline_nir)
         continue;

      gl_shader_stage stage = i;
      assert(stage == pipeline->shaders[i].pipeline_nir->nir->info.stage);

      if (!pipeline->shaders[stage].inlines.can_inline) {
         pipeline->shaders[stage].shader_cso = lvp_shader_compile(pipeline->device, &pipeline->shaders[stage],
                                                            nir_shader_clone(NULL, pipeline->shaders[stage].pipeline_nir->nir));
         if (pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw)
            pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw_cso = lvp_shader_compile(pipeline->device, &pipeline->shaders[stage],
                                                          nir_shader_clone(NULL, pipeline->shaders[MESA_SHADER_TESS_EVAL].tess_ccw->nir));
      }
   }
   pipeline->compiled = true;
}

static VkResult
lvp_graphics_pipeline_create(
   VkDevice _device,
   VkPipelineCache _cache,
   const VkGraphicsPipelineCreateInfo *pCreateInfo,
   VkPipeline *pPipeline)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline_cache, cache, _cache);
   struct lvp_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc(&device->vk.alloc, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   uint64_t t0 = os_time_get_nano();
   result = lvp_graphics_pipeline_init(pipeline, device, cache, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, pipeline);
      return result;
   }

   VkPipelineCreationFeedbackCreateInfo *feedback = (void*)vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (feedback) {
      feedback->pPipelineCreationFeedback->duration = os_time_get_nano() - t0;
      feedback->pPipelineCreationFeedback->flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      memset(feedback->pPipelineStageCreationFeedbacks, 0, sizeof(VkPipelineCreationFeedback) * feedback->pipelineStageCreationFeedbackCount);
   }

   *pPipeline = lvp_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateGraphicsPipelines(
   VkDevice                                    _device,
   VkPipelineCache                             pipelineCache,
   uint32_t                                    count,
   const VkGraphicsPipelineCreateInfo*         pCreateInfos,
   const VkAllocationCallbacks*                pAllocator,
   VkPipeline*                                 pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r = VK_PIPELINE_COMPILE_REQUIRED;
      if (!(pCreateInfos[i].flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT))
         r = lvp_graphics_pipeline_create(_device,
                                          pipelineCache,
                                          &pCreateInfos[i],
                                          &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;
         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }
   if (result != VK_SUCCESS) {
      for (; i < count; i++)
         pPipelines[i] = VK_NULL_HANDLE;
   }

   return result;
}

static VkResult
lvp_compute_pipeline_init(struct lvp_pipeline *pipeline,
                          struct lvp_device *device,
                          struct lvp_pipeline_cache *cache,
                          const VkComputePipelineCreateInfo *pCreateInfo)
{
   pipeline->device = device;
   pipeline->layout = lvp_pipeline_layout_from_handle(pCreateInfo->layout);
   vk_pipeline_layout_ref(&pipeline->layout->vk);
   pipeline->force_min_sample = false;

   pipeline->is_compute_pipeline = true;

   VkResult result = lvp_shader_compile_to_ir(pipeline, &pCreateInfo->stage);
   if (result != VK_SUCCESS)
      return result;

   struct lvp_shader *shader = &pipeline->shaders[MESA_SHADER_COMPUTE];
   if (!shader->inlines.can_inline)
      shader->shader_cso = lvp_shader_compile(pipeline->device, shader, nir_shader_clone(NULL, shader->pipeline_nir->nir));
   pipeline->compiled = true;
   return VK_SUCCESS;
}

static VkResult
lvp_compute_pipeline_create(
   VkDevice _device,
   VkPipelineCache _cache,
   const VkComputePipelineCreateInfo *pCreateInfo,
   VkPipeline *pPipeline)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline_cache, cache, _cache);
   struct lvp_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc(&device->vk.alloc, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   uint64_t t0 = os_time_get_nano();
   result = lvp_compute_pipeline_init(pipeline, device, cache, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, pipeline);
      return result;
   }

   const VkPipelineCreationFeedbackCreateInfo *feedback = (void*)vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   if (feedback) {
      feedback->pPipelineCreationFeedback->duration = os_time_get_nano() - t0;
      feedback->pPipelineCreationFeedback->flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      memset(feedback->pPipelineStageCreationFeedbacks, 0, sizeof(VkPipelineCreationFeedback) * feedback->pipelineStageCreationFeedbackCount);
   }

   *pPipeline = lvp_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateComputePipelines(
   VkDevice                                    _device,
   VkPipelineCache                             pipelineCache,
   uint32_t                                    count,
   const VkComputePipelineCreateInfo*          pCreateInfos,
   const VkAllocationCallbacks*                pAllocator,
   VkPipeline*                                 pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r = VK_PIPELINE_COMPILE_REQUIRED;
      if (!(pCreateInfos[i].flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT))
         r = lvp_compute_pipeline_create(_device,
                                         pipelineCache,
                                         &pCreateInfos[i],
                                         &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;
         if (pCreateInfos[i].flags & VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }
   if (result != VK_SUCCESS) {
      for (; i < count; i++)
         pPipelines[i] = VK_NULL_HANDLE;
   }


   return result;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyShaderEXT(
    VkDevice                                    _device,
    VkShaderEXT                                 _shader,
    const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_shader, shader, _shader);

   if (!shader)
      return;
   shader_destroy(device, shader);

   vk_pipeline_layout_unref(&device->vk, &shader->layout->vk);
   blob_finish(&shader->blob);
   vk_object_base_finish(&shader->base);
   vk_free2(&device->vk.alloc, pAllocator, shader);
}

static VkShaderEXT
create_shader_object(struct lvp_device *device, const VkShaderCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator)
{
   nir_shader *nir = NULL;
   gl_shader_stage stage = vk_to_mesa_shader_stage(pCreateInfo->stage);
   assert(stage <= MESA_SHADER_COMPUTE && stage != MESA_SHADER_NONE);
   if (pCreateInfo->codeType == VK_SHADER_CODE_TYPE_SPIRV_EXT) {
      VkShaderModuleCreateInfo minfo = {
         VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
         NULL,
         0,
         pCreateInfo->codeSize,
         pCreateInfo->pCode,
      };
      VkPipelineShaderStageCreateFlagBits flags = 0;
      if (pCreateInfo->flags & VK_SHADER_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT_EXT)
         flags |= VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT;
      if (pCreateInfo->flags & VK_SHADER_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT)
         flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT;
      VkPipelineShaderStageCreateInfo sinfo = {
         VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         &minfo,
         flags,
         pCreateInfo->stage,
         VK_NULL_HANDLE,
         pCreateInfo->pName,
         pCreateInfo->pSpecializationInfo,
      };
      VkResult result = compile_spirv(device, &sinfo, &nir);
      if (result != VK_SUCCESS)
         goto fail;
   } else {
      assert(pCreateInfo->codeType == VK_SHADER_CODE_TYPE_BINARY_EXT);
      if (pCreateInfo->codeSize < SHA1_DIGEST_LENGTH + VK_UUID_SIZE + 1)
         return VK_NULL_HANDLE;
      struct blob_reader blob;
      const uint8_t *data = pCreateInfo->pCode;
      uint8_t uuid[VK_UUID_SIZE];
      lvp_device_get_cache_uuid(uuid);
      if (memcmp(uuid, data, VK_UUID_SIZE))
         return VK_NULL_HANDLE;
      size_t size = pCreateInfo->codeSize - SHA1_DIGEST_LENGTH - VK_UUID_SIZE;
      unsigned char sha1[20];

      struct mesa_sha1 sctx;
      _mesa_sha1_init(&sctx);
      _mesa_sha1_update(&sctx, data + SHA1_DIGEST_LENGTH + VK_UUID_SIZE, size);
      _mesa_sha1_final(&sctx, sha1);
      if (memcmp(sha1, data + VK_UUID_SIZE, SHA1_DIGEST_LENGTH))
         return VK_NULL_HANDLE;

      blob_reader_init(&blob, data + SHA1_DIGEST_LENGTH + VK_UUID_SIZE, size);
      nir = nir_deserialize(NULL, device->pscreen->get_compiler_options(device->pscreen, PIPE_SHADER_IR_NIR, stage), &blob);
      if (!nir)
         goto fail;
   }
   if (!nir_shader_get_entrypoint(nir))
      goto fail;
   struct lvp_shader *shader = vk_object_zalloc(&device->vk, pAllocator, sizeof(struct lvp_shader), VK_OBJECT_TYPE_SHADER_EXT);
   if (!shader)
      goto fail;
   blob_init(&shader->blob);
   VkPipelineLayoutCreateInfo pci = {
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      NULL,
      0,
      pCreateInfo->setLayoutCount,
      pCreateInfo->pSetLayouts,
      pCreateInfo->pushConstantRangeCount,
      pCreateInfo->pPushConstantRanges,
   };
   shader->layout = lvp_pipeline_layout_create(device, &pci, pAllocator);
   lvp_shader_lower(device, nir, shader, shader->layout);
   lvp_shader_xfb_init(shader);
   if (stage == MESA_SHADER_TESS_EVAL) {
      /* spec requires that all tess modes are set in both shaders */
      nir_lower_patch_vertices(shader->pipeline_nir->nir, shader->pipeline_nir->nir->info.tess.tcs_vertices_out, NULL);
      shader->tess_ccw = create_pipeline_nir(nir_shader_clone(NULL, shader->pipeline_nir->nir));
      shader->tess_ccw->nir->info.tess.ccw = !shader->pipeline_nir->nir->info.tess.ccw;
      shader->tess_ccw_cso = lvp_shader_compile(device, shader, nir_shader_clone(NULL, shader->tess_ccw->nir));
   } else if (stage == MESA_SHADER_FRAGMENT && nir->info.fs.uses_fbfetch_output) {
      /* this is (currently) illegal */
      assert(!nir->info.fs.uses_fbfetch_output);
      shader_destroy(device, shader);

      vk_object_base_finish(&shader->base);
      vk_free2(&device->vk.alloc, pAllocator, shader);
      return VK_NULL_HANDLE;
   }
   nir_serialize(&shader->blob, nir, true);
   shader->shader_cso = lvp_shader_compile(device, shader, nir_shader_clone(NULL, nir));
   return lvp_shader_to_handle(shader);
fail:
   ralloc_free(nir);
   return VK_NULL_HANDLE;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateShadersEXT(
    VkDevice                                    _device,
    uint32_t                                    createInfoCount,
    const VkShaderCreateInfoEXT*                pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderEXT*                                pShaders)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   unsigned i;
   for (i = 0; i < createInfoCount; i++) {
      pShaders[i] = create_shader_object(device, &pCreateInfos[i], pAllocator);
      if (!pShaders[i]) {
         if (pCreateInfos[i].codeType == VK_SHADER_CODE_TYPE_BINARY_EXT) {
            if (i < createInfoCount - 1)
               memset(&pShaders[i + 1], 0, (createInfoCount - i - 1) * sizeof(VkShaderEXT));
            return vk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
         }
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   }
   return VK_SUCCESS;
}


VKAPI_ATTR VkResult VKAPI_CALL lvp_GetShaderBinaryDataEXT(
    VkDevice                                    device,
    VkShaderEXT                                 _shader,
    size_t*                                     pDataSize,
    void*                                       pData)
{
   LVP_FROM_HANDLE(lvp_shader, shader, _shader);
   VkResult ret = VK_SUCCESS;
   if (pData) {
      if (*pDataSize < shader->blob.size + SHA1_DIGEST_LENGTH + VK_UUID_SIZE) {
         ret = VK_INCOMPLETE;
         *pDataSize = 0;
      } else {
         *pDataSize = MIN2(*pDataSize, shader->blob.size + SHA1_DIGEST_LENGTH + VK_UUID_SIZE);
         uint8_t *data = pData;
         lvp_device_get_cache_uuid(data);
         struct mesa_sha1 sctx;
         _mesa_sha1_init(&sctx);
         _mesa_sha1_update(&sctx, shader->blob.data, shader->blob.size);
         _mesa_sha1_final(&sctx, data + VK_UUID_SIZE);
         memcpy(data + SHA1_DIGEST_LENGTH + VK_UUID_SIZE, shader->blob.data, shader->blob.size);
      }
   } else {
      *pDataSize = shader->blob.size + SHA1_DIGEST_LENGTH + VK_UUID_SIZE;
   }
   return ret;
}
