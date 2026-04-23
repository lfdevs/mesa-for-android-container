/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "ir3_nir.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"

/*
 * Lowering for 64b undef instructions, splitting into a two 32b undefs
 */

static nir_def *
lower_64b_undef(nir_builder *b, nir_instr *instr, void *unused)
{
   (void)unused;

   nir_undef_instr *undef = nir_instr_as_undef(instr);
   unsigned num_comp = undef->def.num_components;
   nir_def *components[num_comp];

   for (unsigned i = 0; i < num_comp; i++) {
      nir_def *lowered = nir_undef(b, 2, 32);

      components[i] = nir_pack_64_2x32_split(b,
                                             nir_channel(b, lowered, 0),
                                             nir_channel(b, lowered, 1));
   }

   return nir_build_alu_src_arr(b, nir_op_vec(num_comp), components);
}

static bool
lower_64b_undef_filter(const nir_instr *instr, const void *unused)
{
   (void)unused;

   return instr->type == nir_instr_type_undef &&
      nir_instr_as_undef(instr)->def.bit_size == 64;
}

bool
ir3_nir_lower_64b_undef(nir_shader *shader)
{
   return nir_shader_lower_instructions(
         shader, lower_64b_undef_filter,
         lower_64b_undef, NULL);
}

/*
 * Lowering for load_global/store_global with 64b addresses to ir3 variants,
 * which have an additional arg that is a 32-bit offset to the 64-bit base
 * address.  It's stuffed with a 0 in this path currently, but other generators
 * of global loads in the backend will have nonzero values.
 */

static bool
lower_64b_global_filter(const nir_instr *instr, const void *unused)
{
   (void)unused;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_store_global:
      return true;
   default:
      return false;
   }
}

static nir_def *
lower_64b_global(nir_builder *b, nir_instr *instr, void *unused)
{
   (void)unused;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_global) {
      unsigned num_comp = nir_intrinsic_dest_components(intr);

      /* load_global_constant is redundant and should be removed, because we can
       * express the same thing with extra access flags, but for now translate
       * it to load_global_ir3 with those extra flags.
       */
      enum gl_access_qualifier access = nir_intrinsic_access(intr);
      if (intr->intrinsic == nir_intrinsic_load_global_constant)
         access |= ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER;

      return nir_load_global_ir3(b, num_comp, intr->def.bit_size,
                                 intr->src[0].ssa, nir_imm_int(b, 0),
                                 .access = access);
   } else {
      nir_store_global_ir3(b, intr->src[0].ssa, intr->src[1].ssa,
                           nir_imm_int(b, 0));
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }
}

bool
ir3_nir_lower_64b_global(nir_shader *shader)
{
   return nir_shader_lower_instructions(
         shader, lower_64b_global_filter,
         lower_64b_global, NULL);
}

/*
 * Lowering for 64b registers:
 * - @decl_reg -> split in two 32b ones
 * - @store_reg -> unpack_64_2x32_split_x/y and two separate stores
 * - @load_reg -> two separate loads and pack_64_2x32_split
 */

static void
lower_64b_reg(nir_builder *b, nir_intrinsic_instr *reg)
{
   unsigned num_components = nir_intrinsic_num_components(reg);
   unsigned num_array_elems = nir_intrinsic_num_array_elems(reg);

   nir_def *reg_hi = nir_decl_reg(b, num_components, 32, num_array_elems);
   nir_def *reg_lo = nir_decl_reg(b, num_components, 32, num_array_elems);

   nir_foreach_reg_store_safe (store_reg_src, reg) {
      nir_intrinsic_instr *store =
         nir_instr_as_intrinsic(nir_src_parent_instr(store_reg_src));
      b->cursor = nir_before_instr(&store->instr);

      nir_def *packed = store->src[0].ssa;
      nir_def *unpacked_lo = nir_unpack_64_2x32_split_x(b, packed);
      nir_def *unpacked_hi = nir_unpack_64_2x32_split_y(b, packed);
      int base = nir_intrinsic_base(store);

      if (store->intrinsic == nir_intrinsic_store_reg) {
         nir_build_store_reg(b, unpacked_lo, reg_lo, .base = base);
         nir_build_store_reg(b, unpacked_hi, reg_hi, .base = base);
      } else {
         assert(store->intrinsic == nir_intrinsic_store_reg_indirect);

         nir_def *offset = store->src[2].ssa;
         nir_store_reg_indirect(b, unpacked_lo, reg_lo, offset, .base = base);
         nir_store_reg_indirect(b, unpacked_hi, reg_hi, offset, .base = base);
      }

      nir_instr_remove(&store->instr);
   }

   nir_foreach_reg_load_safe (load_reg_src, reg) {
      nir_intrinsic_instr *load =
         nir_instr_as_intrinsic(nir_src_parent_instr(load_reg_src));
      b->cursor = nir_before_instr(&load->instr);

      int base = nir_intrinsic_base(load);
      nir_def *load_lo, *load_hi;

      if (load->intrinsic == nir_intrinsic_load_reg) {
         load_lo =
            nir_build_load_reg(b, num_components, 32, reg_lo, .base = base);
         load_hi =
            nir_build_load_reg(b, num_components, 32, reg_hi, .base = base);
      } else {
         assert(load->intrinsic == nir_intrinsic_load_reg_indirect);

         nir_def *offset = load->src[1].ssa;
         load_lo = nir_load_reg_indirect(b, num_components, 32, reg_lo, offset,
                                         .base = base);
         load_hi = nir_load_reg_indirect(b, num_components, 32, reg_hi, offset,
                                         .base = base);
      }

      nir_def *packed = nir_pack_64_2x32_split(b, load_lo, load_hi);
      nir_def_rewrite_uses(&load->def, packed);
      nir_instr_remove(&load->instr);
   }

   nir_instr_remove(&reg->instr);
}

bool
ir3_nir_lower_64b_regs(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl (impl, shader) {
      bool impl_progress = false;
      nir_builder b = nir_builder_create(impl);

      nir_foreach_reg_decl_safe (reg, impl) {
         if (nir_intrinsic_bit_size(reg) == 64) {
            lower_64b_reg(&b, reg);
            impl_progress = true;
         }
      }

      if (impl_progress) {
         progress = nir_progress(true, impl, nir_metadata_control_flow);
      }
   }

   return progress;
}

static void
lower_64b_image_load(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_intrinsic_set_format(intr, PIPE_FORMAT_R32G32_UINT);
   nir_intrinsic_set_dest_type(intr, nir_type_uint32);
   unsigned ncomp = intr->num_components;
   intr->num_components = 2;
   intr->def.num_components = 2;
   intr->def.bit_size = 32;

   /* We only load the R component but the original load might need GBA as well,
    * return the default (0, 0, 1) values.
    */
   b->cursor = nir_after_instr(&intr->instr);
   nir_def *val_r64 = nir_pack_64_2x32(b, &intr->def);
   nir_def *vec[4] = {val_r64, nir_imm_int64(b, 0), nir_imm_int64(b, 0),
                      nir_imm_int64(b, 1)};
   assert(ncomp <= ARRAY_SIZE(vec));
   nir_def_rewrite_uses_after(&intr->def, nir_vec(b, vec, ncomp));
}

static void
lower_64b_image_store(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_intrinsic_set_format(intr, PIPE_FORMAT_R32G32_UINT);
   nir_intrinsic_set_src_type(intr, nir_type_uint32);
   intr->num_components = 2;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *val_r64 = nir_channel(b, intr->src[3].ssa, 0);
   nir_def *val_r32g32 = nir_unpack_64_2x32(b, val_r64);
   nir_src_rewrite(&intr->src[3], val_r32g32);
}

static bool
lower_64b_image(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_bindless_image_store: {
      enum pipe_format format = nir_intrinsic_format(intr);
      if (format != PIPE_FORMAT_R64_UINT && format != PIPE_FORMAT_R64_SINT) {
         return false;
      }
      break;
   }
   default:
      return false;
   }

   if (intr->intrinsic == nir_intrinsic_bindless_image_load) {
      lower_64b_image_load(b, intr);
   } else {
      lower_64b_image_store(b, intr);
   }

   return true;
}

/* Lower r64u?i image loads/stores to r32g32u, which is our descriptor format.
 */
bool
ir3_nir_lower_64b_image(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower_64b_image,
                                     nir_metadata_control_flow, NULL);
}
