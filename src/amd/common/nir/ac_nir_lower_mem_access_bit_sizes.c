/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "util/blake3/blake3_impl.h"
#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

typedef struct {
   enum amd_gfx_level gfx_level;
   bool use_llvm;
   bool had_terminate;
} mem_access_cb_data;

static bool
set_smem_access_flags(nir_builder *b, nir_intrinsic_instr *intrin, void *cb_data_)
{
   mem_access_cb_data *cb_data = (mem_access_cb_data *)cb_data_;
   intrin->instr.pass_flags = 0;

   /* Detect descriptors that are used in top level control flow, and mark all smem users as CAN_SPECULATE. */
   if (!cb_data->had_terminate) {
      switch (intrin->intrinsic) {
      case nir_intrinsic_terminate:
      case nir_intrinsic_terminate_if:
         cb_data->had_terminate = true;
         return false;
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ssbo:
         if (nir_def_block(intrin->src[0].ssa)->cf_node.parent->type != nir_cf_node_function)
            break;
         FALLTHROUGH;
      case nir_intrinsic_load_constant:
         nir_def_instr(intrin->src[0].ssa)->pass_flags = 1;
         break;
      default:
         break;
      }
   }

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_load_constant:
      if (cb_data->use_llvm)
         return false;
      break;
   case nir_intrinsic_load_ubo:
      break;
   default:
      return false;
   }

   if (intrin->def.divergent)
      return false;

   /* Check if this instruction can use SMEM. */
   const enum gl_access_qualifier access = nir_intrinsic_access(intrin);
   bool glc = access & (ACCESS_VOLATILE | ACCESS_COHERENT);
   bool reorder = nir_intrinsic_can_reorder(intrin) || ((access & ACCESS_NON_WRITEABLE) && !(access & ACCESS_VOLATILE));
   if (!reorder || (glc && cb_data->gfx_level < GFX8))
      return false;

   if (intrin->intrinsic == nir_intrinsic_load_ssbo && (access & ACCESS_ATOMIC) && intrin->def.bit_size == 64)
      return false;

   nir_intrinsic_set_access(intrin, access | ACCESS_SMEM_AMD);

   /* Check if this instruction can be executed speculatively. */
   if (nir_def_instr(intrin->src[0].ssa)->pass_flags == 1)
      nir_intrinsic_set_access(intrin, nir_intrinsic_access(intrin) | ACCESS_CAN_SPECULATE);

   return access != nir_intrinsic_access(intrin);
}

bool
ac_nir_flag_smem_for_loads(nir_shader *shader, enum amd_gfx_level gfx_level, bool use_llvm)
{
   /* Only use the 'ignore_undef' divergence option for ACO where we can guarantee that
    * uniform phis with undef src are residing in SGPRs, and hence, indeed uniform.
    */
   uint32_t options =
      shader->options->divergence_analysis_options | (use_llvm ? 0 : nir_divergence_ignore_undef_if_phi_srcs);
   nir_foreach_function_impl(impl, shader) {
      nir_divergence_analysis_impl(impl, (nir_divergence_options)options);
   }

   mem_access_cb_data cb_data = {
      .gfx_level = gfx_level,
      .use_llvm = use_llvm,
      .had_terminate = false,
   };
   return nir_shader_intrinsics_pass(shader, &set_smem_access_flags, nir_metadata_all, &cb_data);
}

static nir_mem_access_size_align
lower_mem_access_cb(nir_intrinsic_op intrin, uint8_t bytes, uint8_t bit_size, uint32_t align_mul, uint32_t align_offset,
                    bool offset_is_const, enum gl_access_qualifier access, const void *cb_data_)
{
   const mem_access_cb_data *cb_data = (mem_access_cb_data *)cb_data_;
   const bool is_load = nir_intrinsic_infos[intrin].has_dest;
   const bool is_smem = intrin == nir_intrinsic_load_push_constant || (access & ACCESS_SMEM_AMD);
   const uint32_t combined_align = nir_combined_align(align_mul, align_offset);
   nir_mem_access_size_align res;

   if (intrin == nir_intrinsic_load_shared || intrin == nir_intrinsic_store_shared) {
      /* Don't use >64-bit LDS loads for performance reasons. */
      unsigned max_bytes = intrin == nir_intrinsic_store_shared && cb_data->gfx_level >= GFX7 ? 16 : 8;
      bytes = MIN3(bytes, combined_align, max_bytes);
      bytes = bytes == 12 ? bytes : round_down_to_power_of_2(bytes);

      /* Split unsupported shared access. */
      res.bit_size = MIN2(bit_size, bytes * 8ull);
      res.align = res.bit_size / 8;
      res.num_components = bytes / res.align;
      res.shift = nir_mem_access_shift_method_bytealign_amd;
      return res;
   }

   const bool is_buffer_load = intrin == nir_intrinsic_load_ubo ||
                               intrin == nir_intrinsic_load_ssbo ||
                               intrin == nir_intrinsic_load_constant;

   if (is_smem) {
      const bool supported_subdword = cb_data->gfx_level >= GFX12 &&
                                      intrin != nir_intrinsic_load_push_constant &&
                                      (!cb_data->use_llvm || intrin != nir_intrinsic_load_ubo);

      /* Round up subdword loads if unsupported. */
      if (bytes <= 2 && combined_align % bytes == 0 && supported_subdword) {
         bit_size = bytes * 8;
      } else if (bytes % 4 || combined_align % 4) {
         if (is_buffer_load)
            bytes += 4 - MIN2(combined_align, 4);
         bytes = align(bytes, 4);
         bit_size = 32;
      }

      /* Generally, require an alignment of 4. */
      res.align = MIN2(4, bytes);
      bit_size = MAX2(bit_size, res.align * 8);

      /* Maximum SMEM load size is 512 bits (16 dwords). */
      bytes = MIN2(bytes, 64);

      /* Lower unsupported sizes. */
      if (!util_is_power_of_two_nonzero(bytes) && (cb_data->gfx_level < GFX12 || bytes != 12)) {
         const uint8_t larger = util_next_power_of_two(bytes);
         const uint8_t smaller = larger / 2;
         const bool is_aligned = align_mul % smaller == 0;

         /* Overfetch up to 1 dword if this is a bounds-checked buffer load or the access is aligned. */
         bool overfetch = bytes + 4 >= larger && (is_buffer_load || is_aligned);
         bytes = overfetch ? larger : smaller;
         res.align = is_aligned ? smaller : res.align;
      }
      res.num_components = DIV_ROUND_UP(bytes, bit_size / 8);
      res.bit_size = bit_size;
      res.shift = nir_mem_access_shift_method_shift64;
      return res;
   }

   /* Make 8-bit accesses 16-bit if possible */
   if (is_load && bit_size == 8 && combined_align >= 2 && bytes % 2 == 0)
      bit_size = 16;
   /* Make 8/16-bit accesses 32-bit if possible */
   if (bit_size <= 16 && combined_align >= 4 && bytes % 4 == 0)
      bit_size = 32;

   bit_size = MIN2(bit_size, combined_align == 4 ? 64 : combined_align * 8ull);

   unsigned max_components = 4;
   if (cb_data->use_llvm && access & (ACCESS_COHERENT | ACCESS_VOLATILE) &&
       (intrin == nir_intrinsic_load_global || intrin == nir_intrinsic_store_global))
      max_components = 1;

   res.num_components = MIN2(DIV_ROUND_UP(bytes, bit_size / 8), max_components);
   res.bit_size = bit_size;
   res.align = MIN2(bit_size / 8, 4); /* 64-bit access only requires 4 byte alignment. */
   res.shift = nir_mem_access_shift_method_shift64;

   if (!is_load)
      return res;

   /* Lower 8/16-bit loads to 32-bit, unless it's a scalar load. */
   const bool supported_subdword = res.num_components == 1 &&
                                   (!cb_data->use_llvm || intrin != nir_intrinsic_load_ubo);

   if (res.bit_size >= 32 || supported_subdword)
      return res;

   const uint32_t max_pad = 4 - MIN2(combined_align, 4);

   /* Global/scratch loads don't have bounds checking, so increasing the size might not be safe. */
   if (!is_buffer_load) {
      if (align_mul < 4) {
         /* If we split the load, only lower it to 32-bit if this is a SMEM load. */
         const unsigned chunk_bytes = align(bytes, 4) - max_pad;
         if (chunk_bytes < bytes)
            return res;
      }

      res.num_components = DIV_ROUND_UP(bytes, 4);
      res.num_components = nir_round_down_components(res.num_components);
   } else {
      res.num_components = DIV_ROUND_UP(bytes + max_pad, 4);
      res.num_components = nir_round_up_components(res.num_components);
   }
   res.num_components = MIN2(res.num_components, max_components);
   res.bit_size = 32;
   res.align = 4;
   res.shift = nir_mem_access_shift_method_bytealign_amd;

   return res;
}

bool
ac_nir_lower_mem_access_bit_sizes(nir_shader *shader, enum amd_gfx_level gfx_level, bool use_llvm)
{
   mem_access_cb_data cb_data = {
      .gfx_level = gfx_level,
      .use_llvm = use_llvm,
   };
   nir_lower_mem_access_bit_sizes_options lower_mem_access_options = {
      .callback = &lower_mem_access_cb,
      .modes = nir_var_mem_ubo | nir_var_mem_push_const | nir_var_mem_ssbo |
               nir_var_mem_global | nir_var_mem_constant | nir_var_mem_shared |
               nir_var_shader_temp,
      .may_lower_unaligned_stores_to_atomics = false,
      .cb_data = &cb_data,
   };
   return nir_lower_mem_access_bit_sizes(shader, &lower_mem_access_options);
}
