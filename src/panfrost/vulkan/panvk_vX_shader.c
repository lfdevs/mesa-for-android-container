/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_shader.c which is:
 * Copyright © 2019 Google LLC
 *
 * Also derived from anv_pipeline.c which is
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "genxml/gen_macros.h"

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_instance.h"
#include "panvk_mempool.h"
#include "panvk_physical_device.h"
#include "panvk_shader.h"

#include "spirv/nir_spirv.h"
#include "util/memstream.h"
#include "util/mesa-sha1.h"
#include "util/u_dynarray.h"
#include "nir_builder.h"
#include "nir_conversion_builder.h"
#include "nir_deref.h"

#include "vk_graphics_state.h"
#include "vk_shader_module.h"

#include "compiler/bifrost_nir.h"
#include "util/pan_lower_framebuffer.h"
#include "pan_shader.h"

#include "vk_log.h"
#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"
#include "vk_shader.h"
#include "vk_util.h"

static nir_def *
load_sysval_from_push_const(nir_builder *b, unsigned offset, unsigned bit_size,
                            unsigned num_comps)
{
   return nir_load_push_constant(
      b, num_comps, bit_size, nir_imm_int(b, 0),
      /* Push constants are placed first, and then come the sysvals. */
      .base = offset + 256, .range = num_comps * bit_size / 8);
}

static bool
panvk_lower_sysvals(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   unsigned num_comps = intr->def.num_components;
   unsigned bit_size = intr->def.bit_size;
   nir_def *val = NULL;
   b->cursor = nir_before_instr(instr);

#define SYSVAL(ptype, name) offsetof(struct panvk_##ptype##_sysvals, name)
   switch (intr->intrinsic) {
   case nir_intrinsic_load_base_workgroup_id:
      val = load_sysval_from_push_const(b, SYSVAL(compute, base), bit_size,
                                        num_comps);
      break;
   case nir_intrinsic_load_num_workgroups:
      val = load_sysval_from_push_const(b, SYSVAL(compute, num_work_groups),
                                        bit_size, num_comps);
      break;
   case nir_intrinsic_load_workgroup_size:
      val = load_sysval_from_push_const(b, SYSVAL(compute, local_group_size),
                                        bit_size, num_comps);
      break;
   case nir_intrinsic_load_viewport_scale:
      val = load_sysval_from_push_const(b, SYSVAL(graphics, viewport.scale),
                                        bit_size, num_comps);
      break;
   case nir_intrinsic_load_viewport_offset:
      val = load_sysval_from_push_const(b, SYSVAL(graphics, viewport.offset),
                                        bit_size, num_comps);
      break;
   case nir_intrinsic_load_first_vertex:
      val = load_sysval_from_push_const(b, SYSVAL(graphics, vs.first_vertex),
                                        bit_size, num_comps);
      break;
   case nir_intrinsic_load_base_vertex:
      val = load_sysval_from_push_const(b, SYSVAL(graphics, vs.base_vertex),
                                        bit_size, num_comps);
      break;
   case nir_intrinsic_load_base_instance:
      val = load_sysval_from_push_const(b, SYSVAL(graphics, vs.base_instance),
                                        bit_size, num_comps);
      break;
   case nir_intrinsic_load_blend_const_color_rgba:
      val = load_sysval_from_push_const(b, SYSVAL(graphics, blend.constants),
                                        bit_size, num_comps);
      break;

   case nir_intrinsic_load_layer_id:
      assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      val = load_sysval_from_push_const(b, SYSVAL(graphics, layer_id), bit_size,
                                        num_comps);
      break;

   default:
      return false;
   }
#undef SYSVAL

   b->cursor = nir_after_instr(instr);
   nir_def_rewrite_uses(&intr->def, val);
   return true;
}

static bool
lower_gl_pos_layer_writes(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic != nir_intrinsic_copy_deref)
      return false;

   nir_variable *dst_var = nir_intrinsic_get_var(intr, 0);
   nir_variable *src_var = nir_intrinsic_get_var(intr, 1);

   if (!dst_var || dst_var->data.mode != nir_var_shader_out || !src_var ||
       src_var->data.mode != nir_var_shader_temp)
      return false;

   if (dst_var->data.location == VARYING_SLOT_LAYER) {
      /* We don't really write the layer, we just make sure primitives are
       * discarded if gl_Layer doesn't match the layer passed to the draw.
       */
      b->cursor = nir_instr_remove(instr);
      return true;
   }

   if (dst_var->data.location == VARYING_SLOT_POS) {
      nir_variable *temp_layer_var = data;
      nir_variable *temp_pos_var = src_var;

      b->cursor = nir_before_instr(instr);
      nir_def *layer = nir_load_var(b, temp_layer_var);
      nir_def *pos = nir_load_var(b, temp_pos_var);
      nir_def *inf_pos = nir_imm_vec4(b, INFINITY, INFINITY, INFINITY, 1.0f);
      nir_def *ref_layer = load_sysval_from_push_const(
         b, offsetof(struct panvk_graphics_sysvals, layer_id), 32, 1);

      nir_store_var(b, temp_pos_var,
                    nir_bcsel(b, nir_ieq(b, layer, ref_layer), pos, inf_pos),
                    0xf);
      return true;
   }

   return false;
}

static bool
lower_layer_writes(nir_shader *nir)
{
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      return false;

   nir_variable *temp_layer_var = NULL;
   bool has_layer_var = false;

   nir_foreach_variable_with_modes(var, nir,
                                   nir_var_shader_out | nir_var_shader_temp) {
      if (var->data.mode == nir_var_shader_out &&
          var->data.location == VARYING_SLOT_LAYER)
         has_layer_var = true;

      if (var->data.mode == nir_var_shader_temp &&
          var->data.location == VARYING_SLOT_LAYER)
         temp_layer_var = var;
   }

   if (!has_layer_var)
      return false;

   assert(temp_layer_var);

   return nir_shader_instructions_pass(
      nir, lower_gl_pos_layer_writes,
      nir_metadata_block_index | nir_metadata_dominance, temp_layer_var);
}

static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length, *align = comp_size * (length == 3 ? 4 : length);
}

static inline nir_address_format
panvk_buffer_ubo_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return nir_address_format_32bit_index_offset;
   default:
      unreachable("Invalid robust buffer access behavior");
   }
}

static inline nir_address_format
panvk_buffer_ssbo_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
      return nir_address_format_64bit_global_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return nir_address_format_64bit_bounded_global;
   default:
      unreachable("Invalid robust buffer access behavior");
   }
}

static const nir_shader_compiler_options *
panvk_get_nir_options(UNUSED struct vk_physical_device *vk_pdev,
                      UNUSED gl_shader_stage stage,
                      UNUSED const struct vk_pipeline_robustness_state *rs)
{
   return GENX(pan_shader_get_compiler_options)();
}

static struct spirv_to_nir_options
panvk_get_spirv_options(UNUSED struct vk_physical_device *vk_pdev,
                        UNUSED gl_shader_stage stage,
                        const struct vk_pipeline_robustness_state *rs)
{
   return (struct spirv_to_nir_options){
      .ubo_addr_format = panvk_buffer_ubo_addr_format(rs->uniform_buffers),
      .ssbo_addr_format = panvk_buffer_ssbo_addr_format(rs->storage_buffers),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
   };
}

static void
panvk_preprocess_nir(UNUSED struct vk_physical_device *vk_pdev, nir_shader *nir)
{
   /* Ensure to regroup output variables at the same location */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, nir_lower_io_to_vector, nir_var_shader_out);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir),
              true, true);

   /* This needs to be done just after the io_to_temporaries pass, because we
    * rely on in/out temporaries to collect the final layer_id value. */
   NIR_PASS_V(nir, lower_layer_writes);

   NIR_PASS_V(nir, nir_lower_indirect_derefs,
              nir_var_shader_in | nir_var_shader_out, UINT32_MAX);

   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);
   NIR_PASS_V(nir, nir_opt_loop);

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      struct nir_input_attachment_options lower_input_attach_opts = {
         .use_fragcoord_sysval = true,
         .use_layer_id_sysval = true,
      };

      NIR_PASS_V(nir, nir_lower_input_attachments, &lower_input_attach_opts);
   }

   /* Do texture lowering here.  Yes, it's a duplication of the texture
    * lowering in bifrost_compile.  However, we need to lower texture stuff
    * now, before we call panvk_per_arch(nir_lower_descriptors)() because some
    * of the texture lowering generates nir_texop_txs which we handle as part
    * of descriptor lowering.
    *
    * TODO: We really should be doing this in common code, not dpulicated in
    * panvk.  In order to do that, we need to rework the panfrost compile
    * flow to look more like the Intel flow:
    *
    *  1. Compile SPIR-V to NIR and maybe do a tiny bit of lowering that needs
    *     to be done really early.
    *
    *  2. pan_preprocess_nir: Does common lowering and runs the optimization
    *     loop.  Nothing here should be API-specific.
    *
    *  3. Do additional lowering in panvk
    *
    *  4. pan_postprocess_nir: Does final lowering and runs the optimization
    *     loop again.  This can happen as part of the final compile.
    *
    * This would give us a better place to do panvk-specific lowering.
    */
   nir_lower_tex_options lower_tex_options = {
      .lower_txs_lod = true,
      .lower_txp = ~0,
      .lower_tg4_broadcom_swizzle = true,
      .lower_txd = true,
      .lower_invalid_implicit_lod = true,
   };
   NIR_PASS_V(nir, nir_lower_tex, &lower_tex_options);
   NIR_PASS_V(nir, nir_lower_system_values);

   nir_lower_compute_system_values_options options = {
      .has_base_workgroup_id = true,
   };

   NIR_PASS_V(nir, nir_lower_compute_system_values, &options);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, nir_lower_wpos_center);

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
}

static void
panvk_hash_graphics_state(struct vk_physical_device *device,
                          const struct vk_graphics_pipeline_state *state,
                          VkShaderStageFlags stages, blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

   /* We don't need to do anything here yet */

   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

static void
panvk_lower_nir(struct panvk_device *dev, nir_shader *nir,
                uint32_t set_layout_count,
                struct vk_descriptor_set_layout *const *set_layouts,
                const struct vk_pipeline_robustness_state *rs,
                const struct panfrost_compile_inputs *compile_input,
                struct panvk_shader *shader)
{
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   gl_shader_stage stage = nir->info.stage;

   NIR_PASS_V(nir, panvk_per_arch(nir_lower_descriptors), dev, set_layout_count,
              set_layouts, shader);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ubo,
              panvk_buffer_ubo_addr_format(rs->uniform_buffers));
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ssbo,
              panvk_buffer_ssbo_addr_format(rs->storage_buffers));
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_global,
              nir_address_format_64bit_global);

   if (gl_shader_stage_uses_workgroup(stage)) {
      if (!nir->info.shared_memory_explicit_layout) {
         NIR_PASS_V(nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
                    shared_type_info);
      }

      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared,
                 nir_address_format_32bit_offset);
   }

   if (stage == MESA_SHADER_VERTEX) {
      /* We need the driver_location to match the vertex attribute location,
       * so we can use the attribute layout described by
       * vk_vertex_input_state where there are holes in the attribute locations.
       */
      nir_foreach_shader_in_variable(var, nir) {
         assert(var->data.location >= VERT_ATTRIB_GENERIC0 &&
                var->data.location <= VERT_ATTRIB_GENERIC15);
         var->data.driver_location = var->data.location - VERT_ATTRIB_GENERIC0;
      }
   } else {
      nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs,
                                  stage);
   }

   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                               stage);

   /* Needed to turn shader_temp into function_temp since the backend only
    * handles the latter for now.
    */
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (unlikely(instance->debug_flags & PANVK_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   pan_shader_preprocess(nir, compile_input->gpu_id);

   if (stage == MESA_SHADER_VERTEX)
      NIR_PASS_V(nir, pan_lower_image_index, MAX_VS_ATTRIBS);

   NIR_PASS_V(nir, nir_shader_instructions_pass, panvk_lower_sysvals,
              nir_metadata_control_flow, NULL);
}

static VkResult
panvk_compile_nir(struct panvk_device *dev, nir_shader *nir,
                  VkShaderCreateFlagsEXT shader_flags,
                  struct panfrost_compile_inputs *compile_input,
                  struct panvk_shader *shader)
{
   const bool dump_asm =
      shader_flags & VK_SHADER_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_MESA;

   struct util_dynarray binary;
   util_dynarray_init(&binary, NULL);
   GENX(pan_shader_compile)(nir, compile_input, &binary, &shader->info);

   void *bin_ptr = util_dynarray_element(&binary, uint8_t, 0);
   unsigned bin_size = util_dynarray_num_elements(&binary, uint8_t);

   shader->bin_size = 0;
   shader->bin_ptr = NULL;

   if (bin_size) {
      void *data = malloc(bin_size);

      if (data == NULL)
         return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

      memcpy(data, bin_ptr, bin_size);
      shader->bin_size = bin_size;
      shader->bin_ptr = data;
   }
   util_dynarray_fini(&binary);

   if (dump_asm) {
      shader->nir_str = nir_shader_as_str(nir, NULL);

      char *data = NULL;
      size_t disasm_size = 0;

      if (shader->bin_size) {
         struct u_memstream mem;
         if (u_memstream_open(&mem, &data, &disasm_size)) {
            FILE *const stream = u_memstream_get(&mem);
            pan_shader_disassemble(stream, shader->bin_ptr, shader->bin_size,
                                   compile_input->gpu_id, false);
            u_memstream_close(&mem);
         }
      }

      char *asm_str = malloc(disasm_size + 1);
      memcpy(asm_str, data, disasm_size);
      asm_str[disasm_size] = '\0';
      free(data);

      shader->asm_str = asm_str;
   }

   /* Patch the descriptor count */
   shader->info.ubo_count =
      shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_UBO] +
      shader->desc_info.dyn_ubos.count;
   shader->info.texture_count =
      shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_TEXTURE];
   shader->info.sampler_count =
      shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_SAMPLER];

   /* Dummy sampler. */
   if (!shader->info.sampler_count && shader->info.texture_count)
      shader->info.sampler_count++;

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      /* We leave holes in the attribute locations, but pan_shader.c assumes the
       * opposite. Patch attribute_count accordingly, so
       * pan_shader_prepare_rsd() does what we expect.
       */
      uint32_t gen_attribs =
         (shader->info.attributes_read & VERT_BIT_GENERIC_ALL) >>
         VERT_ATTRIB_GENERIC0;

      shader->info.attribute_count = util_last_bit(gen_attribs);

      /* NULL IDVS shaders are not allowed. */
      if (!bin_size)
         shader->info.vs.idvs = false;
   }

   /* Image attributes start at MAX_VS_ATTRIBS in the VS attribute table,
    * and zero in other stages.
    */
   if (shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_IMG] > 0)
      shader->info.attribute_count =
         shader->desc_info.others.count[PANVK_BIFROST_DESC_TABLE_IMG] +
         (nir->info.stage == MESA_SHADER_VERTEX ? MAX_VS_ATTRIBS : 0);

   shader->local_size.x = nir->info.workgroup_size[0];
   shader->local_size.y = nir->info.workgroup_size[1];
   shader->local_size.z = nir->info.workgroup_size[2];

   return VK_SUCCESS;
}

static VkResult
panvk_shader_upload(struct panvk_device *dev, struct panvk_shader *shader,
                    const VkAllocationCallbacks *pAllocator)
{
   shader->code_mem = (struct panvk_priv_mem){0};
   shader->rsd = (struct panvk_priv_mem){0};

   if (!shader->bin_size)
      return VK_SUCCESS;

   shader->code_mem = panvk_pool_upload_aligned(
      &dev->mempools.exec, shader->bin_ptr, shader->bin_size, 128);

   if (shader->info.stage == MESA_SHADER_FRAGMENT)
      return VK_SUCCESS;

   shader->rsd = panvk_pool_alloc_desc(&dev->mempools.rw, RENDERER_STATE);

   pan_pack(panvk_priv_mem_host_addr(shader->rsd), RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(&shader->info, panvk_shader_get_dev_addr(shader),
                             &cfg);
   }

   return VK_SUCCESS;
}

static void
panvk_shader_destroy(struct vk_device *vk_dev, struct vk_shader *vk_shader,
                     const VkAllocationCallbacks *pAllocator)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   free((void *)shader->asm_str);
   ralloc_free((void *)shader->nir_str);

   panvk_pool_free_mem(&dev->mempools.exec, shader->code_mem);
   panvk_pool_free_mem(&dev->mempools.exec, shader->rsd);
   panvk_pool_free_mem(&dev->mempools.exec, shader->desc_info.others.map);

   free((void *)shader->bin_ptr);
   vk_shader_free(&dev->vk, pAllocator, &shader->vk);
}

static const struct vk_shader_ops panvk_shader_ops;

static VkResult
panvk_compile_shader(struct panvk_device *dev,
                     struct vk_shader_compile_info *info,
                     const struct vk_graphics_pipeline_state *state,
                     const VkAllocationCallbacks *pAllocator,
                     struct vk_shader **shader_out)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   struct panvk_shader *shader;
   VkResult result;

   /* We consume the NIR, regardless of success or failure */
   nir_shader *nir = info->nir;

   shader = vk_shader_zalloc(&dev->vk, &panvk_shader_ops, info->stage,
                             pAllocator, sizeof(*shader));
   if (shader == NULL)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct panfrost_compile_inputs inputs = {
      .gpu_id = phys_dev->kmod.props.gpu_prod_id,
      .no_ubo_to_push = true,
   };

   panvk_lower_nir(dev, nir, info->set_layout_count, info->set_layouts,
                   info->robustness, &inputs, shader);

   result = panvk_compile_nir(dev, nir, info->flags, &inputs, shader);

   if (result != VK_SUCCESS) {
      panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return result;
   }

   result = panvk_shader_upload(dev, shader, pAllocator);

   if (result != VK_SUCCESS) {
      panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
      return result;
   }

   *shader_out = &shader->vk;

   return result;
}

static VkResult
panvk_compile_shaders(struct vk_device *vk_dev, uint32_t shader_count,
                      struct vk_shader_compile_info *infos,
                      const struct vk_graphics_pipeline_state *state,
                      const VkAllocationCallbacks *pAllocator,
                      struct vk_shader **shaders_out)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   VkResult result;
   uint32_t i;

   for (i = 0; i < shader_count; i++) {
      result = panvk_compile_shader(dev, &infos[i], state, pAllocator,
                                    &shaders_out[i]);

      /* Clean up NIR for the current shader */
      ralloc_free(infos[i].nir);

      if (result != VK_SUCCESS)
         goto err_cleanup;
   }

   /* TODO: If we get multiple shaders here, we can perform part of the link
    * logic at compile time. */

   return VK_SUCCESS;

err_cleanup:
   /* Clean up all the shaders before this point */
   for (uint32_t j = 0; j < i; j++)
      panvk_shader_destroy(&dev->vk, shaders_out[j], pAllocator);

   /* Clean up all the NIR after this point */
   for (uint32_t j = i + 1; j < shader_count; j++)
      ralloc_free(infos[j].nir);

   /* Memset the output array */
   memset(shaders_out, 0, shader_count * sizeof(*shaders_out));

   return result;
}

static VkResult
shader_desc_info_deserialize(struct blob_reader *blob,
                             struct panvk_shader *shader)
{
   shader->desc_info.used_set_mask = blob_read_uint32(blob);
   shader->desc_info.dyn_ubos.count = blob_read_uint32(blob);
   blob_copy_bytes(blob, shader->desc_info.dyn_ubos.map,
                   shader->desc_info.dyn_ubos.count);
   shader->desc_info.dyn_ssbos.count = blob_read_uint32(blob);
   blob_copy_bytes(blob, shader->desc_info.dyn_ssbos.map,
                   shader->desc_info.dyn_ssbos.count);

   uint32_t others_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      shader->desc_info.others.count[i] = blob_read_uint32(blob);
      others_count += shader->desc_info.others.count[i];
   }

   if (others_count) {
      struct panvk_device *dev = to_panvk_device(shader->vk.base.device);
      struct panvk_pool_alloc_info alloc_info = {
         .size = others_count * sizeof(uint32_t),
         .alignment = sizeof(uint32_t),
      };
      shader->desc_info.others.map =
         panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
      uint32_t *copy_table =
         panvk_priv_mem_host_addr(shader->desc_info.others.map);

      if (!copy_table)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      blob_copy_bytes(blob, copy_table, others_count * sizeof(*copy_table));
   }

   return VK_SUCCESS;
}

static VkResult
panvk_deserialize_shader(struct vk_device *vk_dev, struct blob_reader *blob,
                         uint32_t binary_version,
                         const VkAllocationCallbacks *pAllocator,
                         struct vk_shader **shader_out)
{
   struct panvk_device *device = to_panvk_device(vk_dev);
   struct panvk_shader *shader;
   VkResult result;

   struct pan_shader_info info;
   blob_copy_bytes(blob, &info, sizeof(info));

   struct pan_compute_dim local_size;
   blob_copy_bytes(blob, &local_size, sizeof(local_size));

   const uint32_t bin_size = blob_read_uint32(blob);

   if (blob->overrun)
      return vk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader = vk_shader_zalloc(vk_dev, &panvk_shader_ops, info.stage, pAllocator,
                             sizeof(*shader));
   if (shader == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   shader->info = info;
   shader->local_size = local_size;
   shader->bin_size = bin_size;

   shader->bin_ptr = malloc(bin_size);
   if (shader->bin_ptr == NULL) {
      panvk_shader_destroy(vk_dev, &shader->vk, pAllocator);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   blob_copy_bytes(blob, (void *)shader->bin_ptr, shader->bin_size);

   result = shader_desc_info_deserialize(blob, shader);

   if (result != VK_SUCCESS) {
      panvk_shader_destroy(vk_dev, &shader->vk, pAllocator);
      return vk_error(device, result);
   }

   if (blob->overrun) {
      panvk_shader_destroy(vk_dev, &shader->vk, pAllocator);
      return vk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);
   }

   result = panvk_shader_upload(device, shader, pAllocator);

   if (result != VK_SUCCESS) {
      panvk_shader_destroy(vk_dev, &shader->vk, pAllocator);
      return result;
   }

   *shader_out = &shader->vk;

   return result;
}

static void
shader_desc_info_serialize(struct blob *blob, const struct panvk_shader *shader)
{
   blob_write_uint32(blob, shader->desc_info.used_set_mask);
   blob_write_uint32(blob, shader->desc_info.dyn_ubos.count);
   blob_write_bytes(blob, shader->desc_info.dyn_ubos.map,
                    sizeof(*shader->desc_info.dyn_ubos.map) *
                       shader->desc_info.dyn_ubos.count);
   blob_write_uint32(blob, shader->desc_info.dyn_ssbos.count);
   blob_write_bytes(blob, shader->desc_info.dyn_ssbos.map,
                    sizeof(*shader->desc_info.dyn_ssbos.map) *
                       shader->desc_info.dyn_ssbos.count);

   unsigned others_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      blob_write_uint32(blob, shader->desc_info.others.count[i]);
      others_count += shader->desc_info.others.count[i];
   }

   blob_write_bytes(blob,
                    panvk_priv_mem_host_addr(shader->desc_info.others.map),
                    sizeof(uint32_t) * others_count);
}

static bool
panvk_shader_serialize(struct vk_device *vk_dev,
                       const struct vk_shader *vk_shader, struct blob *blob)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   /**
    * We can't currently cache assembly
    * TODO: Implement seriaization with assembly
    **/
   if (shader->nir_str != NULL || shader->asm_str != NULL)
      return false;

   blob_write_bytes(blob, &shader->info, sizeof(shader->info));
   blob_write_bytes(blob, &shader->local_size, sizeof(shader->local_size));
   blob_write_uint32(blob, shader->bin_size);
   blob_write_bytes(blob, shader->bin_ptr, shader->bin_size);
   shader_desc_info_serialize(blob, shader);

   return !blob->out_of_memory;
}

#define WRITE_STR(field, ...)                                                  \
   ({                                                                          \
      memset(field, 0, sizeof(field));                                         \
      UNUSED int i = snprintf(field, sizeof(field), __VA_ARGS__);              \
      assert(i > 0 && i < sizeof(field));                                      \
   })

static VkResult
panvk_shader_get_executable_properties(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t *executable_count, VkPipelineExecutablePropertiesKHR *properties)
{
   UNUSED struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out, properties,
                          executable_count);

   vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props)
   {
      props->stages = mesa_to_vk_shader_stage(shader->info.stage);
      props->subgroupSize = 8;
      WRITE_STR(props->name, "%s",
                _mesa_shader_stage_to_string(shader->info.stage));
      WRITE_STR(props->description, "%s shader",
                _mesa_shader_stage_to_string(shader->info.stage));
   }

   return vk_outarray_status(&out);
}

static VkResult
panvk_shader_get_executable_statistics(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *statistic_count,
   VkPipelineExecutableStatisticKHR *statistics)
{
   UNUSED struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out, statistics,
                          statistic_count);

   assert(executable_index == 0);

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat)
   {
      WRITE_STR(stat->name, "Code Size");
      WRITE_STR(stat->description,
                "Size of the compiled shader binary, in bytes");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = shader->bin_size;
   }

   /* TODO: more executable statistics (VK_KHR_pipeline_executable_properties) */

   return vk_outarray_status(&out);
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR *ir,
              const char *data)
{
   ir->isText = VK_TRUE;

   size_t data_len = strlen(data) + 1;

   if (ir->pData == NULL) {
      ir->dataSize = data_len;
      return true;
   }

   strncpy(ir->pData, data, ir->dataSize);
   if (ir->dataSize < data_len)
      return false;

   ir->dataSize = data_len;
   return true;
}

static VkResult
panvk_shader_get_executable_internal_representations(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *internal_representation_count,
   VkPipelineExecutableInternalRepresentationKHR *internal_representations)
{
   UNUSED struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          internal_representations,
                          internal_representation_count);
   bool incomplete_text = false;

   if (shader->nir_str != NULL) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR,
                               &out, ir)
      {
         WRITE_STR(ir->name, "NIR shader");
         WRITE_STR(ir->description,
                   "NIR shader before sending to the back-end compiler");
         if (!write_ir_text(ir, shader->nir_str))
            incomplete_text = true;
      }
   }

   if (shader->asm_str != NULL) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR,
                               &out, ir)
      {
         WRITE_STR(ir->name, "Assembly");
         WRITE_STR(ir->description, "Final Assembly");
         if (!write_ir_text(ir, shader->asm_str))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}

static mali_pixel_format
get_varying_format(gl_shader_stage stage, gl_varying_slot loc,
                   enum pipe_format pfmt)
{
   switch (loc) {
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
#if PAN_ARCH <= 6
      return (MALI_R16F << 12) | panfrost_get_default_swizzle(1);
#else
      return (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_R000;
#endif
   case VARYING_SLOT_POS:
#if PAN_ARCH <= 6
      return (MALI_SNAP_4 << 12) | panfrost_get_default_swizzle(4);
#else
      return (MALI_SNAP_4 << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
#endif
   default:
      assert(pfmt != PIPE_FORMAT_NONE);
      return GENX(panfrost_format_from_pipe_format)(pfmt)->hw;
   }
}

struct varyings_info {
   enum pipe_format fmts[VARYING_SLOT_MAX];
   BITSET_DECLARE(active, VARYING_SLOT_MAX);
};

static void
collect_varyings_info(const struct pan_shader_varying *varyings,
                      unsigned varying_count, struct varyings_info *info)
{
   for (unsigned i = 0; i < varying_count; i++) {
      gl_varying_slot loc = varyings[i].location;

      if (varyings[i].format == PIPE_FORMAT_NONE)
         continue;

      info->fmts[loc] = varyings[i].format;
      BITSET_SET(info->active, loc);
   }
}

static inline enum panvk_varying_buf_id
varying_buf_id(gl_varying_slot loc)
{
   switch (loc) {
   case VARYING_SLOT_POS:
      return PANVK_VARY_BUF_POSITION;
   case VARYING_SLOT_PSIZ:
      return PANVK_VARY_BUF_PSIZ;
   default:
      return PANVK_VARY_BUF_GENERAL;
   }
}

static mali_pixel_format
varying_format(gl_varying_slot loc, enum pipe_format pfmt)
{
   switch (loc) {
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
#if PAN_ARCH <= 6
      return (MALI_R16F << 12) | panfrost_get_default_swizzle(1);
#else
      return (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_R000;
#endif
   case VARYING_SLOT_POS:
#if PAN_ARCH <= 6
      return (MALI_SNAP_4 << 12) | panfrost_get_default_swizzle(4);
#else
      return (MALI_SNAP_4 << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
#endif
   default:
      return GENX(panfrost_format_from_pipe_format)(pfmt)->hw;
   }
}

static struct panvk_priv_mem
emit_varying_attrs(struct panvk_pool *desc_pool,
                   const struct pan_shader_varying *varyings,
                   unsigned varying_count, const struct varyings_info *info,
                   unsigned *buf_offsets)
{
   unsigned attr_count = BITSET_COUNT(info->active);
   struct panvk_priv_mem mem =
      panvk_pool_alloc_desc_array(desc_pool, attr_count, ATTRIBUTE);
   struct mali_attribute_packed *attrs = panvk_priv_mem_host_addr(mem);
   unsigned attr_idx = 0;

   for (unsigned i = 0; i < varying_count; i++) {
      pan_pack(&attrs[attr_idx++], ATTRIBUTE, cfg) {
         gl_varying_slot loc = varyings[i].location;
         enum pipe_format pfmt = varyings[i].format != PIPE_FORMAT_NONE
                                    ? info->fmts[loc]
                                    : PIPE_FORMAT_NONE;

         if (pfmt == PIPE_FORMAT_NONE) {
#if PAN_ARCH >= 7
            cfg.format = (MALI_CONSTANT << 12) | MALI_RGB_COMPONENT_ORDER_0000;
#else
            cfg.format = (MALI_CONSTANT << 12) | PAN_V6_SWIZZLE(0, 0, 0, 0);
#endif
         } else {
            cfg.buffer_index = varying_buf_id(loc);
            cfg.offset = buf_offsets[loc];
            cfg.format = varying_format(loc, info->fmts[loc]);
         }
         cfg.offset_enable = false;
      }
   }

   return mem;
}

void
panvk_per_arch(link_shaders)(struct panvk_pool *desc_pool,
                             struct panvk_shader *vs, struct panvk_shader *fs,
                             struct panvk_shader_link *link)
{
   BITSET_DECLARE(active_attrs, VARYING_SLOT_MAX) = {0};
   unsigned buf_strides[PANVK_VARY_BUF_MAX] = {0};
   unsigned buf_offsets[VARYING_SLOT_MAX] = {0};
   struct varyings_info out_vars = {0};
   struct varyings_info in_vars = {0};
   unsigned loc;

   assert(vs);
   assert(vs->info.stage == MESA_SHADER_VERTEX);

   collect_varyings_info(vs->info.varyings.output,
                         vs->info.varyings.output_count, &out_vars);

   if (fs) {
      assert(fs->info.stage == MESA_SHADER_FRAGMENT);
      collect_varyings_info(fs->info.varyings.input,
                            fs->info.varyings.input_count, &in_vars);
   }

   BITSET_OR(active_attrs, in_vars.active, out_vars.active);

   /* Handle the position and point size buffers explicitly, as they are
    * passed through separate buffer pointers to the tiler job.
    */
   if (BITSET_TEST(out_vars.active, VARYING_SLOT_POS)) {
      buf_strides[PANVK_VARY_BUF_POSITION] = sizeof(float) * 4;
      BITSET_CLEAR(active_attrs, VARYING_SLOT_POS);
   }

   if (BITSET_TEST(out_vars.active, VARYING_SLOT_PSIZ)) {
      buf_strides[PANVK_VARY_BUF_PSIZ] = sizeof(uint16_t);
      BITSET_CLEAR(active_attrs, VARYING_SLOT_PSIZ);
   }

   BITSET_FOREACH_SET(loc, active_attrs, VARYING_SLOT_MAX) {
      /* We expect the VS to write to all inputs read by the FS, and the
       * FS to read all inputs written by the VS. If that's not the
       * case, we keep PIPE_FORMAT_NONE to reflect the fact we should use a
       * sink attribute (writes are discarded, reads return zeros).
       */
      if (in_vars.fmts[loc] == PIPE_FORMAT_NONE ||
          out_vars.fmts[loc] == PIPE_FORMAT_NONE) {
         in_vars.fmts[loc] = PIPE_FORMAT_NONE;
         out_vars.fmts[loc] = PIPE_FORMAT_NONE;
         continue;
      }

      unsigned out_size = util_format_get_blocksize(out_vars.fmts[loc]);
      unsigned buf_idx = varying_buf_id(loc);

      /* Always trust the VS input format, so we can:
       * - discard components that are never read
       * - use float types for interpolated fragment shader inputs
       * - use fp16 for floats with mediump
       * - make sure components that are not written by the FS are set to zero
       */
      out_vars.fmts[loc] = in_vars.fmts[loc];

      /* Special buffers are handled explicitly before this loop, everything
       * else should be laid out in the general varying buffer.
       */
      assert(buf_idx == PANVK_VARY_BUF_GENERAL);

      /* Keep things aligned a 32-bit component. */
      buf_offsets[loc] = buf_strides[buf_idx];
      buf_strides[buf_idx] += ALIGN_POT(out_size, 4);
   }

   link->vs.attribs = emit_varying_attrs(desc_pool, vs->info.varyings.output,
                                         vs->info.varyings.output_count,
                                         &out_vars, buf_offsets);

   if (fs)
      link->fs.attribs = emit_varying_attrs(desc_pool, fs->info.varyings.input,
                                            fs->info.varyings.input_count,
                                            &in_vars, buf_offsets);

   memcpy(link->buf_strides, buf_strides, sizeof(link->buf_strides));
}

static const struct vk_shader_ops panvk_shader_ops = {
   .destroy = panvk_shader_destroy,
   .serialize = panvk_shader_serialize,
   .get_executable_properties = panvk_shader_get_executable_properties,
   .get_executable_statistics = panvk_shader_get_executable_statistics,
   .get_executable_internal_representations =
      panvk_shader_get_executable_internal_representations,
};

const struct vk_device_shader_ops panvk_per_arch(device_shader_ops) = {
   .get_nir_options = panvk_get_nir_options,
   .get_spirv_options = panvk_get_spirv_options,
   .preprocess_nir = panvk_preprocess_nir,
   .hash_graphics_state = panvk_hash_graphics_state,
   .compile = panvk_compile_shaders,
   .deserialize = panvk_deserialize_shader,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_bind_shaders = panvk_per_arch(cmd_bind_shaders),
};
