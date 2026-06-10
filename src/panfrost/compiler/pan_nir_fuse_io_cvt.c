/*
 * Copyright (C) 2026 Amazon.com, Inc. or its affiliates.
 * SPDX-License-Identifier: MIT
 */
#include "pan_nir.h"

static unsigned
nir_src_float_cvt_bits(nir_src *use, bool *is_mp)
{
   nir_instr *parent = nir_src_use_instr(use);

   if (parent->type != nir_instr_type_alu)
      return 0;

   nir_alu_instr *alu = nir_instr_as_alu(parent);

   switch (alu->op) {
      case nir_op_f2f16:
         return 16;
      case nir_op_f2fmp:
         *is_mp |= true;
         return 16;
      case nir_op_f2f32:
         return 32;
      default:
         return 0;
   }
}

static bool
op_supports_cvt_fusion(nir_intrinsic_instr *instr, uint64_t gpu_id)
{
   /* We might also convert LD_CVT but I haven't seen any case where it's
    * useful, maybe enable it when we have a case to check it on.
    */
   switch (instr->intrinsic) {
      case nir_intrinsic_load_var_pan:
      case nir_intrinsic_load_var_buf_pan:
         /* LD_VAR[_BUF] performs conversion BEFORE interpolation, we cannot
          * just change the interpolation semantics at highp.  mediump on the
          * other hand lets us juggle between 32 and 16 bits freely.
          */
         return nir_intrinsic_io_semantics(instr).medium_precision;
      case nir_intrinsic_load_var_flat_pan:
         return true;
      case nir_intrinsic_load_var_buf_flat_pan:
         /* TODO: v14 can even fuse flat buf conversions */
         return false;
      default:
         return false;
   }
}

struct fuse_ctx {
   uint64_t gpu_id;
   struct pan_varying_layout *layout;
};

static bool
fuse_io_instr(struct nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const struct fuse_ctx *ctx = data;

   if (!op_supports_cvt_fusion(intr, ctx->gpu_id))
      return false;

   unsigned orig_bit_size = intr->def.bit_size;
   assert(orig_bit_size == 32 || orig_bit_size == 16);
   unsigned converted_bit_size = orig_bit_size == 32 ? 16 : 32;
   bool is_mp = false;

   /* Check if all usages are conversions */
   nir_foreach_use_including_if(src, &intr->def) {
      if (nir_src_is_if(src) ||
          nir_src_float_cvt_bits(src, &is_mp) != converted_bit_size)
         return false;
   }

   /* If they are, the load is always followed by conversion and we thus can
    * fuse the cvt into the load.
    */
   intr->def.bit_size = converted_bit_size;
   /* Update the dest_type.  This will not change the in-memory representation
    * of _buf intrinsics as those are stored in the src_type.
    */
   if (nir_intrinsic_has_dest_type(intr)) {
      nir_alu_type dest_type = nir_intrinsic_dest_type(intr);
      nir_alu_type base_type = nir_get_glsl_base_type_for_nir_type(dest_type);
      nir_intrinsic_set_dest_type(intr, nir_type_float | converted_bit_size);

      if (base_type != nir_type_float) {
         const nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

         /* Right now we have int descriptors, but the loaded value is always
          * used as a flot, no harm in just "promoting" it to float.  The cast
          * is to discard the const modifier, this is safe.
          */
         struct pan_varying_slot *slot = (struct pan_varying_slot *)
            pan_varying_layout_find_slot(ctx->layout, sem.location);
         slot->alu_type = nir_alu_type_get_type_size(slot->alu_type) |
                          nir_type_float;
      }
   }

   /* We don't remove conversions, nir_opt_algebraic will fold f2f16 a@16
    * and f2f32 a@32 automatically, everything but f2fmp of course.
    */
   if (is_mp) {
      b->cursor = nir_after_instr(&intr->instr);
      nir_def *up_cvt = nir_f2f32(b, &intr->def);
      nir_def_rewrite_uses_after(&intr->def, up_cvt);
   }

   return true;
}

bool
pan_nir_fuse_io_cvt(nir_shader *nir, uint64_t gpu_id,
                    struct pan_varying_layout *layout)
{
   struct fuse_ctx ctx = {
      .gpu_id = gpu_id,
      .layout = layout,
   };
   return nir_shader_intrinsics_pass(nir, fuse_io_instr,
                                     nir_metadata_control_flow, (void *)&ctx);
}
