/*
 * Copyright 2022 Alyssa Rosenzweig
 * Copyright 2021 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "util/macros.h"
#include "agx_compiler.h"
#include "nir.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"
#include "nir_intrinsics_indices.h"
#include "nir_opcodes.h"

static nir_preamble_class
preamble_class(nir_def *def)
{
   nir_intrinsic_instr *intr = nir_def_as_intrinsic_or_null(def);
   if (!intr || (nir_intrinsic_has_desc_set(intr) &&
                 nir_intrinsic_desc_set(intr) >= 32 /* encoding restriction */))
      return nir_preamble_class_general;

   if (intr->intrinsic == nir_intrinsic_bindless_image_agx)
      return nir_preamble_class_image;
   else if (intr->intrinsic == nir_intrinsic_bindless_sampler_agx)
      return nir_preamble_class_sampler;
   else
      return nir_preamble_class_general;
}

static void
def_size(nir_def *def, unsigned *size, unsigned *align,
         nir_preamble_class *class)
{
   unsigned bit_size = MAX2(def->bit_size, 16);

   *size = (bit_size * def->num_components) / 16;
   *align = bit_size / 16;
   *class = preamble_class(def);
}

static bool
all_uses_float(nir_def *def)
{
   nir_foreach_use_including_if(use, def) {
      if (nir_src_is_if(use))
         return false;

      nir_instr *use_instr = nir_src_parent_instr(use);
      if (use_instr->type != nir_instr_type_alu)
         return false;

      nir_alu_instr *use_alu = nir_instr_as_alu(use_instr);
      unsigned src_index = ~0;
      for (unsigned i = 0; i < nir_op_infos[use_alu->op].num_inputs; i++) {
         if (&use_alu->src[i].src == use) {
            src_index = i;
            break;
         }
      }

      assert(src_index != ~0);
      nir_alu_type src_type = nir_alu_type_get_base_type(
         nir_op_infos[use_alu->op].input_types[src_index]);

      if (src_type != nir_type_float)
         return false;

      /* No float modifiers on G13 */
      if (use_alu->op == nir_op_fmax || use_alu->op == nir_op_fmin)
         return false;
   }

   return true;
}

static float
alu_cost(nir_alu_instr *alu)
{
   /* TODO: Model 64-bit better */
   if (alu->def.bit_size == 64)
      return 10.0f;

   switch (alu->op) {
   case nir_op_fsat:
   case nir_op_f2fmp:
   case nir_op_f2f16:
   case nir_op_f2f16_rtne:
   case nir_op_fadd:
   case nir_op_fmul:
   case nir_op_ffma:
   case nir_op_iadd:
   case nir_op_inot:
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
   case nir_op_feq:
   case nir_op_flt:
   case nir_op_fge:
   case nir_op_fneu:
   case nir_op_ieq:
   case nir_op_ine:
   case nir_op_ilt:
   case nir_op_ige:
   case nir_op_ult:
   case nir_op_uge:
   case nir_op_fmin:
   case nir_op_fmax:
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
   case nir_op_isub:
   case nir_op_ineg:
   case nir_op_bcsel:
   case nir_op_b2b1:
   case nir_op_b2b8:
   case nir_op_b2b16:
   case nir_op_b2b32:
   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2f16:
   case nir_op_b2f32:
   case nir_op_i2i32:
   case nir_op_i2i16:
   case nir_op_u2u32:
   case nir_op_u2u16:
   case nir_op_u2u8:
   case nir_op_i2i8:
   case nir_op_iadd_sat:
   case nir_op_isub_sat:
   case nir_op_uadd_sat:
   case nir_op_usub_sat:
   case nir_op_iabs:
      /* SCIB */
      return 1.0;

   case nir_op_ffloor:
   case nir_op_fceil:
   case nir_op_ftrunc:
   case nir_op_fround_even:
   case nir_op_bit_count:
   case nir_op_bitfield_reverse:
   case nir_op_ufind_msb:
   case nir_op_imul:
   case nir_op_imadshl_agx:
   case nir_op_imsubshl_agx:
   case nir_op_ishl:
   case nir_op_ishr:
   case nir_op_ushr:
   case nir_op_flog2:
   case nir_op_fexp2:
   case nir_op_extr_agx:
   case nir_op_ubitfield_extract:
   case nir_op_f2i8:
   case nir_op_f2i16:
   case nir_op_f2i32:
   case nir_op_f2u8:
   case nir_op_f2u16:
   case nir_op_f2u32:
   case nir_op_i2fmp:
   case nir_op_i2f16:
   case nir_op_i2f32:
   case nir_op_u2fmp:
   case nir_op_u2f16:
   case nir_op_u2f32:
   case nir_op_interleave:
      /* IC */
      return 4.0;

   case nir_op_frcp:
      /* IC */
      return 6.0;

   case nir_op_frsq:
      /* IC */
      return 8.0;

   case nir_op_fsqrt:
      /* IC + F32 */
      return 8.5;

   case nir_op_imul_high:
   case nir_op_umul_high:
   case nir_op_imul_2x32_64:
   case nir_op_umul_2x32_64:
      /* IC */
      return 8.0;

   case nir_op_fsin_agx:
      /* 2 IC + 1 F32 in parallel */
      return 8.5;

   case nir_op_fneg:
   case nir_op_fabs:
   case nir_op_f2f32:
   case nir_op_unpack_half_2x16_split_x:
   case nir_op_unpack_half_2x16_split_y:
      /* Float source modifiers will be propagated */
      return all_uses_float(&alu->def) ? 0.0 : 1.0;

   case nir_op_mov:
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_pack_32_2x16_split:
   case nir_op_pack_64_2x32_split:
   case nir_op_unpack_64_2x32_split_x:
   case nir_op_unpack_64_2x32_split_y:
   case nir_op_unpack_32_2x16_split_x:
   case nir_op_unpack_32_2x16_split_y:
   case nir_op_extract_i8:
   case nir_op_extract_u8:
   case nir_op_extract_i16:
   case nir_op_extract_u16:
      /* We optimistically assume that moves get coalesced */
      return 0.0;

   default:
      /* Shrug */
      return 2.0;
   }
}

static float
instr_cost(nir_instr *instr, const void *data)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic:
      switch (nir_instr_as_intrinsic(instr)->intrinsic) {
      case nir_intrinsic_load_global:
      case nir_intrinsic_load_agx:
      case nir_intrinsic_load_global_constant:
      case nir_intrinsic_load_constant_agx:
      case nir_intrinsic_load_ubo:
         return 10.0;
      case nir_intrinsic_ddx:
      case nir_intrinsic_ddx_fine:
      case nir_intrinsic_ddx_coarse:
      case nir_intrinsic_ddy:
      case nir_intrinsic_ddy_fine:
      case nir_intrinsic_ddy_coarse:
         return 1.0;
      case nir_intrinsic_bindless_image_agx:
      case nir_intrinsic_bindless_sampler_agx:
         /* It's worth promoting even with a constant source, but it doesn't
          * turn into instructions so should be less than any other normal
          * instruction... But just enough to get over the image rewrite_cost.
          */
         return 2.5;
      default:
         /* Assume it's a sysval or something */
         return 0.0;
      }

   case nir_instr_type_tex:
      /* Texturing involes lots of memory bandwidth */
      return 20.0;

   case nir_instr_type_alu:
      return alu_cost(nir_instr_as_alu(instr));

   default:
      return 1.0;
   }
}

static float
rewrite_cost(nir_def *def, const void *data)
{
   bool mov_needed = false, vectorizable = true;
   nir_foreach_use(use, def) {
      nir_instr *parent_instr = nir_src_parent_instr(use);
      if (parent_instr->type == nir_instr_type_tex) {
         /* TODO: Maybe check the source index, but biases can be uniform */
         break;
      } else if (parent_instr->type == nir_instr_type_phi) {
         /* Assume we'd eat a move anyway */
      } else if (parent_instr->type != nir_instr_type_alu) {
         mov_needed = true;
         vectorizable = false;
         break;
      } else {
         nir_alu_instr *alu = nir_instr_as_alu(parent_instr);
         if (alu->op == nir_op_vec2 || alu->op == nir_op_vec3 ||
             alu->op == nir_op_vec4) {
            mov_needed = true;
            break;
         } else if (alu->op == nir_op_mov) {
            mov_needed = true;
            vectorizable = false;
         } else {
            /* Assume for non-moves that the const is folded into the src */
         }
      }
   }

   return mov_needed ? ((float)(def->num_components * def->bit_size) /
                        (vectorizable ? 32.0 : 16.0))
                     : 0;
}

static bool
avoid_instr(const nir_instr *instr, const void *data)
{
   return false;
}

static const nir_opt_preamble_options preamble_options = {
   .drawid_uniform = true,
   .subgroup_size_uniform = true,
   /* not supported in hardware */
   .load_workgroup_size_allowed = false,
   .def_size = def_size,
   .instr_cost_cb = instr_cost,
   .rewrite_cost_cb = rewrite_cost,
   .avoid_instr_cb = avoid_instr,

   /* hardware size is 512, but it's polite to leave some wiggle room to push
    * hot constants so we don't end up rematerializing all over the place.
    * 480 seems to be a sweetspot, based on a few minutes of shader-db.
    */
   .preamble_storage_size[nir_preamble_class_general] = 480,

   /* We have at least 32 texture state registers. TODO: check for more? */
   .preamble_storage_size[nir_preamble_class_image] = 32,

   /* We have at least 16 sampler state registers. TODO: check for more? */
   .preamble_storage_size[nir_preamble_class_sampler] = 16,
};

/*
 * Bindless image handles can't be stored to uniforms, so we move them back to
 * the main shader. Effectively un-optimizing the preamble.
 */
static bool
lower_store_preamble(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   int16_t *heaps = data;
   if (intr->intrinsic != nir_intrinsic_store_preamble ||
       nir_intrinsic_preamble_class(intr) == nir_preamble_class_image)
      return false;

   nir_intrinsic_instr *handle = nir_src_as_intrinsic(intr->src[0]);
   if (!handle || handle->intrinsic != nir_intrinsic_bindless_image_agx)
      return false;

   heaps[nir_intrinsic_base(intr)] = nir_intrinsic_desc_set(handle);
   nir_src_rewrite(&intr->src[0], handle->src[0].ssa);
   return true;
}

static bool
lower_preamble(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic == nir_intrinsic_bindless_sampler_agx) {
      /* Rematerialize bindless_sampler_agx before store_preamble with only the
       * byte offset (first source), not the sampler index.
       */
      nir_foreach_use_safe(use, &intr->def) {
         nir_instr *parent = nir_src_parent_instr(use);
         if (parent->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *pintr = nir_instr_as_intrinsic(parent);
         if (pintr->intrinsic != nir_intrinsic_store_preamble ||
             nir_intrinsic_preamble_class(pintr) != nir_preamble_class_sampler)
            continue;

         b->cursor = nir_before_src(use);
         nir_def *repl =
            nir_bindless_sampler_agx(b, intr->src[0].ssa, nir_undef(b, 1, 16),
                                     .desc_set = nir_intrinsic_desc_set(intr));
         nir_src_rewrite(use, repl);
      }

      /* Replace other uses with just the sampler index. */
      nir_def_replace(&intr->def, intr->src[1].ssa);
      return true;
   }

   if (intr->intrinsic != nir_intrinsic_load_preamble)
      return false;

   int16_t *heaps = data;
   b->cursor = nir_after_instr(&intr->instr);

   unsigned base = nir_intrinsic_base(intr);
   nir_def *new_ = NULL;
   bool ts = nir_intrinsic_preamble_class(intr) == nir_preamble_class_image;
   bool ss = nir_intrinsic_preamble_class(intr) == nir_preamble_class_sampler;
   if (!ts && heaps[base] >= 0) {
      new_ = nir_bindless_image_agx(b, &intr->def, .desc_set = heaps[base]);
   }

   nir_foreach_use_safe(use, &intr->def) {
      nir_instr *parent = nir_src_parent_instr(use);

      if (parent->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *pintr = nir_instr_as_intrinsic(parent);

         if (ts) {
            nir_rewrite_image_intrinsic(pintr, nir_imm_intN_t(b, base / 2, 16),
                                        false);
         } else if (new_ != NULL &&
                    pintr->intrinsic != nir_intrinsic_bindless_image_agx) {
            nir_src_rewrite(use, new_);
         }
      } else if (parent->type == nir_instr_type_tex) {
         nir_tex_instr *tex = nir_instr_as_tex(parent);
         nir_tex_src *src = (nir_tex_src *)use;

         if (src->src_type == nir_tex_src_sampler_handle && ss) {
            nir_steal_tex_src(tex, nir_tex_src_sampler_handle);
            tex->sampler_index = base;
         } else if (src->src_type == nir_tex_src_texture_handle && ts) {
            nir_steal_tex_src(tex, nir_tex_src_texture_handle);
            tex->texture_index = base / 2;
         } else if (src->src_type == nir_tex_src_texture_handle) {
            assert(new_ != NULL);
            nir_src_rewrite(use, new_);
         }
      }
   }

   return true;
}

bool
agx_nir_opt_preamble(nir_shader *nir, unsigned *sizes)
{
   bool progress = false;
   NIR_PASS(progress, nir, nir_opt_preamble, &preamble_options, sizes);

   int16_t heap[512];
   memset(heap, ~0, sizeof(heap));

   if (progress) {
      nir_function_intrinsics_pass(nir_shader_get_preamble(nir),
                                   lower_store_preamble,
                                   nir_metadata_control_flow, heap);
   }

   NIR_PASS(progress, nir, nir_shader_intrinsics_pass, lower_preamble,
            nir_metadata_control_flow, heap);

   return progress;
}
