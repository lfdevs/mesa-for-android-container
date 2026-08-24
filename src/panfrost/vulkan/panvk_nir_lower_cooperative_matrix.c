/*
 * Copyright © 2023 Bas Nieuwenhuizen
 * Copyright © 2024 Collabora, Ltd.
 * Copyright © 2026 Google LLC.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_nir.h"
#include "nir_builder.h"
#include "util/hash_table.h"

/* MMUL works on a 4x4 grid of lanes. Each 32-bit register packs one fp32, two
 * fp16 (v2f16), or four int8 (v4s8/v4u8) along the K dimension.
 */
#define PANVK_CMAT_HW_DIM 4

struct lower_cmat_ctx {
   struct hash_table *type_mapping;
   unsigned subgroup_size;
   /* fp16 muladd operands pack two columns per lane (length >= 2). A shader
    * without an fp16 muladd keeps the natural one-element-per-lane layout so
    * fp16 stays elementwise-compatible with fp32 (e.g. for cmat_convert).
    * Mixing an fp16 muladd with an fp16<->fp32 cmat_convert would need both
    * layouts at once, but no advertised fp16 configuration shares its shape
    * and use with an fp32 one, so a valid shader cannot contain that
    * convert.
    */
   bool fp16_packed;
};

/* Number of matrix elements each lane holds (the SPIR-V cmat length). */
static unsigned
get_cmat_length(struct glsl_cmat_description desc,
                const struct lower_cmat_ctx *ctx)
{
   unsigned length = (desc.rows * desc.cols) / ctx->subgroup_size;

   /* fp16 muladd operands (A/B) pack two columns per lane, so a 4x4 tile (one
    * element per lane over 16 lanes) still needs a two-element vector over 8
    * lanes. The accumulator stays native fp32/int32 - no advertised config
    * pairs fp16 with an accumulator use - so packing never applies to it.
    */
   if (ctx->fp16_packed && desc.element_type == GLSL_TYPE_FLOAT16 &&
       desc.use != GLSL_CMAT_USE_ACCUMULATOR)
      length = MAX2(length, 2);

   return length;
}

static const struct glsl_type *
remap_matrix_type(struct lower_cmat_ctx *ctx, const struct glsl_type *orig)
{
   struct hash_entry *entry = _mesa_hash_table_search(ctx->type_mapping, orig);
   if (entry)
      return entry->data;

   const struct glsl_type *new_type = orig;
   const struct glsl_type *leaf = glsl_without_array(orig);

   if (glsl_type_is_cmat(leaf)) {
      struct glsl_cmat_description desc = *glsl_get_cmat_description(leaf);
      new_type = glsl_type_wrap_in_arrays(
         glsl_vector_type(desc.element_type, get_cmat_length(desc, ctx)), orig);
   }

   _mesa_hash_table_insert(ctx->type_mapping, orig, (void *)new_type);
   return new_type;
}

/* Remap a type field in place, returning whether it changed. */
static bool
remap_type_in_place(struct lower_cmat_ctx *ctx, const struct glsl_type **type)
{
   const struct glsl_type *new_type = remap_matrix_type(ctx, *type);
   if (new_type == *type)
      return false;

   *type = new_type;
   return true;
}

static struct glsl_cmat_description
cmat_src_desc(nir_src src)
{
   return *glsl_get_cmat_description(nir_src_as_deref(src)->type);
}

static nir_def *
load_cmat_src(nir_builder *b, nir_src src, const struct lower_cmat_ctx *ctx)
{
   nir_deref_instr *deref = nir_src_as_deref(src);
   struct glsl_cmat_description desc = *glsl_get_cmat_description(deref->type);

   return nir_build_load_deref(b, get_cmat_length(desc, ctx),
                               glsl_base_type_bit_size(desc.element_type),
                               &deref->def, 0);
}

static void
store_cmat_src(nir_builder *b, nir_src dst, nir_def *val)
{
   nir_store_deref(b, nir_src_as_deref(dst), val, ~0);
}

/* Each lane holds `length` matrix elements. For a 4x4 tile that is one
 * element (and the buffer pointee may be a vector - multicomponent loads).
 * Wider tiles spread their extra elements along K (A along columns, B along
 * rows) with a stride of PANVK_CMAT_HW_DIM: component s of every lane forms
 * K slice s of the tile, already in the MMUL operand layout, and tiles of
 * equal shape stay elementwise compatible across element types for
 * cmat_convert.
 */
static bool
lower_cmat_load_store(nir_builder *b, nir_intrinsic_instr *intr,
                      const struct lower_cmat_ctx *ctx)
{
   const bool is_load = intr->intrinsic == nir_intrinsic_cmat_load;
   const struct glsl_cmat_description desc = cmat_src_desc(intr->src[!is_load]);
   const bool row_major = nir_intrinsic_matrix_layout(intr) == GLSL_MATRIX_LAYOUT_ROW_MAJOR;
   const unsigned length = get_cmat_length(desc, ctx);
   nir_deref_instr *deref = nir_src_as_deref(intr->src[is_load]);

   nir_def *lane = nir_load_subgroup_invocation(b);

   if (length == 1) {
      const unsigned elem_size_B = glsl_base_type_bit_size(desc.element_type) / 8;
      const unsigned ptr_stride = glsl_get_bit_size(deref->type) / 8 *
                                  glsl_get_vector_elements(deref->type);
      deref = nir_build_deref_cast(b, &deref->def, deref->modes, deref->type,
                                   ptr_stride);
      const unsigned idx_bits = deref->def.bit_size;

      nir_def *col = nir_umod_imm(b, lane, desc.cols);
      nir_def *row = nir_udiv_imm(b, lane, desc.cols);
      if (row_major) {
         nir_def *tmp = col;
         col = row;
         row = tmp;
      }
      nir_def *stride = nir_u2uN(b, intr->src[2].ssa, idx_bits);
      col = nir_imul(b, nir_u2uN(b, col, idx_bits), stride);
      row = nir_u2uN(b, row, idx_bits);

      nir_deref_instr *e = nir_build_deref_ptr_as_array(b, deref, col);
      e = nir_build_deref_cast(b, &e->def, deref->modes,
                               glsl_scalar_type(desc.element_type),
                               elem_size_B);
      e = nir_build_deref_ptr_as_array(b, e, row);

      if (is_load)
         store_cmat_src(b, intr->src[0], nir_load_deref(b, e));
      else
         nir_store_deref(b, e,
                         load_cmat_src(b, intr->src[!is_load], ctx),
                         ~0);

      nir_instr_remove(&intr->instr);
      return true;
   }

   const bool pack_rows = desc.use == GLSL_CMAT_USE_B;
   const unsigned ptr_vec = glsl_get_vector_elements(deref->type);
   const struct glsl_type *elem_type = glsl_scalar_type(desc.element_type);
   const unsigned elem_size_B = glsl_base_type_bit_size(desc.element_type) / 8;
   deref = nir_build_deref_cast(b, &deref->def, deref->modes, elem_type,
                                elem_size_B);

   const unsigned idx_bits = deref->def.bit_size;
   /* The buffer pointee may be a vector (multicomponent), so its stride is in
    * pointee units - scale it to the scalar elements we address.
    */
   nir_def *stride =
      nir_imul_imm(b, nir_u2uN(b, intr->src[2].ssa, idx_bits), ptr_vec);

   nir_def *g0 = nir_udiv_imm(b, lane, PANVK_CMAT_HW_DIM);
   nir_def *g1 = nir_umod_imm(b, lane, PANVK_CMAT_HW_DIM);

   /* An fp16 muladd operand is a 4x4 tile with two adjacent columns per
    * lane: element (R,C) -> lane 4R + C/2, halfword C&1. It uses 8 of the 16
    * lanes. Mask g1 to keep the unused lanes' addresses in bounds.
    */
   const bool fp16_operand =
      ctx->fp16_packed && desc.element_type == GLSL_TYPE_FLOAT16;

   nir_def *elems[NIR_MAX_VEC_COMPONENTS];
   if (!is_load) {
      nir_def *src = load_cmat_src(b, intr->src[!is_load], ctx);
      for (unsigned p = 0; p < length; p++)
         elems[p] = nir_channel(b, src, p);
   }

   for (unsigned p = 0; p < length; p++) {
      nir_def *row, *col;
      if (fp16_operand) {
         nir_def *cg = nir_iand_imm(b, g1, (desc.cols / 2) - 1);
         row = g0;
         col = nir_iadd_imm(b, nir_imul_imm(b, cg, 2), p);
      } else {
         nir_def *base = pack_rows ? g0 : g1;
         nir_def *off = nir_iadd_imm(b, base, p * PANVK_CMAT_HW_DIM);
         row = pack_rows ? off : g0;
         col = pack_rows ? g1 : off;
      }

      nir_def *outer = row_major ? row : col;
      nir_def *inner = row_major ? col : row;
      nir_def *idx =
         nir_iadd(b, nir_imul(b, nir_u2uN(b, outer, idx_bits), stride),
                  nir_u2uN(b, inner, idx_bits));

      nir_deref_instr *e = nir_build_deref_ptr_as_array(b, deref, idx);

      if (is_load)
         elems[p] = nir_load_deref(b, e);
      else
         nir_store_deref(b, e, elems[p], ~0);
   }

   if (is_load)
      store_cmat_src(b, intr->src[0], nir_vec(b, elems, length));

   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_cmat_instr(nir_builder *b, nir_instr *instr, struct lower_cmat_ctx *ctx)
{
   /* Remap deref types. Processed in reverse, so the intrinsics below still
    * see the original cmat-typed derefs when reading the description.
    */
   if (instr->type == nir_instr_type_deref) {
      nir_deref_instr *deref = nir_instr_as_deref(instr);
      return remap_type_in_place(ctx, &deref->type);
   }

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   b->cursor = nir_before_instr(instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_cmat_construct: {
      nir_def *r = nir_replicate(
         b, intr->src[1].ssa,
         get_cmat_length(cmat_src_desc(intr->src[0]), ctx));
      store_cmat_src(b, intr->src[0], r);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_length: {
      const unsigned length = get_cmat_length(nir_intrinsic_cmat_desc(intr), ctx);
      nir_def_replace(&intr->def, nir_imm_int(b, length));
      return true;
   }

   case nir_intrinsic_cmat_extract: {
      nir_def *mat = load_cmat_src(b, intr->src[0], ctx);
      nir_def *elem = nir_vector_extract(b, mat, intr->src[1].ssa);
      nir_def_replace(&intr->def, elem);
      return true;
   }

   case nir_intrinsic_cmat_insert: {
      nir_def *mat = load_cmat_src(b, intr->src[2], ctx);
      nir_def *r = nir_vector_insert(b, mat, intr->src[1].ssa, intr->src[3].ssa);
      store_cmat_src(b, intr->src[0], r);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_copy: {
      nir_build_copy_deref(b, intr->src[0].ssa, intr->src[1].ssa);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_load:
   case nir_intrinsic_cmat_store:
      return lower_cmat_load_store(b, intr, ctx);

   case nir_intrinsic_cmat_muladd: {
      struct glsl_cmat_description a_desc = cmat_src_desc(intr->src[1]);
      unsigned bits = glsl_base_type_bit_size(a_desc.element_type);

      nir_def *a = load_cmat_src(b, intr->src[1], ctx);
      nir_def *b_mat = load_cmat_src(b, intr->src[2], ctx);
      nir_def *acc = load_cmat_src(b, intr->src[3], ctx);

      /* The multiply signedness comes from the MulAdd operands, not the
       * declared element type - matrixmuladd_cross declares the opposite.
       */
      nir_alu_type type;
      if (bits == 8) {
         const bool a_signed = nir_intrinsic_cmat_signed_mask(intr) & NIR_CMAT_A_SIGNED;
         type = a_signed ? nir_type_int8 : nir_type_uint8;
      } else {
         type = nir_type_float | bits;
      }

      if (bits == 32 && a->num_components > 1) {
         /* The tile is wider than the 4x4x4 fp32 MMUL. Component s of every
          * lane is K slice s in the operand layout, so chain one MMUL per
          * slice on the accumulator.
          */
         for (unsigned s = 0; s < a->num_components; s++) {
            acc = nir_cmat_muladd_pan(b, nir_channel(b, a, s),
                                      nir_channel(b, b_mat, s), acc,
                                      .src_type = type);
         }
      } else {
         /* Each lane's element vector packs into one 32-bit MMUL operand
          * register (fp16 packs two columns, int8 four along K, fp32 is
          * already 32-bit). An int8 tile narrower than the 4x4x16 MMUL
          * zero-pads the unused bytes, the v4s8/v4u8 dot products ignore
          * them. The accumulator C and result D stay native fp32/int32.
          */
         if (bits == 8 && a->num_components < 4) {
            a = nir_pad_vector_imm_int(b, a, 0, 4);
            b_mat = nir_pad_vector_imm_int(b, b_mat, 0, 4);
         }
         acc = nir_cmat_muladd_pan(b, nir_pack_bits(b, a, 32),
                                   nir_pack_bits(b, b_mat, 32), acc,
                                   .src_type = type);
      }
      store_cmat_src(b, intr->src[0], acc);
      nir_instr_remove(instr);
      return true;
   }

   /* All three share the fp_math_ctrl dance. Only the second operand
    * differs: unary has none, binary loads a second matrix, scalar takes a
    * raw scalar.
    */
   case nir_intrinsic_cmat_unary_op:
   case nir_intrinsic_cmat_binary_op:
   case nir_intrinsic_cmat_scalar_op: {
      nir_def *a = load_cmat_src(b, intr->src[1], ctx);
      b->fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr);

      nir_def *r;
      if (intr->intrinsic == nir_intrinsic_cmat_unary_op) {
         r = nir_build_alu1(b, nir_intrinsic_alu_op(intr), a);
      } else {
         nir_def *y = intr->intrinsic == nir_intrinsic_cmat_binary_op
                         ? load_cmat_src(b, intr->src[2], ctx)
                         : intr->src[2].ssa;
         r = nir_build_alu2(b, nir_intrinsic_alu_op(intr), a, y);
      }

      b->fp_math_ctrl = nir_fp_fast_math;
      store_cmat_src(b, intr->src[0], r);
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_bitcast: {
      store_cmat_src(b, intr->src[0],
                     load_cmat_src(b, intr->src[1], ctx));
      nir_instr_remove(instr);
      return true;
   }

   case nir_intrinsic_cmat_convert: {
      struct glsl_cmat_description dst = cmat_src_desc(intr->src[0]);
      struct glsl_cmat_description src = cmat_src_desc(intr->src[1]);
      nir_def *ret = load_cmat_src(b, intr->src[1], ctx);

      if (dst.element_type != src.element_type) {
         b->fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr);
         nir_op op = nir_type_conversion_op(
            nir_get_nir_type_for_glsl_base_type(src.element_type),
            nir_get_nir_type_for_glsl_base_type(dst.element_type),
            nir_rounding_mode_undef);
         ret = nir_build_alu1(b, op, ret);
         b->fp_math_ctrl = nir_fp_fast_math;
      }

      store_cmat_src(b, intr->src[0], ret);
      nir_instr_remove(instr);
      return true;
   }

   default:
      return false;
   }
}

static bool
lower_cmat_impl(nir_function_impl *impl, struct lower_cmat_ctx *ctx)
{
   bool progress = false;

   nir_foreach_function_temp_variable(var, impl) {
      if (remap_type_in_place(ctx, &var->type))
         progress = true;
   }

   nir_builder b = nir_builder_create(impl);
   nir_foreach_block_reverse_safe(block, impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         if (lower_cmat_instr(&b, instr, ctx))
            progress = true;
      }
   }

   return nir_progress(progress, impl, nir_metadata_control_flow);
}

struct cmat_muladd_info {
   bool any_8bit;
   bool any_fp16;
};

static struct cmat_muladd_info
gather_cmat_muladd_info(nir_shader *nir)
{
   struct cmat_muladd_info info = { false, false };

   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_cmat_muladd)
               continue;

            struct glsl_cmat_description a = cmat_src_desc(intr->src[1]);
            if (glsl_base_type_bit_size(a.element_type) == 8)
               info.any_8bit = true;
            if (a.element_type == GLSL_TYPE_FLOAT16)
               info.any_fp16 = true;
         }
      }
   }

   return info;
}

static bool
cmat_k_dim_divisible(const struct glsl_type *type, unsigned k)
{
   type = glsl_without_array(type);
   if (!glsl_type_is_cmat(type))
      return true;

   const struct glsl_cmat_description *desc = glsl_get_cmat_description(type);
   switch (desc->use) {
   case GLSL_CMAT_USE_A:
      return desc->cols % k == 0;
   case GLSL_CMAT_USE_B:
      return desc->rows % k == 0;
   default:
      return true;
   }
}

static bool
shader_cmat_k_dims_divisible(nir_shader *nir, unsigned k)
{
   nir_foreach_variable_with_modes(var, nir, nir_var_shader_temp) {
      if (!cmat_k_dim_divisible(var->type, k))
         return false;
   }

   nir_foreach_function_impl(impl, nir) {
      nir_foreach_function_temp_variable(var, impl) {
         if (!cmat_k_dim_divisible(var->type, k))
            return false;
      }
   }

   return true;
}

/* The int8 MMUL contracts 16 elements along K (v4s8/v4u8 dot products), the
 * fp32 and fp16 ones contract 4. All matrices split with one uniform
 * granularity so both sides of a cmat_convert always split into the same
 * tiles. K = 16 is used when the shader multiplies 8-bit matrices and every
 * matrix K dimension allows the split: an fp32 muladd then runs one MMUL per
 * K slice of the tile, and an int8 muladd on a K = 4 tile zero-pads its
 * operands. The fp16 muladd operand layout only covers 4x4 tiles, so an fp16
 * muladd keeps K = 4.
 */
static unsigned
panvk_cmat_k_gran(nir_shader *nir, struct cmat_muladd_info muladd)
{
   const unsigned k_wide = 4 * PANVK_CMAT_HW_DIM;

   if (muladd.any_8bit && !muladd.any_fp16 &&
       shader_cmat_k_dims_divisible(nir, k_wide))
      return k_wide;

   return PANVK_CMAT_HW_DIM;
}

bool
panvk_nir_lower_cooperative_matrix(nir_shader *nir, unsigned subgroup_size)
{
   if (nir->info.stage != MESA_SHADER_COMPUTE ||
       !nir->info.cs.has_cooperative_matrix)
      return false;

   struct cmat_muladd_info muladd = gather_cmat_muladd_info(nir);
   struct nir_lower_coopmat_args args = {
      .m_gran = PANVK_CMAT_HW_DIM,
      .n_gran = PANVK_CMAT_HW_DIM,
      .k_gran = panvk_cmat_k_gran(nir, muladd),
   };
   bool progress = nir_lower_cooperative_matrix_flexible_dimensions(nir, &args);

   if (progress) {
      NIR_PASS(_, nir, nir_opt_deref);
      NIR_PASS(_, nir, nir_opt_dce);
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_function_temp | nir_var_shader_temp, NULL);
   }

   struct lower_cmat_ctx ctx = {
      .type_mapping = _mesa_pointer_hash_table_create(NULL),
      .subgroup_size = subgroup_size,
      .fp16_packed = muladd.any_fp16,
   };

   nir_foreach_variable_with_modes(var, nir, nir_var_shader_temp) {
      if (remap_type_in_place(&ctx, &var->type))
         progress = true;
   }

   nir_foreach_function_impl(impl, nir) {
      if (lower_cmat_impl(impl, &ctx))
         progress = true;
   }

   _mesa_hash_table_destroy(ctx.type_mapping, NULL);
   return progress;
}
