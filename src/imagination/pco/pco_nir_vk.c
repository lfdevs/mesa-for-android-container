/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_vk.c
 *
 * \brief PCO NIR Vulkan lowering pass.
 */

#include "nir.h"
#include "nir_builder.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

static void
set_resource_used(pco_common_data *common, unsigned desc_set, unsigned binding)
{
   assert(desc_set < ARRAY_SIZE(common->desc_sets));
   pco_descriptor_set_data *desc_set_data = &common->desc_sets[desc_set];
   desc_set_data->used = true;

   assert(desc_set_data->bindings && binding < desc_set_data->binding_count);
   pco_binding_data *binding_data = &desc_set_data->bindings[binding];
   binding_data->used = true;
}

/**
 * \brief Lowers load_vulkan_descriptor.
 *
 * Packs the descriptor set, binding, and array element.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in] common Shader common data.
 * \return The replacement/lowered def.
 */
static nir_def *lower_load_vulkan_descriptor(nir_builder *b,
                                             nir_intrinsic_instr *intr,
                                             pco_common_data *common)
{
   nir_intrinsic_instr *vk_res_idx = nir_src_as_intrinsic(intr->src[0]);
   assert(vk_res_idx->intrinsic == nir_intrinsic_vulkan_resource_index);

   assert(nir_intrinsic_desc_type(intr) == nir_intrinsic_desc_type(vk_res_idx));

   unsigned desc_set = nir_intrinsic_desc_set(vk_res_idx);
   unsigned binding = nir_intrinsic_binding(vk_res_idx);
   unsigned elem = nir_src_as_uint(vk_res_idx->src[0]);

   set_resource_used(common, desc_set, binding);

   uint32_t desc_set_binding = pco_pack_desc(desc_set, binding);
   return nir_imm_ivec3(b, desc_set_binding, elem, 0);
}

static nir_def *array_elem_from_deref(nir_builder *b, nir_deref_instr *deref)
{
   unsigned array_elem = 0;
   if (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);

      array_elem = nir_src_as_uint(deref->arr.index);

      deref = nir_deref_instr_parent(deref);
   }

   assert(deref->deref_type == nir_deref_type_var);
   return nir_imm_int(b, array_elem);
}

static inline bool is_comb_img_smp(unsigned desc_set,
                                   unsigned binding,
                                   const pco_common_data *common)
{
   const pco_descriptor_set_data *desc_set_data = &common->desc_sets[desc_set];
   assert(desc_set_data->bindings && binding < desc_set_data->binding_count);

   const pco_binding_data *binding_data = &desc_set_data->bindings[binding];
   return binding_data->is_img_smp;
}

static void lower_tex_deref_to_binding(nir_builder *b,
                                       nir_tex_instr *tex,
                                       unsigned deref_index,
                                       pco_common_data *common)
{
   nir_tex_src *deref_src = &tex->src[deref_index];
   nir_deref_instr *deref =
      nir_def_as_deref(deref_src->src.ssa);

   b->cursor = nir_before_instr(&tex->instr);

   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(var);
   unsigned desc_set = var->data.descriptor_set;
   unsigned binding = var->data.binding;
   nir_def *elem = array_elem_from_deref(b, deref);

   set_resource_used(common, desc_set, binding);

   uint32_t desc_set_binding = pco_pack_desc(desc_set, binding);
   if (deref_src->src_type == nir_tex_src_texture_deref) {
      tex->texture_index = desc_set_binding;
      deref_src->src_type = nir_tex_src_backend1;
   } else {
      tex->sampler_index = desc_set_binding;
      deref_src->src_type = nir_tex_src_backend2;
   }

   nir_src_rewrite(&deref_src->src, elem);
}

static void
add_txf_sampler(nir_builder *b, nir_tex_instr *tex, pco_common_data *common)
{
   int deref_index = nir_tex_instr_src_index(tex, nir_tex_src_backend1);
   assert(deref_index >= 0);
   nir_tex_src *deref_src = &tex->src[deref_index];

   unsigned desc_set;
   unsigned binding;
   pco_unpack_desc(tex->texture_index, &desc_set, &binding);
   nir_def *elem = deref_src->src.ssa;

   /* If it's not a combined image/sampler, use the point sampler. */
   if (!is_comb_img_smp(desc_set, binding, common)) {
      desc_set = PCO_POINT_SAMPLER;
      binding = PCO_POINT_SAMPLER;
      elem = nir_imm_int(b, 0);

      common->uses.point_sampler = true;
   }

   tex->sampler_index = pco_pack_desc(desc_set, binding);
   nir_tex_instr_add_src(tex, nir_tex_src_backend2, elem);
}

static inline void
lower_tex_derefs(nir_builder *b, nir_tex_instr *tex, pco_common_data *common)
{
   int deref_index;

   deref_index = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (deref_index >= 0)
      lower_tex_deref_to_binding(b, tex, deref_index, common);

   deref_index = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (deref_index >= 0)
      lower_tex_deref_to_binding(b, tex, deref_index, common);
   else if (tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms)
      add_txf_sampler(b, tex, common);
}

static nir_def *
lower_image_derefs(nir_builder *b, nir_intrinsic_instr *intr, pco_data *data)
{
   nir_src *deref_src = &intr->src[0];
   nir_deref_instr *deref = nir_src_as_deref(*deref_src);
   b->cursor = nir_before_instr(&intr->instr);

   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(var);

   unsigned desc_set = var->data.descriptor_set;
   unsigned binding = var->data.binding;
   set_resource_used(&data->common, desc_set, binding);

   if (nir_intrinsic_format(intr) == PIPE_FORMAT_NONE)
      nir_intrinsic_set_format(intr, var->data.image.format);

   enum glsl_sampler_dim image_dim = nir_intrinsic_image_dim(intr);
   bool ia = image_dim == GLSL_SAMPLER_DIM_SUBPASS ||
             image_dim == GLSL_SAMPLER_DIM_SUBPASS_MS;

   if (ia) {
      unsigned ia_idx = var->data.index;
      bool onchip = data->fs.ia_formats[ia_idx] != PIPE_FORMAT_NONE;

      bool is_stencil = data->fs.ia_has_stencil & BITFIELD_BIT(ia_idx);
      is_stencil &= glsl_get_sampler_result_type(glsl_without_array_or_matrix(
                       var->type)) != GLSL_TYPE_FLOAT;
      onchip &= !is_stencil;

      if (onchip) {
         nir_def *elem = array_elem_from_deref(b, deref);
         nir_def *index = nir_vec4(b,
                                   nir_imm_int(b, desc_set),
                                   nir_imm_int(b, binding),
                                   elem,
                                   nir_imm_int(b, ia_idx));

         nir_src_rewrite(deref_src, index);

         return NIR_LOWER_INSTR_PROGRESS;
      }

      /* Sampler not needed for on-chip input attachments. */
      data->common.uses.ia_sampler = true;
   } else if (intr->intrinsic == nir_intrinsic_image_deref_load ||
              intr->intrinsic == nir_intrinsic_image_deref_store) {
      /* Sampler not needed for other types of image accesses. */
      data->common.uses.point_sampler = true;
   }

   nir_def *elem = array_elem_from_deref(b, deref);
   nir_def *index =
      nir_vec3(b, nir_imm_int(b, desc_set), nir_imm_int(b, binding), elem);

   nir_src_rewrite(deref_src, index);

   return NIR_LOWER_INSTR_PROGRESS;
}

static nir_def *lower_is_null_descriptor(nir_builder *b,
                                         nir_intrinsic_instr *intr)
{
   nir_src *deref_src = &intr->src[0];
   nir_deref_instr *deref = nir_src_as_deref(*deref_src);

   /* Will be taken care of by lower_load_vulkan_descriptor. */
   if (!deref)
      return NULL;

   b->cursor = nir_before_instr(&intr->instr);

   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(var);
   unsigned desc_set = var->data.descriptor_set;
   unsigned binding = var->data.binding;
   nir_def *elem = array_elem_from_deref(b, deref);

   uint32_t desc_set_binding = pco_pack_desc(desc_set, binding);
   nir_def *index = nir_vec2(b, nir_imm_int(b, desc_set_binding), elem);

   nir_src_rewrite(deref_src, index);
   return NIR_LOWER_INSTR_PROGRESS;
}

/**
 * \brief Lowers a Vulkan-related instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return The replacement/lowered def.
 */
static nir_def *lower_vk(nir_builder *b, nir_instr *instr, void *cb_data)
{
   pco_data *data = cb_data;
   pco_common_data *common = &data->common;

   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_vulkan_descriptor:
         return lower_load_vulkan_descriptor(b, intr, common);

      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
      case nir_intrinsic_image_deref_size:
         return lower_image_derefs(b, intr, data);

      case nir_intrinsic_is_null_descriptor:
         return lower_is_null_descriptor(b, intr);

      default:
         break;
      }

      break;
   }

   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      lower_tex_derefs(b, tex, common);
      return NIR_LOWER_INSTR_PROGRESS;
   }

   default:
      break;
   }

   UNREACHABLE("");
}

/**
 * \brief Filters Vulkan-related instructions.
 *
 * \param[in] instr NIR instruction.
 * \param[in] cb_data User callback data.
 * \return True if the instruction matches the filter.
 */
static bool is_vk(const nir_instr *instr, UNUSED const void *cb_data)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_vulkan_descriptor:
      case nir_intrinsic_is_null_descriptor:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
      case nir_intrinsic_image_deref_size:
         return true;

      default:
         break;
      }

      break;
   }

   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      if (nir_tex_instr_src_index(tex, nir_tex_src_texture_deref) >= 0 ||
          nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref) >= 0) {
         return true;
      }

      FALLTHROUGH;
   }

   default:
      break;
   }

   return false;
}

/**
 * \brief Vulkan lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \param[in,out] data Shader data.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_vk(nir_shader *shader, pco_data *data)
{
   bool progress = false;

   progress |= nir_shader_lower_instructions(shader, is_vk, lower_vk, data);

   return progress;
}
