/*
 * Copyright 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "glsl_types.h"
#include "nir.h"
#include "vtn_private.h"

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
   const uint32_t rows = vtn_constant_uint(b, w[4]);
   const uint32_t cols = vtn_constant_uint(b, w[5]);

   vtn_assert(rows < 256);
   vtn_assert(cols < 256);

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

   case SpvOpCooperativeMatrixConvertNV: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_convert_nv");
      nir_cmat_convert(&b->nb, &dst->def, &src->def);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixTransposeNV: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_transpose_nv");
      nir_cmat_transpose(&b->nb, &dst->def, &src->def);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixReduceNV: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

      struct vtn_function *reduce_fn = vtn_value(b, w[5], vtn_value_type_function)->func;

      reduce_fn->referenced = true;
      reduce_fn->nir_func->cmat_call = true;
      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_reduce_nv");
      nir_cmat_call_instr *call = nir_cmat_call_instr_create(b->nb.shader, nir_cmat_call_op_reduce, reduce_fn->nir_func);
      call->params[0] = nir_src_for_ssa(&dst->def);
      call->params[1] = nir_src_for_ssa(&src->def);
      call->const_index[0] = w[4];
      nir_builder_instr_insert(&b->nb, &call->instr);
      vtn_push_var_ssa(b, w[2], dst->var);
      break;
   }

   case SpvOpCooperativeMatrixPerElementOpNV: {
      struct vtn_type *dst_type = vtn_get_type(b, w[1]);
      nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

      struct vtn_function *per_element_fn = vtn_value(b, w[4], vtn_value_type_function)->func;

      per_element_fn->referenced = true;
      per_element_fn->nir_func->cmat_call = true;
      nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_per_element_nv");

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

         nir_deref_instr *dst = vtn_create_cmat_temporary(b, dst_type->type, "cmat_convert");
         nir_cmat_convert(&b->nb, &dst->def, &src->def, .saturate = saturate, .cmat_signed_mask = signed_mask);
         vtn_push_var_ssa(b, w[2], dst->var);

         break;
      }

      case SpvOpFNegate:
      case SpvOpSNegate: {
         struct vtn_type *dst_type = vtn_get_type(b, w[1]);
         nir_deref_instr *src = vtn_get_cmat_deref(b, w[3]);

         bool ignored = false;
         nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &ignored, &ignored,
                                                     glsl_get_cmat_element(src->type),
                                                     glsl_get_cmat_element(dst_type->type));

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
         bool ignored = false;

         struct vtn_type *dst_type = vtn_get_type(b, w[1]);
         nir_deref_instr *mat_a = vtn_get_cmat_deref(b, w[3]);
         nir_deref_instr *mat_b = vtn_get_cmat_deref(b, w[4]);

         nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &ignored, &ignored,
                                                     glsl_get_cmat_element(mat_a->type),
                                                     glsl_get_cmat_element(dst_type->type));

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
