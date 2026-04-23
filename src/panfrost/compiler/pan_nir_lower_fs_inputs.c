/*
 * Copyright (C) 2025 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_nir.h"
#include "nir_builder.h"

#include "panfrost/model/pan_model.h"


struct lower_fs_inputs_ctx {
   unsigned arch;
   const struct pan_varying_layout *varying_layout;
   struct pan_shader_info *info;
};

static bool
lower_fs_input_load(struct nir_builder *b,
                    nir_intrinsic_instr *load, void *cb_data)
{
   const struct lower_fs_inputs_ctx *ctx = cb_data;

   if (load->intrinsic != nir_intrinsic_load_input &&
       load->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   const nir_io_semantics sem = nir_intrinsic_io_semantics(load);
   const nir_alu_type dest_type = nir_intrinsic_dest_type(load);

   /* Indirect array varyings are not yet supported (num_slots > 1) */
   assert(sem.num_slots == 1);
   assert(nir_src_as_uint(*nir_get_io_offset_src(load)) == 0);

   nir_intrinsic_instr *bary;
   switch (load->intrinsic) {
   case nir_intrinsic_load_input:
      bary = NULL;
      break;
   case nir_intrinsic_load_interpolated_input:
      /* Cannot interpolate ints */
      assert(nir_alu_type_get_base_type(dest_type) == nir_type_float);
      bary = nir_src_as_intrinsic(load->src[0]);
      break;
   default:
      UNREACHABLE("Already handled");
   }

   b->cursor = nir_before_instr(&load->instr);

   const unsigned component = nir_intrinsic_component(load);
   const unsigned load_comps = load->num_components + component;

   const struct pan_varying_slot *slot = NULL;
   bool use_ld_var_buf = false;
   if (ctx->varying_layout && ctx->arch >= 9) {
      pan_varying_layout_require_layout(ctx->varying_layout);
      slot = pan_varying_layout_find_slot(ctx->varying_layout, sem.location);
      assert(slot);
      use_ld_var_buf = slot->offset < pan_ld_var_buf_off_size(ctx->arch);
   }

   nir_def *res;
   if (use_ld_var_buf) {
      const nir_alu_type src_type = slot->alu_type;
      nir_def *offset_B = nir_imm_int(b, slot->offset);

      if (load->intrinsic == nir_intrinsic_load_interpolated_input) {
         res = nir_load_var_buf_pan(b, load_comps, load->def.bit_size,
                                    offset_B, &bary->def,
                                    .src_type = src_type,
                                    .io_semantics = sem);
      } else {
         res = nir_load_var_buf_flat_pan(b, load_comps, load->def.bit_size,
                                         offset_B,
                                         .src_type = src_type,
                                         .io_semantics = sem);
      }
   } else {
      ctx->info->bifrost.uses_ld_var = true;
      const uint32_t base = nir_intrinsic_base(load);
      nir_def *idx = nir_imm_int(b, base);

      if (load->intrinsic == nir_intrinsic_load_interpolated_input) {
         res = nir_load_var_pan(b, load_comps, load->def.bit_size,
                                idx, &bary->def,
                                .dest_type = dest_type,
                                .io_semantics = sem);
      } else {
         res = nir_load_var_flat_pan(b, load_comps, load->def.bit_size, idx,
                                     .dest_type = dest_type,
                                     .io_semantics = sem);
      }
   }

   if (component > 0) {
      unsigned swiz[NIR_MAX_VEC_COMPONENTS] = {0, };
      for (unsigned c = 0; c < load->num_components; c++)
         swiz[c] = component + c;

      res = nir_swizzle(b, res, swiz, load->num_components);
   }

   nir_def_replace(&load->def, res);
   return true;
}

bool
pan_nir_lower_fs_inputs(nir_shader *shader, uint64_t gpu_id,
                        const struct pan_varying_layout *varying_layout,
                        struct pan_shader_info *info)
{
   const struct lower_fs_inputs_ctx ctx = {
      .arch = pan_arch(gpu_id),
      .varying_layout = varying_layout,
      .info = info,
   };
   return nir_shader_intrinsics_pass(shader, lower_fs_input_load,
                                     nir_metadata_control_flow,
                                     (void *)&ctx);
}
