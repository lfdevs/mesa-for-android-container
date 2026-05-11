/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "genxml/gen_macros.h"
#include "pan_context.h"
#include "pan_shader.h"
#include "pan_nir.h"

static unsigned
mid_image_offset(nir_shader *nir)
{
   /* Bifrost and Midgard shaders get passed images through the vertex
    * attribute descriptor array.  For vertex shaders, we need to add an
    * offset to all image intrinsics so they point to the right attribute.
    */
   if (nir->info.stage == MESA_SHADER_VERTEX)
      return util_bitcount64(nir->info.inputs_read);
   else
      return 0;
}

static unsigned
mid_texel_buffer_offset(nir_shader *nir, uint64_t gpu_id)
{
   /* Bifrost needs to use attributes to access texel buffers. We place these
    * after images, which are also accessed using attributes.
    */
   assert(pan_arch(gpu_id) <= 7);
   if (pan_arch(gpu_id) >= 6)
      return mid_image_offset(nir) + BITSET_LAST_BIT(nir->info.images_used);
   else
      return 0;
}

static bool
mid_lower_tex(nir_builder *b, nir_tex_instr *tex, uint64_t gpu_id)
{
   if (tex->sampler_dim != GLSL_SAMPLER_DIM_BUF)
      return false;

   /* We don't adjust the index for queries because those go through the
    * sysvals table and it's simpler if they stay the original texture index.
    *
    * TODO: Fix this once we have better query code.
    */
   if (tex->op != nir_texop_txf)
      return false;

   unsigned tex_offset = mid_texel_buffer_offset(b->shader, gpu_id);
   if (tex_offset == 0)
      return false;

   tex->texture_index += tex_offset;
   return true;
}

static bool
mid_lower_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
   unsigned image_offset = mid_image_offset(b->shader);
   if (image_offset == 0)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_src *tex_handle = &intrin->src[0];
   nir_def *new_handle = nir_iadd_imm(b, tex_handle->ssa, image_offset);
   nir_src_rewrite(tex_handle, new_handle);

   return true;
}

static bool
mid_lower_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
      return mid_lower_image_intrin(b, intrin);

   case nir_intrinsic_image_size:
   case nir_intrinsic_image_samples:
      /* We don't adjust the index for queries because those go through the
       * sysvals table and it's simpler if they stay the original texture
       * index.
       *
       * TODO: Fix this once we have better query code.
       */
      return false;

   default:
      return false;
   }
}

static bool
mid_lower_instr(nir_builder *b, nir_instr *instr, void *data)
{
   uint64_t gpu_id = *(uint64_t *)data;

   switch (instr->type) {
   case nir_instr_type_tex:
      return mid_lower_tex(b, nir_instr_as_tex(instr), gpu_id);
   case nir_instr_type_intrinsic:
      return mid_lower_intrinsic(b, nir_instr_as_intrinsic(instr));
   default:
      return false;
   }
}

static bool
mid_lower_res_indices(nir_shader *shader, uint64_t gpu_id)
{
   return nir_shader_instructions_pass(shader, mid_lower_instr,
                                       nir_metadata_control_flow,
                                       &gpu_id);
}

static bool
va_lower_tex(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_before_instr(&tex->instr);

   nir_def *tex_offset = nir_steal_tex_src(tex, nir_tex_src_texture_offset);
   nir_def *samp_offset = nir_steal_tex_src(tex, nir_tex_src_sampler_offset);

   if (tex_offset != NULL) {
      tex_offset = pan_nir_res_handle(b, PAN_TABLE_TEXTURE,
                                      tex->texture_index, tex_offset);
      nir_tex_instr_add_src(tex, nir_tex_src_texture_offset, tex_offset);
   } else {
      tex->texture_index =
         pan_res_handle(PAN_TABLE_TEXTURE, tex->texture_index);
   }

   /* By ABI with the compiler, we assume there is a valid sampler bound at
    * index 0 for txf.
    */
   if (!nir_tex_instr_need_sampler(tex)) {
      tex->sampler_index = pan_res_handle(PAN_TABLE_SAMPLER, 0);
   } else if (samp_offset != NULL) {
      samp_offset = pan_nir_res_handle(b, PAN_TABLE_SAMPLER,
                                       tex->sampler_index, samp_offset);
      nir_tex_instr_add_src(tex, nir_tex_src_sampler_offset, samp_offset);
   } else {
      tex->sampler_index =
         pan_res_handle(PAN_TABLE_SAMPLER, tex->sampler_index);
   }

   return true;
}

static bool
va_lower_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_src *tex_handle = &intrin->src[0];
   nir_def *new_handle =
      pan_nir_res_handle(b, PAN_TABLE_IMAGE, 0, tex_handle->ssa);
   nir_src_rewrite(tex_handle, new_handle);

   return true;
}

static bool
va_lower_input_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
   /* All vertex attributes come from the attribute table.
    * Fragment inputs come from the attribute table too, unless they've
    * been allocated on the heap.
    */
   if (b->shader->info.stage == MESA_SHADER_VERTEX ||
       b->shader->info.stage == MESA_SHADER_FRAGMENT) {
      nir_intrinsic_set_base(
         intrin,
         pan_res_handle(PAN_TABLE_ATTRIBUTE, nir_intrinsic_base(intrin)));
      return true;
   }

   return false;
}

static bool
va_lower_load_ubo_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *new_offset =
      pan_nir_res_handle(b, PAN_TABLE_UBO, 0, intrin->src[0].ssa);

   nir_src_rewrite(&intrin->src[0], new_offset);

   return true;
}

static bool
va_lower_ssbo_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
   b->cursor = nir_before_instr(&intrin->instr);
   bool is_store = intrin->intrinsic == nir_intrinsic_store_ssbo;
   nir_src *handle = &intrin->src[is_store ? 1 : 0];

   nir_def *new_handle = pan_nir_res_handle(b, PAN_TABLE_SSBO, 0, handle->ssa);

   nir_src_rewrite(handle, new_handle);

   return true;
}

static bool
va_lower_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_image_size:
   case nir_intrinsic_image_samples:
      return va_lower_image_intrin(b, intrin);
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      return va_lower_input_intrin(b, intrin);
   case nir_intrinsic_load_ubo:
      return va_lower_load_ubo_intrin(b, intrin);
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
      return va_lower_ssbo_intrin(b, intrin);
   default:
      return false;
   }
}

static bool
va_lower_instr(nir_builder *b, nir_instr *instr, void *data)
{
   switch (instr->type) {
   case nir_instr_type_tex:
      return va_lower_tex(b, nir_instr_as_tex(instr));
   case nir_instr_type_intrinsic:
      return va_lower_intrinsic(b, nir_instr_as_intrinsic(instr));
   default:
      return false;
   }
}

static bool
va_lower_res_indices(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader, va_lower_instr,
                                       nir_metadata_control_flow, NULL);
}

bool
panfrost_nir_lower_res_indices(nir_shader *shader, uint64_t gpu_id)
{
   if (pan_arch(gpu_id) >= 9)
      return va_lower_res_indices(shader);
   else
      return mid_lower_res_indices(shader, gpu_id);
}
