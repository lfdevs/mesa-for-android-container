/*
 * Copyright © 2025 Red Hat
 *
 * SPDX-License-Identifier: MIT
 */
#include "lvp_nir.h"

extern unsigned lp_native_vector_width;

#define MAX_CMAT_LEN 16
#define CMAT_LEN (lp_native_vector_width / 32)

/* This pass lowers cooperative matrix.
 *
 * for lavapipe we advertise 8x8 matrix.
 * This means we can store vec8[8] and get the backend to do the right thing.
 */
static unsigned
get_cmat_size(struct glsl_cmat_description matrix_desc)
{
   return matrix_desc.cols * matrix_desc.rows;
}

static unsigned
get_cmat_length(struct glsl_cmat_description matrix_desc)
{
   return get_cmat_size(matrix_desc) / CMAT_LEN;
}

static const struct glsl_type *
remap_matrix_type(struct hash_table *mapping, const struct glsl_type *orig)
{
  struct hash_entry *entry = _mesa_hash_table_search(mapping, orig);

   if (entry)
      return entry->data;

   const struct glsl_type *new_type = orig;

   if (glsl_type_is_cmat(orig)) {
      struct glsl_cmat_description matrix_desc =
         *glsl_get_cmat_description(orig);

      new_type = glsl_vector_type(matrix_desc.element_type, get_cmat_length(matrix_desc));
   } else if (glsl_type_is_array(orig)) {
      const struct glsl_type *elem_type = glsl_get_array_element(orig);
      const struct glsl_type *new_elem_type =
         remap_matrix_type(mapping, elem_type);

      if (elem_type != new_elem_type) {
         new_type = glsl_array_type(new_elem_type, glsl_get_length(orig),
                                    glsl_get_explicit_stride(orig));
      }
   }
   _mesa_hash_table_insert(mapping, orig, (void *)new_type);
   return new_type;
}

static nir_def *
load_cmat_deref(nir_builder *b, nir_deref_instr *src)
{
   struct glsl_cmat_description matrix_desc =
      *glsl_get_cmat_description(src->type);

   return nir_build_load_deref(
      b, get_cmat_length(matrix_desc),
      glsl_base_type_bit_size(matrix_desc.element_type), &src->def, 0);
}

static ALWAYS_INLINE nir_def *
load_cmat_src(nir_builder *b, nir_src src)
{
   return load_cmat_deref(b, nir_src_as_deref(src));
}

static ALWAYS_INLINE struct glsl_cmat_description
cmat_src_desc(nir_src src)
{
   nir_deref_instr *deref = nir_src_as_deref(src);
   return *glsl_get_cmat_description(deref->type);
}

static void
store_cmat_deref(nir_builder *b, nir_deref_instr *dst, nir_def *val)
{
   ASSERTED struct glsl_cmat_description matrix_desc =
      *glsl_get_cmat_description(dst->type);

   assert(val->bit_size == glsl_base_type_bit_size(matrix_desc.element_type));
   assert(val->num_components == get_cmat_length(matrix_desc));

   nir_store_deref(b, dst, val, ~0);
}

static ALWAYS_INLINE void
store_cmat_src(nir_builder *b, nir_src dst_src, nir_def *val)
{
   store_cmat_deref(b, nir_src_as_deref(dst_src), val);
}

static bool
lower_cmat_copy(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_build_copy_deref(b, intr->src[0].ssa, intr->src[1].ssa);
   nir_instr_remove(&intr->instr);
   return true;
}

static nir_def *
convert_use(nir_builder *b, nir_def *src, enum glsl_cmat_use src_use,
            enum glsl_cmat_use dst_use)
{
   nir_def *comps[NIR_MAX_VEC_COMPONENTS] = {0};
   nir_def *out_comps[NIR_MAX_VEC_COMPONENTS] = {0};
   unsigned num_comps = src->num_components;
   for (unsigned i = 0; i < num_comps; i++) {
      comps[i] = nir_channel(b, src, i);
      out_comps[i] = nir_imm_zero(b, 1, comps[i]->bit_size);
   }

   nir_def *lane_id = nir_load_subgroup_invocation(b);

   /* construct the outer row */
   for (unsigned i = 0; i < num_comps; i++) {

      for (unsigned j = 0; j < CMAT_LEN; j++) {
         nir_def *else_val = out_comps[i];
         nir_def *active_lane = nir_ieq(b, lane_id, nir_imm_int(b, j));

         out_comps[i] = nir_read_invocation(b, comps[j], nir_imm_int(b, i));

         out_comps[i] = nir_bcsel(b, active_lane, out_comps[i], else_val);
      }
   }
   return nir_vec(b, out_comps, num_comps);
}

static nir_def *
convert_base_type(nir_builder *b, nir_def *src, enum glsl_base_type src_type, enum glsl_base_type dst_type)
{
   if (dst_type == src_type)
      return src;

   nir_op op = nir_type_conversion_op(nir_get_nir_type_for_glsl_base_type(src_type),
                                      nir_get_nir_type_for_glsl_base_type(dst_type), nir_rounding_mode_undef);

   return nir_build_alu1(b, op, src);
}

static bool
lower_cmat_convert(nir_builder *b,
                   nir_intrinsic_instr *intr)
{
   const bool transpose = intr->intrinsic == nir_intrinsic_cmat_transpose;
   struct glsl_cmat_description dst_desc = cmat_src_desc(intr->src[0]);
   struct glsl_cmat_description src_desc = cmat_src_desc(intr->src[1]);
   b->fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr);

   enum glsl_base_type dst_element_type = dst_desc.element_type;
   enum glsl_base_type src_element_type = src_desc.element_type;

   enum glsl_cmat_use dst_use = dst_desc.use;
   enum glsl_cmat_use src_use = src_desc.use;

   nir_def *cmat = load_cmat_src(b, intr->src[1]);

   if (dst_use == GLSL_CMAT_USE_ACCUMULATOR)
      dst_use = GLSL_CMAT_USE_A;
   if (src_use == GLSL_CMAT_USE_ACCUMULATOR)
      src_use = GLSL_CMAT_USE_A;

   if (transpose) {
      if ((src_use == GLSL_CMAT_USE_A && dst_use == GLSL_CMAT_USE_B) ||
          (src_use == GLSL_CMAT_USE_B && dst_use == GLSL_CMAT_USE_A))
         src_use = dst_use;
      else if ((src_use == GLSL_CMAT_USE_A && dst_use == GLSL_CMAT_USE_A) ||
               (src_use == GLSL_CMAT_USE_B && dst_use == GLSL_CMAT_USE_B))
         src_use = dst_use == GLSL_CMAT_USE_A ? GLSL_CMAT_USE_B : GLSL_CMAT_USE_A;
   }

   nir_def *ret = cmat;
   if (dst_use != src_use) {
      ret = convert_use(b, cmat, src_use, dst_use);
   }
   ret = convert_base_type(b, ret, src_element_type, dst_element_type);
   store_cmat_src(b, intr->src[0], ret);
   nir_instr_remove(&intr->instr);
   b->fp_math_ctrl = nir_fp_fast_math;
   return true;
}

static bool
lower_cmat_load_store(nir_builder *b,
                      struct hash_table *type_mapping,
                      nir_intrinsic_instr *intr)
{
   const bool is_load = intr->intrinsic == nir_intrinsic_cmat_load;
   const struct glsl_cmat_description desc = cmat_src_desc(intr->src[!is_load]);
   enum glsl_matrix_layout layout = nir_intrinsic_matrix_layout(intr);
   nir_deref_instr *cmat_deref = nir_src_as_deref(intr->src[!is_load]);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[is_load]);
   nir_def *stride = intr->src[2].ssa;

   nir_def *lane_id = nir_load_subgroup_invocation(b);
   unsigned type_size_B = glsl_base_type_bit_size(desc.element_type) / 8;
   const uint32_t ptr_stride = glsl_get_bit_size(deref->type) / 8 * glsl_get_vector_elements(deref->type);
   deref = nir_build_deref_cast(b, &deref->def, deref->modes, deref->type, ptr_stride);
   const struct glsl_type *cmat_type = remap_matrix_type(type_mapping, cmat_deref->type);
   cmat_deref = nir_build_deref_cast(b, &cmat_deref->def, cmat_deref->modes,
                                     cmat_type, 0);

   /* store B matrix transposed */
   if (desc.use == GLSL_CMAT_USE_B)
      layout =
         layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR ? GLSL_MATRIX_LAYOUT_ROW_MAJOR : GLSL_MATRIX_LAYOUT_COLUMN_MAJOR;

   unsigned idx_bits = deref->def.bit_size;
   nir_def *vars[MAX_CMAT_LEN];

   if (!is_load) {
      nir_def *src = load_cmat_src(b, intr->src[!is_load]);
      for (unsigned i = 0; i < CMAT_LEN; i++) {
         vars[i] = nir_channel(b, src, i);
      }
   }

   for (unsigned i = 0; i < CMAT_LEN; i++) {
      nir_def *col_offset = lane_id;
      nir_def *row_offset = nir_imm_int(b, i);

      if (layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR) {
         SWAP(col_offset, row_offset);
      }

      col_offset = nir_imul(b, col_offset, stride);
      col_offset = nir_u2uN(b, col_offset, idx_bits);
      row_offset = nir_u2uN(b, row_offset, idx_bits);

      nir_deref_instr *iter_deref = nir_build_deref_ptr_as_array(b, deref, col_offset);

      iter_deref = nir_build_deref_cast(b, &iter_deref->def,
                                        deref->modes,
                                        glsl_scalar_type(desc.element_type),
                                        type_size_B);
      iter_deref = nir_build_deref_ptr_as_array(b, iter_deref, row_offset);

      if (is_load) {
         vars[i] = nir_load_deref(b, iter_deref);
      } else {
         nir_store_deref(b, iter_deref, vars[i], ~0);
      }
   }

   if (is_load) {
      nir_def *mat = nir_vec(b, vars, CMAT_LEN);
      nir_store_deref(b, cmat_deref, mat, nir_component_mask(mat->num_components));
   }
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_cmat_construct(nir_builder *b,
                     nir_intrinsic_instr *intr)
{
   nir_deref_instr *dst_deref = nir_src_as_deref(intr->src[0]);
   struct glsl_cmat_description desc = *glsl_get_cmat_description(dst_deref->type);
   nir_def *elem = intr->src[1].ssa;

   nir_def *r = nir_replicate(b, elem, get_cmat_length(desc));

   nir_store_deref(b, dst_deref, r, nir_component_mask(r->num_components));
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_cmat_extract(nir_builder *b,
                     nir_intrinsic_instr *intr)
{
   nir_def *mat = load_cmat_src(b, intr->src[0]);
   nir_def *index = intr->src[1].ssa;
   nir_def *elem = nir_vector_extract(b, mat, index);
   nir_def_replace(&intr->def, elem);
   return true;
}

static bool
lower_cmat_insert(nir_builder *b,
                  nir_intrinsic_instr *intr)
{
   nir_def *elem = intr->src[1].ssa;
   nir_def *mat = load_cmat_src(b, intr->src[2]);
   nir_def *index = intr->src[3].ssa;

   nir_def *r = nir_vector_insert(b, mat, elem, index);
   store_cmat_src(b, intr->src[0], r);

   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_cmat_binary_op(nir_builder *b,
                     nir_intrinsic_instr *intr)
{
   b->fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr);
   nir_def *src_a = load_cmat_src(b, intr->src[1]);
   nir_def *src_b = load_cmat_src(b, intr->src[2]);
   nir_op op = nir_intrinsic_alu_op(intr);

   nir_def *ret = nir_build_alu2(b, op, src_a, src_b);
   store_cmat_src(b, intr->src[0], ret);

   nir_instr_remove(&intr->instr);
   b->fp_math_ctrl = nir_fp_fast_math;
   return true;
}

static bool
lower_cmat_unary_op(nir_builder *b,
                     nir_intrinsic_instr *intr)
{
   b->fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr);
   nir_def *src = load_cmat_src(b, intr->src[1]);
   nir_op op = nir_intrinsic_alu_op(intr);

   nir_def *ret = nir_build_alu1(b, op, src);
   store_cmat_src(b, intr->src[0], ret);

   nir_instr_remove(&intr->instr);
   b->fp_math_ctrl = nir_fp_fast_math;
   return true;
}

static bool
lower_cmat_scalar_op(nir_builder *b,
                     nir_intrinsic_instr *intr)
{
   b->fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr);
   nir_def *src_a = load_cmat_src(b, intr->src[1]);
   nir_op op = nir_intrinsic_alu_op(intr);

   nir_def *ret = nir_build_alu2(b, op, src_a, intr->src[2].ssa);
   store_cmat_src(b, intr->src[0], ret);

   nir_instr_remove(&intr->instr);
   b->fp_math_ctrl = nir_fp_fast_math;
   return true;
}

static bool
lower_cmat_length(nir_builder *b,
                  nir_intrinsic_instr *intr)
{
   nir_def_replace(&intr->def, nir_imm_int(b, CMAT_LEN));
   return true;
}

static bool
lower_cmat_muladd(nir_builder *b,
                  nir_intrinsic_instr *intr)
{
   const struct glsl_cmat_description a_desc = cmat_src_desc(intr->src[1]);
   const struct glsl_cmat_description b_desc = cmat_src_desc(intr->src[2]);
   const struct glsl_cmat_description c_desc = cmat_src_desc(intr->src[3]);
   nir_def *cmat_a = load_cmat_src(b, intr->src[1]);
   nir_def *cmat_b = load_cmat_src(b, intr->src[2]);
   nir_def *cmat_c = load_cmat_src(b, intr->src[3]);

   unsigned a_length = get_cmat_length(a_desc);
   unsigned b_length = get_cmat_length(b_desc);
   unsigned c_length = get_cmat_length(c_desc);
   nir_def *a_comps[NIR_MAX_VEC_COMPONENTS];
   nir_def *b_comps[NIR_MAX_VEC_COMPONENTS];
   nir_def *c_comps[NIR_MAX_VEC_COMPONENTS];
   nir_def *d_comps[NIR_MAX_VEC_COMPONENTS];
   const nir_cmat_signed cmat_signed_mask = nir_intrinsic_cmat_signed_mask(intr);

   enum glsl_base_type c_element_type =
      glsl_apply_signedness_to_base_type(c_desc.element_type, cmat_signed_mask & NIR_CMAT_C_SIGNED);

   for (unsigned i = 0; i < a_length; i++)
      a_comps[i] = nir_channel(b, cmat_a, i);

   for (unsigned i = 0; i < b_length; i++)
      b_comps[i] = nir_channel(b, cmat_b, i);

   for (unsigned i = 0; i < c_length; i++)
      c_comps[i] = nir_channel(b, cmat_c, i);

   nir_def *lane_id = nir_load_subgroup_invocation(b);
   int accum_bit_size = glsl_base_type_bit_size(c_desc.element_type);
   for (unsigned i = 0; i < CMAT_LEN; i++) {
      nir_def *ref = nir_imm_zero(b, 1, glsl_base_type_bit_size(c_desc.element_type));
      for (unsigned j = 0; j < CMAT_LEN; j++) {
         nir_def *outer_else_val = ref;
         ref = nir_imm_zero(b, 1, glsl_base_type_bit_size(c_desc.element_type));

         nir_def *a_i = a_comps[i];
         nir_def *b_j = b_comps[j]; /* B is stored transposed */
         nir_def *val;
         if (glsl_base_type_is_integer(c_desc.element_type)) {
            if (c_element_type == GLSL_TYPE_INT)
               a_i = nir_i2iN(b, a_i, accum_bit_size);
            else
               a_i = nir_u2uN(b, a_i, accum_bit_size);
            if (c_element_type == GLSL_TYPE_INT)
               b_j = nir_i2iN(b, b_j, accum_bit_size);
            else
               b_j = nir_u2uN(b, b_j, accum_bit_size);

            val = nir_imul(b, a_i, b_j);
            ref = nir_iadd(b, ref, val);
         } else {
            a_i = nir_f2fN(b, a_i, accum_bit_size);
            b_j = nir_f2fN(b, b_j, accum_bit_size);
            val = nir_fmul(b, a_i, b_j);
            ref = nir_fadd(b, ref, val);
         }

         if (glsl_base_type_is_integer(c_desc.element_type)) {
            ref = nir_reduce(b, ref, .reduction_op = nir_op_iadd);
         } else {
            ref = nir_reduce(b, ref, .reduction_op = nir_op_fadd);
         }

         nir_def *lane = nir_ieq_imm(b, lane_id, j);
         ref = nir_bcsel(b, lane, ref, outer_else_val);
      }

      if (glsl_base_type_is_integer(c_desc.element_type)) {
         ref = nir_iadd(b, ref, c_comps[i]);
      } else {
         ref = nir_fadd(b, ref, c_comps[i]);
      }
      d_comps[i] = ref;
   }
   nir_def *ret = nir_vec(b, d_comps, CMAT_LEN);
   store_cmat_src(b, intr->src[0], ret);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_cmat_bitcast(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *src1 = load_cmat_src(b, intr->src[1]);
   nir_store_deref(b, nir_src_as_deref(intr->src[0]), src1, nir_component_mask(src1->num_components));
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_cmat_get_coordinate(nir_builder *b,
                          nir_intrinsic_instr *intr)
{
   struct glsl_cmat_description desc = nir_intrinsic_cmat_desc(intr);

   nir_def *lane_id = nir_load_subgroup_invocation(b);
   nir_def *comps[2];

   comps[0] = intr->src[0].ssa;
   comps[1] = lane_id;

   if (desc.use == GLSL_CMAT_USE_B) {
      SWAP(comps[0], comps[1]);
   }
   nir_def *val = nir_vec(b, comps, 2);
   nir_def_replace(&intr->def, val);
   return true;
}

static bool
lower_cmat_reduce_finish_call(nir_builder *b, nir_cmat_call_instr *call)
{
   nir_deref_instr *dst_deref = nir_src_as_deref(call->params[0]);
   nir_deref_instr *src0_deref = nir_src_as_deref(call->params[1]);
   struct glsl_cmat_description src_desc = *glsl_get_cmat_description(src0_deref->type);
   nir_function *fnptr = call->callee;
   nir_cmat_reduce reduce = nir_cmat_call_reduce_flags(call);
   nir_def *src0 = load_cmat_src(b, call->params[1]);
   nir_def *src1 = load_cmat_src(b, call->params[2]);

   assert(src_desc.use == GLSL_CMAT_USE_ACCUMULATOR);

   nir_def *comps[NIR_MAX_VEC_COMPONENTS] = {0};

   if (reduce & NIR_CMAT_REDUCE_COLUMN) {
      nir_variable *col_tmp = nir_local_variable_create(b->impl, glsl_get_bare_type(fnptr->params[0].type), "col_tmp");
      /* All of the rows contains the same data, so just reduce both first rows. */
      nir_def *row_accum0 = nir_channel(b, src0, 0);
      nir_def *row_accum1 = nir_channel(b, src1, 0);

      nir_deref_instr *col_tmp_deref = nir_build_deref_var(b, col_tmp);

      nir_call(b, fnptr, &col_tmp_deref->def, row_accum0, row_accum1);

      nir_def *first_col = nir_load_deref(b, col_tmp_deref);

      for (unsigned i = 0; i < CMAT_LEN; i++)
         comps[i] = first_col;
   } else if (reduce & NIR_CMAT_REDUCE_ROW) {
      for (unsigned i = 0; i < CMAT_LEN; ++i) {
         nir_def *row0_accum = nir_channel(b, src0, i);
         nir_def *row1_accum = nir_channel(b, src1, i);

         nir_variable *row_tmp = nir_local_variable_create(b->impl, glsl_get_bare_type(fnptr->params[0].type), "row_tmp");
         nir_deref_instr *row_tmp_deref = nir_build_deref_var(b, row_tmp);

         nir_call(b, fnptr, &row_tmp_deref->def, row0_accum, row1_accum);

         nir_def *row = nir_load_deref(b, row_tmp_deref);
         comps[i] = row;
      }
   }
   nir_def *mat = nir_vec(b, comps, CMAT_LEN);
   nir_store_deref(b, dst_deref, mat, nir_component_mask(mat->num_components));
   nir_instr_remove(&call->instr);
   return true;
}

static bool
lower_cmat_reduce_call(nir_builder *b, nir_cmat_call_instr *call)
{
   nir_deref_instr *dst_deref = nir_src_as_deref(call->params[0]);
   nir_deref_instr *src_deref = nir_src_as_deref(call->params[1]);
   struct glsl_cmat_description src_desc = *glsl_get_cmat_description(src_deref->type);
   nir_cmat_reduce reduce = nir_cmat_call_reduce_flags(call);
   nir_def *src = load_cmat_src(b, call->params[1]);
   nir_function *fnptr = call->callee;
   nir_def *lane_id = nir_load_subgroup_invocation(b);

   assert(src_desc.use == GLSL_CMAT_USE_ACCUMULATOR);

   nir_def *comps[NIR_MAX_VEC_COMPONENTS] = {0};
   for (unsigned i = 0; i < CMAT_LEN; ++i) {
      comps[i] = nir_channel(b, src, i);
   }

   if (reduce & NIR_CMAT_REDUCE_COLUMN) {
      nir_variable *col_tmp = nir_local_variable_create(b->impl, glsl_get_bare_type(fnptr->params[0].type), "col_tmp");

      nir_deref_instr *col_tmp_deref = nir_build_deref_var(b, col_tmp);
      nir_store_deref(b, col_tmp_deref, comps[0], 1);

      for (unsigned i = 1; i < CMAT_LEN; i++) {
         nir_def *col_accum_val = nir_load_deref(b, col_tmp_deref);
         nir_call(b, fnptr, &col_tmp_deref->def, col_accum_val, comps[i]);
      }

      for (unsigned i = 0; i < CMAT_LEN; i++)
         comps[i] = nir_load_deref(b, col_tmp_deref);
   }

   if (reduce & NIR_CMAT_REDUCE_ROW) {
      for (unsigned i = 0; i < CMAT_LEN; ++i) {
         nir_def *row_accum = comps[i];
         nir_variable *row_tmp = nir_local_variable_create(b->impl, glsl_get_bare_type(fnptr->params[0].type), "row_tmp");
         nir_deref_instr *row_tmp_deref = nir_build_deref_var(b, row_tmp);
         nir_store_deref(b, row_tmp_deref, row_accum, 1);

         for (unsigned j = 1; j < CMAT_LEN; j *= 2) {
            nir_def *prev_row_accum_val = nir_load_deref(b, row_tmp_deref);
            nir_def *this_row = nir_shuffle(b, prev_row_accum_val, nir_iadd(b, lane_id, nir_imm_int(b, j)));

            nir_call(b, fnptr, &row_tmp_deref->def, prev_row_accum_val, this_row);
         }
         row_tmp_deref = nir_build_deref_var(b, row_tmp);
         comps[i] = nir_load_deref(b, row_tmp_deref);
      }
   }

   /* this should be lowered earlier */
   assert(!(reduce & NIR_CMAT_REDUCE_2X2));
   nir_def *mat = nir_vec(b, comps, CMAT_LEN);
   nir_store_deref(b, dst_deref, mat, nir_component_mask(mat->num_components));
   nir_instr_remove(&call->instr);
   return true;
}

static bool
lower_cmat_reduce_2x2_call(nir_builder *b, nir_cmat_call_instr *call)
{
   nir_deref_instr *dst_deref = nir_src_as_deref(call->params[0]);
   nir_deref_instr *src_deref = nir_src_as_deref(call->params[1]);
   struct glsl_cmat_description src_desc = *glsl_get_cmat_description(src_deref->type);
   nir_function *fnptr = call->callee;
   nir_def *lane_id = nir_load_subgroup_invocation(b);
   assert(src_desc.use == GLSL_CMAT_USE_ACCUMULATOR);

   nir_def *comps[NIR_MAX_VEC_COMPONENTS];

   nir_def *src_components[4][NIR_MAX_VEC_COMPONENTS];
   for (unsigned m = 0; m < 4; m++) {
      nir_def *src = load_cmat_src(b, call->params[m + 1]);
      for (unsigned i = 0; i < CMAT_LEN; i++) {
         src_components[m][i] = nir_channel(b, src, i);
      }
   }
   nir_variable *qd_tmp = nir_local_variable_create(b->impl, glsl_get_bare_type(fnptr->params[0].type), "qd_tmp");
   nir_deref_instr *qd_tmp_deref = nir_build_deref_var(b, qd_tmp);

   for (unsigned m = 0; m < 4; m++) {
      for (unsigned i = 0; i < CMAT_LEN / 2; i++) {
         nir_call(b, fnptr, &qd_tmp_deref->def, src_components[m][i * 2], src_components[m][i * 2 + 1]);
         src_components[m][i] = nir_load_deref(b, qd_tmp_deref);

         nir_def *other_col = nir_shuffle_down(b, src_components[m][i], nir_imm_int(b, 1));
         nir_call(b, fnptr, &qd_tmp_deref->def, src_components[m][i], other_col);
         src_components[m][i] = nir_load_deref(b, qd_tmp_deref);
      }
   }

   nir_def *even = nir_inverse_ballot_imm(b, 0x5555555555555555, 32);
   for (unsigned m = 0; m < 2; m++) {
      for (unsigned i = 0; i < CMAT_LEN / 2; i++) {
         nir_def *m0_comp = src_components[m * 2][i];
         nir_def *m1_comp = nir_shuffle_up(b, src_components[m * 2 + 1][i], nir_imm_int(b, 1));

         nir_def *combined = nir_bcsel(b, even, m0_comp, m1_comp);
         comps[m * (CMAT_LEN / 2) + i] = combined;
      }
   }

   nir_def *low_lane_id = nir_ilt_imm(b, lane_id, 4);
   nir_def *new_lane_id_lo = nir_imul_imm(b, lane_id, 2);
   nir_def *new_lane_id_hi = nir_iadd_imm(b, nir_imul_imm(b, nir_iadd_imm(b, lane_id, -4), 2), 1);
   nir_def *new_lane_id = nir_bcsel(b, low_lane_id, new_lane_id_lo, new_lane_id_hi);

   for (unsigned m = 0; m < CMAT_LEN; m++) {
      comps[m] = nir_shuffle(b, comps[m], new_lane_id);
   }
   nir_def *mat = nir_vec(b, comps, CMAT_LEN);
   nir_store_deref(b, dst_deref, mat, nir_component_mask(mat->num_components));
   nir_instr_remove(&call->instr);
   return true;
}

static bool
lower_cmat_per_element_op_call(nir_builder *b, nir_cmat_call_instr *call)
{
   nir_def *src = load_cmat_src(b, call->params[3]);
   nir_deref_instr *dst_deref = nir_src_as_deref(call->params[0]);
   nir_function *fnptr = call->callee;
   nir_def *lane_id = nir_load_subgroup_invocation(b);

   struct glsl_cmat_description desc = *glsl_get_cmat_description(dst_deref->type);

   nir_variable *elem_tmp = nir_local_variable_create(b->impl, glsl_get_cmat_element(dst_deref->type), "elemtmp");
   nir_deref_instr *elem_deref = nir_build_deref_var(b, elem_tmp);

   nir_def *comps[NIR_MAX_VEC_COMPONENTS];

   for (unsigned i = 0; i < CMAT_LEN; i++) {
      nir_def *src_elem = nir_channel(b, src, i);
      nir_call_instr *new_call = nir_call_instr_create(b->shader, fnptr);

      nir_def *row_val = nir_imm_int(b, i);
      nir_def *col_val = lane_id;

      if (desc.use == GLSL_CMAT_USE_B)
         SWAP(col_val, row_val);

      row_val = nir_iadd(b, call->params[1].ssa, row_val);
      col_val = nir_iadd(b, call->params[2].ssa, col_val);

      new_call->params[0] = nir_src_for_ssa(&elem_deref->def);
      new_call->params[1] = nir_src_for_ssa(row_val);
      new_call->params[2] = nir_src_for_ssa(col_val);
      new_call->params[3] = nir_src_for_ssa(src_elem);

      for (unsigned p = 4; p < call->num_params; p++) {
         nir_deref_instr *deref = nir_src_as_deref(call->params[p]);
         nir_def *def = call->params[p].ssa;
         if (deref) {
            if (glsl_type_is_cmat(deref->type)) {
               def = nir_build_load_deref(b, get_cmat_length(desc),
                                          glsl_base_type_bit_size(desc.element_type), def);
               def = nir_channel(b, def, i);
            }
         }
         new_call->params[p] = nir_src_for_ssa(def);
      }
      nir_builder_instr_insert(b, &new_call->instr);
      comps[i] = nir_build_load_deref(b, 1, glsl_base_type_bit_size(desc.element_type), &elem_deref->def, 0);
   }

   nir_def *mat = nir_vec(b, comps, CMAT_LEN);
   nir_store_deref(b, dst_deref, mat, nir_component_mask(src->num_components));

   nir_instr_remove(&call->instr);
   return true;
}

static bool
lower_cmat_tensor_load_store(nir_builder *b, nir_cmat_call_instr *call)
{
   bool is_load = call->op != nir_cmat_call_op_tensor_store;
   nir_deref_instr *cmat_deref = nir_src_as_deref(call->params[!is_load]);
   struct glsl_cmat_description desc = *glsl_get_cmat_description(cmat_deref->type);

   nir_deref_instr *deref = nir_src_as_deref(call->params[is_load]);

   nir_def *local_idx = nir_load_subgroup_invocation(b);
   nir_def *vars[16];
   nir_def *clips[16];
   if (is_load) {
      nir_def *clip_src = load_cmat_src(b, call->params[4]);
      for (unsigned i = 0; i < CMAT_LEN; ++i)
         clips[i] = nir_channel(b, clip_src, i);
   } else {
      nir_def *src = load_cmat_src(b, call->params[!is_load]);
      for (unsigned i = 0; i < CMAT_LEN; ++i)
         vars[i] = nir_channel(b, src, i);
   }

   struct nir_calc_tensor_info info = {0};
   nir_calc_tensor_derefs_init(b, &info, call);

   if (info.decode_fnptr) {
      if (nir_deref_mode_is_one_of(deref, nir_var_mem_ssbo)) {
         nir_def *def = nir_load_ssbo_address(b, 1, 64, nir_trim_vector(b, &deref->def, 2),
                                              nir_channel(b, &deref->def, 2));
         deref = nir_build_deref_cast(b, def, nir_var_mem_global, deref->type, glsl_base_type_bit_size(desc.element_type) / 8);
      }
   }
   for (unsigned i = 0; i < CMAT_LEN; ++i) {
      nir_deref_instr *iter_deref = deref;
      nir_def *col_offset = local_idx;
      nir_def *row_offset = nir_imm_int(b, i);

      if (desc.use == GLSL_CMAT_USE_B) {
         SWAP(row_offset, col_offset);
      }

      nir_def *idx = nir_calc_tensor_derefs(b, &info, row_offset, col_offset, &iter_deref);
      if (!info.decode_fnptr) {
         if (is_load) {
            if (info.clamp_value) {
               vars[i] = nir_bcsel(b, info.do_clamp,
                                   nir_u2uN(b, info.clamp_value, glsl_base_type_bit_size(desc.element_type)),
                                   nir_load_deref(b, iter_deref));
            } else {
               vars[i] = nir_load_deref(b, iter_deref);
            }
         } else {
            nir_if *store_if_inner = nir_push_if(b, nir_inot(b, info.do_clamp));
            nir_store_deref(b, iter_deref, vars[i], 1);
            nir_pop_if(b, store_if_inner);
         }
      } else {
         vars[i] = idx;
      }

      if (info.clipped_if) {
         nir_push_else(b, info.clipped_if);
         nir_pop_if(b, info.clipped_if);
         if (is_load)
            vars[i] = nir_if_phi(b, vars[i], clips[i]);
      }

   }

   if (is_load) {
      nir_def *mat = nir_vec(b, vars, CMAT_LEN);
      nir_store_deref(b, cmat_deref, mat, nir_component_mask(mat->num_components));
   }

   nir_instr_remove(&call->instr);
   return true;
}

static bool
lower_impl(nir_function_impl *impl,
           struct hash_table *type_mapping)
{
   bool progress = false;
   /* Remap all cmat temp var to array of scalars */
   nir_foreach_function_temp_variable(var, impl) {
      const struct glsl_type *new_type =
         remap_matrix_type(type_mapping, var->type);
      if (new_type != var->type) {
         var->type = new_type;
         progress = true;
      }
   }

   /* Iterate in reverse order so that lowering can still use the matrix types from the derefs before we change it. */
   nir_builder b = nir_builder_create(impl);
   nir_foreach_block_reverse_safe (block, impl) {
      nir_foreach_instr_reverse_safe (instr, block) {
         b.cursor = nir_before_instr(instr);

         switch (instr->type) {
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            switch (intr->intrinsic) {
            case nir_intrinsic_cmat_length:
               progress |= lower_cmat_length(&b, intr);
               break;
            case nir_intrinsic_cmat_construct:
               progress |= lower_cmat_construct(&b, intr);
               break;
            case nir_intrinsic_cmat_extract:
               progress |= lower_cmat_extract(&b, intr);
               break;
            case nir_intrinsic_cmat_insert:
               progress |= lower_cmat_insert(&b, intr);
               break;
            case nir_intrinsic_cmat_load:
            case nir_intrinsic_cmat_store:
               progress |= lower_cmat_load_store(&b, type_mapping, intr);
               break;
            case nir_intrinsic_cmat_binary_op:
               progress |= lower_cmat_binary_op(&b, intr);
               break;
            case nir_intrinsic_cmat_unary_op:
               progress |= lower_cmat_unary_op(&b, intr);
               break;
            case nir_intrinsic_cmat_scalar_op:
               progress |= lower_cmat_scalar_op(&b, intr);
               break;
            case nir_intrinsic_cmat_muladd:
               progress |= lower_cmat_muladd(&b, intr);
               break;
            case nir_intrinsic_cmat_copy:
               progress |= lower_cmat_copy(&b, intr);
               break;
            case nir_intrinsic_cmat_convert:
            case nir_intrinsic_cmat_transpose:
               progress |= lower_cmat_convert(&b, intr);
               break;
            case nir_intrinsic_cmat_bitcast:
               progress |= lower_cmat_bitcast(&b, intr);
               break;
            case nir_intrinsic_cmat_get_coordinate:
               progress |= lower_cmat_get_coordinate(&b, intr);
               break;
            default:
               break;
            }
            break;
         }
         case nir_instr_type_deref: {
            nir_deref_instr *deref = nir_instr_as_deref(instr);
            const struct glsl_type *new_type =
               remap_matrix_type(type_mapping, deref->type);

            if (new_type != deref->type) {
               deref->type = new_type;
               progress = true;
            }
            break;
         }
         case nir_instr_type_cmat_call: {
            nir_cmat_call_instr *call = nir_instr_as_cmat_call(instr);
            switch (call->op) {
            case nir_cmat_call_op_reduce:
               progress |= lower_cmat_reduce_call(&b, call);
               break;
            case nir_cmat_call_op_reduce_finish:
               progress |= lower_cmat_reduce_finish_call(&b, call);
               break;
            case nir_cmat_call_op_reduce_2x2:
               progress |= lower_cmat_reduce_2x2_call(&b, call);
               break;
            case nir_cmat_call_op_per_element_op:
               progress |= lower_cmat_per_element_op_call(&b, call);
               break;
            case nir_cmat_call_op_tensor_load:
            case nir_cmat_call_op_tensor_store:
               progress |= lower_cmat_tensor_load_store(&b, call);
               break;
            default:
               break;
            }
            break;
         }
         default:
            break;
         }
      }
   }
   return nir_progress(progress, impl, nir_metadata_none);
}

bool
lvp_nir_lower_cooperative_matrix(nir_shader *shader)
{
   bool progress = false;

   if (!shader->info.cs.has_cooperative_matrix)
      return false;

   struct hash_table *type_mapping = _mesa_pointer_hash_table_create(NULL);
   /* Remap all cmat shader temp var to array of vectors */
   nir_foreach_variable_with_modes(var, shader, nir_var_shader_temp) {
      const struct glsl_type *new_type =
         remap_matrix_type(type_mapping, var->type);

      if (new_type != var->type) {
         var->type = new_type;
         progress = true;
      }
   }

   progress |= lower_impl(nir_shader_get_entrypoint(shader), type_mapping);

   _mesa_hash_table_destroy(type_mapping, NULL);

   nir_foreach_function_impl(fnim, shader)
      nir_progress(progress, fnim, 0);
   return progress;
}
