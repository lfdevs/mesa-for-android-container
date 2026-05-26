/*
 * Copyright 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_range_analysis.h"

/* Applications like DXVK spam float control restriction flags on all ALU instructions.
 * Remove signed zero, Inf, NaN preserve flags if we can prove that no inputs/outputs
 * are zero/inf/NaN using range analysis.
 * For signed zero, we can go a step further by back propagating when signed zero are not
 * needed, which is a quite common. For example, any float comparison, cosinus, exp2, log2,
 * or addition with non zero value does not care about the zero sign of the inputs. Neither
 * do texture coordinates.
 * Drivers can also set no_signed_zero for fragment output stores based on state, blending,
 * fixed point or R11G11B10 formats do not care about the sign of zero.
 *
 * Future work:
 * For pre raster stages, position doesn't care, and we could back propagate information from
 * the FS for varyings, and interpolated varyings do not care anyway.
 */

struct opt_fp_ctrl_state {
   nir_fp_analysis_state fp_class_state;
};

static inline nir_block *
block_get_loop_preheader(nir_block *block)
{
   nir_cf_node *parent = block->cf_node.parent;
   if (parent->type != nir_cf_node_loop)
      return NULL;
   if (block != nir_cf_node_cf_tree_first(parent))
      return NULL;
   return nir_cf_node_as_block(nir_cf_node_prev(parent));
}


static bool
src_mark_preserve_sz(nir_src *src, UNUSED void *state)
{
   nir_def_instr(src->ssa)->pass_flags = true;
   return true;
}

static bool
can_prop_nsz(nir_alu_instr *alu)
{
   /* Only divide cares about the sign of zero even when the sign of zero
    * of the output doesn't matter.
    */
   switch (alu->op) {
   case nir_op_fdiv:
   case nir_op_frcp:
   case nir_op_frsq:
   case nir_op_fcopysign_pco:
      return false;
   default:
      return true;
   }
}

static bool
opt_alu_fp_math_ctrl(nir_alu_instr *alu, struct opt_fp_ctrl_state *state)
{
   if (alu->op == nir_op_bcsel) {
      src_mark_preserve_sz(&alu->src[0].src, NULL);

      if (alu->instr.pass_flags) {
         src_mark_preserve_sz(&alu->src[1].src, NULL);
         src_mark_preserve_sz(&alu->src[2].src, NULL);
      }
      return false;
   } else if (nir_op_is_vec_or_mov(alu->op) && !alu->instr.pass_flags) {
      return false;
   }

   const nir_op_info *op_info = &nir_op_infos[(int)alu->op];
   unsigned old_fp_math_ctrl = alu->fp_math_ctrl;
   if (alu->fp_math_ctrl & nir_fp_preserve_sz_inf_nan) {
      fp_class_mask class_mask = 0;

      bool dest_is_float = nir_alu_type_get_base_type(op_info->output_type) == nir_type_float;

      if (dest_is_float) {
         class_mask |= nir_analyze_fp_class(&state->fp_class_state, &alu->def);
         if (can_prop_nsz(alu) && (!alu->instr.pass_flags || !(class_mask & FP_CLASS_ANY_ZERO)))
            alu->fp_math_ctrl &= ~nir_fp_preserve_signed_zero;
      }

      for (unsigned i = 0; i < op_info->num_inputs; i++) {
         if (nir_alu_type_get_base_type(op_info->input_types[i]) == nir_type_float)
            class_mask |= nir_analyze_fp_class(&state->fp_class_state, alu->src[i].src.ssa);
      }

      /* If class_mask is 0, the opcode has no float operands/definition,
       * So it must be a special opcode, or operate on bfloats. Ignore these.
       */
      if (class_mask) {
         /* If none of the float operands or the definition can be zero/Inf/NaN,
          * remove the matching fp_math_ctrl flag.
          */
         if (!(class_mask & FP_CLASS_ANY_ZERO))
            alu->fp_math_ctrl &= ~nir_fp_preserve_signed_zero;
         if (!(class_mask & FP_CLASS_ANY_INF))
            alu->fp_math_ctrl &= ~nir_fp_preserve_inf;
         if (!(class_mask & FP_CLASS_NAN))
            alu->fp_math_ctrl &= ~nir_fp_preserve_nan;
      }
   }

   if (alu->fp_math_ctrl & nir_fp_preserve_signed_zero) {
      /* Some alu never cares about the input sign of zero. */
      switch (alu->op) {
      case nir_op_fabs:
      case nir_op_fsat:
      case nir_op_fexp2:
      case nir_op_flog2:
      case nir_op_fcos:
      case nir_op_fcos_normalized_2_pi:
      case nir_op_fmulz:
      case nir_op_ffract:
         break;
      case nir_op_fmin: {
         bool had_neg_zero = false;
         for (unsigned i = 0; i < 2; i++) {
            fp_class_mask fp_class = nir_analyze_fp_class(&state->fp_class_state, alu->src[i].src.ssa);

            if (fp_class & (FP_CLASS_NAN | FP_CLASS_ANY_POS | FP_CLASS_POS_ZERO)) {
               src_mark_preserve_sz(&alu->src[!i].src, NULL);
            } else if (fp_class & FP_CLASS_NEG_ZERO) {
               /* If both operands can be -0.0, at least one needs to be preserved. */
               if (had_neg_zero)
                  src_mark_preserve_sz(&alu->src[!i].src, NULL);
               had_neg_zero = true;
            }
         }
         break;
      }

      case nir_op_fmax: {
         bool had_pos_zero = false;
         for (unsigned i = 0; i < 2; i++) {
            fp_class_mask fp_class = nir_analyze_fp_class(&state->fp_class_state, alu->src[i].src.ssa);

            if (fp_class & (FP_CLASS_NAN | FP_CLASS_ANY_NEG | FP_CLASS_NEG_ZERO)) {
               src_mark_preserve_sz(&alu->src[!i].src, NULL);
            } else if (fp_class & FP_CLASS_POS_ZERO) {
               /* If both operands can be +0.0, at least one needs to be preserved. */
               if (had_pos_zero)
                  src_mark_preserve_sz(&alu->src[!i].src, NULL);
               had_pos_zero = true;
            }
         }
         break;
      }
      case nir_op_fsub:
      case nir_op_fadd: {
         bool had_pos_zero = false;
         for (unsigned i = 0; i < 2; i++) {
            fp_class_mask fp_class = nir_analyze_fp_class(&state->fp_class_state, alu->src[i].src.ssa);

            bool negate = i == 1 && alu->op == nir_op_fsub;

            if (fp_class & (negate ? FP_CLASS_POS_ZERO : FP_CLASS_NEG_ZERO)) {
               src_mark_preserve_sz(&alu->src[!i].src, NULL);
            } else if (fp_class & (negate ? FP_CLASS_NEG_ZERO : FP_CLASS_POS_ZERO)) {
               /* If both operands can be +0.0, at least one needs to be preserved. */
               if (had_pos_zero)
                  src_mark_preserve_sz(&alu->src[!i].src, NULL);
               had_pos_zero = true;
            }
         }
         break;
      }

      case nir_op_ffmaz:
      case nir_op_fmadz:
         src_mark_preserve_sz(&alu->src[2].src, NULL);
         break;
      case nir_op_ffma:
      case nir_op_ffma_weak:
      case nir_op_fmad:
         if ((nir_analyze_fp_class(&state->fp_class_state, alu->src[2].src.ssa) & FP_CLASS_NEG_ZERO) &&
             !nir_alu_srcs_equal(alu, alu, 0, 1)) {
            src_mark_preserve_sz(&alu->src[0].src, NULL);
            src_mark_preserve_sz(&alu->src[1].src, NULL);
         }
         src_mark_preserve_sz(&alu->src[2].src, NULL);
         break;
      case nir_op_fmul:
         if (nir_alu_srcs_equal(alu, alu, 0, 1))
            break;
         FALLTHROUGH;
      default:
         for (unsigned i = 0; i < op_info->num_inputs; i++)
            src_mark_preserve_sz(&alu->src[i].src, NULL);
         break;
      }
   } else {
      /* Only preserve signed zeros for non float operands. */
      for (unsigned i = 0; i < op_info->num_inputs; i++) {
         if (nir_alu_type_get_base_type(op_info->input_types[i]) != nir_type_float)
            src_mark_preserve_sz(&alu->src[i].src, NULL);
      }
   }

   return alu->fp_math_ctrl != old_fp_math_ctrl;
}

static void
prop_tex_fp_math_ctrl(nir_tex_instr *tex)
{
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      /* Floating point tex sources don't care about sign of zero. */
      if (nir_tex_instr_src_type(tex, i) != nir_type_float)
         src_mark_preserve_sz(&tex->src[i].src, NULL);
   }
}

static bool
opt_intrin_fp_math_ctrl(nir_intrinsic_instr *intrin)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_ddx:
   case nir_intrinsic_ddx_coarse:
   case nir_intrinsic_ddx_fine:
   case nir_intrinsic_ddy:
   case nir_intrinsic_ddy_coarse:
   case nir_intrinsic_ddy_fine: {
      unsigned fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intrin);
      if ((fp_math_ctrl & nir_fp_preserve_signed_zero) == 0)
         return false;

      if (intrin->instr.pass_flags) {
         src_mark_preserve_sz(&intrin->src[0], NULL);
         return false;
      } else {
         fp_math_ctrl &= ~nir_fp_preserve_signed_zero;
         nir_intrinsic_set_fp_math_ctrl(intrin, fp_math_ctrl);
         return true;
      }
   }
   default:
      break;
   }

   if (!nir_intrinsic_has_io_semantics(intrin)) {
      nir_foreach_src(&intrin->instr, src_mark_preserve_sz, NULL);
      return false;
   }

   nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);
   const nir_intrinsic_info *info = &nir_intrinsic_infos[(int)intrin->intrinsic];

   if (info->has_dest) {
      nir_foreach_src(&intrin->instr, src_mark_preserve_sz, NULL);

      /* For loads, set no signed zero flag based on gathered info. */
      if (!intrin->instr.pass_flags && !sem.no_signed_zero) {
         sem.no_signed_zero = 1;
         nir_intrinsic_set_io_semantics(intrin, sem);
         return true;
      }

      return false;
   } else {
      /* For stores, propagate the signed zero information for the data source. */
      for (unsigned i = sem.no_signed_zero; i < info->num_srcs; i++)
         src_mark_preserve_sz(&intrin->src[i], NULL);

      return false;
   }
}

static bool
opt_fp_math_ctrl_impl(nir_function_impl *impl)
{
   /* Setup pass flags: Store if signed zeros are needed.
    * Handle loop header phis here already:
    * For now, just disable opts for the back edges.
    * That could be improved, but not sure if it's worth it.
    */
   nir_foreach_block_reverse(block, impl) {
      nir_block *preheader = block_get_loop_preheader(block);

      nir_foreach_instr_reverse(instr, block) {
         instr->pass_flags = false;
         if (instr->type == nir_instr_type_phi && preheader) {
            nir_phi_instr *phi = nir_instr_as_phi(instr);
            nir_foreach_phi_src(src, phi) {
               if (src->pred != preheader)
                  nir_def_instr(src->src.ssa)->pass_flags = true;
            }
         }
      }
   }


   struct opt_fp_ctrl_state state = {0};
   state.fp_class_state = nir_create_fp_analysis_state(impl);

   bool progress = false;

   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_reverse(instr, block) {
         switch (instr->type) {
         case nir_instr_type_alu:
            progress |= opt_alu_fp_math_ctrl(nir_instr_as_alu(instr), &state);
            break;
         case nir_instr_type_tex:
            prop_tex_fp_math_ctrl(nir_instr_as_tex(instr));
            break;
         case nir_instr_type_intrinsic:
            progress |= opt_intrin_fp_math_ctrl(nir_instr_as_intrinsic(instr));
            break;
         case nir_instr_type_phi:
            if (!instr->pass_flags)
               break;
            FALLTHROUGH;
         default:
            nir_foreach_src(instr, src_mark_preserve_sz, NULL);
            break;
         }
      }
   }

   nir_free_fp_analysis_state(&state.fp_class_state);

   return nir_progress(progress, impl, nir_metadata_all);
}

bool
nir_opt_fp_math_ctrl(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader)
      progress |= opt_fp_math_ctrl_impl(impl);

   return progress;
}
