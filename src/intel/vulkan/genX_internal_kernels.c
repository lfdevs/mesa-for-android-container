/* Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_internal_kernels.h"

#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_serialize.h"

#if GFX_VERx10 == 90
# include "intel_gfx90_shaders_binding.h"
#elif GFX_VERx10 == 110
# include "intel_gfx110_shaders_binding.h"
#elif GFX_VERx10 == 120
# include "intel_gfx120_shaders_binding.h"
#elif GFX_VERx10 == 125
# include "intel_gfx125_shaders_binding.h"
#elif GFX_VERx10 == 200
# include "intel_gfx200_shaders_binding.h"
#elif GFX_VERx10 == 300
# include "intel_gfx300_shaders_binding.h"
#elif GFX_VERx10 == 350
# include "intel_gfx350_shaders_binding.h"
#else
# error "Unsupported generation"
#endif

#include "genxml/gen_macros.h"

static nir_def *
_load_param(nir_builder *b, unsigned bit_size, unsigned base, unsigned range)
{
   return
      (b->shader->info.stage == MESA_SHADER_COMPUTE &&
       GFX_VERx10 >= 125) ?
      nir_load_shader_indirect_data_intel(
         b, 1, bit_size, nir_load_indirect_address_intel(b),
         .base = base, .range = range) :
      nir_load_push_data_intel(b, 1, bit_size, nir_imm_int(b, 0),
                               .base = base, .range = range);
}

#define load_param(b, bit_size, struct_name, field_name) \
   _load_param(b, bit_size, offsetof(struct_name, field_name), bit_size / 8)

static nir_def *
load_fragment_index(nir_builder *b)
{
   nir_def *pos_in = nir_f2i32(b, nir_build_frag_coord(b, 2));
   return nir_iadd(b,
                   nir_imul_imm(b, nir_channel(b, pos_in, 1), 8192),
                   nir_channel(b, pos_in, 0));
}

static nir_def *
load_compute_index(nir_builder *b)
{
   return nir_channel(b, nir_load_global_invocation_id(b, 32), 0);
}

uint32_t
genX(call_internal_shader)(nir_builder *b, enum anv_internal_kernel_name shader_name)
{
   switch (shader_name) {
   case ANV_INTERNAL_KERNEL_GENERATED_DRAWS:
      genX(libanv_write_draw)(
         b,
         load_param(b, 64, struct anv_gen_indirect_params, generated_cmds_addr),
         load_param(b, 64, struct anv_gen_indirect_params, wa_insts_addr),
         load_param(b, 64, struct anv_gen_indirect_params, indirect_data_addr),
         load_param(b, 64, struct anv_gen_indirect_params, draw_id_addr),
         load_param(b, 32, struct anv_gen_indirect_params, indirect_data_stride),
         load_param(b, 64, struct anv_gen_indirect_params, draw_count_addr),
         load_param(b, 32, struct anv_gen_indirect_params, draw_base),
         load_param(b, 32, struct anv_gen_indirect_params, instance_multiplier),
         load_param(b, 32, struct anv_gen_indirect_params, max_draw_count),
         load_param(b, 32, struct anv_gen_indirect_params, flags),
         load_param(b, 32, struct anv_gen_indirect_params, mocs),
         load_param(b, 32, struct anv_gen_indirect_params, cmd_primitive_size),
         load_param(b, 32, struct anv_gen_indirect_params, ring_count),
         load_param(b, 64, struct anv_gen_indirect_params, gen_addr),
         load_param(b, 64, struct anv_gen_indirect_params, end_addr),
         load_fragment_index(b));
      return sizeof(struct anv_gen_indirect_params);

   case ANV_INTERNAL_KERNEL_COPY_QUERY_RESULTS_COMPUTE:
   case ANV_INTERNAL_KERNEL_COPY_QUERY_RESULTS_FRAGMENT:
      genX(libanv_query_copy)(
         b,
         load_param(b, 64, struct anv_query_copy_params, destination_addr),
         load_param(b, 32, struct anv_query_copy_params, destination_stride),
         load_param(b, 64, struct anv_query_copy_params, query_data_addr),
         load_param(b, 32, struct anv_query_copy_params, query_base),
         load_param(b, 32, struct anv_query_copy_params, num_queries),
         load_param(b, 32, struct anv_query_copy_params, query_data_offset),
         load_param(b, 32, struct anv_query_copy_params, query_stride),
         load_param(b, 32, struct anv_query_copy_params, num_items),
         load_param(b, 32, struct anv_query_copy_params, flags),
         shader_name == ANV_INTERNAL_KERNEL_COPY_QUERY_RESULTS_COMPUTE ?
         load_compute_index(b) : load_fragment_index(b));
      return sizeof(struct anv_query_copy_params);

   case ANV_INTERNAL_KERNEL_MEMCPY_COMPUTE:
      genX(libanv_memcpy)(
         b,
         load_param(b, 64, struct anv_memcpy_params, dst_addr),
         load_param(b, 64, struct anv_memcpy_params, src_addr),
         load_param(b, 32, struct anv_memcpy_params, num_dwords),
         nir_imul_imm(b, load_compute_index(b), 4));
      return sizeof(struct anv_memcpy_params);

   case ANV_INTERNAL_KERNEL_DGC_GFX_COMPUTE:
   case ANV_INTERNAL_KERNEL_DGC_GFX_FRAGMENT:
      genX(libanv_preprocess_gfx_generate)(
         b,
         load_param(b, 64, struct anv_dgc_gfx_params, cmd_addr),
         load_param(b, 32, struct anv_dgc_gfx_params, cmd_stride),
         load_param(b, 64, struct anv_dgc_gfx_params, data_addr),
         load_param(b, 32, struct anv_dgc_gfx_params, data_stride),
         load_param(b, 64, struct anv_dgc_gfx_params, seq_addr),
         load_param(b, 32, struct anv_dgc_gfx_params, seq_stride),
         load_param(b, 64, struct anv_dgc_gfx_params, seq_count_addr),
         load_param(b, 32, struct anv_dgc_gfx_params, max_seq_count),
         load_param(b, 32, struct anv_dgc_gfx_params, cmd_prolog_size),
         load_param(b, 32, struct anv_dgc_gfx_params, data_prolog_size),
         load_param(b, 64, struct anv_dgc_gfx_params, state_addr),
         load_param(b, 64, struct anv_dgc_gfx_params, const_addr),
         load_param(b, 32, struct anv_dgc_gfx_params, const_size),
         load_param(b, 64, struct anv_dgc_gfx_params, driver_const_addr),
         load_param(b, 64, struct anv_dgc_gfx_params, return_addr),
         load_param(b, 32, struct anv_dgc_gfx_params, flags),
         shader_name == ANV_INTERNAL_KERNEL_DGC_GFX_COMPUTE ?
         load_compute_index(b) : load_fragment_index(b));
      return sizeof(struct anv_dgc_gfx_params);

   case ANV_INTERNAL_KERNEL_DGC_CS_COMPUTE:
   case ANV_INTERNAL_KERNEL_DGC_CS_FRAGMENT:
      genX(libanv_preprocess_cs_generate)(
         b,
         load_param(b, 64, struct anv_dgc_cs_params, cmd_addr),
         load_param(b, 32, struct anv_dgc_cs_params, cmd_stride),
         load_param(b, 64, struct anv_dgc_cs_params, data_addr),
         load_param(b, 32, struct anv_dgc_cs_params, data_stride),
         load_param(b, 64, struct anv_dgc_cs_params, seq_addr),
         load_param(b, 32, struct anv_dgc_cs_params, seq_stride),
         load_param(b, 64, struct anv_dgc_cs_params, seq_count_addr),
         load_param(b, 32, struct anv_dgc_cs_params, max_seq_count),
         load_param(b, 32, struct anv_dgc_cs_params, cmd_prolog_size),
         load_param(b, 32, struct anv_dgc_cs_params, data_prolog_size),
         load_param(b, 64, struct anv_dgc_cs_params, layout_addr),
         load_param(b, 64, struct anv_dgc_cs_params, indirect_set_addr),
         load_param(b, 64, struct anv_dgc_cs_params, interface_descriptor_data_addr),
         load_param(b, 64, struct anv_dgc_cs_params, const_addr),
         load_param(b, 32, struct anv_dgc_cs_params, const_size),
         load_param(b, 64, struct anv_dgc_cs_params, driver_const_addr),
         load_param(b, 64, struct anv_dgc_cs_params, return_addr),
         load_param(b, 32, struct anv_dgc_cs_params, flags),
         shader_name == ANV_INTERNAL_KERNEL_DGC_CS_COMPUTE ?
         load_compute_index(b) : load_fragment_index(b));
      return sizeof(struct anv_dgc_cs_params);

   case ANV_INTERNAL_KERNEL_DGC_CS_POSTPROCESS_COMPUTE:
      genX(libanv_postprocess_cs_generate)(
         b,
         load_param(b, 64, struct anv_dgc_cs_params, cmd_addr),
         load_param(b, 32, struct anv_dgc_cs_params, cmd_stride),
         load_param(b, 64, struct anv_dgc_cs_params, data_addr),
         load_param(b, 32, struct anv_dgc_cs_params, data_stride),
         load_param(b, 64, struct anv_dgc_cs_params, seq_count_addr),
         load_param(b, 32, struct anv_dgc_cs_params, max_seq_count),
         load_param(b, 32, struct anv_dgc_cs_params, cmd_prolog_size),
         load_param(b, 32, struct anv_dgc_cs_params, data_prolog_size),
         load_param(b, 32, struct anv_dgc_cs_params, data_stride),
         load_param(b, 64, struct anv_dgc_cs_params, indirect_set_addr),
         load_param(b, 64, struct anv_dgc_cs_params, return_addr),
         load_compute_index(b));
      return sizeof(struct anv_dgc_cs_params);

   case ANV_INTERNAL_KERNEL_DGC_DUMP_COMPUTE:
   case ANV_INTERNAL_KERNEL_DGC_DUMP_FRAGMENT:
      genX(libanv_dgc_dump)(
         b,
         load_param(b, 64, struct anv_dgc_dump_params, cmd_addr),
         load_param(b, 32, struct anv_dgc_dump_params, n_dwords),
         load_param(b, 64, struct anv_dgc_dump_params, call_addr));
      return sizeof(struct anv_dgc_dump_params);

#if GFX_VERx10 >= 125
   case ANV_INTERNAL_KERNEL_DGC_RT_COMPUTE:
   case ANV_INTERNAL_KERNEL_DGC_RT_FRAGMENT:
      genX(libanv_preprocess_rt_generate)(
         b,
         load_param(b, 64, struct anv_dgc_rt_params, cmd_addr),
         load_param(b, 32, struct anv_dgc_rt_params, cmd_stride),
         load_param(b, 64, struct anv_dgc_rt_params, data_addr),
         load_param(b, 32, struct anv_dgc_rt_params, data_stride),
         load_param(b, 64, struct anv_dgc_rt_params, seq_addr),
         load_param(b, 32, struct anv_dgc_rt_params, seq_stride),
         load_param(b, 64, struct anv_dgc_rt_params, seq_count_addr),
         load_param(b, 32, struct anv_dgc_rt_params, max_seq_count),
         load_param(b, 32, struct anv_dgc_rt_params, cmd_prolog_size),
         load_param(b, 32, struct anv_dgc_rt_params, data_prolog_size),
         load_param(b, 64, struct anv_dgc_rt_params, layout_addr),
         load_param(b, 64, struct anv_dgc_rt_params, compute_walker_addr),
         load_param(b, 64, struct anv_dgc_rt_params, rtdg_global_addr),
         load_param(b, 64, struct anv_dgc_rt_params, const_addr),
         load_param(b, 32, struct anv_dgc_rt_params, const_size),
         load_param(b, 64, struct anv_dgc_rt_params, driver_const_addr),
         load_param(b, 64, struct anv_dgc_rt_params, return_addr),
         load_param(b, 32, struct anv_dgc_rt_params, flags),
         shader_name == ANV_INTERNAL_KERNEL_DGC_RT_COMPUTE ?
         load_compute_index(b) : load_fragment_index(b));
      return sizeof(struct anv_dgc_rt_params);
#endif /* GFX_VERx10 >= 125 */

   default:
      UNREACHABLE("Invalid shader name");
      break;
   }
}
