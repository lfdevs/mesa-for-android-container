/*
 * Copyright 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "glsl_types.h"
#include "nir.h"
#include "vtn_private.h"
#include "util/stack_array.h"

static enum glsl_cmat_use
vtn_cooperative_matrix_use_to_glsl(SpvCooperativeMatrixUse use)
{
   switch (use) {
   case SpvCooperativeMatrixUseMatrixAKHR:
      return GLSL_CMAT_USE_A;
   case SpvCooperativeMatrixUseMatrixBKHR:
      return GLSL_CMAT_USE_B;
   case SpvCooperativeMatrixUseMatrixAccumulatorKHR:
      return GLSL_CMAT_USE_ACCUMULATOR;
   default:
      UNREACHABLE("Unexpected cooperative matrix use");
   }
}

void
vtn_handle_cooperative_type(struct vtn_builder *b, struct vtn_value *val,
                            SpvOp opcode, const uint32_t *w, unsigned count)
{
   vtn_assert(opcode == SpvOpTypeCooperativeMatrixKHR);

   b->shader->info.cs.has_cooperative_matrix = true;

   struct vtn_type *component_type = vtn_get_type(b, w[2]);

   const mesa_scope scope = vtn_translate_scope(b, vtn_constant_uint(b, w[3]));
   const uint64_t rows = vtn_constant_uint(b, w[4]);
   const uint64_t cols = vtn_constant_uint(b, w[5]);

   vtn_assert(rows <= UINT16_MAX);
   vtn_assert(cols <= UINT16_MAX);
   vtn_assert(cols == 0 || rows <= UINT32_MAX / cols);

   enum glsl_cmat_use use = vtn_cooperative_matrix_use_to_glsl(vtn_constant_uint(b, w[6]));

   val->type->base_type = vtn_base_type_cooperative_matrix;
   vtn_fail_if(!glsl_type_is_numeric(component_type->type),
               "OpTypeCooperativeMatrixKHR "
               "Component Type must be a scalar numerical type.");

   val->type->desc.element_type = glsl_get_base_type(component_type->type);
   val->type->desc.scope = scope;
   val->type->desc.rows = rows;
   val->type->desc.cols = cols;
   val->type->desc.use = use;

   val->type->type = glsl_cmat_type(&val->type->desc);
   val->type->component_type = component_type;
}

static enum glsl_matrix_layout
vtn_matrix_layout_to_glsl(SpvCooperativeMatrixLayout layout)
{
   switch (layout) {
   case SpvCooperativeMatrixLayoutRowMajorKHR:
      return GLSL_MATRIX_LAYOUT_ROW_MAJOR;
   case SpvCooperativeMatrixLayoutColumnMajorKHR:
      return GLSL_MATRIX_LAYOUT_COLUMN_MAJOR;
   default:
      UNREACHABLE("Unexpected cooperative matrix layout");
   }
}

nir_deref_instr *
vtn_create_cmat_temporary(struct vtn_builder *b, const struct glsl_type *t, const char *name)
{
   nir_variable *var = nir_local_variable_create(b->nb.impl, t, name);
   return nir_build_deref_var(&b->nb, var);
}

static nir_deref_instr *
vtn_get_cmat_deref(struct vtn_builder *b, uint32_t value_id)
{
   nir_deref_instr *deref = vtn_get_deref_for_id(b, value_id);
   vtn_assert(glsl_type_is_cmat(deref->type));
   return deref;
}

static struct vtn_pointer *
vtn_cast_pointer_to_byte_pointer(struct vtn_builder *b, struct vtn_pointer *p)
{
   assert(!p->type->pointed);

   struct vtn_type *t = vtn_zalloc(b, struct vtn_type);
   t->base_type = vtn_base_type_scalar;
   t->type = glsl_uint8_t_type();
   t->length = 1;
   return vtn_cast_pointer(b, p, t);
}

static bool
desc_type_is_signed(const struct glsl_type *type)
{
   if (type->cmat_desc.element_type == GLSL_TYPE_INT8 ||
       type->cmat_desc.element_type == GLSL_TYPE_INT16 ||
       type->cmat_desc.element_type == GLSL_TYPE_INT)
      return true;
   return false;
}

void
vtn_handle_cooperative_instruction(struct vtn_builder *b, SpvOp opcode,
                                   const uint32_t *w, unsigned count)
{
   switch (opcode) {
   case SpvOpCooperativeMatrixLoadKHR: {
      struct vtn_value *src_val = vtn_value(b, w[3], vtn_value_type_pointer);
      struct vtn_pointer *src = vtn_value_to_pointer(b, src_val);
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);

      /* Untyped pointers are effectively used as byte pointers. */
      if (!src->type->pointed)
         src = vtn_cast_pointer_to_byte_pointer(b, src);

      const SpvCooperativeMatrixLayout layout = vtn_constant_uint(b, w[4]);
      nir_def *stride = count > 5 ? vtn_get_nir_ssa(b, w[5]) : nir_imm_zero(&b->nb, 1, 32);

      SpvMemoryAccessMask access = SpvMemoryAccessMaskNone;
      if (count > 6) {
         unsigned idx = 6, alignment;
         SpvScope scope;
         vtn_get_mem_operands(b, w, count, &idx, &access, &alignment, NULL, &scope);
         vtn_emit_make_visible_barrier(b, access, scope, src->mode);
      }

      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_bitcast");
      nir_cmat_load(&b->nb, &dst->def, vtn_pointer_to_ssa(b, src), stride,
                    .matrix_layout = vtn_matrix_layout_to_glsl(layout));
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixStoreKHR: {
      struct vtn_value *dest_val = vtn_value(b, w[1], vtn_value_type_pointer);
      struct vtn_pointer *dest = vtn_value_to_pointer(b, dest_val);

      /* Untyped pointers are effectively used as byte pointers. */
      if (!dest->type->pointed)
         dest = vtn_cast_pointer_to_byte_pointer(b, dest);

      const SpvCooperativeMatrixLayout layout = vtn_constant_uint(b, w[3]);
      nir_def *stride = count > 4 ? vtn_get_nir_ssa(b, w[4]) : nir_imm_zero(&b->nb, 1, 32);

      nir_deref_instr *src = vtn_get_cmat_deref(b, w[2]);
      nir_cmat_store(&b->nb, vtn_pointer_to_ssa(b, dest), &src->def, stride,
                     .matrix_layout = vtn_matrix_layout_to_glsl(layout));

      SpvMemoryAccessMask access = SpvMemoryAccessMaskNone;
      if (count > 5) {
         unsigned idx = 5, alignment;
         SpvScope scope;
         vtn_get_mem_operands(b, w, count, &idx, &access, &alignment, &scope, NULL);
         vtn_emit_make_available_barrier(b, access, scope, dest->mode);
      }

      break;
   }

   case SpvOpCooperativeMatrixLengthKHR: {
      struct vtn_type *type = vtn_get_type(b, w[3]);
      nir_def *def = nir_cmat_length(&b->nb, .cmat_desc = type->desc);
      vtn_push_nir_ssa(b, w[2], def);
      break;
   }

   case SpvOpCooperativeMatrixMulAddKHR: {
      nir_deref_instr *mat_a = vtn_get_cmat_deref(b, w[3]);
      nir_deref_instr *mat_b = vtn_get_cmat_deref(b, w[4]);
      nir_deref_instr *mat_c = vtn_get_cmat_deref(b, w[5]);

      const uint32_t operands = count > 6 ? w[6] : 0;
      const bool saturate = operands & SpvCooperativeMatrixOperandsSaturatingAccumulationKHRMask;
      const unsigned signed_mask = operands & (SpvCooperativeMatrixOperandsMatrixASignedComponentsKHRMask |
                                               SpvCooperativeMatrixOperandsMatrixBSignedComponentsKHRMask |
                                               SpvCooperativeMatrixOperandsMatrixCSignedComponentsKHRMask |
                                               SpvCooperativeMatrixOperandsMatrixResultSignedComponentsKHRMask);

      STATIC_ASSERT((unsigned)SpvCooperativeMatrixOperandsMatrixASignedComponentsKHRMask == NIR_CMAT_A_SIGNED);
      STATIC_ASSERT((unsigned)SpvCooperativeMatrixOperandsMatrixBSignedComponentsKHRMask == NIR_CMAT_B_SIGNED);
      STATIC_ASSERT((unsigned)SpvCooperativeMatrixOperandsMatrixCSignedComponentsKHRMask == NIR_CMAT_C_SIGNED);
      STATIC_ASSERT((unsigned)SpvCooperativeMatrixOperandsMatrixResultSignedComponentsKHRMask == NIR_CMAT_RESULT_SIGNED);

      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_muladd");

      nir_cmat_muladd(&b->nb, &dst->def, &mat_a->def, &mat_b->def, &mat_c->def,
                      .saturate = saturate,
                      .cmat_signed_mask = signed_mask);

      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpBitcast: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      vtn_assert(dst_type->base_type == vtn_base_type_cooperative_matrix);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_bitcast");
      nir_cmat_bitcast(&b->nb, &dst->def, &src->def);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixGetCoordinateEXT: {
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);
      nir_def *coord = vtn_get_nir_ssa(b, w[4]);
      nir_def *def = nir_cmat_get_coordinate(&b->nb, coord, .cmat_desc = src->type->cmat_desc);
      vtn_push_nir_ssa(b, w[2], def);
      break;
   }

   case SpvOpCooperativeMatrixConvertNV: {
      struct vtn_value *val = vtn_untyped_value(b, w[2]);
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);
      const bool transpose = vtn_has_decoration(b, val, SpvDecorationCooperativeMatrixTransposeEXT);
      unsigned signed_mask = 0;

      if (desc_type_is_signed(dst_type->type))
         signed_mask |= NIR_CMAT_RESULT_SIGNED;

      if (desc_type_is_signed(src->type))
         signed_mask |= NIR_CMAT_A_SIGNED;

      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_convert_nv");
      if (transpose)
         nir_cmat_transpose(&b->nb, &dst->def, &src->def, .cmat_signed_mask = signed_mask);
      else
         nir_cmat_convert(&b->nb, &dst->def, &src->def, .cmat_signed_mask = signed_mask);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixTransposeNV: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);
      unsigned signed_mask = 0;

      if (desc_type_is_signed(dst_type->type))
         signed_mask |= NIR_CMAT_RESULT_SIGNED;

      if (desc_type_is_signed(src->type))
         signed_mask |= NIR_CMAT_A_SIGNED;

      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_transpose_nv");
      nir_cmat_transpose(&b->nb, &dst->def, &src->def, .cmat_signed_mask = signed_mask);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixReduceEXT: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

      struct vtn_function *reduce_fn = vtn_value(b, w[5], vtn_value_type_function)->func;

      reduce_fn->referenced = true;
      reduce_fn->nir_func->cmat_call = true;
      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_reduce");
      nir_cmat_call_instr *call = nir_cmat_call_instr_create(b->nb.shader, nir_cmat_call_op_reduce, reduce_fn->nir_func);
      call->params[0] = nir_src_for_ssa(&dst->def);
      call->params[1] = nir_src_for_ssa(&src->def);
      call->const_index[0] = w[4];
      nir_builder_instr_insert(&b->nb, &call->instr);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixPerElementOpEXT: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

      struct vtn_function *per_element_fn = vtn_value(b, w[4], vtn_value_type_function)->func;

      per_element_fn->referenced = true;
      per_element_fn->nir_func->cmat_call = true;
      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_per_element");

      nir_cmat_call_instr *call = nir_cmat_call_instr_create(b->nb.shader, nir_cmat_call_op_per_element_op, per_element_fn->nir_func);
      call->params[0] = nir_src_for_ssa(&dst->def);
      call->params[1] = nir_src_for_ssa(nir_imm_zero(&b->nb, 1, 32));
      call->params[2] = nir_src_for_ssa(nir_imm_zero(&b->nb, 1, 32));
      call->params[3] = nir_src_for_ssa(&src->def);

      for (unsigned i = 0; i < count - 5; i++) {
         struct vtn_ssa_value *ssa = vtn_ssa_value(b, w[5 + i]);
         nir_def *def;
         nir_deref_instr *deref = NULL;

         if (ssa->is_variable) {
            deref = nir_build_deref_var(&b->nb, ssa->var);
            def = &deref->def;
         } else if (glsl_type_is_vector_or_scalar(ssa->type)) {
            def = ssa->def;
         } else
            def = ssa->elems[0]->def;

         call->params[4 + i] = nir_src_for_ssa(def);
      }
      nir_builder_instr_insert(&b->nb, &call->instr);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixLoadTensorNV: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      struct vtn_value *src_val = vtn_value(b, w[3], vtn_value_type_pointer);
      struct vtn_pointer *src_p = vtn_value_to_pointer(b, src_val);
      nir_deref_instr *clip_src = vtn_get_cmat_deref(b, w[4]);
      nir_def *view = NULL;
      struct vtn_function *decode_fn = NULL;
      SpvMemoryAccessMask access = SpvMemoryAccessMaskNone;
      unsigned idx = 6;
      if (count > 6) {
         unsigned alignment;
         SpvScope scope;
         vtn_get_mem_operands(b, w, count, &idx, &access, &alignment, NULL, &scope);
         vtn_emit_make_visible_barrier(b, access, scope, src_p->mode);
      }

      uint32_t tensor_addressing = w[idx];
      struct nir_cmat_tensor_load tensor_load = { 0 };
      idx++;
      if (tensor_addressing & SpvTensorAddressingOperandsTensorViewMask) {
         struct vtn_type *view_type = vtn_get_value_type(b, w[idx]);
         struct vtn_ssa_value *view_val = vtn_ssa_value(b, w[idx]);
         nir_deref_instr *view_tmp = vtn_create_cmat_temporary(b, view_val->type, "store_view_tmp");
         vtn_local_store(b, view_val, view_tmp, 0);
         view = &view_tmp->def;
         tensor_load.tensor_view = 1;
         for (unsigned p = 0; p < NIR_TENSOR_VIEW_MAX_PERMUTATIONS; p++)
            tensor_load.view_permutations[p] = view_type->tensor_view_permutations[p];
         tensor_load.view_has_dims = view_type->tensor_view_has_dims;
	 idx++;
      }
      if (tensor_addressing & SpvTensorAddressingOperandsDecodeFuncMask) {
         decode_fn = vtn_value(b, w[idx], vtn_value_type_function)->func;
         decode_fn->referenced = true;
         decode_fn->nir_func->cmat_call = true;
         idx++;
      }
      assert(idx == count);
      if (!view) {
         view = nir_undef(&b->nb, 1, 32);
      }

      struct vtn_type *layout_type = vtn_get_value_type(b, w[5]);
      tensor_load.layout_clamp_mode = layout_type->tensor_layout_clamp_mode;

      struct vtn_ssa_value *layout_val = vtn_ssa_value(b, w[5]);
      nir_deref_instr *layout_tmp = vtn_create_cmat_temporary(b, layout_val->type, "store_layout_tmp");
      vtn_local_store(b, layout_val, layout_tmp, 0);

      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_tensor");

      nir_cmat_call_op op = nir_cmat_call_op_tensor_load;
      nir_cmat_call_instr *call = nir_cmat_call_instr_create(b->nb.shader, op,
                                                             decode_fn ? decode_fn->nir_func : NULL);

      call->params[0] = nir_src_for_ssa(&dst->def);
      call->params[1] = nir_src_for_ssa(vtn_pointer_to_ssa(b, src_p));
      call->params[2] = nir_src_for_ssa(&layout_tmp->def);
      call->params[3] = nir_src_for_ssa(view);
      call->params[4] = nir_src_for_ssa(&clip_src->def);
      nir_cmat_call_set_tensor_load_info(call, tensor_load);
      nir_cmat_call_set_cmat_desc(call, dst_type->type->cmat_desc);

      nir_builder_instr_insert(&b->nb, &call->instr);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }
   case SpvOpCooperativeMatrixStoreTensorNV: {
      struct vtn_value *dest_val = vtn_value(b, w[1], vtn_value_type_pointer);
      struct vtn_pointer *dest = vtn_value_to_pointer(b, dest_val);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[2]);

      struct vtn_ssa_value *layout_val = vtn_ssa_value(b, w[3]);
      nir_deref_instr *layout_tmp = vtn_create_cmat_temporary(b, layout_val->type, "store_layout_tmp");
      vtn_local_store(b, layout_val, layout_tmp, 0);

      nir_def *view = NULL;
      unsigned idx = 4;
      SpvMemoryAccessMask access = SpvMemoryAccessMaskNone;
      unsigned alignment;
      SpvScope scope;

      if (count > 4)
         vtn_get_mem_operands(b, w, count, &idx, &access, &alignment, &scope, NULL);

      struct nir_cmat_tensor_load tensor_load = { 0 };
      if (w[idx] != 0) {
         if (w[idx] == SpvTensorAddressingOperandsTensorViewMask) {
            struct vtn_type *view_type = vtn_get_value_type(b, w[idx + 1]);
            struct vtn_ssa_value *view_val = vtn_ssa_value(b, w[idx + 1]);
            nir_deref_instr *view_tmp = vtn_create_cmat_temporary(b, view_val->type, "store_view_tmp");
            vtn_local_store(b, view_val, view_tmp, 0);
            view = &view_tmp->def;
            tensor_load.tensor_view = 1;
            for (unsigned p = 0; p < NIR_TENSOR_VIEW_MAX_PERMUTATIONS; p++)
               tensor_load.view_permutations[p] = view_type->tensor_view_permutations[p];
            tensor_load.view_has_dims = view_type->tensor_view_has_dims;
         }
      }

      if (!view) {
         view = nir_undef(&b->nb, 1, 32);
      }

      struct vtn_type *layout_type = vtn_get_value_type(b, w[3]);
      tensor_load.layout_clamp_mode = layout_type->tensor_layout_clamp_mode;
      nir_cmat_call_op op = nir_cmat_call_op_tensor_store;
      nir_cmat_call_instr *call = nir_cmat_call_instr_create(b->nb.shader, op, NULL);

      call->params[0] = nir_src_for_ssa(vtn_pointer_to_ssa(b, dest));
      call->params[1] = nir_src_for_ssa(&src->def);
      call->params[2] = nir_src_for_ssa(&layout_tmp->def);
      call->params[3] = nir_src_for_ssa(view);
      nir_cmat_call_set_tensor_load_info(call, tensor_load);
      nir_cmat_call_set_cmat_desc(call, src->type->cmat_desc);
      nir_builder_instr_insert(&b->nb, &call->instr);

      if (access != SpvMemoryAccessMaskNone)
         vtn_emit_make_available_barrier(b, access, scope, dest->mode);
      break;
   }
   default:
      UNREACHABLE("Unexpected opcode for cooperative matrix instruction");
   }
}

void
vtn_handle_cooperative_alu(struct vtn_builder *b, struct vtn_value *dest_val,
                           const struct glsl_type *dest_type, SpvOp opcode,
                           const uint32_t *w, unsigned count)
{
      vtn_assert(glsl_type_is_cmat(dest_type));

      switch (opcode) {
      case SpvOpConvertFToU:
      case SpvOpConvertFToS:
      case SpvOpConvertSToF:
      case SpvOpConvertUToF:
      case SpvOpUConvert:
      case SpvOpSConvert:
      case SpvOpFConvert: {
         struct vtn_type *dst_type = vtn_get_type(b, w[1]);
         nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);
         struct vtn_value *dest_val = vtn_untyped_value(b, w[2]);

         /* The Convert operations define whether integers are interpreted
          * as signed or unsigned regardless of their original type.  So take
          * note of that in the intrinsic.  Reuse nir_cmat_signed for that.
          */
         const unsigned signed_mask =
            (vtn_convert_op_src_type(opcode) == nir_type_int ? NIR_CMAT_A_SIGNED : 0) |
            (vtn_convert_op_dst_type(opcode) == nir_type_int ? NIR_CMAT_RESULT_SIGNED : 0);

         const bool saturate = vtn_has_decoration(b, dest_val, SpvDecorationSaturatedToLargestFloat8NormalConversionEXT);
         const bool transpose = vtn_has_decoration(b, dest_val, SpvDecorationCooperativeMatrixTransposeEXT);

         nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_convert");

         if (transpose)
            nir_cmat_transpose(&b->nb, &dst->def, &src->def, .saturate = saturate, .cmat_signed_mask = signed_mask);
         else
            nir_cmat_convert(&b->nb, &dst->def, &src->def, .saturate = saturate, .cmat_signed_mask = signed_mask);
         vtn_push_var_ssa(b, w[2], dst->var);

         break;
      }

      case SpvOpFNegate:
      case SpvOpSNegate: {
         struct vtn_type *dst_type = vtn_get_type(b, w[1]);
         nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

         bool swap = false;
         unsigned extra_fp_math_ctrl = nir_fp_fast_math;
         nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &swap, &extra_fp_math_ctrl);
         b->nb.fp_math_ctrl |= extra_fp_math_ctrl;

         nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_unary");
         nir_cmat_unary_op(&b->nb, &dst->def, &src->def,
                           .alu_op = op);
         vtn_push_var_ssa(b, w[2], dst->var);
         break;
      }

      case SpvOpFAdd:
      case SpvOpFSub:
      case SpvOpFMul:
      case SpvOpFDiv:
      case SpvOpIAdd:
      case SpvOpISub:
      case SpvOpIMul:
      case SpvOpSDiv:
      case SpvOpUDiv: {
         bool swap = false;
         unsigned extra_fp_math_ctrl = nir_fp_fast_math;

         struct vtn_type *dst_type = vtn_get_type(b, w[1]);
         nir_deref_instr *mat_a = vtn_get_cmat_deref(b, w[3]);
         nir_deref_instr *mat_b = vtn_get_cmat_deref(b, w[4]);

         nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &swap, &extra_fp_math_ctrl);
         b->nb.fp_math_ctrl |= extra_fp_math_ctrl;

         nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_binary");
         nir_cmat_binary_op(&b->nb, &dst->def, &mat_a->def, &mat_b->def,
                            .alu_op = op);
         vtn_push_var_ssa(b, w[2], dst->var);
         break;
      }

      case SpvOpMatrixTimesScalar: {
         struct vtn_type *dst_type = vtn_get_type(b, w[1]);
         nir_deref_instr *mat = vtn_get_cmat_deref(b, w[3]);

         struct vtn_ssa_value *scalar_val = vtn_ssa_value(b, w[4]);
         vtn_assert(glsl_type_is_scalar(scalar_val->type));
         nir_op op = glsl_type_is_integer(scalar_val->type) ? nir_op_imul : nir_op_fmul;

         nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_times_scalar");
         nir_cmat_scalar_op(&b->nb, &dst->def, &mat->def, scalar_val->def,
                            .alu_op = op);
         vtn_push_var_ssa(b, w[2], dst->var);
         break;
      }

      default:
         UNREACHABLE("invalid cooperative matrix alu instruction");
      }
}

struct vtn_ssa_value *
vtn_cooperative_matrix_extract(struct vtn_builder *b, struct vtn_ssa_value *mat,
                               const uint32_t *indices, unsigned num_indices)
{
   vtn_assert(glsl_type_is_cmat(mat->type));
   nir_deref_instr *mat_deref = vtn_get_deref_for_ssa_value(b, mat);

   vtn_assert(num_indices == 1);
   nir_def *index = nir_imm_intN_t(&b->nb, indices[0], 32);

   const struct glsl_type *element_type = glsl_get_cmat_element(mat->type);
   struct vtn_ssa_value *ret = vtn_create_ssa_value(b, element_type);
   ret->def = nir_cmat_extract(&b->nb, glsl_get_bit_size(element_type), &mat_deref->def, index);
   return ret;
}

struct vtn_ssa_value *
vtn_cooperative_matrix_insert(struct vtn_builder *b, struct vtn_ssa_value *mat,
                              struct vtn_ssa_value *insert, const uint32_t *indices,
                              unsigned num_indices)
{
   vtn_assert(glsl_type_is_cmat(mat->type));
   nir_deref_instr *mat_deref = vtn_get_deref_for_ssa_value(b, mat);

   vtn_assert(num_indices == 1);
   nir_def *index = nir_imm_intN_t(&b->nb, indices[0], 32);

   nir_deref_instr *dst = vtn_create_cmat_temporary(b, mat_deref->type, "cmat_insert");
   nir_cmat_insert(&b->nb, &dst->def, insert->def, &mat_deref->def, index);

   struct vtn_ssa_value *ret = vtn_create_ssa_value(b, dst->type);
   vtn_set_ssa_value_var(b, ret, dst->var);
   return ret;
}

static struct vtn_type *
vtn_create_internal_uint_type(struct vtn_builder *b) {
   struct vtn_type *t = vtn_zalloc(b, struct vtn_type);
   t->base_type = vtn_base_type_scalar;
   t->type = &glsl_type_builtin_uint;
   return t;
}

static struct vtn_type *
vtn_create_internal_tvec_type(struct vtn_builder *b, int dim) {
   struct vtn_type *t = vtn_zalloc(b, struct vtn_type);
   t->base_type = vtn_base_type_vector;
   t->type = glsl_vector_type(GLSL_TYPE_UINT, dim);
   return t;
}

void
vtn_handle_tensor_layout_type(struct vtn_builder *b, struct vtn_value *val,
                              SpvOp opcode, const uint32_t *w, unsigned count)
{
   int dim = vtn_constant_uint(b, w[2]);
   int num_fields = 6;

   struct vtn_type *vec_type = vtn_create_internal_tvec_type(b, dim);
   struct vtn_type *uint_type = vtn_create_internal_uint_type(b);

   if (opcode == SpvOpTypeTensorLayoutNV) {
      val->type->base_type = vtn_base_type_tensor_layout;
      val->type->tensor_layout_clamp_mode = vtn_constant_uint(b, w[3]);
      val->type->tensor_layout_members = vtn_alloc_array(b, struct vtn_type *, num_fields);

      for (unsigned i = 0; i < 5; i++) {
         val->type->tensor_layout_members[i] = vec_type;
      }
      val->type->tensor_layout_members[5] = uint_type;

#define FILL_ST_FIELD(idx, ftype, fname) \
      fields[(idx)] = (struct glsl_struct_field) {   \
         .type = (ftype),                            \
         .name = (fname),                            \
         .location = -1,                             \
         .offset = -1,                               \
      }

      STACK_ARRAY(struct glsl_struct_field, fields, num_fields);

      FILL_ST_FIELD(NIR_TENSOR_LAYOUT_BLOCKSIZE, glsl_vector_type(GLSL_TYPE_UINT, dim), "blockSize");
      FILL_ST_FIELD(NIR_TENSOR_LAYOUT_LAYOUT_DIM, glsl_vector_type(GLSL_TYPE_UINT, dim), "layoutDim");
      FILL_ST_FIELD(NIR_TENSOR_LAYOUT_STRIDE, glsl_vector_type(GLSL_TYPE_UINT, dim), "stride");
      FILL_ST_FIELD(NIR_TENSOR_LAYOUT_OFFSET, glsl_vector_type(GLSL_TYPE_INT, dim), "offset");
      FILL_ST_FIELD(NIR_TENSOR_LAYOUT_SPAN, glsl_vector_type(GLSL_TYPE_UINT, dim), "span");
      FILL_ST_FIELD(NIR_TENSOR_LAYOUT_CLAMP_VALUE, &glsl_type_builtin_uint, "clampValue");

      val->type->length = num_fields;
      val->type->type = glsl_struct_type(fields, num_fields,
                                         "tensor_layout", false);
      STACK_ARRAY_FINISH(fields);
   } else {
      val->type->base_type = vtn_base_type_tensor_view;
      for (unsigned d = 0; d < dim; d++) {
         val->type->tensor_view_permutations[d] = vtn_constant_uint(b, w[4 + d]);
      }
      val->type->tensor_view_has_dims = vtn_constant_uint(b, w[3]);
      val->type->tensor_view_members = vtn_alloc_array(b, struct vtn_type *, num_fields);

      for (unsigned i = 0; i < 2; i++) {
         val->type->members[i] = vec_type;
      }
      for (unsigned i = 2; i < 6; i++) {
         val->type->members[i] = uint_type;
      }

      STACK_ARRAY(struct glsl_struct_field, fields, num_fields);

      FILL_ST_FIELD(NIR_TENSOR_VIEW_DIM, glsl_vector_type(GLSL_TYPE_UINT, dim), "viewDim");
      FILL_ST_FIELD(NIR_TENSOR_VIEW_STRIDE, glsl_vector_type(GLSL_TYPE_UINT, dim), "viewStride");
      FILL_ST_FIELD(NIR_TENSOR_VIEW_CLIP_ROW_OFFSET, &glsl_type_builtin_uint, "clipRowOffset");
      FILL_ST_FIELD(NIR_TENSOR_VIEW_CLIP_ROW_SPAN, &glsl_type_builtin_uint, "clipRowSpan");
      FILL_ST_FIELD(NIR_TENSOR_VIEW_CLIP_COL_OFFSET, &glsl_type_builtin_uint, "clipColOffset");
      FILL_ST_FIELD(NIR_TENSOR_VIEW_CLIP_COL_SPAN, &glsl_type_builtin_uint, "clipColSpan");

#undef FILL_ST_FIELD

      val->type->length = num_fields;
      val->type->type = glsl_struct_type(fields, num_fields,
                                         "tensor_view", false);
      STACK_ARRAY_FINISH(fields);
   }
}

static struct vtn_ssa_value *
init_vec_field(struct vtn_builder *b,
               struct vtn_ssa_value *field,
               const uint32_t *w)
{
   struct vtn_ssa_value *new_field = vtn_create_ssa_value(b, field->type);
   nir_def *srcs[5];
   int nc = glsl_get_vector_elements(field->type);
   for (unsigned i = 0; i < nc; i++) {
      srcs[i] = vtn_get_nir_ssa(b, w[4 + i]);
   }
   new_field->def = nc > 1 ? vtn_vector_construct(b, nc, nc, srcs) : srcs[0];
   return new_field;
}

static void
init_vec(struct vtn_builder *b,
         unsigned index,
         const uint32_t *w)
{
   struct vtn_ssa_value *src = vtn_ssa_value(b, w[3]);
   struct vtn_type *src_type = vtn_get_value_type(b, w[3]);
   struct vtn_ssa_value *field = vtn_composite_extract(b, src, &index, 1);

   struct vtn_ssa_value *new = vtn_composite_copy_logical(b, src, src_type);
   struct vtn_ssa_value *new_field = init_vec_field(b, field, w);
   new = vtn_composite_insert(b, new, src_type, new_field, &index, 1);
   vtn_push_ssa_value(b, w[2], new);
}

static void
init_clip_scalars(struct vtn_builder *b,
                  unsigned index_base,
                  const uint32_t *w)
{
   struct vtn_ssa_value *src = vtn_ssa_value(b, w[3]);
   struct vtn_type *src_type = vtn_get_value_type(b, w[3]);
   struct vtn_ssa_value *new = vtn_composite_copy_logical(b, src, src_type);

   for (unsigned f = 0; f < 4; f++) {
      uint32_t this_index = index_base + f;
      struct vtn_ssa_value *field = vtn_composite_extract(b, src, &this_index, 1);
      struct vtn_ssa_value *new_field = vtn_create_ssa_value(b, field->type);
      int nc = glsl_get_vector_elements(field->type);
      assert(nc <= 1);
      new_field->def = vtn_get_nir_ssa(b, w[4 + f]);

      new = vtn_composite_insert(b, new, src_type, new_field, &this_index, 1);
   }
   vtn_push_ssa_value(b, w[2], new);
}

void
vtn_handle_tensor_layout_instruction(struct vtn_builder *b, SpvOp opcode,
                                     const uint32_t *w, unsigned count)
{
   switch (opcode) {
   case SpvOpCreateTensorLayoutNV: {
      /*
       * The layoutDimension, stride, span, and offset elements are initialized to zero.
       * clampValue is initialized to zero.
       */
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_constant);
      val->constant = rzalloc(b, nir_constant);
      val->constant->num_elements = val->type->length;
      val->constant->elements = ralloc_array(b, nir_constant *, val->constant->num_elements);
      val->constant->elements[NIR_TENSOR_LAYOUT_BLOCKSIZE] = rzalloc(b, nir_constant);
      for (unsigned dim = 0; dim < glsl_get_vector_elements(glsl_get_struct_field(val->type->type, NIR_TENSOR_LAYOUT_BLOCKSIZE)); dim++) {
         val->constant->elements[NIR_TENSOR_LAYOUT_BLOCKSIZE]->values[dim].u32 = 1;
      }
      for (unsigned i = 1; i < val->constant->num_elements; i++)
         val->constant->elements[i] = vtn_null_constant(b, val->type->tensor_layout_members[i]);

      break;
   }
   case SpvOpCreateTensorViewNV: {
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_constant);
      val->constant = rzalloc(b, nir_constant);
      val->constant->num_elements = val->type->length;
      val->constant->elements = ralloc_array(b, nir_constant *, val->constant->num_elements);
      for (unsigned i = 0; i < val->constant->num_elements; i++) {
         if (i == NIR_TENSOR_VIEW_CLIP_ROW_SPAN || i == NIR_TENSOR_VIEW_CLIP_COL_SPAN) {
            val->constant->elements[i] = rzalloc(b, nir_constant);
            val->constant->elements[i]->values[0].u32 = 0xffffffff;
         } else {
            val->constant->elements[i] = vtn_null_constant(b, val->type->tensor_view_members[i]);
         }
      }
      break;
   }
   case SpvOpTensorLayoutSetDimensionNV: {
      struct vtn_ssa_value *src = vtn_ssa_value(b, w[3]);
      struct vtn_type *src_type = vtn_get_value_type(b, w[3]);

      unsigned dim_index = NIR_TENSOR_LAYOUT_LAYOUT_DIM;
      struct vtn_ssa_value *dim_field = vtn_composite_extract(b, src, &dim_index, 1);

      unsigned span_index = NIR_TENSOR_LAYOUT_SPAN;
      struct vtn_ssa_value *span_field = vtn_composite_extract(b, src, &dim_index, 1);

      unsigned offset_index = NIR_TENSOR_LAYOUT_OFFSET;
      struct vtn_ssa_value *offset_field = vtn_composite_extract(b, src, &dim_index, 1);

      struct vtn_ssa_value *new = vtn_composite_copy_logical(b, src, src_type);
      struct vtn_ssa_value *new_dim_field = init_vec_field(b, dim_field, w);
      struct vtn_ssa_value *new_span_field = init_vec_field(b, span_field, w);
      struct vtn_ssa_value *new_offset_field = vtn_create_ssa_value(b, offset_field->type);
      nir_def *offsets[5];

      int nc = glsl_get_vector_elements(dim_field->type);
      for (unsigned i = 0; i < nc; i++) {
         offsets[i] = nir_imm_int(&b->nb, 0);
      }

      new_offset_field->def = nc > 1 ? vtn_vector_construct(b, nc, nc, offsets) : offsets[0];

      new = vtn_composite_insert(b, new, src_type, new_dim_field, &dim_index, 1);
      new = vtn_composite_insert(b, new, src_type, new_span_field, &span_index, 1);
      new = vtn_composite_insert(b, new, src_type, new_offset_field, &offset_index, 1);

      nir_def *strides[5];
      unsigned stride_index = NIR_TENSOR_LAYOUT_STRIDE;
      struct vtn_ssa_value *field = vtn_composite_extract(b, new, &stride_index, 1);
      unsigned bs_index = NIR_TENSOR_LAYOUT_BLOCKSIZE;
      struct vtn_ssa_value *bs_field = vtn_composite_extract(b, new, &bs_index, 1);

      strides[nc - 1] = nir_imm_int(&b->nb, 1);
      for (int dim = nc - 2; dim >= 0; dim--) {
         strides[dim] = nir_imul(&b->nb, strides[dim + 1],
                                 nir_udiv(&b->nb,
                                          nir_channel(&b->nb, new_dim_field->def, dim + 1),
                                          nir_channel(&b->nb, bs_field->def, dim + 1)));
      }
      struct vtn_ssa_value *new_stride_field = vtn_create_ssa_value(b, field->type);
      new_stride_field->def = nc > 1 ? vtn_vector_construct(b, nc, nc, strides) : strides[0];
      new = vtn_composite_insert(b, new, src_type, new_stride_field, &stride_index, 1);
      vtn_push_ssa_value(b, w[2], new);
      break;
   }
   case SpvOpTensorLayoutSetBlockSizeNV: {
      init_vec(b, NIR_TENSOR_LAYOUT_BLOCKSIZE, w);
      break;
   }
   case SpvOpTensorLayoutSetStrideNV: {
      init_vec(b, NIR_TENSOR_LAYOUT_STRIDE, w);
      break;
   }
   case SpvOpTensorLayoutSliceNV: {
      struct vtn_ssa_value *src = vtn_ssa_value(b, w[3]);
      struct vtn_type *src_type = vtn_get_value_type(b, w[3]);
      struct vtn_ssa_value *new = vtn_composite_copy_logical(b, src, src_type);

      unsigned offset_index = NIR_TENSOR_LAYOUT_OFFSET;
      /* offsets have to be added */
      struct vtn_ssa_value *field = vtn_composite_extract(b, src, &offset_index, 1);
      struct vtn_ssa_value *new_field = vtn_create_ssa_value(b, field->type);
      nir_def *srcs[5];
      int nc = glsl_get_vector_elements(field->type);
      for (unsigned i = 0; i < nc; i++) {
         srcs[i] = nir_iadd(&b->nb, nir_channel(&b->nb, field->def, i), vtn_get_nir_ssa(b, w[4 + i * 2]));
      }
      new_field->def = nc > 1 ? vtn_vector_construct(b, nc, nc, srcs) : srcs[0];
      new = vtn_composite_insert(b, new, src_type, new_field, &offset_index, 1);

      unsigned span_index = NIR_TENSOR_LAYOUT_SPAN;
      field = vtn_composite_extract(b, new, &span_index, 1);
      new_field = vtn_create_ssa_value(b, field->type);
      nc = glsl_get_vector_elements(field->type);
      for (unsigned i = 0; i < nc; i++) {
         srcs[i] = vtn_get_nir_ssa(b, w[5 + i * 2]);
      }
      new_field->def = nc > 1 ? vtn_vector_construct(b, nc, nc, srcs) : srcs[0];
      new = vtn_composite_insert(b, new, src_type, new_field, &span_index, 1);

      vtn_push_ssa_value(b, w[2], new);
      break;
   }
   case SpvOpTensorLayoutSetClampValueNV: {
      init_vec(b, NIR_TENSOR_LAYOUT_CLAMP_VALUE, w);
      break;
   }
   case SpvOpTensorViewSetDimensionNV: {
      unsigned dim_index = NIR_TENSOR_VIEW_DIM;
      struct vtn_ssa_value *src = vtn_ssa_value(b, w[3]);
      struct vtn_type *src_type = vtn_get_value_type(b, w[3]);
      struct vtn_ssa_value *dim_field = vtn_composite_extract(b, src, &dim_index, 1);

      struct vtn_ssa_value *new = vtn_composite_copy_logical(b, src, src_type);
      struct vtn_ssa_value *new_dim_field = init_vec_field(b, dim_field, w);

      new = vtn_composite_insert(b, new, src_type, new_dim_field, &dim_index, 1);

      nir_def *strides[5];
      unsigned stride_index = NIR_TENSOR_VIEW_STRIDE;
      struct vtn_ssa_value *field = vtn_composite_extract(b, new, &stride_index, 1);
      int nc = glsl_get_vector_elements(field->type);
      strides[nc - 1] = nir_imm_int(&b->nb, 1);
      for (int dim = nc - 2; dim >= 0; dim--) {
         strides[dim] = nir_imul(&b->nb, strides[dim + 1], nir_channel(&b->nb, new_dim_field->def, dim + 1));
      }
      struct vtn_ssa_value *new_stride_field = vtn_create_ssa_value(b, field->type);
      new_stride_field->def = nc > 1 ? vtn_vector_construct(b, nc, nc, strides) : strides[0];
      new = vtn_composite_insert(b, new, src_type, new_stride_field, &stride_index, 1);
      vtn_push_ssa_value(b, w[2], new);
      break;
   }
   case SpvOpTensorViewSetStrideNV: {
      init_vec(b, NIR_TENSOR_VIEW_STRIDE, w);
      break;
   }
   case SpvOpTensorViewSetClipNV: {
      init_clip_scalars(b, NIR_TENSOR_VIEW_CLIP_ROW_OFFSET, w);
      break;
   }
   default:
      break;
   }
}
