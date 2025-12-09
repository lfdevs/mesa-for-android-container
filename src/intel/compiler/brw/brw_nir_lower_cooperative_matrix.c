/*
 * Copyright 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/**
 * \file brw_nir_lower_cooperative_matrix.c
 * Lower cooperative matrix to subgroup operations.
 *
 * All supported matrix types are assumed to have either 8 rows or 8
 * columns. The other dimension of the matrix is typically 8 times the number
 * of data elements that can be stored in a 32-bit dword. Matrix data is
 * indexed by a combination of an array element and a subgroup invocation ID.
 *
 * Two layouts for matrix data are used. In the first layout,
 * subgroupShuffle(slice[N], ...) accesses row N of the matrix. This will be
 * called row-major hereafter. In the other layout,
 * subgroupShuffle(slice[...], M) accesses column M of the matrix. This will
 * be called column-major hereafter. In cases where a single 32-bit value is
 * stored in each entry, these layouts are identical.
 *
 * The subtle difference arises when multiple values are packed into a single
 * 32-bit dword. If two 16-bit values are packed in a single 32-bit value in
 * column-major, subgroupShuffle(slice[0], 1) holds matrix entries m[1][1] and
 * m[2][1] (in m[row][column] notation). In row-major, that same shuffle holds
 * m[0][2] and m[0][3].
 *
 * There is an alternate way to think about the matrix layouts. Every matrix
 * size supported by the Intel driver is either Sx8 (e.g., 16x8 for float16 B
 * matrix) or Sx8T (e.g., 8x32 for int8 A matrix). The A matrix and B matrix
 * layouts are such that a single 8 dword register hold an entire row of the
 * matrix.
 *
 * Consider a matrix stored starting in register g32. In an A matrix, the
 * packed dwords of g32 contain only the data for a single row of the
 * matrix. g32 is row 0, g33 is row 1, etc. In a B matrix, the packed dwords
 * of g(32+N).X contain only the data for a single column of the
 * matrix. g[32:40].0 is column 0, g[32:40].1 is column 1, etc.
 *
 * This leads to some shenanigans in \c lower_cmat_load_store.
 *
 * In the common case, A, C, and result matrices are stored row major while B
 * matrices are stored column major. This arrangement facilitates efficient
 * dot product operations using DPAS or DP4A instructions.
 *
 * Future optimizations are possible when row and column major are
 * flipped. That is, efficient dot products are also possible when A, C, and
 * result matrices are column major while B is row major.
 */

#include "brw_nir.h"

typedef struct {
   /* Vector type that holds the elements packed. */
   const glsl_type *type;

   /* How many cmat elements per slice element. */
   unsigned packing_factor;

   struct glsl_cmat_description desc;

   /* Used by the tables.  Variable holding a slice or
    * arrays-of-arrays of slices.
    *
    * If present, the var->type (without arrays!) should match
    * the type above.
    */
   nir_variable *var;
} slice_info;

#define BRW_MAX_PACKING_FACTOR 4

struct lower_cmat_state {
   void *temp_ctx;

   nir_shader *shader;

   struct hash_table *slice_var_to_slice_info;

   struct hash_table *mat_var_to_slice_info;

   unsigned subgroup_size;

   struct {
      nir_def *tmp[NIR_MAX_VEC_COMPONENTS * BRW_MAX_PACKING_FACTOR];
   } scratch;
};

static bool
cmat_descriptions_are_equal(struct glsl_cmat_description a,
                            struct glsl_cmat_description b)
{
   return a.element_type == b.element_type &&
          a.scope == b.scope &&
          a.rows == b.rows &&
          a.cols == b.cols &&
          a.use == b.use;
}

static void
print_coop_types(struct lower_cmat_state *state)
{
   fprintf(stderr, "--- Slices to Cooperative Matrix type table\n");
   hash_table_foreach(state->slice_var_to_slice_info, e) {
      nir_variable *var = (void *)e->key;
      const slice_info *info = e->data;
      fprintf(stderr, "%p: %s -> %s\n", var, var->name,
              glsl_get_type_name(glsl_cmat_type(&info->desc)));
   }
   fprintf(stderr, "\n\n");
}

static const slice_info *
get_slice_info(struct lower_cmat_state *state, nir_deref_instr *deref)
{
   nir_variable *var = nir_deref_instr_get_variable(deref);
   struct hash_entry *entry = _mesa_hash_table_search(state->slice_var_to_slice_info, var);

   assert(entry != NULL);

   return entry->data;
}

static bool
lower_cmat_filter(const nir_instr *instr, const void *_state)
{
   if (instr->type == nir_instr_type_deref) {
      nir_deref_instr *deref = nir_instr_as_deref(instr);
      return glsl_type_is_cmat(deref->type);
   }

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_cmat_construct:
   case nir_intrinsic_cmat_load:
   case nir_intrinsic_cmat_store:
   case nir_intrinsic_cmat_length:
   case nir_intrinsic_cmat_muladd:
   case nir_intrinsic_cmat_convert:
   case nir_intrinsic_cmat_unary_op:
   case nir_intrinsic_cmat_binary_op:
   case nir_intrinsic_cmat_scalar_op:
   case nir_intrinsic_cmat_bitcast:
   case nir_intrinsic_cmat_insert:
   case nir_intrinsic_cmat_extract:
   case nir_intrinsic_cmat_copy:
      return true;

   default:
      return false;
   }
}

static void
init_slice_info(struct lower_cmat_state *state,
                struct glsl_cmat_description desc,
                slice_info *info)
{
   enum glsl_base_type base_type;

   /* Number of matrix elements stored by each subgroup invocation. If the
    * data is packed, the slice size will be less than this.
    */
   const unsigned elements_per_invocation =
      (desc.rows * desc.cols) / state->subgroup_size;

   assert(elements_per_invocation > 0);

   const unsigned element_bits = 32;
   const unsigned bits = glsl_base_type_get_bit_size(desc.element_type);

   /* Each invocation must have at least one dword of data, and that dword
    * must be tightly packed with values. No matter the matrix dimensions, a
    * matrix of uint8_t data must pack 4 values in each entry.
    */
   const unsigned packing_factor = element_bits / bits;
   assert(packing_factor <= BRW_MAX_PACKING_FACTOR);

   assert(elements_per_invocation >= packing_factor);

   switch (desc.element_type) {
   case GLSL_TYPE_FLOAT:
      base_type = GLSL_TYPE_FLOAT;
      break;
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_BFLOAT16:
   case GLSL_TYPE_UINT8:
   case GLSL_TYPE_UINT16:
      base_type = GLSL_TYPE_UINT;
      break;
   case GLSL_TYPE_INT:
   case GLSL_TYPE_INT8:
   case GLSL_TYPE_INT16:
      base_type = GLSL_TYPE_INT;
      break;
   default:
      UNREACHABLE("Invalid cooperative matrix element type.");
   }

   unsigned len = elements_per_invocation / packing_factor;

   /* Supported matrix sizes are designed to fill either 4 or 8 SIMD8
    * registers on DG2. That means:
    *
    *          4 regsiters   8 registers
    * SIMD32     len = 1       len = 2
    * SIMD16     len = 2       len = 4
    * SIMD8      len = 4       len = 8
    *
    * On Xe2, supported matrix sizes are still designed to fill 4 registers
    * (e.g., 8x32 uint8_t) or 8 registers (e.g., 16x16 float16). However, the
    * 16x16 float16 matrix will assign 16 elements per channel at SIMD16.
    */
   assert(len == 1 || len == 2 || len == 4 || len == 8 || len == 16);

   const struct glsl_type *slice_type = glsl_vector_type(base_type, len);

   info->type = slice_type;
   info->desc = desc;
   info->packing_factor = packing_factor;
}

static void
lower_cmat_load_store(nir_builder *b, nir_intrinsic_instr *intrin,
                      struct lower_cmat_state *state)
{
   const bool load = intrin->intrinsic == nir_intrinsic_cmat_load;
   const unsigned mat_src = load ? 0 : 1;
   const unsigned ptr_src = load ? 1 : 0;

   nir_deref_instr *slice = nir_src_as_deref(intrin->src[mat_src]);
   const slice_info *info = get_slice_info(state, slice);
   const struct glsl_cmat_description desc = info->desc;

   nir_def *results[NIR_MAX_VEC_COMPONENTS];
   const unsigned num_components = glsl_get_vector_elements(slice->type);

   nir_deref_instr *pointer = nir_src_as_deref(intrin->src[ptr_src]);
   const unsigned ptr_comp_width = glsl_get_bit_size(pointer->type);
   const unsigned ptr_num_comps = glsl_get_vector_elements(pointer->type);

   /* The stride is given in number of elements of the pointed type, which
    * doesn't necessarily match the matrix element type, so we need to adjust
    * it considering it may be a vector and have a different bit-width.
    */
   nir_def *stride = nir_udiv_imm(b,
                                  nir_imul_imm(b,
                                               intrin->src[2].ssa,
                                               ptr_comp_width * ptr_num_comps),
                                  glsl_base_type_get_bit_size(desc.element_type));

   /* The data that will be packed is in successive columns for A and
    * accumulator matrices. The data that will be packed for B matrices is in
    * successive rows.
    */
   const unsigned cols =
      desc.use != GLSL_CMAT_USE_B ? desc.cols / info->packing_factor : desc.cols;

   nir_def *invocation = nir_load_subgroup_invocation(b);
   nir_def *invocation_div_cols = nir_udiv_imm(b, invocation, cols);
   nir_def *invocation_mod_cols = nir_umod_imm(b, invocation, cols);

   nir_def *i_stride;

   const bool memory_layout_matches_register_layout =
      (nir_intrinsic_matrix_layout(intrin) == GLSL_MATRIX_LAYOUT_ROW_MAJOR) ==
      (desc.use != GLSL_CMAT_USE_B);

   if (memory_layout_matches_register_layout) {
      /* In the row-major arrangement, data is loaded a dword at a time
       * instead of a single element at a time. For this reason the stride is
       * divided by the packing factor.
       */
      i_stride = nir_udiv_imm(b, stride, info->packing_factor);
   } else {
      /* In the column-major arrangement, data is loaded a single element at a
       * time. Because the data elements are transposed, the step direction
       * that moves a single (packed) element in the row-major arrangement has
       * to explicitly step over the packing factor count of elements. For
       * this reason the stride is multiplied by the packing factor.
       *
       * NOTE: The unscaled stride is also still needed when stepping from one
       * packed element to the next. This occurs in the for-j loop below.
       */
      i_stride = nir_imul_imm(b, stride, info->packing_factor);
   }

   nir_def *base_offset;
   nir_def *i_step;

   if (nir_intrinsic_matrix_layout(intrin) == GLSL_MATRIX_LAYOUT_ROW_MAJOR) {
      base_offset = nir_iadd(b,
                             nir_imul(b,
                                      invocation_div_cols,
                                      i_stride),
                             invocation_mod_cols);

      i_step = nir_imul_imm(b, i_stride, state->subgroup_size / cols);
   } else {
      base_offset = nir_iadd(b,
                             nir_imul(b,
                                      invocation_mod_cols,
                                      i_stride),
                             invocation_div_cols);

      i_step = nir_imm_int(b, state->subgroup_size / cols);
   }

   if (memory_layout_matches_register_layout) {
      const struct glsl_type *element_type =
         glsl_scalar_type(glsl_get_base_type(slice->type));

      pointer = nir_build_deref_cast(b, &pointer->def, pointer->modes,
                                     element_type,
                                     glsl_get_bit_size(element_type) / 8);

      for (unsigned i = 0; i < num_components; i++) {
         nir_def *offset = nir_imul_imm(b, i_step, i);

         nir_deref_instr *memory_deref =
            nir_build_deref_ptr_as_array(b, pointer,
                                         nir_i2iN(b,
                                                  nir_iadd(b,
                                                           base_offset,
                                                           offset),
                                                  pointer->def.bit_size));

         if (load) {
            results[i] = nir_load_deref(b, memory_deref);
         } else {
            nir_def *src = nir_channel(b, nir_load_deref(b, slice), i);
            nir_store_deref(b, memory_deref, src, 0x1);
         }
      }
   } else {
      const struct glsl_type *element_type = glsl_scalar_type(desc.element_type);
      const unsigned element_bits = glsl_base_type_get_bit_size(desc.element_type);
      const unsigned element_stride = element_bits / 8;

      pointer = nir_build_deref_cast(b, &pointer->def, pointer->modes, element_type,
                                     element_stride);

      for (unsigned i = 0; i < num_components; i++) {
         nir_def *i_offset = nir_imul_imm(b, i_step, i);
         nir_def *v[4];

         for (unsigned j = 0; j < info->packing_factor; j++) {
            nir_def *offset = nir_iadd(b, nir_imul_imm(b, stride, j), i_offset);

            nir_deref_instr *memory_deref =
               nir_build_deref_ptr_as_array(b, pointer,
                                            nir_i2iN(b,
                                                     nir_iadd(b,
                                                              base_offset,
                                                              offset),
                                                     pointer->def.bit_size));

            if (load) {
               v[j] = nir_load_deref(b, memory_deref);
            } else {
               nir_def *src = nir_channel(b, nir_load_deref(b, slice), i);

               nir_def *v =
                  nir_channel(b, nir_unpack_bits(b, src, element_bits), j);

               nir_store_deref(b, memory_deref, v, 0x1);
            }
         }

         if (load) {
            results[i] = nir_pack_bits(b, nir_vec(b, v, info->packing_factor),
                                       info->packing_factor * element_bits);
         }
      }
   }

   if (load)
      nir_store_deref(b, slice, nir_vec(b, results, num_components),
                      nir_component_mask(num_components));
}

/* Unpack, apply operation, then pack again. */
static nir_def *
emit_packed_alu1(nir_builder *b,
                 struct lower_cmat_state *state,
                 const slice_info *src_info,
                 const slice_info *dst_info,
                 nir_op op,
                 nir_def *src)
{
   const unsigned dst_bits = glsl_base_type_bit_size(dst_info->desc.element_type);
   const unsigned src_bits = glsl_base_type_bit_size(src_info->desc.element_type);

   const unsigned src_components = glsl_get_vector_elements(src_info->type);
   const unsigned dst_components = glsl_get_vector_elements(dst_info->type);
   assert(src_components * src_info->packing_factor ==
          dst_components * dst_info->packing_factor);

   /* Store the result of all individual unpacked values. */
   assert(src_components * src_info->packing_factor <= ARRAY_SIZE(state->scratch.tmp));
   nir_def **tmp = state->scratch.tmp;

   for (unsigned i = 0; i < src_components; i++) {
      nir_def *chan = nir_channel(b, src, i);

      for (unsigned j = 0; j < src_info->packing_factor; j++) {
         const unsigned pos = (i * src_info->packing_factor) + j;
         nir_def *val = nir_channel(b, nir_unpack_bits(b, chan, src_bits), j);
         tmp[pos] = nir_build_alu1(b, op, val);
      }
   }

   /* Store each element of the result, might pack multiple values. */
   nir_def *results[NIR_MAX_VEC_COMPONENTS] = {};
   assert(dst_components <= ARRAY_SIZE(results));

   /* Store each packed element in destination, to be combined
    * into results.
    */
   nir_def *partial[BRW_MAX_PACKING_FACTOR];

   for (unsigned i = 0; i < dst_components; i++) {
      for (unsigned j = 0; j < dst_info->packing_factor; j++) {
         const unsigned pos = (i * dst_info->packing_factor) + j;
         partial[j] = tmp[pos];
      }

      results[i] =
         nir_pack_bits(b, nir_vec(b, partial, dst_info->packing_factor),
                       dst_info->packing_factor * dst_bits);
   }

   return nir_vec(b, results, dst_components);
}

static nir_op
get_cmat_conversion_op(enum glsl_base_type src,
                       enum glsl_base_type dst)
{
   if (src == GLSL_TYPE_BFLOAT16) {
      assert(dst == GLSL_TYPE_FLOAT);
      return nir_op_bf2f;

   } else if (dst == GLSL_TYPE_BFLOAT16) {
      assert(src == GLSL_TYPE_FLOAT);
      return nir_op_f2bf;

   } else {
      return nir_type_conversion_op(nir_get_nir_type_for_glsl_base_type(src),
                                    nir_get_nir_type_for_glsl_base_type(dst),
                                    nir_rounding_mode_undef);
   }
}

static void
lower_cmat_convert(nir_builder *b, nir_intrinsic_instr *intrin,
                   struct lower_cmat_state *state)
{
   nir_deref_instr *dst_slice = nir_src_as_deref(intrin->src[0]);
   nir_deref_instr *src_slice = nir_src_as_deref(intrin->src[1]);

   const slice_info *dst_info = get_slice_info(state, dst_slice);
   const slice_info *src_info = get_slice_info(state, src_slice);

   /* Cooperative matrices must have the same "shape" to be converted. */
   assert(src_info->desc.rows  == dst_info->desc.rows);
   assert(src_info->desc.cols  == dst_info->desc.cols);
   assert(src_info->desc.use   == dst_info->desc.use);
   assert(src_info->desc.scope == dst_info->desc.scope);

   nir_def *src = nir_load_deref(b, src_slice);

   const unsigned dst_components = glsl_get_vector_elements(dst_info->type);
   const unsigned dst_bits = glsl_base_type_bit_size(dst_info->desc.element_type);

   nir_def *result = nir_convert_cmat_intel(b,
                                            dst_components,
                                            dst_info->packing_factor * dst_bits,
                                            src,
                                            .dst_cmat_desc = dst_info->desc,
                                            .src_cmat_desc = src_info->desc);

   nir_store_deref(b, dst_slice, result, nir_component_mask(result->num_components));
}

static void
lower_cmat_unary_op(nir_builder *b, nir_intrinsic_instr *intrin,
                    struct lower_cmat_state *state)
{
   nir_deref_instr *dst_slice = nir_src_as_deref(intrin->src[0]);
   nir_deref_instr *src_slice = nir_src_as_deref(intrin->src[1]);

   const slice_info *dst_info = get_slice_info(state, dst_slice);
   const slice_info *src_info = get_slice_info(state, src_slice);
   assert(cmat_descriptions_are_equal(src_info->desc, dst_info->desc));

   nir_def *result = emit_packed_alu1(b, state, src_info, dst_info,
                                      nir_intrinsic_alu_op(intrin),
                                      nir_load_deref(b, src_slice));

   nir_store_deref(b, dst_slice, result, nir_component_mask(result->num_components));
}

static void
lower_cmat_binary_op(nir_builder *b, nir_intrinsic_instr *intrin,
                     struct lower_cmat_state *state)
{
   nir_deref_instr *dst_slice = nir_src_as_deref(intrin->src[0]);
   nir_deref_instr *src_a_slice = nir_src_as_deref(intrin->src[1]);
   nir_deref_instr *src_b_slice = nir_src_as_deref(intrin->src[2]);

   nir_def *src_a = nir_load_deref(b, src_a_slice);
   nir_def *src_b = nir_load_deref(b, src_b_slice);
   nir_def *results[NIR_MAX_VEC_COMPONENTS];
   const unsigned num_components = glsl_get_vector_elements(dst_slice->type);

   const slice_info *info = get_slice_info(state, dst_slice);
   ASSERTED const slice_info *src_a_info = get_slice_info(state, src_a_slice);
   ASSERTED const slice_info *src_b_info = get_slice_info(state, src_b_slice);

   assert(cmat_descriptions_are_equal(info->desc, src_a_info->desc));
   assert(cmat_descriptions_are_equal(info->desc, src_b_info->desc));

   const unsigned bits = glsl_base_type_bit_size(info->desc.element_type);

   for (unsigned i = 0; i < num_components; i++) {
      nir_def *val_a = nir_channel(b, src_a, i);
      nir_def *val_b = nir_channel(b, src_b, i);

      results[i] =
         nir_pack_bits(b, nir_build_alu2(b, nir_intrinsic_alu_op(intrin),
                                         nir_unpack_bits(b, val_a, bits),
                                         nir_unpack_bits(b, val_b, bits)),
                       info->packing_factor * bits);
   }

   nir_store_deref(b, dst_slice, nir_vec(b, results, num_components),
                   nir_component_mask(num_components));
}

static void
lower_cmat_scalar_op(nir_builder *b, nir_intrinsic_instr *intrin,
                     struct lower_cmat_state *state)
{
   nir_deref_instr *dst_slice = nir_src_as_deref(intrin->src[0]);
   nir_deref_instr *src_slice = nir_src_as_deref(intrin->src[1]);
   nir_def *scalar = intrin->src[2].ssa;

   nir_def *src = nir_load_deref(b, src_slice);
   nir_def *results[NIR_MAX_VEC_COMPONENTS];
   const unsigned num_components = glsl_get_vector_elements(dst_slice->type);

   const slice_info *info = get_slice_info(state, dst_slice);
   ASSERTED const slice_info *src_info = get_slice_info(state, src_slice);
   assert(cmat_descriptions_are_equal(info->desc, src_info->desc));

   const unsigned bits = glsl_base_type_bit_size(info->desc.element_type);

   for (unsigned i = 0; i < num_components; i++) {
      nir_def *val = nir_channel(b, src, i);

      results[i] =
         nir_pack_bits(b, nir_build_alu2(b, nir_intrinsic_alu_op(intrin),
                                         nir_unpack_bits(b, val, bits),
                                         scalar),
                       info->packing_factor * bits);
   }

   nir_store_deref(b, dst_slice, nir_vec(b, results, num_components),
                   nir_component_mask(num_components));
}

static nir_deref_instr *
lower_cmat_deref(nir_builder *b, nir_deref_instr *deref,
                 struct lower_cmat_state *state)
{
   nir_deref_instr *parent = nir_deref_instr_parent(deref);
   if (parent) {
      assert(deref->deref_type == nir_deref_type_array);
      parent = lower_cmat_deref(b, parent, state);
      return nir_build_deref_array(b, parent, deref->arr.index.ssa);
   } else {
      assert(deref->deref_type == nir_deref_type_var);
      assert(deref->var);
      assert(glsl_type_is_cmat(glsl_without_array(deref->var->type)));

      struct hash_entry *entry = _mesa_hash_table_search(state->mat_var_to_slice_info, deref->var);
      assert(entry);
      const slice_info *info = entry->data;
      return nir_build_deref_var(b, info->var);
   }
}

static nir_def *
lower_cmat_instr(nir_builder *b, nir_instr *instr, void *_state)
{
   struct lower_cmat_state *state = _state;

   if (instr->type == nir_instr_type_deref) {
      nir_deref_instr *deref = lower_cmat_deref(b, nir_instr_as_deref(instr), state);
      return &deref->def;
   }

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_cmat_load:
   case nir_intrinsic_cmat_store:
      lower_cmat_load_store(b, intrin, state);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   case nir_intrinsic_cmat_construct: {
      nir_deref_instr *slice = nir_src_as_deref(intrin->src[0]);
      nir_def *src = intrin->src[1].ssa;

      const slice_info *info = get_slice_info(state, slice);

      if (info->packing_factor > 1) {
         src = nir_pack_bits(b, nir_replicate(b, src, info->packing_factor),
                             info->packing_factor * glsl_base_type_get_bit_size(info->desc.element_type));
      }

      const unsigned num_components = glsl_get_vector_elements(slice->type);

      nir_store_deref(b, slice, nir_replicate(b, src, num_components),
                      nir_component_mask(num_components));
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }

   case nir_intrinsic_cmat_convert:
      lower_cmat_convert(b, intrin, state);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   case nir_intrinsic_cmat_unary_op:
      lower_cmat_unary_op(b, intrin, state);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   case nir_intrinsic_cmat_binary_op:
      lower_cmat_binary_op(b, intrin, state);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   case nir_intrinsic_cmat_scalar_op:
      lower_cmat_scalar_op(b, intrin, state);
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   case nir_intrinsic_cmat_length: {
      slice_info info = {};
      init_slice_info(state, nir_intrinsic_cmat_desc(intrin), &info);
      return nir_imm_intN_t(b, info.packing_factor *
                               glsl_get_vector_elements(info.type), 32);
   }

   case nir_intrinsic_cmat_muladd: {
      nir_deref_instr *dst_slice = nir_src_as_deref(intrin->src[0]);
      nir_deref_instr *A_slice = nir_src_as_deref(intrin->src[1]);
      nir_deref_instr *B_slice = nir_src_as_deref(intrin->src[2]);
      nir_deref_instr *accum_slice = nir_src_as_deref(intrin->src[3]);

      const slice_info *dst_info = get_slice_info(state, dst_slice);
      const slice_info *src_info = get_slice_info(state, A_slice);

      const unsigned num_components = glsl_get_vector_elements(dst_slice->type);

      const nir_cmat_signed cmat_signed_mask =
         nir_intrinsic_cmat_signed_mask(intrin);

      assert(((cmat_signed_mask & NIR_CMAT_A_SIGNED) == 0) ==
             ((cmat_signed_mask & NIR_CMAT_B_SIGNED) == 0));
      assert(((cmat_signed_mask & NIR_CMAT_A_SIGNED) == 0) ==
             ((cmat_signed_mask & NIR_CMAT_C_SIGNED) == 0));
      assert(((cmat_signed_mask & NIR_CMAT_A_SIGNED) == 0) ==
             ((cmat_signed_mask & NIR_CMAT_RESULT_SIGNED) == 0));

      enum glsl_base_type src_type = src_info->desc.element_type;
      enum glsl_base_type dst_type = dst_info->desc.element_type;

      /* For integer types, the signedness is determined by flags on the
       * muladd instruction. The types of the sources play no role. Adjust the
       * types passed to the dpas_intel intrinsic to match.
       */
      if (glsl_base_type_is_integer(src_type)) {
         if ((cmat_signed_mask & NIR_CMAT_A_SIGNED) == 0) {
            src_type = glsl_unsigned_base_type_of(src_type);
            dst_type = glsl_unsigned_base_type_of(dst_type);
         } else {
            src_type = glsl_signed_base_type_of(src_type);
            dst_type = glsl_signed_base_type_of(dst_type);
         }
      }

      nir_def *result =
         nir_dpas_intel(b,
                        dst_info->packing_factor * glsl_base_type_get_bit_size(dst_info->desc.element_type),
                        nir_load_deref(b, accum_slice),
                        nir_load_deref(b, A_slice),
                        nir_load_deref(b, B_slice),
                        .dest_base_type = dst_type,
                        .src_base_type = src_type,
                        .saturate = nir_intrinsic_saturate(intrin),
                        .systolic_depth = 8,
                        .repeat_count = 8);

      nir_store_deref(b, dst_slice, result,
                      nir_component_mask(num_components));

      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }

   case nir_intrinsic_cmat_bitcast: {
      nir_deref_instr *dst_slice = nir_src_as_deref(intrin->src[0]);
      nir_deref_instr *src_slice = nir_src_as_deref(intrin->src[1]);

      const unsigned num_components = glsl_get_vector_elements(dst_slice->type);

      assert(glsl_get_vector_elements(src_slice->type) == num_components);

      nir_store_deref(b, dst_slice, nir_load_deref(b, src_slice),
                      nir_component_mask(num_components));
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }

   case nir_intrinsic_cmat_copy:
      nir_copy_deref(b,
                     nir_src_as_deref(intrin->src[0]),
                     nir_src_as_deref(intrin->src[1]));
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   case nir_intrinsic_cmat_insert: {
      nir_deref_instr *dst_slice = nir_src_as_deref(intrin->src[0]);
      nir_def *scalar = intrin->src[1].ssa;
      nir_deref_instr *src_slice = nir_src_as_deref(intrin->src[2]);
      const nir_src dst_index = intrin->src[3];

      const slice_info *info = get_slice_info(state, dst_slice);
      ASSERTED const slice_info *src_info = get_slice_info(state, src_slice);
      assert(cmat_descriptions_are_equal(info->desc, src_info->desc));

      const unsigned bits = glsl_base_type_bit_size(info->desc.element_type);
      const unsigned num_components = glsl_get_vector_elements(dst_slice->type);

      nir_def *slice_index = nir_udiv_imm(b, dst_index.ssa, info->packing_factor);
      nir_def *vector_index = nir_umod_imm(b, dst_index.ssa, info->packing_factor);
      nir_def *results[NIR_MAX_VEC_COMPONENTS];

      const int slice_constant_index = nir_src_is_const(dst_index)
         ? nir_src_as_uint(dst_index) / info->packing_factor
         : -1;

      for (unsigned i = 0; i < num_components; i++) {
         nir_def *val = nir_channel(b, nir_load_deref(b, src_slice), i);
         nir_def *insert;

         if (slice_constant_index < 0 || slice_constant_index == i) {
            if (info->packing_factor == 1) {
               insert = scalar;
            } else {
               nir_def *unpacked = nir_unpack_bits(b, val, bits);
               nir_def *v = nir_vector_insert(b, unpacked, scalar, vector_index);

               insert = nir_pack_bits(b, v, bits * info->packing_factor);
            }
         } else {
            insert = val;
         }

         results[i] = slice_constant_index < 0
            ? nir_bcsel(b, nir_ieq_imm(b, slice_index, i), insert, val)
            : insert;
      }

      nir_store_deref(b, dst_slice, nir_vec(b, results, num_components),
                      nir_component_mask(num_components));

      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }

   case nir_intrinsic_cmat_extract: {
      nir_deref_instr *slice = nir_src_as_deref(intrin->src[0]);
      const slice_info *info = get_slice_info(state, slice);
      nir_def *index = intrin->src[1].ssa;

      const unsigned bits = glsl_base_type_bit_size(info->desc.element_type);

      nir_def *src =
         nir_vector_extract(b, nir_load_deref(b, slice),
                            nir_udiv_imm(b, index, info->packing_factor));

      if (info->packing_factor == 1) {
         return src;
      } else {
         return nir_vector_extract(b,
                                   nir_unpack_bits(b, src, bits),
                                   nir_umod_imm(b, index, info->packing_factor));
      }

      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }

   default:
      UNREACHABLE("invalid cooperative matrix intrinsic");
   }
}

static const glsl_type *
make_aoa_slice_type(const glsl_type *t, const glsl_type *slice_type)
{
   if (glsl_type_is_array(t)) {
      const glsl_type *s = make_aoa_slice_type(glsl_get_array_element(t), slice_type);
      return glsl_array_type(s, glsl_array_size(t), 0);
   }

   assert(glsl_type_is_cmat(t));
   return slice_type;
}

static void
create_slice_var(struct lower_cmat_state *state, nir_variable *var,
                 nir_function_impl *impl)
{
   const struct glsl_type *mat_type = glsl_without_array(var->type);

   assert(glsl_type_is_cmat(mat_type));
   assert((!impl && var->data.mode == nir_var_shader_temp) ||
          ( impl && var->data.mode == nir_var_function_temp));

   slice_info *info = rzalloc(state->temp_ctx, slice_info);
   init_slice_info(state, *glsl_get_cmat_description(mat_type), info);

   const glsl_type *aoa_slice_type = make_aoa_slice_type(var->type, info->type);

   const char *slice_name = ralloc_asprintf(state->shader, "%s_slice", var->name);
   info->var = impl ?
      nir_local_variable_create(impl, aoa_slice_type, slice_name) :
      nir_variable_create(state->shader, var->data.mode, aoa_slice_type, slice_name);

   _mesa_hash_table_insert(state->mat_var_to_slice_info, var, info);
   _mesa_hash_table_insert(state->slice_var_to_slice_info, info->var, info);
}

bool
brw_nir_lower_cmat(nir_shader *shader, unsigned subgroup_size)
{
   void *temp_ctx = ralloc_context(NULL);

   struct lower_cmat_state state = {
      .temp_ctx = temp_ctx,
      .shader = shader,
      .slice_var_to_slice_info = _mesa_pointer_hash_table_create(temp_ctx),
      .mat_var_to_slice_info = _mesa_pointer_hash_table_create(temp_ctx),
      .subgroup_size = subgroup_size,
   };

   /* Create a slice array for each variable and add a map from the original
    * variable back to it, so it can be reached during lowering.
    *
    * TODO: Cooperative matrix inside struct?
    */
   nir_foreach_variable_in_shader(var, shader) {
      if (glsl_type_is_cmat(glsl_without_array(var->type)))
         create_slice_var(&state, var, NULL);
   }
   nir_foreach_function(func, shader) {
      nir_foreach_function_temp_variable(var, func->impl) {
         if (glsl_type_is_cmat(glsl_without_array(var->type)))
            create_slice_var(&state, var, func->impl);
      }
   }

   bool progress = nir_shader_lower_instructions(shader,
                                                 lower_cmat_filter,
                                                 lower_cmat_instr,
                                                 &state);

   ralloc_free(temp_ctx);

   return progress;
}
