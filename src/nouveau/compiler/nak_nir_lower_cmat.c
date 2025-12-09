/*
 * Copyright © 2023 Bas Nieuwenhuizen
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/macros.h"
#include "glsl_types.h"
#include "nak_private.h"
#include "nir_builder.h"

static enum nak_cmat_type
get_nak_cmat_type_for_muladd(struct glsl_cmat_description a_desc,
                             struct glsl_cmat_description b_desc,
                             struct glsl_cmat_description c_desc)
{
   unsigned m = a_desc.rows;
   unsigned k = b_desc.rows;
   unsigned n = c_desc.cols;

   bool a_is_int8 = a_desc.element_type == GLSL_TYPE_INT8 ||
                    a_desc.element_type == GLSL_TYPE_UINT8;
   bool b_is_int8 = b_desc.element_type == GLSL_TYPE_INT8 ||
                    b_desc.element_type == GLSL_TYPE_UINT8;
   bool c_is_int32 = c_desc.element_type == GLSL_TYPE_INT ||
                     c_desc.element_type == GLSL_TYPE_UINT;

   if (m ==  8 && a_is_int8 &&
       n ==  8 && b_is_int8 &&
       k == 16 && c_is_int32)
      return NAK_CMAT_TYPE_M8N8K16_INT;

   if (m == 16 && a_is_int8 &&
       n ==  8 && b_is_int8 &&
       k == 16 && c_is_int32)
      return NAK_CMAT_TYPE_M16N8K16_INT;

   if (m == 16 && a_is_int8 &&
       n ==  8 && b_is_int8 &&
       k == 32 && c_is_int32)
      return NAK_CMAT_TYPE_M16N8K32_INT;

   if (m == 16 && a_is_int8 &&
       n == 16 && b_is_int8 &&
       k == 32 && c_is_int32)
      return NAK_CMAT_TYPE_M16N16K32_INT_SW;

   if (m == 16 && a_desc.element_type == GLSL_TYPE_FLOAT16 &&
       n ==  8 && b_desc.element_type == GLSL_TYPE_FLOAT16 &&
       k ==  8 && glsl_base_type_is_float(c_desc.element_type))
      return NAK_CMAT_TYPE_M16N8K8_FLOAT;

   if (m == 16 && a_desc.element_type == GLSL_TYPE_FLOAT16 &&
       n ==  8 && b_desc.element_type == GLSL_TYPE_FLOAT16 &&
       k == 16 && glsl_base_type_is_float(c_desc.element_type))
       return NAK_CMAT_TYPE_M16N8K16_FLOAT;

   if (m == 16 && a_desc.element_type == GLSL_TYPE_FLOAT16 &&
       n == 16 && b_desc.element_type == GLSL_TYPE_FLOAT16 &&
       k == 16 && glsl_base_type_is_float(c_desc.element_type))
      return NAK_CMAT_TYPE_M16N16K16_FLOAT_SW;

   UNREACHABLE("Unable to determine matrix muladd layout!");
}

enum nak_matrix_type_layout {
   NAK_MAT_16x32_INT8,
   NAK_MAT_16X16,
};

static enum nak_matrix_type_layout
determine_matrix_type(struct glsl_cmat_description desc)
{
   bool is_int8 = desc.element_type == GLSL_TYPE_INT8 ||
                  desc.element_type == GLSL_TYPE_UINT8;
   bool is_int8_a = is_int8 && desc.use == GLSL_CMAT_USE_A;
   bool is_int8_b = is_int8 && desc.use == GLSL_CMAT_USE_B;
   ASSERTED bool is_int32 = desc.element_type == GLSL_TYPE_INT ||
                            desc.element_type == GLSL_TYPE_UINT;
   ASSERTED bool is_float16 = desc.element_type == GLSL_TYPE_FLOAT16;
   ASSERTED bool is_float32 = desc.element_type == GLSL_TYPE_FLOAT;
   ASSERTED bool use_accum = desc.use == GLSL_CMAT_USE_ACCUMULATOR;

   /* This format doesn't exist on any hardware we are aware of so far and is
    * part of lowering
    */
   if (desc.rows == 32 && desc.cols == 16 && is_int8_b)
      return NAK_MAT_16x32_INT8;

   /* Even though this condition might be correct, we assert on all the
    * combination we actually verified on hardware.
    */
   if (is_int8_a || is_int8_b) {
      assert(
         (desc.rows ==  8 && desc.cols == 16 && is_int8_a) ||
         (desc.rows == 16 && desc.cols ==  8 && is_int8_b) ||
         (desc.rows == 16 && desc.cols == 16 && is_int8_a) ||
         (desc.rows == 16 && desc.cols == 32 && is_int8_a) ||
         (desc.rows == 32 && desc.cols ==  8 && is_int8_b)
      );
      return NAK_MAT_16x32_INT8;
   } else {
      assert(
         (desc.rows ==  8 && desc.cols ==  8 && is_float16 && !use_accum) ||
         (desc.rows == 16 && desc.cols ==  8 && is_float16              ) ||
         (desc.rows == 16 && desc.cols ==  8 && is_float32              ) ||
         (desc.rows == 16 && desc.cols == 16 && is_float16              ) ||
         (desc.rows == 16 && desc.cols == 16 && is_float32              ) ||
         (desc.rows ==  8 && desc.cols ==  8 && is_int32                ) ||
         (desc.rows == 16 && desc.cols ==  8 && is_int32                ) ||
         (desc.rows == 16 && desc.cols == 16 && is_int32                )
      );
      return NAK_MAT_16X16;
   }
}

static unsigned
get_cmat_size(struct glsl_cmat_description matrix_desc)
{
   return matrix_desc.cols * matrix_desc.rows;
}

static unsigned
get_cmat_length(struct glsl_cmat_description matrix_desc)
{
   return get_cmat_size(matrix_desc) / NAK_SUBGROUP_SIZE;
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

      new_type = glsl_vector_type(matrix_desc.element_type,
                                  get_cmat_length(matrix_desc));
   } else if (glsl_type_is_array(orig)) {
      const struct glsl_type *elem_type = glsl_get_array_element(orig);
      const struct glsl_type *new_elem_type =
         remap_matrix_type(mapping, elem_type);

      if (elem_type != new_elem_type) {
         new_type = glsl_array_type(new_elem_type, glsl_get_length(orig),
                                    glsl_get_explicit_stride(orig));
      }
   } else if (glsl_type_is_struct(orig)) {
      unsigned i;
      for (i = 0; i < orig->length; i++) {
         const struct glsl_type *field_type = glsl_get_struct_field(orig, i);
         const struct glsl_type *new_field_type =
            remap_matrix_type(mapping, field_type);

         if (field_type != new_field_type) {
            break;
         }
      }

      /* If we found a cmat, remap the structure type */
      if (i < orig->length) {
         struct glsl_struct_field *fields =
            malloc(sizeof(struct glsl_struct_field) * orig->length);

         /* Copy everything that didn't change */
         memcpy(fields, orig->fields.structure,
                sizeof(struct glsl_struct_field) * i);

         /* Remap the rest */
         for (; i < orig->length; i++) {
            fields[i] = *glsl_get_struct_field_data(orig, i);
            fields[i].type = remap_matrix_type(mapping, fields[i].type);
         }

         new_type =
            glsl_struct_type(fields, orig->length, glsl_get_type_name(orig),
                             glsl_struct_type_is_packed(orig));

         free(fields);
      }
   }

   _mesa_hash_table_insert(mapping, orig, (void *)new_type);
   return new_type;
}

static bool
uses_movm_for_bit_size(unsigned bit_size)
{
   return bit_size == 16;
}

/**
 * Returns true when before stores or after loads the loaded matrix has to be transposed
 */
static bool
transpose_on_load_store(struct glsl_cmat_description desc,
                        enum glsl_matrix_layout layout)
{
   return
      uses_movm_for_bit_size(glsl_base_type_get_bit_size(desc.element_type)) &&
      ((desc.use == GLSL_CMAT_USE_B && layout != GLSL_MATRIX_LAYOUT_COLUMN_MAJOR) ||
       (desc.use != GLSL_CMAT_USE_B && layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR));
}

static nir_def *
transpose_matrix(nir_builder *b, nir_def *value)
{
   unsigned vec_size = value->num_components;
   unsigned bit_size = value->bit_size;

   switch (bit_size) {
   case 32: {
      assert(vec_size == 2);

      nir_def *raw = nir_unpack_64_4x16(b, nir_pack_64_2x32(b, value));
      nir_def *lo = nir_vec2(b,
         nir_channel(b, raw, 0),
         nir_channel(b, raw, 2)
      );
      nir_def *hi = nir_vec2(b,
         nir_channel(b, raw, 1),
         nir_channel(b, raw, 3)
      );

      lo = nir_cmat_mov_transpose_nv(b, lo);
      hi = nir_cmat_mov_transpose_nv(b, hi);

      value = nir_vec2(b,
         nir_pack_32_2x16(b, nir_vec2(b, nir_channel(b, lo, 0), nir_channel(b, hi, 0))),
         nir_pack_32_2x16(b, nir_vec2(b, nir_channel(b, lo, 1), nir_channel(b, hi, 1)))
      );
      break;
   }
   case 16:
      assert(vec_size == 2);
      value = nir_cmat_mov_transpose_nv(b, value);
      break;
   default:
      assert(!"unsupported bit_size for transpose");
      break;
   }

   return value;
}

/**
 * Computes the index in a linear matrix buffer a thread needs to load from in
 * order to execute an MMA on the Matrix.
 *
 * This is a generalized formula based on the Matrix layout descriptions from
 * the CUDA PTX instruction set documentation:
 * https://docs.nvidia.com/cuda/archive/12.8.1/parallel-thread-execution/index.html#matrix-multiply-accumulate-operation-using-mma-instruction
 *
 * \param group_size Size of the value groups the layout tiles around.
 */
static void
compute_mat(struct nir_builder *b, nir_def *lane_id,
            unsigned idx, nir_def **col, nir_def **row,
            bool alternate_tiling_order,
            unsigned group_size)
{
   assert(idx < 4 * group_size);

   nir_def *quad_id = nir_ushr_imm(b, lane_id, 2);
   nir_def *thread_id_in_quad = nir_iand_imm(b, lane_id, 0x3);

   unsigned row_bound = (alternate_tiling_order ? 2 : 1) * group_size;
   unsigned col_bound = (alternate_tiling_order ? 1 : 2) * group_size;

   *row = quad_id;
   if (idx & row_bound)
      *row = nir_iadd_imm(b, *row, 8);

   *col = nir_iadd_imm(b, nir_imul_imm(b, thread_id_in_quad, group_size),
                          idx & (group_size - 1));
   if (idx & col_bound)
      *col = nir_iadd_imm(b, *col, group_size * 4);
}

static void
compute_mat_16x32_int8(struct nir_builder *b, nir_def *lane_id,
                       unsigned idx, nir_def **col, nir_def **row,
                       bool alternate_tiling_order)
{
   compute_mat(b, lane_id, idx, col, row, alternate_tiling_order, 4);
}

static void
compute_mat_16x16(struct nir_builder *b, nir_def *lane_id,
                       unsigned idx, nir_def **col, nir_def **row,
                       bool alternate_tiling_order)
{
   compute_mat(b, lane_id, idx, col, row, alternate_tiling_order, 2);
}

static void
compute_matrix_offsets(struct nir_builder *b, struct glsl_cmat_description desc,
                       enum glsl_matrix_layout layout, nir_def *lane_id,
                       unsigned idx, nir_def **col_offset, nir_def **row_offset)
{
   enum nak_matrix_type_layout cmat_type = determine_matrix_type(desc);
   unsigned bit_size = glsl_base_type_bit_size(desc.element_type);
   bool uses_movm = uses_movm_for_bit_size(bit_size);
   bool alternate_tiling_order =
      (uses_movm && layout != GLSL_MATRIX_LAYOUT_ROW_MAJOR) ||
      (!uses_movm && desc.use == GLSL_CMAT_USE_B);

   switch (cmat_type) {
   case NAK_MAT_16x32_INT8:
      compute_mat_16x32_int8(b, lane_id, idx, col_offset, row_offset, alternate_tiling_order);
      break;

   case NAK_MAT_16X16:
      compute_mat_16x16(b, lane_id, idx, col_offset, row_offset, alternate_tiling_order);
      break;
   }

   /* The layout calculation code relies on col and row being swapped for B
    * row-major and non B col-major matrices.
    */
   if (!uses_movm) {
      if ((desc.use == GLSL_CMAT_USE_B && layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR) ||
          (desc.use != GLSL_CMAT_USE_B && layout != GLSL_MATRIX_LAYOUT_ROW_MAJOR)) {
         nir_def *tmp = *col_offset;
         *col_offset = *row_offset;
         *row_offset = tmp;
      }
   }
}

/* Returns the hw native Matrix muladd operation */
static enum nak_cmat_type
get_hw_nak_cmat_type(enum nak_cmat_type cmat_type, uint8_t sm)
{
   switch (cmat_type) {
   case NAK_CMAT_TYPE_M8N8K16_INT:
      return NAK_CMAT_TYPE_M8N8K16_INT;
   case NAK_CMAT_TYPE_M16N8K16_INT:
      return sm >= 80 ? NAK_CMAT_TYPE_M16N8K16_INT
                      : NAK_CMAT_TYPE_M8N8K16_INT; /* no lowering code yet */
   case NAK_CMAT_TYPE_M16N8K32_INT:
   case NAK_CMAT_TYPE_M16N16K32_INT_SW:
      /* On Turing we only have 8x8x16 */
      return sm >= 80 ? NAK_CMAT_TYPE_M16N8K32_INT
                      : NAK_CMAT_TYPE_M8N8K16_INT;
   case NAK_CMAT_TYPE_M16N8K8_FLOAT:
      return NAK_CMAT_TYPE_M16N8K8_FLOAT;
   case NAK_CMAT_TYPE_M16N8K16_FLOAT:
   case NAK_CMAT_TYPE_M16N16K16_FLOAT_SW:
      return NAK_CMAT_TYPE_M16N8K16_FLOAT;
   default:
      UNREACHABLE("Unknown Matrix muladd type.");
   }
}

static nir_def *
lower_cmat_muladd(nir_builder *b, nir_intrinsic_instr *intr, nir_def *cmat_a,
                  nir_def *cmat_b, nir_def *cmat_c,
                  struct glsl_cmat_description a_desc,
                  struct glsl_cmat_description b_desc,
                  struct glsl_cmat_description c_desc,
                  struct glsl_cmat_description d_desc, uint8_t sm)
{
   unsigned dst_length = get_cmat_length(d_desc);

   enum nak_cmat_type cmat_type =
      get_nak_cmat_type_for_muladd(a_desc, b_desc, c_desc);
   enum nak_cmat_type hw_cmat_type = get_hw_nak_cmat_type(cmat_type, sm);

   nir_cmat_signed cmat_signed = nir_intrinsic_cmat_signed_mask(intr);
   bool a_signed = cmat_signed & NIR_CMAT_A_SIGNED;
   bool b_signed = cmat_signed & NIR_CMAT_B_SIGNED;

   const struct nak_nir_cmat_mul_add_flags flags = {
      .cmat_type = hw_cmat_type,
      .a_type = glsl_apply_signedness_to_base_type(a_desc.element_type, a_signed),
      .b_type = glsl_apply_signedness_to_base_type(b_desc.element_type, b_signed),
      .sat = nir_intrinsic_saturate(intr),
   };

   /* Simple case: we can execute the MMA in one instruction */
   if (cmat_type == hw_cmat_type) {
      return nir_cmat_muladd_nv(b, dst_length, cmat_a, cmat_b, cmat_c,
                                .flags = NAK_AS_U32(flags));
   }

   unsigned a_length = get_cmat_length(a_desc);
   unsigned b_length = get_cmat_length(b_desc);
   unsigned c_length = get_cmat_length(c_desc);

   nir_def *a_comps[NIR_MAX_VEC_COMPONENTS];
   nir_def *b_comps[NIR_MAX_VEC_COMPONENTS];
   nir_def *c_comps[NIR_MAX_VEC_COMPONENTS];
   nir_def *d_comps[NIR_MAX_VEC_COMPONENTS];

   for (unsigned i = 0; i < a_length; i++)
      a_comps[i] = nir_channel(b, cmat_a, i);

   for (unsigned i = 0; i < b_length; i++)
      b_comps[i] = nir_channel(b, cmat_b, i);

   for (unsigned i = 0; i < c_length; i++)
      c_comps[i] = nir_channel(b, cmat_c, i);

   if (hw_cmat_type == NAK_CMAT_TYPE_M8N8K16_INT &&
         (cmat_type == NAK_CMAT_TYPE_M16N8K32_INT ||
          cmat_type == NAK_CMAT_TYPE_M16N16K32_INT_SW)) {
      const unsigned a_hw_length = 4;
      const unsigned b_hw_length = 4;
      const unsigned c_hw_length = 2;
      const unsigned d_hw_length = 2;

      for (unsigned i = 0; i < dst_length / d_hw_length; i++) {
         unsigned cmat_a_lo_offset = (i % 2) * a_hw_length;
         unsigned cmat_a_hi_offset = cmat_a_lo_offset + 8;

         unsigned cmat_b_lo_offset = (i / 2) * b_hw_length;
         if (cmat_type == NAK_CMAT_TYPE_M16N16K32_INT_SW)
            cmat_b_lo_offset *= 2;
         unsigned cmat_b_hi_offset = cmat_b_lo_offset + 4;

         unsigned cmat_c_offset = i * c_hw_length;

         nir_def *cmat_a_lo = nir_vec(b, &a_comps[cmat_a_lo_offset], a_hw_length);
         nir_def *cmat_a_hi = nir_vec(b, &a_comps[cmat_a_hi_offset], a_hw_length);
         nir_def *cmat_b_lo = nir_vec(b, &b_comps[cmat_b_lo_offset], b_hw_length);
         nir_def *cmat_b_hi = nir_vec(b, &b_comps[cmat_b_hi_offset], b_hw_length);
         nir_def *c_part = nir_vec(b, &c_comps[cmat_c_offset], c_hw_length);

         nir_def *new_c = nir_cmat_muladd_nv(b, d_hw_length, cmat_a_lo,
                                             cmat_b_lo, c_part,
                                             .flags = NAK_AS_U32(flags));
         nir_def *tmp_d = nir_cmat_muladd_nv(b, d_hw_length, cmat_a_hi,
                                             cmat_b_hi, new_c,
                                             .flags = NAK_AS_U32(flags));

         for (unsigned c = 0; c < d_hw_length; c++)
            d_comps[i * d_hw_length + c] = nir_channel(b, tmp_d, c);
      }
   } else if ((cmat_type == NAK_CMAT_TYPE_M16N16K32_INT_SW &&
               hw_cmat_type == NAK_CMAT_TYPE_M16N8K32_INT) ||
              (cmat_type == NAK_CMAT_TYPE_M16N16K16_FLOAT_SW &&
               hw_cmat_type == NAK_CMAT_TYPE_M16N8K16_FLOAT))  {
      nir_def *cmat_b_lo = nir_vec(b,  b_comps,               b_length / 2);
      nir_def *cmat_b_hi = nir_vec(b, &b_comps[b_length / 2], b_length / 2);

      nir_def *cmat_c_lo = nir_vec(b,  c_comps,               c_length / 2);
      nir_def *cmat_c_hi = nir_vec(b, &c_comps[c_length / 2], c_length / 2);

      nir_def *cmat_d_lo = nir_cmat_muladd_nv(b, dst_length / 2, cmat_a,
                                              cmat_b_lo, cmat_c_lo,
                                              .flags = NAK_AS_U32(flags));
      nir_def *cmat_d_hi = nir_cmat_muladd_nv(b, dst_length / 2, cmat_a,
                                              cmat_b_hi, cmat_c_hi,
                                              .flags = NAK_AS_U32(flags));

      for (unsigned i = 0; i < dst_length / 2; i++) {
         d_comps[i]                  = nir_channel(b, cmat_d_lo, i);
         d_comps[i + dst_length / 2] = nir_channel(b, cmat_d_hi, i);
      }
   } else {
      assert(0 && "lowering not implemented");
   }

   return nir_vec(b, d_comps, dst_length);
}

static nir_def *
lower_cmat_convert(nir_builder *b, nir_intrinsic_instr *intr, nir_def *cmat,
                   struct glsl_cmat_description a_desc,
                   struct glsl_cmat_description d_desc)
{
   nir_cmat_signed cmat_signed_mask = nir_intrinsic_cmat_signed_mask(intr);

   enum glsl_base_type src_type = glsl_apply_signedness_to_base_type(
      a_desc.element_type, cmat_signed_mask & NIR_CMAT_A_SIGNED);
   enum glsl_base_type dst_type = glsl_apply_signedness_to_base_type(
      d_desc.element_type, cmat_signed_mask & NIR_CMAT_RESULT_SIGNED);

   /* We want to shuffle the smaller values for better packing. */
   bool conv_narrows =
      glsl_base_type_bit_size(src_type) > glsl_base_type_bit_size(dst_type);
   nir_op op =
      nir_type_conversion_op(nir_get_nir_type_for_glsl_base_type(src_type),
                             nir_get_nir_type_for_glsl_base_type(dst_type),
                             nir_rounding_mode_undef);

   /* If the result type is smaller, we convert before shuffling. */
   if (conv_narrows)
      cmat = nir_build_alu1(b, op, cmat);

   enum nak_matrix_type_layout a_layout = determine_matrix_type(a_desc);
   enum nak_matrix_type_layout d_layout = determine_matrix_type(d_desc);

   /* Matrix layout conversion code. For some conversions we also need
    * to fix the layout, so we shuffle values around to achieve that.
    */
   if (a_layout != d_layout) {
      nir_def *lane_id = nir_load_subgroup_invocation(b);
      unsigned mask    = a_layout == NAK_MAT_16X16 ? 0x1 : 0x2;
      unsigned compare = a_layout == NAK_MAT_16X16 ? 0x2 : 0x1;

      nir_def *adj;
      if (a_layout == NAK_MAT_16X16) {
         adj = nir_ishl_imm(b, nir_iand_imm(b, lane_id, mask), 1);
      } else {
         adj = nir_ushr_imm(b, nir_iand_imm(b, lane_id, mask), 1);
      }

      /* lane_id & 0x1c + (lane_id & mask << 1) */
      /* lane_id & 0x1c + (lane_id & mask >> 1) */
      nir_def *lane0 = nir_iadd(b, nir_iand_imm(b, lane_id, 0x1c), adj);
      /* lane_id & 0x1c + (lane_id & mask << 1) + mask */
      /* lane_id & 0x1c + (lane_id & mask >> 1) + mask */
      nir_def *lane1 = nir_iadd_imm(b, lane0, mask);
      nir_def *cond = nir_ieq_imm(b, nir_iand_imm(b, lane_id, compare), 0);

      if (cmat->num_components == 4) {
         nir_def *xy = nir_channels(b, cmat, 0x3);
         nir_def *zw = nir_channels(b, cmat, 0xc);

         nir_def *xy0 = nir_shuffle(b, xy, lane0);
         nir_def *zw0 = nir_shuffle(b, xy, lane1);
         nir_def *xy1 = nir_shuffle(b, zw, lane0);
         nir_def *zw1 = nir_shuffle(b, zw, lane1);

         xy = nir_bcsel(b, cond, xy0, xy1);
         zw = nir_bcsel(b, cond, zw0, zw1);

         cmat = nir_vec4(b,
            nir_channel(b, xy, 0),
            nir_channel(b, xy, 1),
            nir_channel(b, zw, 0),
            nir_channel(b, zw, 1)
         );
      } else if (cmat->num_components == 8 && a_layout == NAK_MAT_16X16) {
         nir_def *abcd = nir_channels(b, cmat, 0x0f);
         nir_def *efgh = nir_channels(b, cmat, 0xf0);

         nir_def *abef0 = nir_shuffle(b, abcd, lane0);
         nir_def *cdgh0 = nir_shuffle(b, abcd, lane1);
         nir_def *abef1 = nir_shuffle(b, efgh, lane0);
         nir_def *cdgh1 = nir_shuffle(b, efgh, lane1);

         nir_def *abef = nir_bcsel(b, cond, abef0, abef1);
         nir_def *cdgh = nir_bcsel(b, cond, cdgh0, cdgh1);

         cmat = nir_vec8(b,
            nir_channel(b, abef, 0),
            nir_channel(b, abef, 1),
            nir_channel(b, cdgh, 0),
            nir_channel(b, cdgh, 1),
            nir_channel(b, abef, 2),
            nir_channel(b, abef, 3),
            nir_channel(b, cdgh, 2),
            nir_channel(b, cdgh, 3)
         );
      } else if (cmat->num_components == 8 && a_layout == NAK_MAT_16x32_INT8) {
         nir_def *abef = nir_channels(b, cmat, 0x33);
         nir_def *cdgh = nir_channels(b, cmat, 0xcc);

         nir_def *abcd0 = nir_shuffle(b, abef, lane0);
         nir_def *efgh0 = nir_shuffle(b, abef, lane1);
         nir_def *abcd1 = nir_shuffle(b, cdgh, lane0);
         nir_def *efgh1 = nir_shuffle(b, cdgh, lane1);

         nir_def *abcd = nir_bcsel(b, cond, abcd0, abcd1);
         nir_def *efgh = nir_bcsel(b, cond, efgh0, efgh1);

         cmat = nir_vec8(b,
            nir_channel(b, abcd, 0),
            nir_channel(b, abcd, 1),
            nir_channel(b, abcd, 2),
            nir_channel(b, abcd, 3),
            nir_channel(b, efgh, 0),
            nir_channel(b, efgh, 1),
            nir_channel(b, efgh, 2),
            nir_channel(b, efgh, 3)
         );
      } else {
         UNREACHABLE("unsupported component counts for Matrix layout conversion");
      }
   }

   /* If the result type is not smaller, we convert after shuffling */
   if (!conv_narrows)
      cmat = nir_build_alu1(b, op, cmat);

   return cmat;
}

static struct nir_def*
try_lower_cmat_load_to_ldsm(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->intrinsic == nir_intrinsic_cmat_load);

   enum glsl_matrix_layout layout = nir_intrinsic_matrix_layout(intr);

   const struct glsl_cmat_description desc = cmat_src_desc(intr->src[0]);
   const unsigned length = get_cmat_length(desc);
   nir_deref_instr *deref = nir_def_as_deref(intr->src[1].ssa);
   const unsigned ptr_bit_size = glsl_get_bit_size(deref->type);
   const unsigned vec = glsl_get_vector_elements(deref->type);
   nir_src stride = intr->src[2];

   /* Even though LDSM operates on 16 bit types, the int8 matrix layout is
    * compatible so that we can use LDSM on it as well. But we can't use it on
    * the 32 bit types, because that actually uses a different data layout on a
    * byte level.
    */
   const unsigned bit_size = glsl_base_type_bit_size(desc.element_type);
   if (!nir_src_is_const(stride)
       || !nir_deref_mode_is(deref, nir_var_mem_shared)
       || bit_size > 16)
       return NULL;

   /* The stride is in elements of the pointed to type, not necessarily the
    * type of the referenced matrix
    */
   unsigned stride_bytes = nir_src_as_uint(stride) * vec * ptr_bit_size / 8;
   if (stride_bytes % 16 != 0)
      return NULL;

   /* check implicit base ptr alignment */
   if ((layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR    && desc.cols * bit_size < 128) ||
       (layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR && desc.rows * bit_size < 128))
         return NULL;

   /* LDSM loads n 8x8 16 bit matrices */
   unsigned mat_size_bits = desc.rows * desc.cols * bit_size;
   unsigned ldsm_count = mat_size_bits / (8 * 8 * 16);

   /* TODO: split bigger ones into multiple LDSM calls */
   if (ldsm_count > 4 || ldsm_count == 0)
      return NULL;

   if ((desc.use != GLSL_CMAT_USE_B && layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR) ||
       (desc.use == GLSL_CMAT_USE_B && layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR)) {
      /* Quite the pain, might not be worth it */
      if (ldsm_count >= 4)
         return NULL;

      /* We'd need to split the rows leading to unaligned loads */
      if (ldsm_count >= 2 && (desc.rows / 2) * bit_size < 128)
         return NULL;
   }

   /* Account for differences in tiling depending on the layout */
   nir_def *offset;
   nir_def *lane_id = nir_load_subgroup_invocation(b);
   if (ldsm_count == 4 && layout != GLSL_MATRIX_LAYOUT_COLUMN_MAJOR) {
      nir_def *lower = nir_iand(b, lane_id, nir_imm_int(b, 0x0f));
      nir_def *upper = nir_iand(b, lane_id, nir_imm_int(b, 0x10));

      offset = nir_imul_imm(b, lower, stride_bytes);
      offset = nir_iadd(b, offset, upper);
   } else if (ldsm_count >= 2 && layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR) {
      nir_def *lower;
      nir_def *lower_lo = nir_iand(b, lane_id, nir_imm_int(b, 0x07));
      nir_def *upper = nir_iand(b, lane_id, nir_imm_int(b, 0x08));
      if (ldsm_count == 4) {
         nir_def *lower_hi = nir_iand(b, lane_id, nir_imm_int(b, 0x10));
         lower = nir_ior(b, lower_lo, nir_ushr_imm(b, lower_hi, 1));
      } else {
         lower = lower_lo;
      }

      offset = nir_imul_imm(b, lower, stride_bytes);
      offset = nir_iadd(b, offset, nir_ishl_imm(b, upper, 1));
   } else {
      offset = nir_imul_imm(b, lane_id, stride_bytes);
   }

   nir_def *base = intr->src[1].ssa;
   offset = nir_u2uN(b, offset, base->bit_size);
   nir_def *addr = nir_iadd(b, base, offset);

   /* flip the layout for B matrices */
   if (desc.use == GLSL_CMAT_USE_B) {
      if (layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR)
         layout = GLSL_MATRIX_LAYOUT_COLUMN_MAJOR;
      else if (layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR)
         layout = GLSL_MATRIX_LAYOUT_ROW_MAJOR;
   }

   /* Each thread loads 32 bits per matrix */
   assert(length * bit_size == 32 * ldsm_count);
   return nir_cmat_load_shared_nv(b, length, bit_size, addr,
                                     .num_matrices = ldsm_count,
                                     .matrix_layout = layout);
}

/**
 * Returns the possibly vectorization width we can use to load/store matrices
 * of the given cmat desc and layout
 */
static int load_store_get_vec_size(const struct glsl_cmat_description desc,
                                   enum glsl_matrix_layout layout)
{
   unsigned bit_size = glsl_base_type_bit_size(desc.element_type);
   bool uses_movm = uses_movm_for_bit_size(bit_size);
   bool needs_transpose =
      (desc.use != GLSL_CMAT_USE_B && layout != GLSL_MATRIX_LAYOUT_ROW_MAJOR) ||
      (desc.use == GLSL_CMAT_USE_B && layout != GLSL_MATRIX_LAYOUT_COLUMN_MAJOR);

   if (needs_transpose && !uses_movm)
      return 1;

   switch (bit_size) {
   case 16:
   case 32:
      return 2;
   case  8:
      return 4;
   default:
      return 1;
   }
}

static nir_deref_instr*
get_cmat_component_deref(nir_builder *b, nir_intrinsic_instr *intr,
                         nir_def *lane_id, unsigned idx)
{
   unsigned deref_src = intr->intrinsic == nir_intrinsic_cmat_store ? 0 : 1;
   unsigned cmat_src = intr->intrinsic == nir_intrinsic_cmat_store ? 1 : 0;

   const struct glsl_cmat_description desc = cmat_src_desc(intr->src[cmat_src]);
   nir_deref_instr *deref = nir_def_as_deref(intr->src[deref_src].ssa);
   unsigned type_size_B = glsl_base_type_bit_size(desc.element_type) / 8;

   const enum glsl_matrix_layout layout = nir_intrinsic_matrix_layout(intr);
   nir_def *stride = intr->src[2].ssa;

   nir_def *col_offset;
   nir_def *row_offset;
   compute_matrix_offsets(b, desc, layout, lane_id, idx,
                              &col_offset, &row_offset);

   row_offset = nir_imul(b, row_offset, stride);
   col_offset = nir_u2uN(b, col_offset, deref->def.bit_size);
   row_offset = nir_u2uN(b, row_offset, deref->def.bit_size);

   unsigned align_mul = 0, align_offset = 0, combined_align = 0;
   nir_get_explicit_deref_align(deref, false, &align_mul, &align_offset);

   if (align_mul)
      combined_align = nir_combined_align(align_mul, align_offset);

   /* VUID-RuntimeSpirv-OpCooperativeMatrixLoadKHR-08986:
    * For OpCooperativeMatrixLoadKHR and OpCooperativeMatrixStoreKHR
    * instructions, the Pointer and Stride operands must be aligned to at least
    * the lesser of 16 bytes or the natural alignment of a row or column
    * (depending on ColumnMajor) of the matrix (where the natural alignment is
    * the number of columns/rows multiplied by the component size) */
   unsigned align_elems =
      layout == GLSL_MATRIX_LAYOUT_COLUMN_MAJOR ? desc.rows : desc.cols;
   unsigned implicit_align = MIN2(16, align_elems * type_size_B);
   if (implicit_align > combined_align) {
      align_mul = implicit_align;
      align_offset = 0;
   }

   /* We have to ignore the incoming stride, but have to choose the type of
    * the pointer as the declared stride is in multiple of the pointer type */
   deref = nir_build_deref_cast_with_alignment(
      b, &deref->def, deref->modes,
      deref->type,
      glsl_get_vector_elements(deref->type) * glsl_get_bit_size(deref->type) / 8,
      align_mul,
      align_offset
   );
   deref = nir_build_deref_ptr_as_array(b, deref, row_offset);
   deref = nir_build_deref_cast(
      b, &deref->def, deref->modes,
      glsl_scalar_type(desc.element_type),
      type_size_B);
   return nir_build_deref_ptr_as_array(b, deref, col_offset);
}

static void
lower_cmat_load(nir_builder *b, nir_intrinsic_instr *intr)
{
   struct nir_def *ldsm = try_lower_cmat_load_to_ldsm(b, intr);
   if (ldsm) {
      store_cmat_src(b, intr->src[0], ldsm);
      return;
   }

   const struct glsl_cmat_description desc = cmat_src_desc(intr->src[0]);
   const enum glsl_matrix_layout layout = nir_intrinsic_matrix_layout(intr);
   const unsigned length = get_cmat_length(desc);

   nir_def *vars[NIR_MAX_VEC_COMPONENTS];
   for (unsigned i = 0; i < length; ++i)
      vars[i] = nir_undef(b, 1, glsl_base_type_bit_size(desc.element_type));

   nir_def *lane_id = nir_load_subgroup_invocation(b);

   int vec_size = load_store_get_vec_size(desc, layout);
   for (unsigned idx = 0; idx < length; idx += vec_size) {
      nir_deref_instr *iter_deref =
         get_cmat_component_deref(b, intr, lane_id, idx);
      nir_variable_mode modes = iter_deref->modes;
      const glsl_type *vec_type = glsl_vector_type(desc.element_type, vec_size);
      iter_deref = nir_build_deref_cast_with_alignment(b,
         &iter_deref->def, modes, vec_type,
         0, vec_size * glsl_base_type_bit_size(desc.element_type) / 8, 0);

      nir_def *value = nir_load_deref(b, iter_deref);
      if (transpose_on_load_store(desc, layout))
         value = transpose_matrix(b, value);

      for (int c = 0; c < vec_size; c++)
         vars[idx + c] = nir_channel(b, value, c);
   }

   nir_def *mat = nir_vec(b, vars, length);
   store_cmat_src(b, intr->src[0], mat);
}

static bool
lower_cmat_instr(nir_builder *b,
                 nir_instr *instr,
                 struct hash_table *type_mapping,
                 const struct nak_compiler *nak)
{
   /* Remap deref types */
   if (instr->type == nir_instr_type_deref) {
      nir_deref_instr *deref = nir_instr_as_deref(instr);
      const struct glsl_type *new_type =
         remap_matrix_type(type_mapping, deref->type);

      if (new_type != deref->type) {
         deref->type = new_type;
         return true;
      } else {
         return false;
      }
   }

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   b->cursor = nir_before_instr(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_cmat_construct: {
      const unsigned length = get_cmat_length(cmat_src_desc(intr->src[0]));
      nir_def *r = nir_replicate(b, intr->src[1].ssa, length);

      store_cmat_src(b, intr->src[0], r);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_load: {
      lower_cmat_load(b, intr);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_store: {
      const struct glsl_cmat_description desc = cmat_src_desc(intr->src[1]);
      const enum glsl_matrix_layout layout = nir_intrinsic_matrix_layout(intr);
      const unsigned length = get_cmat_length(desc);
      nir_def *src = load_cmat_src(b, intr->src[1]);

      nir_def *vars[NIR_MAX_VEC_COMPONENTS];
      for (unsigned i = 0; i < length; i++)
         vars[i] = nir_channel(b, src, i);

      nir_def *lane_id = nir_load_subgroup_invocation(b);

      int vec_size = load_store_get_vec_size(desc, layout);
      for (unsigned idx = 0; idx < length; idx += vec_size) {
         nir_deref_instr *iter_deref =
            get_cmat_component_deref(b, intr, lane_id, idx);

         nir_variable_mode modes = iter_deref->modes;
         const glsl_type *vec_type = glsl_vector_type(desc.element_type, vec_size);
         iter_deref = nir_build_deref_cast_with_alignment(b,
            &iter_deref->def, modes, vec_type,
            0, vec_size * glsl_base_type_bit_size(desc.element_type) / 8, 0);
         nir_def *value = nir_vec(b, &vars[idx], vec_size);
         if (transpose_on_load_store(desc, layout))
            value = transpose_matrix(b, value);
         nir_store_deref(b, iter_deref, value, -1);
      }

      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_length: {
      const unsigned length = get_cmat_length(nir_intrinsic_cmat_desc(intr));
      nir_def_replace(&intr->def, nir_imm_int(b, length));
      return true;
   }

   case nir_intrinsic_cmat_muladd: {
      const struct glsl_cmat_description d_desc = cmat_src_desc(intr->src[0]);

      const struct glsl_cmat_description a_desc = cmat_src_desc(intr->src[1]);
      const struct glsl_cmat_description b_desc = cmat_src_desc(intr->src[2]);
      const struct glsl_cmat_description c_desc = cmat_src_desc(intr->src[3]);

      nir_def *cmat_a = load_cmat_src(b, intr->src[1]);
      nir_def *cmat_b = load_cmat_src(b, intr->src[2]);
      nir_def *cmat_c = load_cmat_src(b, intr->src[3]);

      nir_def *ret = lower_cmat_muladd(b, intr, cmat_a, cmat_b, cmat_c, a_desc,
                                       b_desc, c_desc, d_desc, nak->sm);
      store_cmat_src(b, intr->src[0], ret);
      nir_instr_remove(&intr->instr);
      return true;
   }

   case nir_intrinsic_cmat_unary_op: {
      nir_def *src = load_cmat_src(b, intr->src[1]);
      nir_op op = nir_intrinsic_alu_op(intr);

      nir_def *ret = nir_build_alu1(b, op, src);
      store_cmat_src(b, intr->src[0], ret);

      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_binary_op: {
      nir_def *src_a = load_cmat_src(b, intr->src[1]);
      nir_def *src_b = load_cmat_src(b, intr->src[2]);
      nir_op op = nir_intrinsic_alu_op(intr);

      nir_def *ret = nir_build_alu2(b, op, src_a, src_b);
      store_cmat_src(b, intr->src[0], ret);

      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_scalar_op: {
      nir_def *src_a = load_cmat_src(b, intr->src[1]);
      nir_op op = nir_intrinsic_alu_op(intr);

      nir_def *ret = nir_build_alu2(b, op, src_a, intr->src[2].ssa);
      store_cmat_src(b, intr->src[0], ret);

      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_bitcast: {
      nir_def *mat = load_cmat_src(b, intr->src[1]);
      store_cmat_src(b, intr->src[0], mat);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_extract: {
      nir_def *mat = load_cmat_src(b, intr->src[0]);
      nir_def *index = intr->src[1].ssa;
      nir_def *elem = nir_vector_extract(b, mat, index);
      nir_def_replace(&intr->def, elem);
      return true;
   }

   case nir_intrinsic_cmat_insert: {
      nir_def *elem = intr->src[1].ssa;
      nir_def *mat = load_cmat_src(b, intr->src[2]);
      nir_def *index = intr->src[3].ssa;

      nir_def *r = nir_vector_insert(b, mat, elem, index);
      store_cmat_src(b, intr->src[0], r);

      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_copy: {
      nir_build_copy_deref(b, intr->src[0].ssa, intr->src[1].ssa);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_convert: {
      struct glsl_cmat_description dst_desc = cmat_src_desc(intr->src[0]);
      struct glsl_cmat_description src_desc = cmat_src_desc(intr->src[1]);

      nir_def *cmat = load_cmat_src(b, intr->src[1]);
      nir_def *ret = lower_cmat_convert(b, intr, cmat, src_desc, dst_desc);
      store_cmat_src(b, intr->src[0], ret);

      nir_instr_remove(instr);
      return true;
   }

   default:
      return false;
   }
}

static bool
lower_cmat_impl(nir_function_impl *impl,
                struct hash_table *type_mapping,
                const struct nak_compiler *nak)
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

   nir_builder b = nir_builder_create(impl);
   nir_foreach_block_reverse_safe(block, impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         if (lower_cmat_instr(&b, instr, type_mapping, nak))
            progress = true;
      }
   }

   return nir_progress(progress, impl, nir_metadata_control_flow);
}

bool
nak_nir_lower_cmat(nir_shader *nir, const struct nak_compiler *nak)
{
   bool progress = false;

   if (nir->info.stage != MESA_SHADER_COMPUTE ||
       !nir->info.cs.has_cooperative_matrix)
      return false;

   struct hash_table *type_mapping = _mesa_pointer_hash_table_create(NULL);

   /* Remap all cmat shader temp var to array of scalars */
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_temp) {
      const struct glsl_type *new_type =
         remap_matrix_type(type_mapping, var->type);

      if (new_type != var->type) {
         var->type = new_type;
         progress = true;
      }
   }

   nir_foreach_function_impl(impl, nir) {
      if (lower_cmat_impl(impl, type_mapping, nak))
         progress = true;
   }

   _mesa_hash_table_destroy(type_mapping, NULL);
   return progress;
}
