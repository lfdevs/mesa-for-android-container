/*
 * Copyright © 2025 Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_deref.h"
#include "nir_builder.h"

/*
 * Lower flexible size cooperative matrix operations down to operations at the supported granularity.
 */
struct split_box {
   unsigned outer_rows;
   unsigned outer_cols;
};

struct split_mat {
   struct split_box box;
   nir_variable **split_vars;
};

struct split_info {
   struct hash_table *split_mats;
   unsigned m_gran;
   unsigned n_gran;
   unsigned k_gran;
};

static unsigned get_num_splits_box(const struct split_box *box)
{
   return box->outer_rows * box->outer_cols;
}

static unsigned get_num_splits(struct split_mat *split)
{
   return get_num_splits_box(&split->box);
}

static unsigned split_alloc_vars(struct split_mat *split)
{
   unsigned num_split = get_num_splits_box(&split->box);
   split->split_vars = ralloc_array(split, struct nir_variable *, num_split);
   return num_split;
}

static struct split_mat *find_split(struct hash_table *split_mats,
                                    nir_intrinsic_instr *intr, int idx)
{
   nir_variable *var = nir_intrinsic_get_var(intr, idx);
   struct hash_entry *entry = _mesa_hash_table_search(split_mats, var);
   return entry ? entry->data : NULL;
}

static struct split_mat *find_call_split(struct hash_table *split_mats,
                                         nir_cmat_call_instr *call, int idx)
{
   nir_deref_instr *deref = nir_src_as_deref(call->params[idx]);
   if (!deref)
      return NULL;
   nir_variable *var = nir_deref_instr_get_variable(deref);
   struct hash_entry *entry = _mesa_hash_table_search(split_mats, var);
   return entry ? entry->data : NULL;
}

static struct nir_deref_instr *recreate_derefs(nir_builder *b, nir_src *src,
                                                nir_variable *var)
{
   nir_deref_instr *deref = nir_src_as_deref(*src);
   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   nir_deref_instr *old_head = path.path[0];
   b->cursor = nir_after_instr(&old_head->instr);
   nir_deref_instr *head = nir_build_deref_var(b, var);
   for (int i = 1; path.path[i]; i++) {
      nir_deref_instr *old = path.path[i];
      b->cursor = nir_after_instr(&old->instr);
      head = nir_build_deref_follower(b, head, old);
   }

   nir_deref_path_finish(&path);
   return head;
}

static void
get_rowcol_gran(struct glsl_cmat_description desc, unsigned m_gran,
                unsigned n_gran, unsigned k_gran,
                unsigned *row_gran, unsigned *col_gran)
{
   switch (desc.use) {
   case GLSL_CMAT_USE_A:
   default:
      *row_gran = m_gran;
      *col_gran = k_gran;
      break;
   case GLSL_CMAT_USE_B:
      *row_gran = k_gran;
      *col_gran = n_gran;
      break;
   case GLSL_CMAT_USE_ACCUMULATOR:
      *row_gran = m_gran;
      *col_gran = n_gran;
      break;
   }
}

static void
get_lower_sizes(struct glsl_cmat_description desc, unsigned m_gran,
                unsigned n_gran, unsigned k_gran,
                unsigned *split_rows_out, unsigned *split_cols_out)
{
   unsigned split_rows = 0, split_cols = 0;

   unsigned row_gran, col_gran;

   get_rowcol_gran(desc, m_gran, n_gran, k_gran, &row_gran, &col_gran);

   if (desc.rows && desc.rows != row_gran) {
      split_rows = row_gran;
      assert(desc.rows % split_rows == 0);
   }
   if (desc.cols && desc.cols != col_gran) {
      split_cols = col_gran;
      assert(desc.cols % split_cols == 0);
   }

   *split_rows_out = split_rows;
   *split_cols_out = split_cols;
}

static bool
split_desc(struct glsl_cmat_description *desc, struct split_info *info,
           struct split_box *box)
{
   unsigned split_rows = 0, split_cols = 0;
   box->outer_cols = 1;
   box->outer_rows = 1;

   get_lower_sizes(*desc, info->m_gran, info->n_gran, info->k_gran, &split_rows, &split_cols);

   if (split_rows) {
      box->outer_rows = desc->rows / split_rows;
      desc->rows = split_rows;
   }
   if (split_cols) {
      box->outer_cols = desc->cols / split_cols;
      desc->cols = split_cols;
   }

   if (box->outer_rows == 1 && box->outer_cols == 1)
      return false;
   return true;
}

static bool
split_cmat_construct(nir_builder *b,
                     nir_intrinsic_instr *intr,
                     struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   if (!dst_split)
      return false;

   unsigned splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_construct(b, &dst_deref->def, intr->src[1].ssa);
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_copy(nir_builder *b,
                nir_intrinsic_instr *intr,
                struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src_split = find_split(info->split_mats, intr, 1);

   if (!dst_split)
      return false;

   unsigned splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   assert(src_split);

   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[1], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_copy(b, &dst_deref->def, &src_deref->def);
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_length(nir_builder *b,
                  nir_intrinsic_instr *intr,
                  struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct glsl_cmat_description desc = nir_intrinsic_cmat_desc(intr);
   unsigned row_gran, col_gran;
   unsigned split_rows = 0, split_cols = 0;
   unsigned splits = 1;

   get_rowcol_gran(desc, info->m_gran, info->n_gran, info->k_gran, &row_gran, &col_gran);

   if (desc.rows == row_gran &&
       desc.cols == col_gran)
      return false;

   get_lower_sizes(desc, info->m_gran, info->n_gran, info->k_gran, &split_rows, &split_cols);

   if (split_rows) {
      splits = desc.rows / split_rows;
      desc.rows = split_rows;
   }

   if (split_cols) {
      splits *= desc.cols / split_cols;
      desc.cols = split_cols;
   }

   if (splits <= 1)
      return false;

   b->cursor = nir_before_instr(instr);
   nir_def *def = nir_cmat_length(b, .cmat_desc = desc);
   def = nir_imul_imm(b, def, splits);
   nir_def_replace(&intr->def, def);
   return true;
}

static bool
split_cmat_insert(nir_builder *b,
                  nir_intrinsic_instr *intr,
                  struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src_split = find_split(info->split_mats, intr, 2);

   if (!dst_split)
      return false;

   unsigned splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   assert(src_split);
   b->cursor = nir_before_instr(instr);

   nir_def *len = nir_cmat_length(b, .cmat_desc = *glsl_get_cmat_description(src_split->split_vars[0]->type));
   nir_def *arr_idx = nir_udiv(b, intr->src[3].ssa, len);
   nir_def *base_idx = nir_umod(b, intr->src[3].ssa, len);
   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[2], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);

      nir_def *new_def = nir_cmat_extract(b, nir_src_bit_size(intr->src[1]), &src_deref->def, base_idx);
      nir_def *cond = nir_ieq_imm(b, arr_idx, i);

      new_def = nir_bcsel(b, cond, intr->src[1].ssa, new_def);
      nir_cmat_insert(b, &dst_deref->def, new_def, &src_deref->def, base_idx);
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_extract(nir_builder *b,
                  nir_intrinsic_instr *intr,
                  struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *src_split = find_split(info->split_mats, intr, 0);

   if (!src_split)
      return false;

   unsigned splits = get_num_splits(src_split);
   if (splits <= 1)
      return false;

   b->cursor = nir_before_instr(instr);

   nir_def *len = nir_cmat_length(b, .cmat_desc = *glsl_get_cmat_description(src_split->split_vars[0]->type));
   nir_def *arr_idx = nir_udiv(b, intr->src[1].ssa, len);
   nir_def *base_idx = nir_umod(b, intr->src[1].ssa, len);
   nir_def *last_def = nir_undef(b, 1, intr->def.bit_size);
   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[0], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_def *cond = nir_ieq_imm(b, arr_idx, i);
      nir_def *new_def = nir_cmat_extract(b, intr->def.bit_size, &src_deref->def, base_idx);

      last_def = nir_bcsel(b, cond, new_def, last_def);
   }
   nir_def_replace(&intr->def, last_def);
   return true;
}

static bool
split_cmat_convert(nir_builder *b,
                   nir_intrinsic_instr *intr,
                   struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src_split = find_split(info->split_mats, intr, 1);

   if (!dst_split && !src_split)
      return false;

   assert(dst_split && src_split);

   unsigned splits = get_num_splits(src_split);
   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[1], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_convert(b, &dst_deref->def, &src_deref->def, .saturate = nir_intrinsic_saturate(intr),
                       .cmat_signed_mask = nir_intrinsic_cmat_signed_mask(intr),
                       .fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr));
   }
   nir_instr_remove(instr);
   return true;
}


static bool
split_cmat_transpose(nir_builder *b,
                     nir_intrinsic_instr *intr,
                     struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src_split = find_split(info->split_mats, intr, 1);

   if (!dst_split && !src_split)
      return false;

   assert(dst_split && src_split);

   unsigned dst_splits = get_num_splits(dst_split);

   for (unsigned i = 0; i < dst_splits; i++) {
      int r = i / dst_split->box.outer_rows;
      int c = i % dst_split->box.outer_rows;
      int out_idx = c * dst_split->box.outer_cols + r;
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[out_idx]);
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[1], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_transpose(b, &dst_deref->def, &src_deref->def, .saturate = nir_intrinsic_saturate(intr),
                         .cmat_signed_mask = nir_intrinsic_cmat_signed_mask(intr),
                         .fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr));
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_bitcast(nir_builder *b,
                   nir_intrinsic_instr *intr,
                   struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src_split = find_split(info->split_mats, intr, 1);

   if (!dst_split)
      return false;

   unsigned splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   assert(src_split);

   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[1], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_bitcast(b, &dst_deref->def, &src_deref->def);
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_binary_op(nir_builder *b,
                     nir_intrinsic_instr *intr,
                     struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src0_split = find_split(info->split_mats, intr, 1);
   struct split_mat *src1_split = find_split(info->split_mats, intr, 2);

   if (!dst_split)
      return false;

   unsigned splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   assert(src0_split);
   assert(src1_split);

   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      nir_deref_instr *src0_deref = recreate_derefs(b, &intr->src[1], src0_split->split_vars[i]);
      nir_deref_instr *src1_deref = recreate_derefs(b, &intr->src[2], src1_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_binary_op(b, &dst_deref->def, &src0_deref->def, &src1_deref->def,
                         .alu_op = nir_intrinsic_alu_op(intr),
                         .fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr));
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_unary_op(nir_builder *b,
                    nir_intrinsic_instr *intr,
                    struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src_split = find_split(info->split_mats, intr, 1);

   if (!dst_split)
      return false;

   unsigned splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   assert(src_split);

   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[1], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_unary_op(b, &dst_deref->def, &src_deref->def, .alu_op = nir_intrinsic_alu_op(intr),
                        .fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr));
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_scalar_op(nir_builder *b,
                     nir_intrinsic_instr *intr,
                     struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *dst_split = find_split(info->split_mats, intr, 0);
   struct split_mat *src_split = find_split(info->split_mats, intr, 1);

   if (!dst_split)
      return false;

   unsigned splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   assert(src_split);

   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &intr->src[0], dst_split->split_vars[i]);
      nir_deref_instr *src_deref = recreate_derefs(b, &intr->src[1], src_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      nir_cmat_scalar_op(b, &dst_deref->def, &src_deref->def, intr->src[2].ssa,
                         .alu_op = nir_intrinsic_alu_op(intr),
                         .fp_math_ctrl = nir_intrinsic_fp_math_ctrl(intr));
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_muladd(nir_builder *b,
                  nir_function_impl *impl,
                  nir_intrinsic_instr *intr,
                  struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   struct split_mat *result_split = find_split(info->split_mats, intr, 0);
   struct split_mat *a_split = find_split(info->split_mats, intr, 1);
   struct split_mat *b_split = find_split(info->split_mats, intr, 2);
   struct split_mat *c_split = find_split(info->split_mats, intr, 3);

   unsigned m_splits = 1;
   unsigned n_splits = 1;
   unsigned k_splits = 1;

   if (!result_split && !a_split && !b_split && !c_split)
      return false;

   if (result_split) {
      assert(c_split);
      m_splits = result_split->box.outer_rows;
      n_splits = result_split->box.outer_cols;

      assert(c_split->box.outer_rows == m_splits);
      assert(c_split->box.outer_cols == n_splits);
      if (a_split)
         assert(a_split->box.outer_rows == m_splits);

      if (b_split)
         assert(b_split->box.outer_cols == n_splits);
   }

   if (a_split && a_split->box.outer_cols > 1) {
      assert(b_split);
      assert(b_split->box.outer_rows == a_split->box.outer_cols);
      k_splits = a_split->box.outer_cols;
   }

   for (unsigned m = 0; m < m_splits; m++) {
      for (unsigned n = 0; n < n_splits; n++) {
         unsigned idx = m * n_splits + n;
         nir_deref_instr *dst_deref = result_split ? recreate_derefs(b, &intr->src[0], result_split->split_vars[idx]) : nir_src_as_deref(intr->src[0]);
         nir_deref_instr *c_deref = c_split ? recreate_derefs(b, &intr->src[3], c_split->split_vars[idx]) : nir_src_as_deref(intr->src[3]);

         for (unsigned k = 0; k < k_splits; k++) {
            unsigned a_idx = m * k_splits + k;
            unsigned b_idx = k * n_splits + n;
            nir_deref_instr *a_deref = a_split ? recreate_derefs(b, &intr->src[1], a_split->split_vars[a_idx]) : nir_src_as_deref(intr->src[1]);
            nir_deref_instr *b_deref = b_split ? recreate_derefs(b, &intr->src[2], b_split->split_vars[b_idx]) : nir_src_as_deref(intr->src[2]);
            nir_deref_instr *k_dst_deref = k == k_splits - 1 ? dst_deref : c_deref;
            b->cursor = nir_before_instr(instr);

            nir_cmat_muladd(b, &k_dst_deref->def, &a_deref->def, &b_deref->def, &c_deref->def,
                            .saturate = nir_intrinsic_saturate(intr),
                            .cmat_signed_mask = nir_intrinsic_cmat_signed_mask(intr));
         }
      }
   }

   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_get_coordinate(nir_builder *b,
                          nir_intrinsic_instr *intr,
                          struct split_info *info)
{
   struct glsl_cmat_description desc = nir_intrinsic_cmat_desc(intr);
   struct split_box box;

   if (!split_desc(&desc, info, &box))
      return false;

   nir_def *length = nir_cmat_length(b, .cmat_desc = desc);
   nir_def *rem_def = nir_umod(b, intr->src[0].ssa, length);
   nir_def *split_idx = nir_udiv(b, intr->src[0].ssa, length);

   nir_def *adds[2];
   adds[0] = nir_udiv_imm(b, split_idx, box.outer_cols);
   adds[1] = nir_umod_imm(b, split_idx, box.outer_cols);
   nir_def *def = nir_cmat_get_coordinate(b, rem_def, .cmat_desc = desc);

   adds[0] = nir_imul_imm(b, adds[0], desc.cols);
   adds[1] = nir_imul_imm(b, adds[1], desc.cols);
   nir_def *add_vec = nir_vec(b, adds, 2);
   def = nir_iadd(b, def, add_vec);
   nir_def_replace(&intr->def, def);
   return true;
}

static void
call_reduce(nir_builder *b,
            nir_cmat_call_instr *call,
            nir_cmat_reduce reduce,
            nir_def *dst, nir_def *src0)
{
   nir_cmat_call_instr *ncall = nir_cmat_call_instr_create(b->shader, nir_cmat_call_op_reduce, call->callee);
   ncall->params[0] = nir_src_for_ssa(dst);
   ncall->params[1] = nir_src_for_ssa(src0);
   ncall->const_index[0] = reduce;
   nir_builder_instr_insert(b, &ncall->instr);
}

static void
call_reduce_finish(nir_builder *b,
                   nir_cmat_call_instr *call,
                   nir_cmat_reduce reduce,
                   nir_def *dst, nir_def *src0, nir_def *src1)
{
   nir_cmat_call_instr *ncall = nir_cmat_call_instr_create(b->shader, nir_cmat_call_op_reduce_finish, call->callee);
   ncall->params[0] = nir_src_for_ssa(dst);
   ncall->params[1] = nir_src_for_ssa(src0);
   ncall->params[2] = nir_src_for_ssa(src1);
   ncall->const_index[0] = reduce;
   nir_builder_instr_insert(b, &ncall->instr);
}

static void
call_reduce_2x2(nir_builder *b,
                nir_cmat_call_instr *call,
                nir_def *dst,
                nir_def *src0, nir_def *src1,
                nir_def *src2, nir_def *src3)
{
   nir_cmat_call_instr *ncall = nir_cmat_call_instr_create(b->shader, nir_cmat_call_op_reduce_2x2, call->callee);
   ncall->params[0] = nir_src_for_ssa(dst);
   ncall->params[1] = nir_src_for_ssa(src0);
   ncall->params[2] = nir_src_for_ssa(src1);
   ncall->params[3] = nir_src_for_ssa(src2);
   ncall->params[4] = nir_src_for_ssa(src3);
   nir_builder_instr_insert(b, &ncall->instr);
}

static void
split_cmat_reduce_row_col(nir_builder *b,
                          nir_cmat_call_instr *call,
                          struct split_mat *src_split,
                          struct split_mat *dst_split,
                          nir_deref_instr **temp_derefs)
{
   nir_instr *instr = &call->instr;
   nir_cmat_reduce reduce = nir_cmat_call_reduce_flags(call);
   int src_splits = 1;
   if (src_split)
      src_splits = get_num_splits(src_split);
   if (src_splits > 1) {
      if ((reduce & (NIR_CMAT_REDUCE_ROW | NIR_CMAT_REDUCE_COLUMN)) == (NIR_CMAT_REDUCE_ROW | NIR_CMAT_REDUCE_COLUMN)) {
         for (unsigned i = 1; i < src_splits; i++) {
            nir_deref_instr *second_deref = temp_derefs[i];
            b->cursor = nir_before_instr(instr);
            call_reduce_finish(b, call, reduce, &temp_derefs[0]->def, &temp_derefs[0]->def, &second_deref->def);
         }
      } else if (reduce & NIR_CMAT_REDUCE_ROW) {
         for (unsigned i = 1; i < src_split->box.outer_cols; i++) {
            nir_deref_instr *second_deref = temp_derefs[i];
            b->cursor = nir_before_instr(instr);
            call_reduce_finish(b, call, reduce, &temp_derefs[0]->def, &temp_derefs[0]->def, &second_deref->def);
         }
      } else if (reduce & NIR_CMAT_REDUCE_COLUMN) {
         for (unsigned i = 1; i < src_split->box.outer_rows; i++) {
            nir_deref_instr *second_deref = temp_derefs[i * src_split->box.outer_cols];
            b->cursor = nir_before_instr(instr);
            call_reduce_finish(b, call, reduce, &temp_derefs[0]->def, &temp_derefs[0]->def, &second_deref->def);
         }
      }
   }

   /* at this point temp_derefs should contain all the split reduced src matrices
      now to store them */
   if (dst_split) {
      for (unsigned r = 0; r < dst_split->box.outer_rows; r++) {
         for (unsigned c = 0; c < dst_split->box.outer_cols; c++) {
            int didx = r * dst_split->box.outer_cols + c;
            int idx;
            if ((reduce & (NIR_CMAT_REDUCE_ROW | NIR_CMAT_REDUCE_COLUMN)) == (NIR_CMAT_REDUCE_ROW | NIR_CMAT_REDUCE_COLUMN))
               idx = 0;
            else if (reduce & NIR_CMAT_REDUCE_ROW)
               idx = r % (src_split ? src_split->box.outer_rows : 1);
            else if (reduce & NIR_CMAT_REDUCE_COLUMN)
               idx = c % (src_split ? src_split->box.outer_cols : 1);
            else
               UNREACHABLE("Unknown NIR_CMAT_REDUCE_*");

            nir_deref_instr *deref = recreate_derefs(b, &call->params[0], dst_split->split_vars[didx]);
            b->cursor = nir_before_instr(instr);
            nir_cmat_copy(b, &deref->def, &temp_derefs[idx]->def);
         }
      }
   } else {
      nir_cmat_copy(b, call->params[0].ssa, &temp_derefs[0]->def);
   }
}

static void
split_cmat_reduce_2x2(nir_builder *b,
                      nir_cmat_call_instr *call,
                      struct split_mat *src_split,
                      struct split_mat *dst_split)
{
   nir_instr *instr = &call->instr;
   int rows = 1, cols = 1;
   if (dst_split) {
      rows = dst_split->box.outer_rows;
      cols = dst_split->box.outer_cols;
   }

   for (unsigned r = 0; r < rows; r++) {
      for (unsigned c = 0; c < cols; c++) {
         int d_idx = c + r * cols;
         int src_top_left_col = c * 2;
         int src_top_left_row = r * 2;
         int src_top_idx = src_top_left_col + src_top_left_row * src_split->box.outer_cols;
         int src_bottom_idx = src_top_left_col + (src_top_left_row + 1) * src_split->box.outer_cols;
         nir_deref_instr *src0_deref = recreate_derefs(b, &call->params[1], src_split->split_vars[src_top_idx]);
         nir_deref_instr *src1_deref = recreate_derefs(b, &call->params[1], src_split->split_vars[src_top_idx + 1]);
         nir_deref_instr *src2_deref = recreate_derefs(b, &call->params[1], src_split->split_vars[src_bottom_idx]);
         nir_deref_instr *src3_deref = recreate_derefs(b, &call->params[1], src_split->split_vars[src_bottom_idx + 1]);
         nir_deref_instr *dst_deref = dst_split ? recreate_derefs(b, &call->params[0], dst_split->split_vars[d_idx]) : nir_src_as_deref(call->params[0]);
         b->cursor = nir_before_instr(instr);
         call_reduce_2x2(b, call, &dst_deref->def, &src0_deref->def, &src1_deref->def, &src2_deref->def, &src3_deref->def);
      }
   }
}

static bool
split_cmat_call_reduce(nir_builder *b,
                       nir_function_impl *impl,
                       nir_cmat_call_instr *call,
                       struct split_info *info)
{
   nir_instr *instr = &call->instr;
   nir_cmat_reduce reduce = nir_cmat_call_reduce_flags(call);
   struct split_mat *dst_split = find_call_split(info->split_mats, call, 0);
   struct split_mat *src_split = find_call_split(info->split_mats, call, 1);

   if (reduce & (NIR_CMAT_REDUCE_ROW | NIR_CMAT_REDUCE_COLUMN)) {
      assert(!(reduce & ~(NIR_CMAT_REDUCE_ROW | NIR_CMAT_REDUCE_COLUMN)));

      /* for each source split - reduce it by itself. */
      int src_splits = 1;
      if (src_split)
         src_splits = get_num_splits(src_split);
      nir_deref_instr **temp_derefs = ralloc_array(NULL, nir_deref_instr *, src_splits);

      const struct glsl_type *temp_type = nir_cmat_call_get_var(call, 1)->type;
      if (src_splits > 1)
         temp_type = src_split->split_vars[0]->type;
      for (unsigned i = 0; i < src_splits; i++) {
         nir_variable *temp_var = nir_local_variable_create(impl, temp_type,
                                                            "reduce_split_srcs");
         temp_derefs[i] = nir_build_deref_var(b, temp_var);
      }

      /* reduce each individual src matrix */
      for (unsigned i = 0; i < src_splits; i++) {
         nir_deref_instr *src_deref = src_split ?
            recreate_derefs(b, &call->params[1], src_split->split_vars[i]) :
            nir_src_as_deref(call->params[1]);
         b->cursor = nir_before_instr(instr);
         call_reduce(b, call, reduce, &temp_derefs[i]->def, &src_deref->def);
      }

      split_cmat_reduce_row_col(b, call, src_split, dst_split, temp_derefs);
      ralloc_free(temp_derefs);
   } else if (reduce & NIR_CMAT_REDUCE_2X2) {
      assert(reduce == NIR_CMAT_REDUCE_2X2);

      /* dst can have target dimensions, but src but be at least twice as large */
      assert (src_split);

      split_cmat_reduce_2x2(b, call, src_split, dst_split);
   }

   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_load_store(nir_builder *b,
                      nir_intrinsic_instr *intr,
                      struct split_info *info)
{
   nir_instr *instr = &intr->instr;
   const bool is_load = intr->intrinsic == nir_intrinsic_cmat_load;
   enum glsl_matrix_layout layout = nir_intrinsic_matrix_layout(intr);
   nir_variable *var = nir_intrinsic_get_var(intr, !is_load);
   struct hash_entry *entry = _mesa_hash_table_search(info->split_mats, var);
   if (!entry)
      return false;

   struct split_mat *split = entry->data;
   unsigned splits = get_num_splits(split);
   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *new_deref = recreate_derefs(b, &intr->src[!is_load], split->split_vars[i]);
      nir_deref_instr *ptr_deref;
      nir_def *stride = intr->src[2].ssa;
      nir_def *ptr = intr->src[is_load].ssa;

      b->cursor = nir_before_instr(instr);
      if (i > 0) {
         nir_deref_instr *addr_deref = nir_src_as_deref(intr->src[is_load]);
         unsigned dst_bit_size = addr_deref->def.bit_size;
         nir_def *this_index = nir_imm_zero(b, 1, dst_bit_size);
         unsigned deref_bytes_size = glsl_get_explicit_size(addr_deref->type, false);
         const struct glsl_type *scalar_type = glsl_get_scalar_type(glsl_get_cmat_element(var->type));
         unsigned elem_size = glsl_get_explicit_size(scalar_type, false);
         struct glsl_cmat_description desc = *glsl_get_cmat_description(split->split_vars[i]->type);
         unsigned row_offset, col_offset;

         row_offset = (i % split->box.outer_cols) * desc.cols;
         col_offset = (i / split->box.outer_cols) * desc.rows;

         if (layout == GLSL_MATRIX_LAYOUT_ROW_MAJOR)
            SWAP(row_offset, col_offset);

         ptr_deref = nir_build_deref_cast(b, &addr_deref->def, addr_deref->modes, scalar_type, elem_size);
         stride = nir_udiv_imm(b, nir_imul_imm(b, stride, deref_bytes_size), elem_size);

         if (col_offset)
            this_index = nir_imm_intN_t(b, col_offset, dst_bit_size);
         if (row_offset)
            this_index = nir_iadd(b, this_index, nir_u2uN(b, nir_imul_imm(b, stride, row_offset), dst_bit_size));
         ptr_deref = nir_build_deref_ptr_as_array(b, ptr_deref, this_index);
         ptr = &ptr_deref->def;
      }
      if (is_load)
         nir_cmat_load(b, &new_deref->def, ptr, stride,
                       .matrix_layout = layout);
      else
         nir_cmat_store(b, ptr, &new_deref->def, stride,
                        .matrix_layout = layout);
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_call_per_element_op(nir_builder *b,
                               nir_cmat_call_instr *call,
                               struct split_info *info)
{
   nir_instr *instr = &call->instr;
   struct split_mat *dst_split = find_call_split(info->split_mats, call, 0);
   struct split_mat *src_split = find_call_split(info->split_mats, call, 3);
   if (!dst_split)
      return false;

   assert(src_split);
   int splits = get_num_splits(dst_split);
   if (splits <= 1)
      return false;

   for (unsigned i = 0; i < splits; i++) {
      int r = i / dst_split->box.outer_cols;
      int c = i % dst_split->box.outer_cols;
      nir_deref_instr *dst_deref = recreate_derefs(b, &call->params[0], dst_split->split_vars[i]);
      nir_deref_instr *src_deref = recreate_derefs(b, &call->params[3], src_split->split_vars[i]);
      struct glsl_cmat_description cmat_desc = *glsl_get_cmat_description(src_split->split_vars[0]->type);
      nir_cmat_call_instr *new_call = nir_cmat_call_instr_create(b->shader, nir_cmat_call_op_per_element_op, call->callee);
      new_call->params[0] = nir_src_for_ssa(&dst_deref->def);
      new_call->params[1] = nir_src_for_ssa(nir_imm_int(b, cmat_desc.rows * r));
      new_call->params[2] = nir_src_for_ssa(nir_imm_int(b, cmat_desc.cols * c));
      new_call->params[3] = nir_src_for_ssa(&src_deref->def);

      for (unsigned j = 4; j < call->num_params; j++) {
         if (nir_src_as_deref(call->params[j])) {
            struct split_mat *src1_split = find_call_split(info->split_mats, call, j);
            nir_deref_instr *src1_deref = src1_split ? recreate_derefs(b, &call->params[j], src1_split->split_vars[i]) : nir_src_as_deref(call->params[j]);
            new_call->params[j] = src1_deref ? nir_src_for_ssa(&src1_deref->def) : call->params[j];
         } else
            new_call->params[j] = call->params[j];
      }
      b->cursor = nir_before_instr(instr);
      nir_builder_instr_insert(b, &new_call->instr);
   }
   nir_instr_remove(instr);
   return true;
}

static bool
split_cmat_call_tensor_load_store(nir_builder *b,
                                  nir_cmat_call_instr *call,
                                  struct split_info *info)
{
   bool is_load = call->op != nir_cmat_call_op_tensor_store;
   nir_instr *instr = &call->instr;
   struct split_mat *split = find_call_split(info->split_mats, call, !is_load);
   if (!split)
      return false;

   struct split_mat *clip_split = NULL;
   if (is_load) {
      clip_split = find_call_split(info->split_mats, call, 4);
   }
   unsigned splits = get_num_splits(split);
   if (splits <= 1)
      return false;

   struct nir_cmat_tensor_load tensor_load = nir_cmat_call_tensor_load_info(call);
   for (unsigned i = 0; i < splits; i++) {
      nir_deref_instr *dst_deref = recreate_derefs(b, &call->params[!is_load], split->split_vars[i]);
      nir_deref_instr *clip_deref = NULL;

      if (clip_split)
         clip_deref = recreate_derefs(b, &call->params[4], clip_split->split_vars[i]);
      b->cursor = nir_before_instr(instr);
      struct nir_cmat_tensor_load split_tensor_load = { 0 };

      split_tensor_load.tensor_view = tensor_load.tensor_view;
      split_tensor_load.view_has_dims = tensor_load.view_has_dims;
      split_tensor_load.layout_clamp_mode = tensor_load.layout_clamp_mode;
      for (unsigned p = 0; p < NIR_TENSOR_VIEW_MAX_PERMUTATIONS; p++)
         split_tensor_load.view_permutations[p] = tensor_load.view_permutations[p];

      if (i > 0) {
         split_tensor_load.split_row_index = i / split->box.outer_cols;
         split_tensor_load.split_col_index = i % split->box.outer_cols;
      }

      nir_cmat_call_instr *splitcall = nir_cmat_call_instr_create(b->shader, call->op, call->callee);
      splitcall->params[!is_load] = nir_src_for_ssa(&dst_deref->def);
      splitcall->params[is_load] = call->params[is_load];
      splitcall->params[2] = call->params[2];
      splitcall->params[3] = call->params[3];
      if (is_load)
         splitcall->params[4] = nir_src_for_ssa(&clip_deref->def);
      nir_cmat_call_set_tensor_load_info(splitcall, split_tensor_load);
      nir_cmat_call_dup_cmat_desc(splitcall, call);
      nir_builder_instr_insert(b, &splitcall->instr);
   }

   nir_instr_remove(instr);
   return true;
}

static bool
split_matrix_impl(nir_function_impl *impl, struct split_info *info)
{
   bool progress = false;
   nir_builder b = nir_builder_create(impl);
   nir_foreach_block_reverse (block, impl) {
      nir_foreach_instr_reverse_safe (instr, block) {
         b.cursor = nir_before_instr(instr);
         switch (instr->type) {
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            switch (intr->intrinsic) {
            case nir_intrinsic_cmat_construct:
               progress |= split_cmat_construct(&b, intr, info);
               break;
            case nir_intrinsic_cmat_copy:
               progress |= split_cmat_copy(&b, intr, info);
               break;
            case nir_intrinsic_cmat_length:
               progress |= split_cmat_length(&b, intr, info);
               break;
            case nir_intrinsic_cmat_insert:
               progress |= split_cmat_insert(&b, intr, info);
               break;
            case nir_intrinsic_cmat_extract:
               progress |= split_cmat_extract(&b, intr, info);
               break;
            case nir_intrinsic_cmat_convert:
               progress |= split_cmat_convert(&b, intr, info);
               break;
            case nir_intrinsic_cmat_transpose:
               progress |= split_cmat_transpose(&b, intr, info);
               break;
            case nir_intrinsic_cmat_bitcast:
               progress |= split_cmat_bitcast(&b, intr, info);
               break;
            case nir_intrinsic_cmat_binary_op:
               progress |= split_cmat_binary_op(&b, intr, info);
               break;
            case nir_intrinsic_cmat_unary_op:
               progress |= split_cmat_unary_op(&b, intr, info);
               break;
            case nir_intrinsic_cmat_scalar_op:
               progress |= split_cmat_scalar_op(&b, intr, info);
               break;
            case nir_intrinsic_cmat_muladd:
               progress |= split_cmat_muladd(&b, impl, intr, info);
               break;
            case nir_intrinsic_cmat_load:
            case nir_intrinsic_cmat_store:
               progress |= split_cmat_load_store(&b, intr, info);
               break;
            case nir_intrinsic_cmat_get_coordinate:
               progress |= split_cmat_get_coordinate(&b, intr, info);
               break;
            default:
               break;
            }
            break;
         }
         case nir_instr_type_cmat_call: {
            nir_cmat_call_instr *cmat_call = nir_instr_as_cmat_call(instr);
            switch (cmat_call->op) {
            case nir_cmat_call_op_reduce:
               progress |= split_cmat_call_reduce(&b, impl, cmat_call, info);
               break;
            case nir_cmat_call_op_per_element_op:
               progress |= split_cmat_call_per_element_op(&b, cmat_call, info);
               break;
            case nir_cmat_call_op_tensor_load:
            case nir_cmat_call_op_tensor_store:
               progress |= split_cmat_call_tensor_load_store(&b, cmat_call, info);
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
   return progress;
}

static void
split_var(nir_shader *shader,
          nir_function_impl *impl,
          struct split_info *info,
          nir_variable *var)
{
   if (!glsl_type_is_cmat(glsl_without_array(var->type)))
      return;

   if (var->data.how_declared == nir_var_hidden)
      return;

   const struct glsl_type *type = var->type;
   if (glsl_type_is_array(type)) {
      type = glsl_without_array(var->type);
   }

   struct glsl_cmat_description desc = *glsl_get_cmat_description(type);

   struct split_box box;
   if (!split_desc(&desc, info, &box))
       return;

   const struct glsl_type *new_type = glsl_type_wrap_in_arrays(glsl_cmat_type(&desc), var->type);

   struct split_mat *split_mat = rzalloc(info->split_mats, struct split_mat);
   if (!split_mat)
      return;

   split_mat->box = box;
   unsigned num_split = split_alloc_vars(split_mat);
   for (unsigned i = 0; i < num_split; i++) {
      if (!nir_variable_is_global(var)) {
         split_mat->split_vars[i] = nir_local_variable_create(impl,
                                                              new_type, var->name);
      } else {
         split_mat->split_vars[i] = nir_variable_create(shader, var->data.mode,
                                                        new_type, var->name);
      }
      split_mat->split_vars[i]->data.how_declared = nir_var_hidden;
   }

   _mesa_hash_table_insert(info->split_mats, var, split_mat);
}

static bool
lower_dimensions(nir_shader *shader, nir_function_impl *impl,
                 const struct nir_lower_coopmat_args *args)
{
   void *mem_ctx = ralloc_context(NULL);
   struct hash_table *split_mats = _mesa_pointer_hash_table_create(mem_ctx);

   bool progress = false;

   struct split_info split_info = {
      .split_mats = split_mats,
      .m_gran = args->m_gran,
      .n_gran = args->n_gran,
      .k_gran = args->k_gran,
   };

   nir_foreach_variable_in_shader(var, shader) {
      split_var(shader, NULL, &split_info, var);
   }
   nir_foreach_function_temp_variable (var, impl) {
      split_var(shader, impl, &split_info, var);
   }

   progress = split_matrix_impl(impl, &split_info);
   _mesa_hash_table_destroy(split_mats, NULL);
   ralloc_free(mem_ctx);
   return progress;
}

bool
nir_lower_cooperative_matrix_flexible_dimensions(nir_shader *shader,
                                                 const struct nir_lower_coopmat_args *args)
{
   bool progress = false;

   if (!shader->info.cs.has_cooperative_matrix)
      return false;

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   progress |= lower_dimensions(shader, impl, args);

   nir_foreach_function_impl(fnim, shader)
      nir_progress(progress, fnim, 0);
   return progress;
}

/*
 * Cooperative block loads have a decode function.
 * It can take either a Buffer or PhysicalBuffer,
 * but only expects a PhysicalBuffer.
 * This helps finds the cast deref that casts the
 * result of load_param(1).
 */
static nir_deref_instr *
find_decode_deref(nir_function_impl *impl)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_deref) {
            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (deref->deref_type != nir_deref_type_cast)
               continue;

            if (!nir_def_is_intrinsic(deref->parent.ssa))
               continue;

            nir_intrinsic_instr *parent_intrin = nir_def_as_intrinsic(deref->parent.ssa);
            if (parent_intrin->intrinsic != nir_intrinsic_load_param)
               continue;

            if (nir_intrinsic_param_idx(parent_intrin) != 1)
               continue;

            return deref;
         }
      }
   }
   return NULL;
}

void
nir_calc_tensor_derefs_init(nir_builder *b, struct nir_calc_tensor_info *info,
                            nir_cmat_call_instr *call)
{
   struct nir_cmat_tensor_load tensor_load = nir_cmat_call_tensor_load_info(call);
   nir_deref_instr *layout = nir_src_as_deref(call->params[2]);
   nir_tensor_clamp_mode clamp_mode = tensor_load.layout_clamp_mode;
   struct glsl_cmat_description orig_desc = nir_cmat_call_cmat_desc(call);
   bool is_store = call->op == nir_cmat_call_op_tensor_store;
   nir_deref_instr *cmat_deref = nir_src_as_deref(call->params[is_store]);
   struct glsl_cmat_description desc = *glsl_get_cmat_description(cmat_deref->type);
   bool add_clamp = false;

   info->row_imm_offset = tensor_load.split_row_index * desc.rows;
   info->col_imm_offset = tensor_load.split_col_index * desc.cols;
   info->desc = desc;
   info->view = NULL;
   if (tensor_load.tensor_view) {
      info->view = nir_src_as_deref(call->params[3]);
      for (unsigned p = 0; p < NIR_TENSOR_VIEW_MAX_PERMUTATIONS; p++)
         info->view_permutations[p] = tensor_load.view_permutations[p];
      info->view_has_dims = tensor_load.view_has_dims;
   }

   info->cols = orig_desc.cols;

   info->decode_fnptr = call->callee;

   if (is_store && clamp_mode != NIR_TENSOR_CLAMP_UNDEFINED)
      clamp_mode = NIR_TENSOR_CLAMP_CONSTANT;

   add_clamp = clamp_mode != NIR_TENSOR_CLAMP_UNDEFINED;
   info->clamp_mode = clamp_mode;
   if (info->clamp_mode == NIR_TENSOR_CLAMP_CONSTANT)
      info->clamp_value = nir_load_struct_field(b, layout, NIR_TENSOR_LAYOUT_CLAMP_VALUE);
   info->offsets = nir_load_struct_field(b, layout, NIR_TENSOR_LAYOUT_OFFSET);
   info->block_sizes = nir_load_struct_field(b, layout, NIR_TENSOR_LAYOUT_BLOCKSIZE);
   info->layout_dims = add_clamp ? nir_load_struct_field(b, layout, NIR_TENSOR_LAYOUT_LAYOUT_DIM) : NULL;
   info->spans = nir_load_struct_field(b, layout, NIR_TENSOR_LAYOUT_SPAN);
   info->strides = nir_load_struct_field(b, layout, NIR_TENSOR_LAYOUT_STRIDE);

   if (info->view) {
      info->clip_row_offset = nir_load_struct_field(b, info->view, NIR_TENSOR_VIEW_CLIP_ROW_OFFSET);
      info->clip_col_offset = nir_load_struct_field(b, info->view, NIR_TENSOR_VIEW_CLIP_COL_OFFSET);
      info->clip_col_span = nir_load_struct_field(b, info->view, NIR_TENSOR_VIEW_CLIP_COL_SPAN);
      info->clip_row_span = nir_load_struct_field(b, info->view, NIR_TENSOR_VIEW_CLIP_ROW_SPAN);

      if (info->view_has_dims) {
         info->view_dims = nir_load_struct_field(b, info->view, NIR_TENSOR_VIEW_DIM);
         info->view_strides = nir_load_struct_field(b, info->view, NIR_TENSOR_VIEW_STRIDE);
      } else {
         info->view_dims = info->spans;
         info->view_strides = NULL;
      }
   }
}

static nir_def *
nir_calc_tensor_view_linear(nir_builder *b,
                            struct nir_calc_tensor_info *info,
                            nir_def *row, nir_def *col)
{
   nir_def *clipped_row = nir_ior(b,
                                  nir_ult(b, row, info->clip_row_offset),
                                  nir_uge(b, row, nir_iadd(b, info->clip_row_span, info->clip_row_offset)));
   nir_def *clipped_col = nir_ior(b,
                                  nir_ult(b, col, info->clip_col_offset),
                                  nir_uge(b, col, nir_iadd(b, info->clip_col_span, info->clip_col_offset)));
   nir_def *unclipped = nir_inot(b, nir_ior(b, clipped_row, clipped_col));

   info->clipped_if = nir_push_if(b, unclipped);

   row = nir_isub(b, row, info->clip_row_offset);
   col = nir_isub(b, col, info->clip_col_offset);

   /* matrixCoordToLinear */

   nir_def *width = nir_umin(b, nir_imm_int(b, info->cols), info->clip_col_span);
   nir_def *idx = nir_iadd(b, nir_imul(b, row, width), col);

   int nc = info->view_dims->num_components;
   nir_def *view_coord[5];
   for (int dim = nc - 1; dim >= 0; dim--) {
      uint32_t i = info->view_permutations[dim];
      nir_def *view_dim = nir_channel(b, info->view_dims, i);
      view_coord[i] = nir_umod(b, idx, view_dim);
      idx = nir_udiv(b, idx, view_dim);
   }

   /* viewCoordToLinear */
   nir_def *stride[5];
   if (info->view_strides) {
      for (int dim = 0; dim < nc; dim++) {
         stride[dim] = nir_channel(b, info->view_strides, dim);
      }
   } else {
      stride[nc - 1] = nir_imm_int(b, 1);
      for (int dim = nc - 2; dim >= 0; --dim) {
         stride[dim] = nir_imul(b, stride[dim + 1], nir_channel(b, info->spans, dim + 1));
      }
   }

   idx = nir_imm_int(b, 0);
   for (int dim = nc - 1; dim >= 0; --dim) {
      idx = nir_iadd(b, idx, nir_imul(b, view_coord[dim], stride[dim]));
   }
   return idx;
}

nir_def *
nir_calc_tensor_derefs(nir_builder *b, struct nir_calc_tensor_info *info,
                       nir_def *row, nir_def *col,
                       nir_deref_instr **iter_deref_p)
{
   nir_def *idx;
   nir_def *span_coord[5];

   col = nir_iadd_imm(b, col, info->col_imm_offset);
   row = nir_iadd_imm(b, row, info->row_imm_offset);

   nir_deref_instr *iter_deref = *iter_deref_p;
   iter_deref = nir_build_deref_cast(b, &iter_deref->def, iter_deref->modes,
                                     glsl_scalar_type(info->desc.element_type),
                                     glsl_base_type_bit_size(info->desc.element_type) / 8);

   if (info->view) {
      idx = nir_calc_tensor_view_linear(b, info, row, col);
   } else {
      /* matrixCoordToLinear */
      idx = nir_iadd(b, nir_imul_imm(b, row, info->cols), col);
   }

   /* linearToSpanCoord */
   int nc = info->spans->num_components;

   for (int dim = nc - 1; dim >= 0; dim--) {
      nir_def *span_dim = nir_channel(b, info->spans, dim);
      span_coord[dim] = nir_umod(b, idx, span_dim);
      idx = nir_udiv(b, idx, span_dim);
   }

   /* spanCoordToTensorCoord */
   nir_def *block_coord[5];
   nir_def *coord_in_block[5];

   info->do_clamp = nir_imm_false(b);
   for (unsigned dim = 0; dim < nc; dim++) {
      nir_def *c = nir_iadd(b, span_coord[dim], nir_channel(b, info->offsets, dim));

      nir_def *this_clamp;
      if (info->clamp_mode != NIR_TENSOR_CLAMP_UNDEFINED) {
         this_clamp = nir_ior(b, nir_ilt_imm(b, c, 0), nir_ige(b, c, nir_channel(b, info->layout_dims, dim)));
         info->do_clamp = nir_ior(b, info->do_clamp, this_clamp);
      } else
         this_clamp = nir_imm_false(b);

      nir_def *c_clamped = c;
      switch (info->clamp_mode) {
      case NIR_TENSOR_CLAMP_UNDEFINED:
      case NIR_TENSOR_CLAMP_CONSTANT:
         c_clamped = nir_imm_int(b, 0);
         break;
      case NIR_TENSOR_CLAMP_EDGE:
         c_clamped = nir_imin(b, nir_imax_imm(b, c, 0), nir_iadd_imm(b, nir_channel(b, info->layout_dims, dim), -1));
         break;
      case NIR_TENSOR_CLAMP_REPEAT:
         c_clamped = nir_imod(b, c, nir_channel(b, info->layout_dims, dim));
         break;
      case NIR_TENSOR_CLAMP_REPEAT_MIRRORED: {
         nir_def *dimX = nir_channel(b, info->layout_dims, dim);
         c_clamped = nir_imod(b, c, nir_iadd_imm(b, nir_imul_imm(b, dimX, 2), -2));
         nir_def *val = nir_isub(b, nir_iadd_imm(b, nir_imul_imm(b, dimX, 2), -2), c_clamped);
         c_clamped = nir_bcsel(b, nir_ige(b, c_clamped, dimX), val, c_clamped);
         break;
      }
      }

      if (info->clamp_mode != NIR_TENSOR_CLAMP_UNDEFINED)
         c = nir_bcsel(b, this_clamp, c_clamped, c);
      if (info->decode_fnptr)
         coord_in_block[dim] = nir_umod(b, c, nir_channel(b, info->block_sizes, dim));
      block_coord[dim] = nir_udiv(b, c, nir_channel(b, info->block_sizes, dim));
   }

   /* tensorCoordToLinear */
   idx = nir_imm_int(b, 0);
   for (unsigned dim = 0; dim < nc; dim++) {
      idx = nir_iadd(b, idx, nir_imul(b, block_coord[dim], nir_channel(b, info->strides, dim)));
   }

   if (info->decode_fnptr) {
      nir_variable *decode_out_tmp = nir_local_variable_create(b->impl, glsl_get_bare_type(info->decode_fnptr->params[0].type), "decode_tmp");
      nir_deref_instr *decode_out_tmp_deref = nir_build_deref_var(b, decode_out_tmp);
      nir_call_instr *call = nir_call_instr_create(b->shader, info->decode_fnptr);
      nir_deref_instr *param_deref = find_decode_deref(info->decode_fnptr->impl);
      /*
       * The first parameter must be a pointer type with storage class PhysicalStorageBuffer,
       * and the parameter is filled a pointer computed by multiplying the index returned by
       * matrixCoordToTensorElement(WithView) by the size of the pointee type.
       */
      nir_def *idx_def;
      if (param_deref) {
         nir_deref_instr *idx_deref = nir_build_deref_cast(b, &iter_deref->def,
                                                           iter_deref->modes,
                                                           param_deref->type,
                                                           glsl_get_explicit_size(param_deref->type, true));
         idx_deref = nir_build_deref_ptr_as_array(b, idx_deref, nir_u2uN(b, idx, iter_deref->def.bit_size));
         idx_def = &idx_deref->def;
      } else
         idx_def = nir_undef(b, info->decode_fnptr->params[1].num_components,
                             info->decode_fnptr->params[1].bit_size);

      call->params[0] = nir_src_for_ssa(&decode_out_tmp_deref->def);
      call->params[1] = nir_src_for_ssa(idx_def);
      for (unsigned dim = 0; dim < nc; dim++) {
         call->params[2 + dim] = nir_src_for_ssa(block_coord[dim]);
         call->params[2 + nc + dim] = nir_src_for_ssa(coord_in_block[dim]);
      }
      nir_builder_instr_insert(b, &call->instr);
      idx = nir_load_deref(b, decode_out_tmp_deref);
   } else {
      *iter_deref_p = nir_build_deref_ptr_as_array(b, iter_deref, nir_u2uN(b, idx, iter_deref->def.bit_size));
   }

   return idx;
}

