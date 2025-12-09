/*
 * Copyright © 2025 Arm Ltd.
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
#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_instance.h"
#include "panvk_mempool.h"
#include "panvk_physical_device.h"
#include "panvk_sampler.h"
#include "panvk_shader.h"

#include "spirv/nir_spirv.h"
#include "util/memstream.h"
#include "util/mesa-sha1.h"
#include "util/shader_stats.h"
#include "util/u_dynarray.h"
#include "nir_builder.h"
#include "nir_conversion_builder.h"
#include "nir_deref.h"

#include "shader_enums.h"
#include "vk_graphics_state.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_shader_module.h"
#include "vk_ycbcr_conversion.h"

#include "compiler/bifrost/bifrost_nir.h"
#include "compiler/pan_compiler.h"
#include "compiler/pan_nir.h"
#include "pan_shader.h"

#include "vk_log.h"
#include "vk_pipeline.h"
#include "vk_pipeline_layout.h"
#include "vk_shader.h"
#include "vk_util.h"

#define FAU_WORD_COUNT 64

struct panvk_lower_sysvals_context {
   struct panvk_shader_variant *shader;
   const struct vk_graphics_pipeline_state *state;
};

static bool
panvk_lower_sysvals(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   const struct panvk_lower_sysvals_context *ctx = data;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   unsigned bit_size = intr->def.bit_size;
   nir_def *val = NULL;
   b->cursor = nir_before_instr(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_base_workgroup_id:
      val = load_sysval(b, compute, bit_size, base);
      break;
   case nir_intrinsic_load_num_workgroups:
      val = load_sysval(b, compute, bit_size, num_work_groups);
      break;
   case nir_intrinsic_load_workgroup_size:
      val = load_sysval(b, compute, bit_size, local_group_size);
      break;
   case nir_intrinsic_load_viewport_scale:
      val = load_sysval(b, graphics, bit_size, viewport.scale);
      break;
   case nir_intrinsic_load_viewport_offset:
      val = load_sysval(b, graphics, bit_size, viewport.offset);
      break;
   case nir_intrinsic_load_first_vertex:
      val = load_sysval(b, graphics, bit_size, vs.first_vertex);
      break;
   case nir_intrinsic_load_base_instance:
      val = load_sysval(b, graphics, bit_size, vs.base_instance);
      break;
   case nir_intrinsic_load_noperspective_varyings_pan:
      /* TODO: use a VS epilog specialized on constant noperspective_varyings
       * with VK_EXT_graphics_pipeline_libraries and VK_EXT_shader_object */
      assert(b->shader->info.stage == MESA_SHADER_VERTEX);
      val = load_sysval(b, graphics, bit_size, vs.noperspective_varyings);
      break;

#if PAN_ARCH < 9
   case nir_intrinsic_load_raw_vertex_offset_pan:
      val = load_sysval(b, graphics, bit_size, vs.raw_vertex_offset);
      break;
   case nir_intrinsic_load_layer_id:
      assert(b->shader->info.stage == MESA_SHADER_FRAGMENT);
      val = load_sysval(b, graphics, bit_size, layer_id);
      break;
   case nir_intrinsic_load_view_index:
      assert(b->shader->info.stage != MESA_SHADER_COMPUTE);
      if (ctx->state->rp->view_mask == 0)
         val = nir_imm_zero(b, 1, 32);
      else
         val = load_sysval(b, graphics, bit_size, layer_id);
      break;
#endif

   case nir_intrinsic_load_draw_id:
      /* Multidraw is supported on v10. */
      if (PAN_ARCH >= 10)
         return false;

      /* TODO: We only implement single-draw direct and indirect draws, so this
       * is sufficient. We'll revisit this when we get around to implementing
       * multidraw. */
      assert(b->shader->info.stage == MESA_SHADER_VERTEX);
      val = nir_imm_int(b, 0);
      break;

   case nir_intrinsic_load_printf_buffer_address:
      val = load_sysval(b, common, bit_size, printf_buffer_address);
      break;

   case nir_intrinsic_load_input_attachment_target_pan: {
      const struct vk_input_attachment_location_state *ial =
         ctx->state ? ctx->state->ial : NULL;

      if (ial && nir_src_is_const(intr->src[0])) {
         uint32_t index = nir_src_as_uint(intr->src[0]);
         uint32_t depth_idx = ial->depth_att == MESA_VK_ATTACHMENT_NO_INDEX
                                 ? 0
                                 : ial->depth_att + 1;
         uint32_t stencil_idx = ial->stencil_att == MESA_VK_ATTACHMENT_NO_INDEX
                                   ? 0
                                   : ial->stencil_att + 1;
         uint32_t target = ~0;

         if (depth_idx == index || stencil_idx == index) {
            target = PANVK_ZS_ATTACHMENT;
         } else {
            for (unsigned i = 0; i < ial->color_attachment_count; i++) {
               if (ial->color_map[i] == MESA_VK_ATTACHMENT_UNUSED)
                  continue;

               if (ial->color_map[i] + 1 == index) {
                  target = PANVK_COLOR_ATTACHMENT(i);
                  break;
               }
            }
         }

         val = nir_imm_int(b, target);
      } else {
         nir_def *ia_info =
            load_sysval_entry(b, graphics, bit_size, iam, intr->src[0].ssa);

         val = nir_channel(b, ia_info, 0);
      }
      break;
   }

   case nir_intrinsic_load_input_attachment_conv_pan: {
      nir_def *ia_info =
         load_sysval_entry(b, graphics, bit_size, iam, intr->src[0].ssa);

      val = nir_channel(b, ia_info, 1);
      break;
   }

   default:
      return false;
   }

   assert(val->num_components == intr->def.num_components);

   b->cursor = nir_after_instr(instr);
   nir_def_rewrite_uses(&intr->def, val);
   return true;
}

static bool
panvk_lower_load_vs_input(nir_builder *b, nir_intrinsic_instr *intrin,
                           UNUSED void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_input)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *ld_attr = nir_load_attribute_pan(
      b, intrin->def.num_components, intrin->def.bit_size,
      PAN_ARCH < 9 ?
         nir_load_raw_vertex_id_pan(b) :
         nir_load_vertex_id(b),
      nir_load_instance_id(b),
      nir_get_io_offset_src(intrin)->ssa,
      .base = nir_intrinsic_base(intrin),
      .component = nir_intrinsic_component(intrin),
      .dest_type = nir_intrinsic_dest_type(intrin));
   nir_def_replace(&intrin->def, ld_attr);

   return true;
}

static bool
panvk_lower_load_fs_input(nir_builder *b, nir_intrinsic_instr *intrin,
                          UNUSED void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_input)
      return false;

   /* Lower PrimitiveID varying loads to the equivalent intrinsic. This only
    * works since v6 and will require additional changes if PrimitiveID is
    * explicitly written to (for example by a geometry shader). */
   if (nir_intrinsic_io_semantics(intrin).location ==
       VARYING_SLOT_PRIMITIVE_ID) {
      b->cursor = nir_before_instr(&intrin->instr);
      nir_def_replace(&intrin->def, nir_load_primitive_id(b));
      return true;
   }

   return false;
}

#if PAN_ARCH < 9
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
      nir_def *ref_layer = load_sysval(b, graphics, 32, layer_id);

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

   return nir_shader_instructions_pass(nir, lower_gl_pos_layer_writes,
                                       nir_metadata_control_flow,
                                       temp_layer_var);
}
#endif

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
      return PAN_ARCH < 9 ? nir_address_format_32bit_index_offset
                           : nir_address_format_vec2_index_32bit_offset;
   default:
      UNREACHABLE("Invalid robust buffer access behavior");
   }
}

static inline nir_address_format
panvk_buffer_ssbo_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED_EXT:
      return PAN_ARCH < 9 ? nir_address_format_64bit_global_32bit_offset
                           : nir_address_format_vec2_index_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT:
      return PAN_ARCH < 9 ? nir_address_format_64bit_bounded_global
                           : nir_address_format_vec2_index_32bit_offset;
   default:
      UNREACHABLE("Invalid robust buffer access behavior");
   }
}

static const nir_shader_compiler_options *
panvk_get_nir_options(UNUSED struct vk_physical_device *vk_pdev,
                      UNUSED mesa_shader_stage stage,
                      UNUSED const struct vk_pipeline_robustness_state *rs)
{
   struct panvk_physical_device *phys_dev = to_panvk_physical_device(vk_pdev);
   return pan_get_nir_shader_compiler_options(pan_arch(phys_dev->kmod.props.gpu_id));
}

static struct spirv_to_nir_options
panvk_get_spirv_options(UNUSED struct vk_physical_device *vk_pdev,
                        UNUSED mesa_shader_stage stage,
                        const struct vk_pipeline_robustness_state *rs)
{
   return (struct spirv_to_nir_options){
      .ubo_addr_format = panvk_buffer_ubo_addr_format(rs->uniform_buffers),
      .ssbo_addr_format = panvk_buffer_ssbo_addr_format(rs->storage_buffers),
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .shared_addr_format = nir_address_format_32bit_offset,
   };
}

static void
panvk_preprocess_nir(struct vk_physical_device *vk_pdev,
                     nir_shader *nir,
                     UNUSED const struct vk_pipeline_robustness_state *rs)
{
   struct panvk_physical_device *pdev = to_panvk_physical_device(vk_pdev);

   /* Ensure to regroup output variables at the same location */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_opt_vectorize_io_vars, nir_var_shader_out);

   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
            nir_shader_get_entrypoint(nir), nir_var_shader_out);

#if PAN_ARCH < 9
   /* This needs to be done just after the io_to_temporaries pass, because we
    * rely on out temporaries to collect the final layer_id value.
    */
   NIR_PASS(_, nir, lower_layer_writes);
#endif

   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_split_var_copies);

   NIR_PASS(_, nir, nir_opt_copy_prop_vars);
   NIR_PASS(_, nir, nir_opt_combine_stores, nir_var_all);
   NIR_PASS(_, nir, nir_opt_loop);

   NIR_PASS(_, nir, nir_opt_barrier_modes);
   NIR_PASS(_, nir, nir_opt_acquire_release_barriers, SCOPE_DEVICE);

   /* Do texture lowering here. We need to lower texture stuff
    * now, before we call panvk_per_arch(nir_lower_descriptors)() because some
    * of the texture lowering generates nir_texop_txs which we handle as part
    * of descriptor lowering.
    *
    * TODO: We really should be doing this in common code, not duplicated in
    * panvk. In order to do that, we need to rework the panfrost compile
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
   pan_nir_lower_texture_early(nir, pdev->kmod.props.gpu_id);
   NIR_PASS(_, nir, nir_lower_system_values);

   nir_lower_compute_system_values_options options = {
      .has_base_workgroup_id = true,
   };

   NIR_PASS(_, nir, nir_lower_compute_system_values, &options);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_lower_wpos_center);

   pan_optimize_nir(nir, pdev->kmod.props.gpu_id);

   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);

   assert(pdev->kmod.props.shader_present != 0);
   uint64_t core_max_id = util_last_bit(pdev->kmod.props.shader_present) - 1;
   NIR_PASS(_, nir, nir_inline_sysval, nir_intrinsic_load_core_max_id_arm,
            core_max_id);
}

static void
panvk_hash_state(struct vk_physical_device *device,
                 const struct vk_graphics_pipeline_state *state,
                 const struct vk_features *enabled_features,
                 VkShaderStageFlags stages, blake3_hash blake3_out)
{
   struct mesa_blake3 blake3_ctx;
   _mesa_blake3_init(&blake3_ctx);

   if (state != NULL) {
      /* This doesn't impact the shader compile but it does go in the
       * panvk_shader and gets [de]serialized along with the binary so
       * we need to hash it.
       */
      bool sample_shading_enable =
         state->ms && state->ms->sample_shading_enable;
      _mesa_blake3_update(&blake3_ctx, &sample_shading_enable,
                          sizeof(sample_shading_enable));

      _mesa_blake3_update(&blake3_ctx, &state->rp->view_mask,
                          sizeof(state->rp->view_mask));

      if (state->ial)
         _mesa_blake3_update(&blake3_ctx, state->ial, sizeof(*state->ial));
   }

   _mesa_blake3_final(&blake3_ctx, blake3_out);
}

#if PAN_ARCH >= 9
static bool
valhall_pack_buf_idx(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   unsigned index_src;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      index_src = 0;
      break;

   case nir_intrinsic_store_ssbo:
      index_src = 1;
      break;

   default:
      return false;
   }

   nir_def *index = intrin->src[index_src].ssa;

   /* The descriptor lowering pass can add UBO loads, and those already have the
    * right index format. */
   if (index->num_components == 1)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   /* The valhall backend expects nir_address_format_32bit_index_offset,
    * but address mode is nir_address_format_vec2_index_32bit_offset to allow
    * us to store the array size, set and index without losing information
    * while walking the descriptor deref chain (needed to do a bound check on
    * the array index when we reach the end of the chain).
    * Turn it back to nir_address_format_32bit_index_offset after IOs
    * have been lowered. */
   nir_def *packed_index =
      nir_iadd(b, nir_channel(b, index, 0), nir_channel(b, index, 1));
   nir_src_rewrite(&intrin->src[index_src], packed_index);
   return true;
}
#endif

static bool
valhall_lower_get_ssbo_size(struct nir_builder *b,
                            nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_get_ssbo_size)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *res_handle = nir_channel(b, intr->src[0].ssa, 0);
   nir_def *table_idx = nir_ushr_imm(b, res_handle, 24);
   nir_def *res_idx = nir_iand_imm(b, res_handle, BITFIELD_MASK(24));
   nir_def *res_table = nir_ior_imm(b, table_idx, pan_res_handle(62, 0));
   nir_def *buf_idx = nir_iadd(b, res_idx, nir_channel(b, intr->src[0].ssa, 1));
   nir_def *desc_offset = nir_imul_imm(b, buf_idx, PANVK_DESCRIPTOR_SIZE);
   nir_def *size = nir_load_ubo(
      b, 1, 32, res_table, nir_iadd_imm(b, desc_offset, 4), .range = ~0u,
      .align_mul = PANVK_DESCRIPTOR_SIZE, .align_offset = 4);

   nir_def_replace(&intr->def, size);
   return true;
}

static bool
collect_push_constant(struct nir_builder *b, nir_intrinsic_instr *intr,
                      void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   struct panvk_shader_variant *shader = data;
   uint32_t base = nir_intrinsic_base(intr);
   bool is_sysval = base >= SYSVALS_PUSH_CONST_BASE;
   uint32_t offset, size;

   if (is_sysval)
      base -= SYSVALS_PUSH_CONST_BASE;

   /* If the offset is dynamic, we need to flag [base:base+range] as used, to
    * allow global mem access. */
   if (!nir_src_is_const(intr->src[0])) {
      offset = base;
      size = nir_intrinsic_range(intr);

      /* Flag the push_uniforms sysval as needed if we have an indirect offset.
       */
      shader_use_sysval(shader, common, push_uniforms);
   } else {
      offset = base + nir_src_as_uint(intr->src[0]);
      size = (intr->def.bit_size / 8) * intr->def.num_components;
   }

   if (is_sysval)
      shader_use_sysval_range(shader, offset, size);
   else
      shader_use_push_const_range(shader, offset, size);

   return true;
}

static bool
move_push_constant(struct nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   struct panvk_shader_variant *shader = data;
   unsigned base = nir_intrinsic_base(intr);
   bool is_sysval = base >= SYSVALS_PUSH_CONST_BASE;

   if (is_sysval)
      base -= SYSVALS_PUSH_CONST_BASE;

   b->cursor = nir_before_instr(&intr->instr);

   if (nir_src_is_const(intr->src[0])) {
      unsigned offset = base + nir_src_as_uint(intr->src[0]);

      /* We place the sysvals first, and then comes the user push constants.
       * We do that so we always have the blend constants at offset 0 for
       * blend shaders. */
      if (is_sysval)
         offset = shader_remapped_sysval_offset(shader, offset);
      else
         offset = shader_remapped_push_const_offset(shader, offset);

      nir_src_rewrite(&intr->src[0], nir_imm_int(b, offset));

      /* We always set the range/base to zero, to make sure no pass is using it
       * after that point. */
      nir_intrinsic_set_base(intr, 0);
      nir_intrinsic_set_range(intr, 0);
   } else {
      /* We don't use load_sysval() on purpose, because it would set
       * .base=SYSVALS_PUSH_CONST_BASE, and we're supposed to force a base of
       * zero in this pass. */
      unsigned push_const_buf_offset = shader_remapped_sysval_offset(
         shader, sysval_offset(common, push_uniforms));
      nir_def *push_const_buf = nir_load_push_constant(
         b, 1, 64, nir_imm_int(b, push_const_buf_offset));
      unsigned push_const_offset = is_sysval ?
         shader_remapped_sysval_offset(shader, base) :
         shader_remapped_push_const_offset(shader, base);
      nir_def *offset = nir_iadd_imm(b, intr->src[0].ssa, push_const_offset);
      unsigned align = nir_combined_align(nir_intrinsic_align_mul(intr),
                                          nir_intrinsic_align_offset(intr));

      /* We assume an alignment of 64-bit max for packed push-constants. */
      align = MIN2(align, FAU_WORD_SIZE);
      nir_def *value = nir_load_global(
         b, intr->def.num_components, intr->def.bit_size,
         nir_iadd(b, push_const_buf, nir_u2u64(b, offset)), .align_mul = align);

      nir_def_replace(&intr->def, value);
   }

   return true;
}

static void
lower_load_push_consts(nir_shader *nir, struct panvk_shader_variant *shader)
{
   /* Before we lower load_push_constant()s with a dynamic offset to global
    * loads, we want to run a few optimization passes to get rid of offset
    * calculation involving only constant values. */
   bool progress = false;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_opt_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);

      nir_opt_peephole_select_options peephole_select_options = {
         .limit = 64,
         .expensive_alu_ok = true,
      };
      NIR_PASS(progress, nir, nir_opt_peephole_select, &peephole_select_options);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
   } while (progress);

   /* We always reserve the 4 blend constant words for fragment shaders,
    * because we don't know the blend configuration at this point, and
    * we might end up with a blend shader reading those blend constants. */
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      /* We rely on blend constants being placed first and covering 4 words. */
      STATIC_ASSERT(
         offsetof(struct panvk_graphics_sysvals, blend.constants) == 0 &&
         sizeof(((struct panvk_graphics_sysvals *)NULL)->blend.constants) ==
            16);

      shader_use_sysval(shader, graphics, blend.constants);
   }

   progress = false;
   NIR_PASS(progress, nir, nir_shader_intrinsics_pass, collect_push_constant,
            nir_metadata_all, shader);

   /* Some load_push_constant instructions might be eliminated after
    * scalarization+dead-code-elimination. Since these pass happen in
    * bifrost_compile(), we can't run the push_constant packing after the
    * optimization took place, so let's just have our own FAU count instead
    * of using info.push.count to make it consistent with the
    * used_{sysvals,push_consts} bitmaps, even if it sometimes implies loading
    * more than we really need. Doing that also takes into account the fact
    * blend constants are never loaded from the fragment shader, but might be
    * needed in the blend shader. */
   shader->fau.sysval_count = BITSET_COUNT(shader->fau.used_sysvals);
   /* 32 FAUs (256 bytes) are reserved for API push constants */
   assert(shader->fau.sysval_count <= FAU_WORD_COUNT - 32 &&
          "too many sysval FAUs");
   shader->fau.total_count =
      shader->fau.sysval_count + BITSET_COUNT(shader->fau.used_push_consts);
   assert(shader->fau.total_count <= FAU_WORD_COUNT &&
          "asking for more FAUs than the hardware has to offer");

   if (!progress)
      return;

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, move_push_constant,
            nir_metadata_control_flow, shader);
}

struct lower_ycbcr_state {
   uint32_t set_layout_count;
   struct vk_descriptor_set_layout *const *set_layouts;
};

static const struct vk_ycbcr_conversion_state *
lookup_ycbcr_conversion(const void *_state, uint32_t set,
                        uint32_t binding, uint32_t array_index)
{
   const struct lower_ycbcr_state *state = _state;
   assert(set < state->set_layout_count);
   assert(state->set_layouts[set] != NULL);
   const struct panvk_descriptor_set_layout *set_layout =
      to_panvk_descriptor_set_layout(state->set_layouts[set]);
   assert(binding < set_layout->binding_count);

   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &set_layout->bindings[binding];

   if (bind_layout->immutable_samplers == NULL)
      return NULL;

   array_index = MIN2(array_index, bind_layout->desc_count - 1);

   const struct panvk_sampler *sampler =
      bind_layout->immutable_samplers[array_index];

   return sampler && sampler->vk.ycbcr_conversion ?
          &sampler->vk.ycbcr_conversion->state : NULL;
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void
panvk_lower_nir(struct panvk_device *dev, nir_shader *nir,
                uint32_t set_layout_count,
                struct vk_descriptor_set_layout *const *set_layouts,
                const struct vk_pipeline_robustness_state *rs,
                const uint32_t *noperspective_varyings,
                const struct vk_graphics_pipeline_state *state,
                const struct pan_compile_inputs *compile_input,
                struct panvk_shader_variant *shader)
{
   mesa_shader_stage stage = nir->info.stage;

   const nir_opt_access_options access_options = {
      .is_vulkan = true,
   };
   NIR_PASS(_, nir, nir_opt_access, &access_options);

#if PAN_ARCH >= 10
   if (stage == MESA_SHADER_VERTEX && compile_input->view_mask) {
      nir_lower_multiview_options options = {
         .view_mask = compile_input->view_mask,
         .allowed_per_view_outputs = ~0
      };
      /* The only case where this should fail is with memory/image writes,
       * which we don't support in vertex shaders */
      assert(nir_can_lower_multiview(nir, options));
      NIR_PASS(_, nir, nir_lower_multiview, options);
      /* Pull output writes out of the loop and give them constant offsets for
       * pan_nir_lower_store_components */
      NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries,
               nir_shader_get_entrypoint(nir), nir_var_shader_out);
   }
#endif

   /* Lower input intrinsics for fragment shaders early to get the max
    * number of varying loads, as this number is required during descriptor
    * lowering for v9+. */
   if (stage == MESA_SHADER_FRAGMENT) {
      nir_assign_io_var_locations(nir, nir_var_shader_in);
#if PAN_ARCH >= 9
      shader->desc_info.max_varying_loads = nir->num_inputs;
#endif
   }

   const struct lower_ycbcr_state ycbcr_state = {
      .set_layout_count = set_layout_count,
      .set_layouts = set_layouts,
   };
   NIR_PASS(_, nir, nir_vk_lower_ycbcr_tex, lookup_ycbcr_conversion,
            &ycbcr_state);

   panvk_per_arch(nir_lower_descriptors)(nir, dev, rs, set_layout_count,
                                         set_layouts, state, shader);

   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo,
            panvk_buffer_ubo_addr_format(rs->uniform_buffers));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ssbo,
            panvk_buffer_ssbo_addr_format(rs->storage_buffers));
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const,
            nir_address_format_32bit_offset);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_global,
            nir_address_format_64bit_global);

   /* nir_lower_non_uniform_access needs to run after lowering UBO and SSBO
    * IO. This means we run it after nir_lower_descriptors, which reads the
    * array indices, but it's okay because lower_descriptors treats all
    * dynamic indices the same. */
   enum nir_lower_non_uniform_access_type lower_non_uniform_access_types =
      nir_lower_non_uniform_ubo_access |
      nir_lower_non_uniform_ssbo_access |
      nir_lower_non_uniform_texture_access |
      nir_lower_non_uniform_image_access |
      nir_lower_non_uniform_get_ssbo_size;
#if PAN_ARCH < 9
   lower_non_uniform_access_types |=
      nir_lower_non_uniform_texture_offset_access;
#endif

   /* In practice, most shaders do not have non-uniform-qualified accesses
    * thus a cheaper and likely to fail check is run first. */
   if (nir_has_non_uniform_access(nir, lower_non_uniform_access_types)) {
      NIR_PASS(_, nir, nir_opt_non_uniform_access);
      struct nir_lower_non_uniform_access_options opts = {
         .types = lower_non_uniform_access_types,
      };
      NIR_PASS(_, nir, nir_lower_non_uniform_access, &opts);
   }

#if PAN_ARCH >= 9
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, valhall_lower_get_ssbo_size,
            nir_metadata_control_flow, NULL);
   NIR_PASS(_, nir, nir_shader_instructions_pass, valhall_pack_buf_idx,
            nir_metadata_control_flow, NULL);
#endif

   if (mesa_shader_stage_uses_workgroup(stage)) {
      NIR_PASS(_, nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
               shared_type_info);

      NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_shared,
               nir_address_format_32bit_offset);
   }

   if (nir->info.zero_initialize_shared_memory && nir->info.shared_size > 0) {
      /* Align everything up to 16 bytes to take advantage of load store
       * vectorization. */
      nir->info.shared_size = align(nir->info.shared_size, 16);
      NIR_PASS(_, nir, nir_zero_initialize_shared_memory, nir->info.shared_size,
               16);

      /* We need to call lower_compute_system_values again because
       * nir_zero_initialize_shared_memory generates load_invocation_id which
       * has to be lowered to load_invocation_index.
       */
      NIR_PASS(_, nir, nir_lower_compute_system_values, NULL);
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
   } else if (stage != MESA_SHADER_FRAGMENT) {
      /* Input varyings in fragment shader have been lowered early. */
      nir_assign_io_var_locations(nir, nir_var_shader_in);
   }

   nir_assign_io_var_locations(nir, nir_var_shader_out);

   /* Needed to turn shader_temp into function_temp since the backend only
    * handles the latter for now.
    */
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (PANVK_DEBUG(NIR)) {
      mesa_logi("translated nir:");
      nir_log_shaderi(nir);
   }

   pan_preprocess_nir(nir, compile_input->gpu_id);

   /* Postprocess can add copies back in and lower_io can't handle them */
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_indirect_derefs_to_if_else_trees,
            nir_var_shader_in | nir_var_shader_out, UINT32_MAX);
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            glsl_type_size, nir_lower_io_use_interpolated_input_intrinsics);

   pan_postprocess_nir(nir, compile_input->gpu_id);
   pan_nir_lower_texture_late(nir, compile_input->gpu_id);

   if (stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, panvk_lower_load_vs_input,
               nir_metadata_control_flow, NULL);
   else if (stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, panvk_lower_load_fs_input,
               nir_metadata_control_flow, NULL);

   /* since valhall, panvk_per_arch(nir_lower_descriptors) separates the
    * driver set and the user sets, and does not need pan_nir_lower_image_index
    */
   if (PAN_ARCH < 9 && stage == MESA_SHADER_VERTEX)
      NIR_PASS(_, nir, pan_nir_lower_image_index, MAX_VS_ATTRIBS);

   if (noperspective_varyings && stage == MESA_SHADER_VERTEX) {
      NIR_PASS(_, nir, nir_inline_sysval,
               nir_intrinsic_load_noperspective_varyings_pan,
               *noperspective_varyings);
   }

   struct panvk_lower_sysvals_context lower_sysvals_ctx = {
      .shader = shader,
      .state = state,
   };

   NIR_PASS(_, nir, nir_shader_instructions_pass, panvk_lower_sysvals,
            nir_metadata_control_flow, &lower_sysvals_ctx);

   lower_load_push_consts(nir, shader);
}

static VkResult
panvk_compile_nir(struct panvk_device *dev, nir_shader *nir,
                  VkShaderCreateFlagsEXT shader_flags,
                  struct pan_compile_inputs *compile_input,
                  struct panvk_shader_variant *shader)
{
   const bool dump_asm =
      shader_flags & VK_SHADER_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_MESA;

   struct util_dynarray binary = UTIL_DYNARRAY_INIT;
   pan_shader_compile(nir, compile_input, &binary, &shader->info);

   /* Propagate potential additional FAU values into the panvk info struct. */
   /* FAU consts are pushed as 32bit values, but total_count is for 64bit
    * ones. */
   shader->fau.total_count += DIV_ROUND_UP(shader->info.fau_consts_count, 2);

   void *bin_ptr = util_dynarray_element(&binary, uint8_t, 0);
   unsigned bin_size = util_dynarray_num_elements(&binary, uint8_t);

   shader->bin_size = 0;
   shader->bin_ptr = NULL;

   if (bin_size) {
      void *data = malloc(bin_size);

      if (data == NULL)
         return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

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
            pan_disassemble(stream, shader->bin_ptr, shader->bin_size,
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

#if PAN_ARCH < 9
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
#endif

   switch (nir->info.stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      shader->cs.local_size.x = nir->info.workgroup_size[0];
      shader->cs.local_size.y = nir->info.workgroup_size[1];
      shader->cs.local_size.z = nir->info.workgroup_size[2];
      break;

   case MESA_SHADER_FRAGMENT:
      shader->fs.earlyzs_lut = pan_earlyzs_analyze(&shader->info, PAN_ARCH);
      break;

   default:
      break;
   }

   return VK_SUCCESS;
}

#if PAN_ARCH >= 9
static enum mali_flush_to_zero_mode
shader_ftz_mode(struct panvk_shader_variant *shader)
{
   if (shader->info.ftz_fp32) {
      if (shader->info.ftz_fp16)
         return MALI_FLUSH_TO_ZERO_MODE_ALWAYS;
      else
         return MALI_FLUSH_TO_ZERO_MODE_DX11;
   } else {
      /* We don't have a "flush FP16, preserve FP32" mode, but APIs
       * should not be able to generate that.
       */
      assert(!shader->info.ftz_fp16 && !shader->info.ftz_fp32);
      return MALI_FLUSH_TO_ZERO_MODE_PRESERVE_SUBNORMALS;
   }
}
#endif

static VkResult
panvk_shader_upload(struct panvk_device *dev,
                    struct panvk_shader_variant *shader,
                    const VkAllocationCallbacks *pAllocator)
{
   shader->code_mem = (struct panvk_priv_mem){0};

#if PAN_ARCH < 9
   shader->rsd = (struct panvk_priv_mem){0};
#else
   shader->spd = (struct panvk_priv_mem){0};
#endif

   if (!shader->bin_size)
      return VK_SUCCESS;

   shader->code_mem = panvk_pool_upload_aligned(
      &dev->mempools.exec, shader->bin_ptr, shader->bin_size, 128);
   if (!panvk_priv_mem_dev_addr(shader->code_mem))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

#if PAN_ARCH < 9
   if (shader->info.stage == MESA_SHADER_FRAGMENT)
      return VK_SUCCESS;

   shader->rsd = panvk_pool_alloc_desc(&dev->mempools.rw, RENDERER_STATE);
   if (!panvk_priv_mem_dev_addr(shader->rsd))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   pan_cast_and_pack(panvk_priv_mem_host_addr(shader->rsd), RENDERER_STATE,
                     cfg) {
      pan_shader_prepare_rsd(&shader->info,
                             panvk_shader_variant_get_dev_addr(shader), &cfg);
   }
#else
   if (shader->info.stage != MESA_SHADER_VERTEX) {
      shader->spd = panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_dev_addr(shader->spd))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      pan_cast_and_pack(panvk_priv_mem_host_addr(shader->spd), SHADER_PROGRAM,
                        cfg) {
         cfg.stage = pan_shader_stage(&shader->info);

         if (cfg.stage == MALI_SHADER_STAGE_FRAGMENT)
            cfg.fragment_coverage_bitmask_type = MALI_COVERAGE_BITMASK_TYPE_GL;
#if PAN_ARCH < 12
         else if (cfg.stage == MALI_SHADER_STAGE_VERTEX)
            cfg.vertex_warp_limit = MALI_WARP_LIMIT_HALF;
#endif

         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader);
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);

         if (cfg.stage == MALI_SHADER_STAGE_FRAGMENT)
            cfg.requires_helper_threads = shader->info.contains_barrier;
      }
   } else {
#if PAN_ARCH >= 12
      shader->spds.all_points =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_dev_addr(shader->spds.all_points))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      pan_cast_and_pack(panvk_priv_mem_host_addr(shader->spds.all_points),
                        SHADER_PROGRAM, cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader);
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }

      shader->spds.all_triangles =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_dev_addr(shader->spds.all_triangles))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      pan_cast_and_pack(panvk_priv_mem_host_addr(shader->spds.all_triangles),
                        SHADER_PROGRAM, cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader) +
                      shader->info.vs.no_psiz_offset;
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }
#else
      shader->spds.pos_points =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_dev_addr(shader->spds.pos_points))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      pan_cast_and_pack(panvk_priv_mem_host_addr(shader->spds.pos_points),
                        SHADER_PROGRAM, cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.vertex_warp_limit = MALI_WARP_LIMIT_HALF;
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader);
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }

      shader->spds.pos_triangles =
         panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
      if (!panvk_priv_mem_dev_addr(shader->spds.pos_triangles))
         return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      pan_cast_and_pack(panvk_priv_mem_host_addr(shader->spds.pos_triangles),
                        SHADER_PROGRAM, cfg) {
         cfg.stage = pan_shader_stage(&shader->info);
         cfg.vertex_warp_limit = MALI_WARP_LIMIT_HALF;
         cfg.register_allocation =
            pan_register_allocation(shader->info.work_reg_count);
         cfg.binary = panvk_shader_variant_get_dev_addr(shader) +
                      shader->info.vs.no_psiz_offset;
         cfg.preload.r48_r63 = (shader->info.preload >> 48);
         cfg.flush_to_zero_mode = shader_ftz_mode(shader);
      }

      if (shader->info.vs.secondary_enable) {
         shader->spds.var =
            panvk_pool_alloc_desc(&dev->mempools.rw, SHADER_PROGRAM);
         if (!panvk_priv_mem_dev_addr(shader->spds.var))
            return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

         pan_cast_and_pack(panvk_priv_mem_host_addr(shader->spds.var),
                           SHADER_PROGRAM, cfg) {
            unsigned work_count = shader->info.vs.secondary_work_reg_count;

            cfg.stage = pan_shader_stage(&shader->info);
            cfg.vertex_warp_limit = MALI_WARP_LIMIT_FULL;
            cfg.register_allocation = pan_register_allocation(work_count);
            cfg.binary = panvk_shader_variant_get_dev_addr(shader) +
                         shader->info.vs.secondary_offset;
            cfg.preload.r48_r63 = (shader->info.vs.secondary_preload >> 48);
            cfg.flush_to_zero_mode = shader_ftz_mode(shader);
         }
      }
#endif
   }
#endif

   return VK_SUCCESS;
}

static void
panvk_shader_variant_destroy(struct panvk_shader_variant *shader)
{
   free((void *)shader->asm_str);
   ralloc_free((void *)shader->nir_str);

   panvk_pool_free_mem(&shader->code_mem);

#if PAN_ARCH < 9
   panvk_pool_free_mem(&shader->rsd);
   panvk_pool_free_mem(&shader->desc_info.others.map);
#else
   if (shader->info.stage != MESA_SHADER_VERTEX) {
      panvk_pool_free_mem(&shader->spd);
   } else {
#if PAN_ARCH >= 12
      panvk_pool_free_mem(&shader->spds.all_points);
      panvk_pool_free_mem(&shader->spds.all_triangles);
#else
      panvk_pool_free_mem(&shader->spds.var);
      panvk_pool_free_mem(&shader->spds.pos_points);
      panvk_pool_free_mem(&shader->spds.pos_triangles);
#endif
   }
#endif

   if (shader->own_bin)
      free((void *)shader->bin_ptr);
}

static void
panvk_shader_destroy(struct vk_device *vk_dev, struct vk_shader *vk_shader,
                     const VkAllocationCallbacks *pAllocator)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   panvk_shader_foreach_variant(shader, variant) {
      panvk_shader_variant_destroy(variant);
   }

   vk_shader_free(vk_dev, pAllocator, &shader->vk);
}

static const struct vk_shader_ops panvk_shader_ops;

static VkResult
panvk_compile_shader(struct panvk_device *dev,
                     struct vk_shader_compile_info *info,
                     const struct vk_graphics_pipeline_state *state,
                     const uint32_t *noperspective_varyings,
                     const VkAllocationCallbacks *pAllocator,
                     struct vk_shader **shader_out)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   struct panvk_shader *shader;
   VkResult result;

   /* We consume the NIR, regardless of success or failure */
   nir_shader *nir = info->nir;

   size_t size =
      sizeof(struct panvk_shader) + sizeof(struct panvk_shader_variant) *
                                       panvk_shader_num_variants(info->stage);
   shader = vk_shader_zalloc(&dev->vk, &panvk_shader_ops, info->stage,
                             pAllocator, size);
   if (shader == NULL)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   nir_variable_mode robust2_modes = 0;
   if (info->robustness->uniform_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT)
      robust2_modes |= nir_var_mem_ubo;
   if (info->robustness->storage_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2_EXT)
      robust2_modes |= nir_var_mem_ssbo;

   struct pan_compile_inputs inputs = {
      .gpu_id = phys_dev->kmod.props.gpu_id,
      .gpu_variant = phys_dev->kmod.props.gpu_variant,
      .view_mask = (state && state->rp) ? state->rp->view_mask : 0,
      .robust2_modes = robust2_modes,
      .robust_descriptors = dev->vk.enabled_features.nullDescriptor,
   };

   if (info->stage == MESA_SHADER_FRAGMENT && state != NULL &&
       state->ms != NULL && state->ms->sample_shading_enable)
      nir->info.fs.uses_sample_shading = true;

   if (info->stage == MESA_SHADER_VERTEX) {
      struct pan_compile_inputs input_variants[PANVK_VS_VARIANTS] = {0};
      nir_shader *nir_variants[PANVK_VS_VARIANTS] = {0};

      /* First we apply lowering for variants */
      for (enum panvk_vs_variant v = 0; v < PANVK_VS_VARIANTS; ++v) {
         struct panvk_shader_variant *variant = &shader->variants[v];
         bool last = (v + 1) == PANVK_VS_VARIANTS;

         input_variants[v] = inputs;

         /* Each variant gets its own NIR. To save an extra clone, we use the
          * original NIR for the last stage.
          */
         nir_variants[v] = last ? nir : nir_shader_clone(NULL, nir);

         panvk_lower_nir(dev, nir_variants[v], info->set_layout_count,
                         info->set_layouts, info->robustness,
                         noperspective_varyings, state, &input_variants[v],
                         variant);

         /* Allow the remaining FAU space to be filled with constants. */
         input_variants[v].fau_consts.max_amount =
            2 * (FAU_WORD_COUNT - variant->fau.total_count);
         input_variants[v].fau_consts.offset = variant->fau.total_count * 2;
         input_variants[v].fau_consts.values = &variant->info.fau_consts[0];
         assert(input_variants[v].fau_consts.max_amount <= ARRAY_SIZE(variant->info.fau_consts));

         variant->own_bin = true;

         result = panvk_compile_nir(dev, nir_variants[v], info->flags,
                                    &input_variants[v], variant);

         /* We need to update info.push.count because it's used to initialize the
         * RSD in pan_shader_prepare_rsd(). */
         variant->info.push.count = variant->fau.total_count * 2;

         if (result != VK_SUCCESS) {
            panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
            return result;
         }

         result = panvk_shader_upload(dev, variant, pAllocator);

         if (result != VK_SUCCESS) {
            panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
            return result;
         }
      }
   } else {
      struct panvk_shader_variant *variant =
         (struct panvk_shader_variant *)panvk_shader_only_variant(shader);
      variant->own_bin = true;

      panvk_lower_nir(dev, nir, info->set_layout_count, info->set_layouts,
                      info->robustness, noperspective_varyings, state, &inputs,
                      variant);

#if PAN_ARCH >= 9
      if (info->stage == MESA_SHADER_FRAGMENT)
         /* Use LD_VAR_BUF[_IMM] for varyings if possible. */
         inputs.valhall.use_ld_var_buf = panvk_use_ld_var_buf(variant);
#endif

      /* Allow the remaining FAU space to be filled with constants. */
      inputs.fau_consts.max_amount = 2 * (FAU_WORD_COUNT - variant->fau.total_count);
      inputs.fau_consts.offset = variant->fau.total_count * 2;
      inputs.fau_consts.values = &variant->info.fau_consts[0];
      assert(inputs.fau_consts.max_amount <= ARRAY_SIZE(variant->info.fau_consts));

      result = panvk_compile_nir(dev, nir, info->flags, &inputs, variant);

      /* We need to update info.push.count because it's used to initialize the
      * RSD in pan_shader_prepare_rsd(). */
      variant->info.push.count = variant->fau.total_count * 2;

      if (result != VK_SUCCESS) {
         panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }

      result = panvk_shader_upload(dev, variant, pAllocator);

      if (result != VK_SUCCESS) {
         panvk_shader_destroy(&dev->vk, &shader->vk, pAllocator);
         return result;
      }
   }

   *shader_out = &shader->vk;

   return result;
}

VkResult
panvk_per_arch(create_shader_from_binary)(struct panvk_device *dev,
                                          const struct pan_shader_info *info,
                                          struct pan_compute_dim local_size,
                                          const void *bin_ptr, size_t bin_size,
                                          struct panvk_shader **shader_out)
{
   struct panvk_shader *shader;
   VkResult result;

   size_t size =
      sizeof(struct panvk_shader) + sizeof(struct panvk_shader_variant) *
                                       panvk_shader_num_variants(info->stage);
   shader = vk_shader_zalloc(&dev->vk, &panvk_shader_ops, info->stage,
                             &dev->vk.alloc, size);
   if (shader == NULL)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(panvk_shader_num_variants(info->stage) == 1);

   struct panvk_shader_variant *variant = &shader->variants[0];
   variant->info = *info;
   variant->cs.local_size = local_size;
   variant->bin_ptr = bin_ptr;
   variant->bin_size = bin_size;
   variant->own_bin = false;
   variant->nir_str = NULL;
   variant->asm_str = NULL;

   result = panvk_shader_upload(dev, variant, &dev->vk.alloc);

   if (result != VK_SUCCESS) {
      panvk_shader_destroy(&dev->vk, &shader->vk, &dev->vk.alloc);
      return result;
   }

   *shader_out = shader;

   return result;
}

static VkResult
panvk_compile_shaders(struct vk_device *vk_dev, uint32_t shader_count,
                      struct vk_shader_compile_info *infos,
                      const struct vk_graphics_pipeline_state *state,
                      const struct vk_features *enabled_features,
                      const VkAllocationCallbacks *pAllocator,
                      struct vk_shader **shaders_out)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   bool use_static_noperspective = false;
   uint32_t noperspective_varyings = 0;
   VkResult result;
   int32_t i;

   /* Vulkan runtime passes us shaders in stage order, so the FS will always
    * be last if it exists. Iterate shaders in reverse order to ensure FS is
    * processed before VS. */
   for (i = shader_count - 1; i >= 0; i--) {
      uint32_t *noperspective_varyings_ptr =
         use_static_noperspective ? &noperspective_varyings : NULL;
      result =
         panvk_compile_shader(dev, &infos[i], state, noperspective_varyings_ptr,
                              pAllocator, &shaders_out[i]);

      if (result != VK_SUCCESS)
         goto err_cleanup;

      /* If we are linking VS and FS, we can use the static interpolation
       * qualifiers from the FS in the VS. */
      if (infos[i].nir->info.stage == MESA_SHADER_FRAGMENT) {
         struct panvk_shader *shader =
            container_of(shaders_out[i], struct panvk_shader, vk);
         const struct panvk_shader_variant *variant =
            panvk_shader_only_variant(shader);

         use_static_noperspective = true;
         noperspective_varyings = variant->info.varyings.noperspective;
      }

      /* Clean up NIR for the current shader */
      ralloc_free(infos[i].nir);
   }

   /* TODO: If we get multiple shaders here, we can perform part of the link
    * logic at compile time. */

   return VK_SUCCESS;

err_cleanup:
   /* Clean up all the shaders before this point */
   for (int32_t j = shader_count - 1; j > i; j--)
      panvk_shader_destroy(&dev->vk, shaders_out[j], pAllocator);

   /* Clean up all the NIR from this point */
   for (int32_t j = i; j >= 0; j--)
      ralloc_free(infos[j].nir);

   /* Memset the output array */
   memset(shaders_out, 0, shader_count * sizeof(*shaders_out));

   return result;
}

static VkResult
shader_desc_info_deserialize(struct panvk_device *dev,
                             struct blob_reader *blob,
                             struct panvk_shader_variant *shader)
{
   shader->desc_info.used_set_mask = blob_read_uint32(blob);

#if PAN_ARCH < 9
   shader->desc_info.dyn_ubos.count = blob_read_uint32(blob);
   blob_copy_bytes(blob, shader->desc_info.dyn_ubos.map,
                   sizeof(*shader->desc_info.dyn_ubos.map) *
                      shader->desc_info.dyn_ubos.count);
   shader->desc_info.dyn_ssbos.count = blob_read_uint32(blob);
   blob_copy_bytes(blob, shader->desc_info.dyn_ssbos.map,
                   sizeof(*shader->desc_info.dyn_ssbos.map) *
                      shader->desc_info.dyn_ssbos.count);

   uint32_t others_count = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(shader->desc_info.others.count); i++) {
      shader->desc_info.others.count[i] = blob_read_uint32(blob);
      others_count += shader->desc_info.others.count[i];
   }

   if (others_count) {
      struct panvk_pool_alloc_info alloc_info = {
         .size = others_count * sizeof(uint32_t),
         .alignment = sizeof(uint32_t),
      };
      shader->desc_info.others.map =
         panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
      uint32_t *copy_table =
         panvk_priv_mem_host_addr(shader->desc_info.others.map);

      if (!copy_table)
         return panvk_error(shader, VK_ERROR_OUT_OF_DEVICE_MEMORY);

      blob_copy_bytes(blob, copy_table, others_count * sizeof(*copy_table));
   }
#else
   shader->desc_info.dyn_bufs.count = blob_read_uint32(blob);
   blob_copy_bytes(blob, shader->desc_info.dyn_bufs.map,
                   sizeof(*shader->desc_info.dyn_bufs.map) *
                      shader->desc_info.dyn_bufs.count);
   shader->desc_info.max_varying_loads = blob_read_uint32(blob);
#endif

   return VK_SUCCESS;
}

static VkResult
panvk_deserialize_shader_variant(struct vk_device *vk_dev,
                                 struct blob_reader *blob,
                                 const VkAllocationCallbacks *pAllocator,
                                 struct panvk_shader_variant *shader)
{
   struct panvk_device *device = to_panvk_device(vk_dev);
   struct pan_shader_info info;
   VkResult result;

   blob_copy_bytes(blob, &info, sizeof(info));
   if (blob->overrun)
      return panvk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader->info = info;
   blob_copy_bytes(blob, &shader->fau, sizeof(shader->fau));

   switch (shader->info.stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      blob_copy_bytes(blob, &shader->cs.local_size,
                      sizeof(shader->cs.local_size));
      break;

   case MESA_SHADER_FRAGMENT:
      shader->fs.earlyzs_lut = pan_earlyzs_analyze(&shader->info, PAN_ARCH);
      blob_copy_bytes(blob, &shader->fs.input_attachment_read,
                      sizeof(shader->fs.input_attachment_read));
      break;

   default:
      break;
   }

   shader->bin_size = blob_read_uint32(blob);

   if (blob->overrun)
      return panvk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   shader->bin_ptr = malloc(shader->bin_size);
   if (shader->bin_ptr == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   blob_copy_bytes(blob, (void *)shader->bin_ptr, shader->bin_size);

   result = shader_desc_info_deserialize(device, blob, shader);

   if (result != VK_SUCCESS)
      return panvk_error(device, result);

   uint32_t nir_str_size = blob_read_uint32(blob);
   uint32_t asm_str_size = blob_read_uint32(blob);
   const char *nir_str = blob_read_bytes(blob, nir_str_size);
   const char *asm_str = blob_read_bytes(blob, asm_str_size);

   if (blob->overrun)
      return panvk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   if (nir_str_size > 0) {
      shader->nir_str = ralloc_strndup(NULL, nir_str, nir_str_size);
      if (shader->nir_str == NULL)
         return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   if (asm_str_size > 0) {
      shader->asm_str = strndup(asm_str, asm_str_size);
      if (shader->asm_str == NULL)
         return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   result = panvk_shader_upload(device, shader, pAllocator);

   if (result != VK_SUCCESS)
      return result;

   return result;
}

static VkResult
panvk_deserialize_shader(struct vk_device *vk_dev, struct blob_reader *blob,
                         uint32_t binary_version,
                         const VkAllocationCallbacks *pAllocator,
                         struct vk_shader **shader_out)
{
   struct panvk_device *device = to_panvk_device(vk_dev);
   struct panvk_shader *shader;

   mesa_shader_stage stage = blob_read_uint8(blob);
   if (blob->overrun)
      return vk_error(device, VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT);

   size_t size =
      sizeof(struct panvk_shader) +
      sizeof(struct panvk_shader_variant) * panvk_shader_num_variants(stage);
   shader =
      vk_shader_zalloc(vk_dev, &panvk_shader_ops, stage, pAllocator, size);
   if (shader == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   panvk_shader_foreach_variant(shader, variant) {
      VkResult result =
         panvk_deserialize_shader_variant(vk_dev, blob, pAllocator, variant);

      if (result != VK_SUCCESS) {
         panvk_shader_destroy(vk_dev, &shader->vk, pAllocator);
         return result;
      }
   }

   *shader_out = &shader->vk;

   return VK_SUCCESS;
}

static void
shader_desc_info_serialize(struct blob *blob,
                           const struct panvk_shader_variant *shader)
{
   blob_write_uint32(blob, shader->desc_info.used_set_mask);

#if PAN_ARCH < 9
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
#else
   blob_write_uint32(blob, shader->desc_info.dyn_bufs.count);
   blob_write_bytes(blob, shader->desc_info.dyn_bufs.map,
                    sizeof(*shader->desc_info.dyn_bufs.map) *
                       shader->desc_info.dyn_bufs.count);
   blob_write_uint32(blob, shader->desc_info.max_varying_loads);
#endif
}

static bool
panvk_shader_serialize_variant(struct vk_device *vk_dev,
                               const struct panvk_shader_variant *shader,
                               struct blob *blob)
{
   blob_write_bytes(blob, &shader->info, sizeof(shader->info));
   blob_write_bytes(blob, &shader->fau, sizeof(shader->fau));

   switch (shader->info.stage) {
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      blob_write_bytes(blob, &shader->cs.local_size,
                       sizeof(shader->cs.local_size));
      break;

   case MESA_SHADER_FRAGMENT:
      blob_write_bytes(blob, &shader->fs.input_attachment_read,
                       sizeof(shader->fs.input_attachment_read));
      break;

   default:
      break;
   }

   blob_write_uint32(blob, shader->bin_size);
   blob_write_bytes(blob, shader->bin_ptr, shader->bin_size);
   shader_desc_info_serialize(blob, shader);

   /* Include the terminating NULL in the serialization */
   uint32_t nir_str_size = shader->nir_str ? strlen(shader->nir_str) + 1 : 0;
   uint32_t asm_str_size = shader->asm_str ? strlen(shader->asm_str) + 1 : 0;
   blob_write_uint32(blob, nir_str_size);
   blob_write_uint32(blob, asm_str_size);
   blob_write_bytes(blob, shader->nir_str, nir_str_size);
   blob_write_bytes(blob, shader->asm_str, asm_str_size);

   return !blob->out_of_memory;
}

static bool
panvk_shader_serialize(struct vk_device *vk_dev,
                       const struct vk_shader *vk_shader, struct blob *blob)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   blob_write_uint8(blob, vk_shader->stage);

   panvk_shader_foreach_variant(shader, variant) {
      panvk_shader_serialize_variant(vk_dev, variant, blob);
   }

   return !blob->out_of_memory;
}

static VkResult
panvk_shader_get_executable_properties(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t *executable_count, VkPipelineExecutablePropertiesKHR *properties)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out, properties,
                          executable_count);

   panvk_shader_foreach_variant(shader, variant) {
      /* Ignore absent variants but always add vertex on IDVS */
      if (variant->bin_size == 0 &&
          (variant->info.stage != MESA_SHADER_VERTEX || !variant->info.vs.idvs))
         continue;

      const char *variant_name = panvk_shader_variant_name(shader, variant);
      const char *stage_name = _mesa_shader_stage_to_string(shader->vk.stage);

      vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props)
      {
         props->stages = mesa_to_vk_shader_stage(shader->vk.stage);
         props->subgroupSize = pan_subgroup_size(PAN_ARCH);

         if (variant_name != NULL) {
            VK_PRINT_STR(props->name, "%s %s", variant_name, stage_name);
            VK_PRINT_STR(props->description, "%s %s shader", variant_name,
                         stage_name);
         } else {
            VK_COPY_STR(props->name, stage_name);
            VK_PRINT_STR(props->description, "%s shader", stage_name);
         }
      }

      if (variant->info.stage == MESA_SHADER_VERTEX && variant->info.vs.idvs) {
         vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out,
                                  props)
         {
            props->stages = mesa_to_vk_shader_stage(shader->vk.stage);
            props->subgroupSize = pan_subgroup_size(PAN_ARCH);
            VK_COPY_STR(props->name, "varying");
            VK_COPY_STR(props->description, "varying shader");
         }
      }
   }

   return vk_outarray_status(&out);
}

static const struct panvk_shader_variant *
get_variant_from_executable_index(struct panvk_shader *shader,
                                  uint32_t executable_index)
{
   uint32_t i = 0;

   panvk_shader_foreach_variant(shader, variant) {
      /* Ignore absent variants but always add vertex on IDVS */
      if (variant->bin_size == 0 &&
          (variant->info.stage != MESA_SHADER_VERTEX || !variant->info.vs.idvs))
         continue;

      if (i == executable_index)
         return variant;

      i++;
   }

   return NULL;
}

static VkResult
panvk_shader_get_executable_statistics(
   UNUSED struct vk_device *device, const struct vk_shader *vk_shader,
   uint32_t executable_index, uint32_t *statistic_count,
   VkPipelineExecutableStatisticKHR *statistics)
{
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   bool needs_vary = false;
   if (shader->vk.stage == MESA_SHADER_VERTEX) {
      assert(executable_index == 0 || executable_index == 1);

      needs_vary = executable_index == 1;

      /* Readjust index to skip embedded varying variant */
      if (executable_index >= 1)
         executable_index--;
   }

   assert(executable_index < panvk_shader_num_variants(shader->vk.stage));
   const struct panvk_shader_variant *variant =
      get_variant_from_executable_index(shader, executable_index);
   assert(variant != NULL);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out, statistics,
                          statistic_count);

   assert(executable_index == 0 || executable_index == 1);
   const struct pan_stats *stats =
      needs_vary ? &variant->info.stats_idvs_varying : &variant->info.stats;

   vk_add_pan_stats(out, stats);
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
   struct panvk_shader *shader =
      container_of(vk_shader, struct panvk_shader, vk);

   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          internal_representations,
                          internal_representation_count);

   bool needs_vary = false;
   if (shader->vk.stage == MESA_SHADER_VERTEX) {
      assert(executable_index == 0 || executable_index == 1);

      needs_vary = executable_index == 1;

      /* Readjust index to skip embedded varying variant */
      if (executable_index >= 1)
         executable_index--;
   }

   /* XXX: Varying shader assembly */
   if (needs_vary)
      return vk_outarray_status(&out);

   assert(executable_index < panvk_shader_num_variants(shader->vk.stage));
   const struct panvk_shader_variant *variant =
      get_variant_from_executable_index(shader, executable_index);
   assert(variant != NULL);

   bool incomplete_text = false;

   if (variant->nir_str != NULL) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR,
                               &out, ir)
      {
         VK_COPY_STR(ir->name, "NIR shader");
         VK_COPY_STR(ir->description,
                     "NIR shader before sending to the back-end compiler");
         if (!write_ir_text(ir, variant->nir_str))
            incomplete_text = true;
      }
   }

   if (variant->asm_str != NULL) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR,
                               &out, ir)
      {
         VK_COPY_STR(ir->name, "Assembly");
         VK_COPY_STR(ir->description, "Final Assembly");
         if (!write_ir_text(ir, variant->asm_str))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}

#if PAN_ARCH < 9
static mali_pixel_format
get_varying_format(mesa_shader_stage stage, gl_varying_slot loc,
                   enum pipe_format pfmt)
{
   switch (loc) {
   case VARYING_SLOT_PNTC:
   case VARYING_SLOT_PSIZ:
#if PAN_ARCH <= 6
      return (MALI_R16F << 12) | pan_get_default_swizzle(1);
#else
      return (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_R000;
#endif
   case VARYING_SLOT_POS:
#if PAN_ARCH <= 6
      return (MALI_SNAP_4 << 12) | pan_get_default_swizzle(4);
#else
      return (MALI_SNAP_4 << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
#endif
   default:
      assert(pfmt != PIPE_FORMAT_NONE);
      return GENX(pan_format_from_pipe_format)(pfmt)->hw;
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
      return (MALI_R16F << 12) | pan_get_default_swizzle(1);
#else
      return (MALI_R16F << 12) | MALI_RGB_COMPONENT_ORDER_R000;
#endif
   case VARYING_SLOT_POS:
#if PAN_ARCH <= 6
      return (MALI_SNAP_4 << 12) | pan_get_default_swizzle(4);
#else
      return (MALI_SNAP_4 << 12) | MALI_RGB_COMPONENT_ORDER_RGBA;
#endif
   default:
      return GENX(pan_format_from_pipe_format)(pfmt)->hw;
   }
}

static VkResult
emit_varying_attrs(struct panvk_pool *desc_pool,
                   const struct pan_shader_varying *varyings,
                   unsigned varying_count, const struct varyings_info *info,
                   unsigned *buf_offsets, struct panvk_priv_mem *mem)
{
   *mem = panvk_pool_alloc_desc_array(desc_pool, varying_count, ATTRIBUTE);

   if (varying_count && !panvk_priv_mem_dev_addr(*mem))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   struct mali_attribute_packed *attrs = panvk_priv_mem_host_addr(*mem);
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

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(link_shaders)(struct panvk_pool *desc_pool,
                             const struct panvk_shader_variant *vs,
                             const struct panvk_shader_variant *fs,
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

   VkResult result = emit_varying_attrs(
      desc_pool, vs->info.varyings.output, vs->info.varyings.output_count,
      &out_vars, buf_offsets, &link->vs.attribs);
   if (result != VK_SUCCESS)
      return result;

   if (fs) {
      result = emit_varying_attrs(desc_pool, fs->info.varyings.input,
                                  fs->info.varyings.input_count, &in_vars,
                                  buf_offsets, &link->fs.attribs);
      if (result != VK_SUCCESS)
         return result;
   }

   memcpy(link->buf_strides, buf_strides, sizeof(link->buf_strides));
   return VK_SUCCESS;
}
#endif

static const struct vk_shader_ops panvk_shader_ops = {
   .destroy = panvk_shader_destroy,
   .serialize = panvk_shader_serialize,
   .get_executable_properties = panvk_shader_get_executable_properties,
   .get_executable_statistics = panvk_shader_get_executable_statistics,
   .get_executable_internal_representations =
      panvk_shader_get_executable_internal_representations,
};

static void
panvk_cmd_bind_shader(struct panvk_cmd_buffer *cmd, const mesa_shader_stage stage,
                      struct panvk_shader *shader)
{
   switch (stage) {
   case MESA_SHADER_COMPUTE:
      if (cmd->state.compute.shader != shader) {
         cmd->state.compute.shader = shader;
         compute_state_set_dirty(cmd, CS);
         compute_state_set_dirty(cmd, PUSH_UNIFORMS);
      }
      break;
   case MESA_SHADER_VERTEX:
      if (cmd->state.gfx.vs.shader != shader) {
         cmd->state.gfx.vs.shader = shader;
         gfx_state_set_dirty(cmd, VS);
         gfx_state_set_dirty(cmd, VS_PUSH_UNIFORMS);
      }
      break;
   case MESA_SHADER_FRAGMENT:
      if (cmd->state.gfx.fs.shader != shader) {
         cmd->state.gfx.fs.shader = shader;
         gfx_state_set_dirty(cmd, FS);
         gfx_state_set_dirty(cmd, FS_PUSH_UNIFORMS);
      }
      break;
   default:
      assert(!"Unsupported stage");
      break;
   }
}

static void
panvk_cmd_bind_shaders(struct vk_command_buffer *vk_cmd, uint32_t stage_count,
                       const mesa_shader_stage *stages,
                       struct vk_shader **const shaders)
{
   struct panvk_cmd_buffer *cmd =
      container_of(vk_cmd, struct panvk_cmd_buffer, vk);

   for (uint32_t i = 0; i < stage_count; i++) {
      struct panvk_shader *shader =
         container_of(shaders[i], struct panvk_shader, vk);

      panvk_cmd_bind_shader(cmd, stages[i], shader);
   }
}

const struct vk_device_shader_ops panvk_per_arch(device_shader_ops) = {
   .get_nir_options = panvk_get_nir_options,
   .get_spirv_options = panvk_get_spirv_options,
   .preprocess_nir = panvk_preprocess_nir,
   .hash_state = panvk_hash_state,
   .compile = panvk_compile_shaders,
   .deserialize = panvk_deserialize_shader,
   .cmd_set_dynamic_graphics_state = vk_cmd_set_dynamic_graphics_state,
   .cmd_bind_shaders = panvk_cmd_bind_shaders,
};

static void
panvk_internal_shader_destroy(struct vk_device *vk_dev,
                              struct vk_shader *vk_shader,
                              const VkAllocationCallbacks *pAllocator)
{
   struct panvk_device *dev = to_panvk_device(vk_dev);
   struct panvk_internal_shader *shader =
      container_of(vk_shader, struct panvk_internal_shader, vk);

   panvk_pool_free_mem(&shader->code_mem);

#if PAN_ARCH < 9
   panvk_pool_free_mem(&shader->rsd);
#else
   panvk_pool_free_mem(&shader->spd);
#endif

   vk_shader_free(&dev->vk, pAllocator, &shader->vk);
}

static const struct vk_shader_ops panvk_internal_shader_ops = {
   .destroy = panvk_internal_shader_destroy,
};

VkResult
panvk_per_arch(create_internal_shader)(
   struct panvk_device *dev, nir_shader *nir,
   struct pan_compile_inputs *compiler_inputs,
   struct panvk_internal_shader **shader_out)
{
   struct panvk_internal_shader *shader =
      vk_shader_zalloc(&dev->vk, &panvk_internal_shader_ops, nir->info.stage,
                       NULL, sizeof(*shader));
   if (shader == NULL)
      return panvk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result;
   struct util_dynarray binary;

   util_dynarray_init(&binary, nir);
   pan_shader_compile(nir, compiler_inputs, &binary, &shader->info);

   unsigned bin_size = util_dynarray_num_elements(&binary, uint8_t);
   if (bin_size) {
      shader->code_mem = panvk_pool_upload_aligned(&dev->mempools.exec,
                                                   binary.data, bin_size, 128);
      if (!panvk_priv_mem_dev_addr(shader->code_mem)) {
         result = panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto err_free_shader;
      }
   }

   *shader_out = shader;
   return VK_SUCCESS;

err_free_shader:
   vk_shader_free(&dev->vk, NULL, &shader->vk);
   return result;
}
