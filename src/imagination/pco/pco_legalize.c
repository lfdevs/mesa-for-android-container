/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_legalize.c
 *
 * \brief PCO legalizing pass.
 */

#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"

#include <stdbool.h>

static inline bool ref_needs_olchk(pco_ref ref)
{
   if (!pco_ref_is_reg(ref))
      return false;

   switch (pco_ref_get_reg_class(ref)) {
   case PCO_REG_CLASS_PIXOUT:
      return true;

   case PCO_REG_CLASS_SPEC:
      return ref.val == PCO_SR_OUTPUT_PART ||
             (ref.val >= PCO_SR_TILED_LD_COMP0 &&
              ref.val <= PCO_SR_TILED_ST_COMP3) ||
             (ref.val >= PCO_SR_TILED_LD_COMP4 &&
              ref.val <= PCO_SR_TILED_ST_COMP7);

   default:
      break;
   }

   return false;
}

/**
 * \brief Insert a mov to legalize how a hardware register is referenced.
 *
 * \param[in,out] instr PCO instr.
 * \param[in,out] ref Reference to be legalized.
 * \param[in] needs_s124 Whether the mapping needs to use S{1,2,4}
 *                       rather than S{0,2,3}.
 * \return True if progress was made.
 */
static void insert_mov_ref(pco_instr *instr, pco_ref *ref, bool needs_s124)
{
   if (instr->op == PCO_OP_MBYP && needs_s124 && !pco_ref_has_mods_set(*ref)) {
      instr->op = PCO_OP_MOVS1;
      return;
   } else if (instr->op == PCO_OP_MOVS1 && !needs_s124) {
      instr->op = PCO_OP_MBYP;
      return;
   }

   assert(pco_ref_is_scalar(*ref));
   pco_ref new_ref = pco_ref_new_ssa(instr->parent_func,
                                     pco_ref_get_bits(*ref),
                                     pco_ref_get_chans(*ref));

   pco_ref_xfer_mods(&new_ref, ref, true);

   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

   enum pco_exec_cnd exec_cnd = pco_instr_get_exec_cnd(instr);
   pco_instr *mov_instr;
   if (needs_s124)
      mov_instr = pco_movs1(&b, new_ref, *ref, .exec_cnd = exec_cnd);
   else
      mov_instr = pco_mbyp(&b, new_ref, *ref, .exec_cnd = exec_cnd);

   if (pco_instr_has_olchk(instr) && pco_instr_get_olchk(instr) &&
       ref_needs_olchk(*ref)) {
      assert(pco_instr_has_olchk(mov_instr));
      pco_instr_set_olchk(mov_instr, true);
      pco_instr_set_olchk(instr, false);
   }

   *ref = new_ref;
}

/**
 * \brief Try to legalize an instruction's hardware source mappings.
 *
 * \param[in,out] instr PCO instr.
 * \param[in] info PCO op info.
 * \return True if progress was made.
 */
static bool try_legalize_src_mappings(pco_instr *instr,
                                      const struct pco_op_info *info)
{
   bool progress = false;
   bool needs_s124;

   /* Check dests. */
   pco_foreach_instr_dest (pdest, instr) {
      unsigned dest_index = pdest - instr->dest;
      if (!info->dest_intrn_map[dest_index])
         continue;

      enum pco_io mapped_src = PCO_IO_S0 + info->dest_intrn_map[dest_index] - 1;

      if (ref_src_map_valid(*pdest, mapped_src, &needs_s124))
         continue;

      insert_mov_ref(instr, pdest, needs_s124);
      progress = true;
   }

   /* Check srcs. */
   pco_foreach_instr_src (psrc, instr) {
      unsigned src_index = psrc - instr->src;
      if (!info->src_intrn_map[src_index])
         continue;

      enum pco_io mapped_src = PCO_IO_S0 + info->src_intrn_map[src_index] - 1;

      if (ref_src_map_valid(*psrc, mapped_src, &needs_s124))
         continue;

      insert_mov_ref(instr, psrc, needs_s124);
      progress = true;
   }

   return progress;
}

static inline bool xfer_op_mods(pco_instr *dest, pco_instr *src)
{
   bool all_xfered = true;

   for (enum pco_op_mod mod = PCO_OP_MOD_NONE + 1; mod < _PCO_OP_MOD_COUNT;
        ++mod) {
      bool dest_has_mod = pco_instr_has_mod(dest, mod);
      bool src_has_mod = pco_instr_has_mod(src, mod);

      if (!dest_has_mod && !src_has_mod)
         continue;

      if (dest_has_mod != src_has_mod) {
         all_xfered = false;
         continue;
      }

      pco_instr_set_mod(dest, mod, pco_instr_get_mod(src, mod));
   }

   return all_xfered;
}

/**
 * \brief Legalize fence pseudo instruction.
 *
 * \param[in,out] instr PCO instr.
 * \return True if progress was made.
 */
static bool legalize_fence(pco_instr *instr)
{
   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_after_instr(instr));

   if (pco_is_last_instr(instr)) {
      pco_nop(&b);
      b.cursor = pco_cursor_after_instr(instr);
   }

   pco_flush_p0(&b);
   pco_br_next(&b, .exec_cnd = PCO_EXEC_CND_E1_Z1);
   pco_br_next(&b, .exec_cnd = PCO_EXEC_CND_E1_Z0);

   pco_instr_delete(instr);

   return true;
}

/**
 * \brief Legalize mov_offset pseudo-instruction.
 *
 * \param[in,out] instr PCO instr.
 * \return True if progress was made.
 */
static bool legalize_mov_offset(pco_instr *instr)
{
   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

   pco_ref dest = instr->dest[0];
   pco_ref src = instr->src[0];
   pco_ref offset = instr->src[1];

   bool is_src = pco_instr_get_offset_sd(instr) == PCO_OFFSET_SD_SRC;
   unsigned base = pco_ref_get_reg_index(is_src ? src : dest);
   bool is_large = !(base <= ROGUE_MAX_REG_OFFSET);

   enum pco_exec_cnd exec_cnd = pco_instr_get_exec_cnd(instr);

   unsigned idx_reg_num = 0;
   pco_ref idx_reg =
      pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

   if (!is_large) {
      pco_mbyp(&b, idx_reg, offset, .exec_cnd = exec_cnd);
   } else {
      pco_movi32(&b, idx_reg, pco_ref_imm32(base), .exec_cnd = exec_cnd);
      pco_iadd32(&b,
                 idx_reg,
                 idx_reg,
                 offset,
                 pco_ref_null(),
                 .exec_cnd = exec_cnd);

      if (is_src)
         src = pco_ref_set_reg_index(src, 0u);
      else
         dest = pco_ref_set_reg_index(dest, 0u);
   }

   if (is_src)
      src = pco_ref_hwreg_idx_from(idx_reg_num, src);
   else
      dest = pco_ref_hwreg_idx_from(idx_reg_num, dest);

   pco_instr *mbyp = pco_ref_is_reg(src) &&
                           pco_ref_get_reg_class(src) == PCO_REG_CLASS_SPEC
                        ? pco_movs1(&b, dest, src, .exec_cnd = exec_cnd)
                        : pco_mbyp(&b, dest, src, .exec_cnd = exec_cnd);

   xfer_op_mods(mbyp, instr);

   pco_instr_delete(instr);

   return true;
}

/**
 * \brief Legalize dynamic index pseudo-instruction.
 *
 * \param[in,out] instr PCO instr.
 * \return True if progress was made.
 */
static bool legalize_dynidx(pco_instr *instr)
{
   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

   pco_ref dest0 = instr->dest[0];
   pco_ref dest1 = instr->dest[1];

   pco_ref src0 = instr->src[0];
   pco_ref src1 = instr->src[1];
   assert(pco_ref_is_hwreg(src0));
   assert(pco_ref_is_hwreg(src1) || pco_ref_is_null(src1));

   unsigned src0_base = pco_ref_get_reg_index(src0);
   unsigned src1_base = pco_ref_is_null(src1) ? 0u
                                              : pco_ref_get_reg_index(src1);

   assert(pco_ref_is_null(src1) == pco_ref_is_null(dest1));

   bool two_srcs = !pco_ref_is_null(src1);
   unsigned rpt = pco_instr_get_rpt(instr);
   assert(rpt < 2 || !two_srcs);

   pco_ref elem = instr->src[2];
   pco_ref stride = instr->src[3];

   unsigned idx_reg_num = 0;
   pco_ref idx_reg =
      pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

   if (src0_base <= ROGUE_MAX_REG_OFFSET && src1_base <= ROGUE_MAX_REG_OFFSET) {
      pco_imul32(&b,
                 idx_reg,
                 elem,
                 stride,
                 pco_ref_null(),
                 .exec_cnd = pco_instr_get_exec_cnd(instr));
   } else {
      unsigned src_base = two_srcs ? MIN2(src0_base, src1_base) : src0_base;
      assert((!two_srcs ||
              (MAX2(src0_base, src1_base) - MIN2(src0_base, src1_base)) <=
                 ROGUE_MAX_REG_OFFSET) &&
             "Need to use two index registers!");

      pco_movi32(&b,
                 idx_reg,
                 pco_ref_imm32(src_base),
                 .exec_cnd = pco_instr_get_exec_cnd(instr));
      pco_imadd32(&b,
                  idx_reg,
                  elem,
                  stride,
                  idx_reg,
                  pco_ref_null(),
                  .exec_cnd = pco_instr_get_exec_cnd(instr));

      src0 = pco_ref_set_reg_index(src0, src0_base - src_base);
      if (two_srcs)
         src1 = pco_ref_set_reg_index(src1, src1_base - src_base);
   }

   src0 = pco_ref_hwreg_idx_from(idx_reg_num, src0);

   if (two_srcs) {
      src1 = pco_ref_hwreg_idx_from(idx_reg_num, src1);
      pco_mbyp2(&b,
                dest0,
                dest1,
                src0,
                src1,
                .exec_cnd = pco_instr_get_exec_cnd(instr));
   } else {
      pco_mbyp(&b,
               dest0,
               src0,
               .exec_cnd = pco_instr_get_exec_cnd(instr),
               .rpt = rpt);
   }

   pco_instr_delete(instr);

   return true;
}

/**
 * \brief Legalize sample dynamic index pseudo-instruction.
 *
 * \param[in,out] instr PCO instr.
 * \return True if progress was made.
 */
static bool legalize_smp_dynidx(pco_instr *instr)
{
   enum pco_exec_cnd exec_cnd = pco_instr_get_exec_cnd(instr);
   bool wrt = instr->op == PCO_OP_SMP_WRT_DYNIDX;

   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

   unsigned tex_idx_reg_num = 0;

   pco_ref *tex_state = &instr->src[1];
   assert(pco_ref_is_hwreg(*tex_state));
   unsigned tex_state_base = pco_ref_get_reg_index(*tex_state);
   bool tex_state_base_large = !(tex_state_base <= ROGUE_MAX_REG_OFFSET);

   pco_ref *tex_state_elem = &instr->src[6];
   pco_ref *tex_state_stride = &instr->src[7];
   assert(pco_ref_is_null(*tex_state_elem) ==
          pco_ref_is_null(*tex_state_stride));

   pco_ref tex_idx_reg =
      pco_ref_hwreg_idx(tex_idx_reg_num, tex_idx_reg_num, PCO_REG_CLASS_INDEX);

   if (!pco_ref_is_null(*tex_state_elem)) {
      if (!tex_state_base_large) {
         pco_imul32(&b,
                    tex_idx_reg,
                    *tex_state_elem,
                    *tex_state_stride,
                    pco_ref_null(),
                    .exec_cnd = exec_cnd);
      } else {
         pco_movi32(&b,
                    tex_idx_reg,
                    pco_ref_imm32(tex_state_base),
                    .exec_cnd = exec_cnd);
         pco_imadd32(&b,
                     tex_idx_reg,
                     *tex_state_elem,
                     *tex_state_stride,
                     tex_idx_reg,
                     pco_ref_null(),
                     .exec_cnd = exec_cnd);
         *tex_state = pco_ref_set_reg_index(*tex_state, 0u);
      }

      *tex_state = pco_ref_hwreg_idx_from(tex_idx_reg_num, *tex_state);

      *tex_state_elem = pco_ref_null();
      *tex_state_stride = pco_ref_null();
   } else if (tex_state_base_large) {
      pco_movi32(&b,
                 tex_idx_reg,
                 pco_ref_imm32(tex_state_base),
                 .exec_cnd = exec_cnd);
      *tex_state = pco_ref_set_reg_index(*tex_state, 0u);
      *tex_state = pco_ref_hwreg_idx_from(tex_idx_reg_num, *tex_state);
   }

   /**/

   unsigned smp_idx_reg_num = 1;

   pco_ref *smp_state = &instr->src[3];
   assert(pco_ref_is_hwreg(*smp_state));
   unsigned smp_state_base = pco_ref_get_reg_index(*smp_state);
   bool smp_state_base_large = !(smp_state_base <= ROGUE_MAX_REG_OFFSET);

   pco_ref *smp_state_elem = &instr->src[8];
   pco_ref *smp_state_stride = &instr->src[9];
   assert(pco_ref_is_null(*smp_state_elem) ==
          pco_ref_is_null(*smp_state_stride));

   pco_ref smp_idx_reg =
      pco_ref_hwreg_idx(smp_idx_reg_num, smp_idx_reg_num, PCO_REG_CLASS_INDEX);

   if (!pco_ref_is_null(*smp_state_elem)) {
      if (!smp_state_base_large) {
         pco_imul32(&b,
                    smp_idx_reg,
                    *smp_state_elem,
                    *smp_state_stride,
                    pco_ref_null(),
                    .exec_cnd = exec_cnd);
      } else {
         pco_movi32(&b,
                    smp_idx_reg,
                    pco_ref_imm32(smp_state_base),
                    .exec_cnd = exec_cnd);
         pco_imadd32(&b,
                     smp_idx_reg,
                     *smp_state_elem,
                     *smp_state_stride,
                     smp_idx_reg,
                     pco_ref_null(),
                     .exec_cnd = exec_cnd);
         *smp_state = pco_ref_set_reg_index(*smp_state, 0u);
      }

      *smp_state = pco_ref_hwreg_idx_from(smp_idx_reg_num, *smp_state);

      *smp_state_elem = pco_ref_null();
      *smp_state_stride = pco_ref_null();
   } else if (smp_state_base_large) {
      pco_movi32(&b,
                 smp_idx_reg,
                 pco_ref_imm32(smp_state_base),
                 .exec_cnd = exec_cnd);
      *smp_state = pco_ref_set_reg_index(*smp_state, 0u);
      *smp_state = pco_ref_hwreg_idx_from(smp_idx_reg_num, *smp_state);
   }

   instr->op = wrt ? PCO_OP_SMP_WRT : PCO_OP_SMP;
   instr->num_srcs = 6u;

   return true;
}

/**
 * \brief Legalize atomic offset pseudo-instruction.
 *
 * \param[in,out] instr PCO instr.
 * \return True if progress was made.
 */
static bool legalize_atomic_offset(pco_instr *instr)
{
   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

   pco_ref dest = instr->dest[0];
   pco_ref shmem_dest = instr->dest[1];

   pco_ref shmem_src = instr->src[0];
   pco_ref value = instr->src[1];
   pco_ref value_swap = instr->src[2];
   pco_ref offset = instr->src[3];

   assert(pco_refs_are_equal(shmem_dest, shmem_src, true));

   unsigned base = pco_ref_get_reg_index(shmem_src);
   bool is_large = !(base <= ROGUE_MAX_REG_OFFSET);

   enum pco_exec_cnd exec_cnd = pco_instr_get_exec_cnd(instr);

   unsigned idx_reg_num = 0;
   pco_ref idx_reg =
      pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

   if (!is_large) {
      pco_mbyp(&b, idx_reg, offset, .exec_cnd = exec_cnd);
   } else {
      pco_movi32(&b, idx_reg, pco_ref_imm32(base), .exec_cnd = exec_cnd);
      pco_iadd32(&b,
                 idx_reg,
                 idx_reg,
                 offset,
                 pco_ref_null(),
                 .exec_cnd = exec_cnd);

      shmem_src = pco_ref_set_reg_index(shmem_src, 0u);
      shmem_dest = pco_ref_set_reg_index(shmem_dest, 0u);
   }

   shmem_dest = pco_ref_hwreg_idx_from(idx_reg_num, shmem_dest);
   shmem_src = pco_ref_hwreg_idx_from(idx_reg_num, shmem_src);

   pco_instr *repl;
   enum pco_atom_op atom_op = pco_instr_get_atom_op(instr);
   switch (atom_op) {
   case PCO_ATOM_OP_ADD:
      assert(pco_ref_is_null(value_swap));
      repl = pco_iadd32_atomic(&b,
                               dest,
                               shmem_dest,
                               shmem_src,
                               value,
                               pco_ref_null(),
                               .s = true);
      break;

   case PCO_ATOM_OP_XCHG:
      assert(pco_ref_is_null(value_swap));
      repl = pco_xchg_atomic(&b, dest, shmem_dest, shmem_src, value);
      break;

   case PCO_ATOM_OP_CMPXCHG:
      assert(!pco_ref_is_null(value_swap));
      repl = pco_cmpxchg_atomic(&b,
                                dest,
                                shmem_dest,
                                shmem_src,
                                value,
                                value_swap,
                                .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      break;

   case PCO_ATOM_OP_UMIN:
      assert(pco_ref_is_null(value_swap));
      repl = pco_min_atomic(&b,
                            dest,
                            shmem_dest,
                            shmem_src,
                            value,
                            .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      break;

   case PCO_ATOM_OP_IMIN:
      assert(pco_ref_is_null(value_swap));
      repl = pco_min_atomic(&b,
                            dest,
                            shmem_dest,
                            shmem_src,
                            value,
                            .tst_type_main = PCO_TST_TYPE_MAIN_S32);
      break;

   case PCO_ATOM_OP_UMAX:
      assert(pco_ref_is_null(value_swap));
      repl = pco_max_atomic(&b,
                            dest,
                            shmem_dest,
                            shmem_src,
                            value,
                            .tst_type_main = PCO_TST_TYPE_MAIN_U32);
      break;

   case PCO_ATOM_OP_IMAX:
      assert(pco_ref_is_null(value_swap));
      repl = pco_max_atomic(&b,
                            dest,
                            shmem_dest,
                            shmem_src,
                            value,
                            .tst_type_main = PCO_TST_TYPE_MAIN_S32);
      break;

   case PCO_ATOM_OP_AND:
      assert(pco_ref_is_null(value_swap));
      repl = pco_logical_atomic(&b,
                                dest,
                                shmem_dest,
                                shmem_src,
                                value,
                                .logiop = PCO_LOGIOP_AND);
      break;

   case PCO_ATOM_OP_OR:
      assert(pco_ref_is_null(value_swap));
      repl = pco_logical_atomic(&b,
                                dest,
                                shmem_dest,
                                shmem_src,
                                value,
                                .logiop = PCO_LOGIOP_OR);
      break;

   case PCO_ATOM_OP_XOR:
      assert(pco_ref_is_null(value_swap));
      repl = pco_logical_atomic(&b,
                                dest,
                                shmem_dest,
                                shmem_src,
                                value,
                                .logiop = PCO_LOGIOP_XOR);
      break;

   default:
      UNREACHABLE("");
   }

   xfer_op_mods(repl, instr);

   pco_instr_delete(instr);

   return true;
}

static bool legalize_pseudo_post_ra(pco_instr *instr)
{
   switch (instr->op) {
   case PCO_OP_FENCE:
      return legalize_fence(instr);

   case PCO_OP_MOV:
      if (pco_ref_is_reg(instr->src[0]) &&
          pco_ref_get_reg_class(instr->src[0]) == PCO_REG_CLASS_SPEC)
         instr->op = PCO_OP_MOVS1;
      else
         instr->op = PCO_OP_MBYP;

      return true;

   case PCO_OP_MOV_OFFSET:
      return legalize_mov_offset(instr);

   case PCO_OP_DYNIDX:
      return legalize_dynidx(instr);

   case PCO_OP_SMP_DYNIDX:
   case PCO_OP_SMP_WRT_DYNIDX:
      return legalize_smp_dynidx(instr);

   case PCO_OP_OP_ATOMIC_OFFSET:
      return legalize_atomic_offset(instr);

   case PCO_OP_FLUSH_DMA: {
      pco_builder b =
         pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

      pco_ref dest = instr->dest[0];
      pco_ref addr = instr->src[0];

      unsigned chans = pco_ref_get_chans(dest);
      assert(chans == 1);

      pco_ld(&b, dest, pco_ref_drc(PCO_DRC_0), pco_ref_imm8(chans), addr);
      pco_wdf(&b, pco_ref_drc(PCO_DRC_0));

      pco_instr_delete(instr);
      return true;
   }

   default:
      break;
   }

   return false;
}

static bool legalize_vote(pco_instr *instr)
{
   ASSERTED enum pco_exec_cnd exec_cnd = pco_instr_get_exec_cnd(instr);
   assert(exec_cnd == PCO_EXEC_CND_E1_ZX);

   enum pco_vote_op vote_op = pco_instr_get_vote_op(instr);
   bool is_all = vote_op == PCO_VOTE_OP_ALL;

   pco_ref dest = instr->dest[0];
   pco_ref value = instr->src[0];

   pco_func *func = instr->parent_func;
   pco_ref result = pco_ref_new_vreg(func);

   pco_builder b =
      pco_builder_create(func, pco_cursor_after_instr(instr));

   /* result = false */
   pco_mbyp(&b, result, pco_false);

   /* p0 = !value */
   pco_tstz(&b,
            pco_ref_null(),
            pco_ref_pred(PCO_PRED_P0),
            value,
            .tst_type_main = PCO_TST_TYPE_MAIN_U32);

   pco_ref emc = pco_emc_ref(func, &b);

   /* Mask instance if (all ? value : !value) */
   pco_cndst(&b,
             pco_ref_pred(PCO_PRED_PE),
             emc,
             emc,
             pco_ref_imm8(1),
             .exec_cnd = PCO_EXEC_CND_EX_ZX,
             .cnd = is_all ? PCO_CND_P0_TRUE : PCO_CND_P0_FALSE);

   /* all: if no instances are running, i.e. value is true for all of them
    * any: if at least one instance is running, i.e. value is true for at least
    *      one of them
    *
    * If the condition is true, skip to result = true instruction.
    */
   pco_br_skip_next(&b, .branch_cnd = is_all ? PCO_BRANCH_CND_ALLINST : PCO_BRANCH_CND_ANYINST);

   /* Skip the result = true instruction (if the above branch isn't taken). */
   pco_br_skip_next(&b);

   /* result = true
    * Execute even if the instance is masked out (for the all case).
    * This will only not execute if the instruction is skipped by a branch.
    */
   pco_mbyp(&b, result, pco_true, .exec_cnd = PCO_EXEC_CND_EX_ZX);

   /* Restore emc. */
   pco_cndend(&b,
              pco_ref_pred(PCO_PRED_PE),
              emc,
              emc,
              pco_ref_imm8(1),
              .exec_cnd = PCO_EXEC_CND_EX_ZX);

   /* Copy the result to the destination. */
   /* TODO: just replace it in its users instead. */
   pco_mbyp(&b, dest, result);

   pco_instr_delete(instr);

   return true;
}

static bool legalize_pseudo_pre_ra(pco_instr *instr)
{
   switch (instr->op) {
   case PCO_OP_VOTE:
      return legalize_vote(instr);

   default:
      break;
   }

   return false;
}

static bool legalize_pseudo(pco_instr *instr, bool pre_ra)
{
   return pre_ra ?
      legalize_pseudo_pre_ra(instr) :
      legalize_pseudo_post_ra(instr);
}

static bool try_legalize_large_hwreg_offsets(pco_instr *instr,
                                             const struct pco_op_info *info)
{
   switch (instr->op) {
   /* Skip, will be handled. */
   case PCO_OP_DYNIDX:
   case PCO_OP_SMP_DYNIDX:
   case PCO_OP_SMP_WRT_DYNIDX:
   case PCO_OP_MOV_OFFSET:
   case PCO_OP_OP_ATOMIC_OFFSET:
      return false;

   default:
      break;
   }

   unsigned large_hwreg_count = 0;
   pco_ref *large_hwregs[_PCO_OP_MAX_SRCS + _PCO_OP_MAX_DESTS] = { 0 };

   enum pco_reg_class hwreg_class = ~0;
   unsigned min_large_offset = ~0;
   ASSERTED unsigned max_large_offset = 0;

   /* Check dests. */
   pco_foreach_instr_dest_hwreg (pdest, instr) {
      if (pco_ref_get_reg_index(*pdest) <= ROGUE_MAX_REG_OFFSET)
         continue;

      large_hwregs[large_hwreg_count++] = pdest;
      assert(hwreg_class == ~0 || hwreg_class == pco_ref_get_reg_class(*pdest));
      hwreg_class = pco_ref_get_reg_class(*pdest);

      min_large_offset = MIN2(min_large_offset, pco_ref_get_reg_index(*pdest));
      max_large_offset = MAX2(max_large_offset, pco_ref_get_reg_index(*pdest));
   }

   /* Check srcs. */
   pco_foreach_instr_src_hwreg (psrc, instr) {
      if (pco_ref_get_reg_index(*psrc) <= ROGUE_MAX_REG_OFFSET)
         continue;

      large_hwregs[large_hwreg_count++] = psrc;
      assert(hwreg_class == ~0 || hwreg_class == pco_ref_get_reg_class(*psrc));
      hwreg_class = pco_ref_get_reg_class(*psrc);

      min_large_offset = MIN2(min_large_offset, pco_ref_get_reg_index(*psrc));
      max_large_offset = MAX2(max_large_offset, pco_ref_get_reg_index(*psrc));
   }

   if (!large_hwreg_count)
      return false;

   /* We'd need more than one indexed register to support this. */
   assert((max_large_offset - min_large_offset) <= ROGUE_MAX_REG_OFFSET);

   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

   unsigned idx_reg_num = 0;
   pco_ref idx_reg =
      pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

   pco_ref imm = pco_ref_imm32(min_large_offset);
   pco_movi32(&b, idx_reg, imm, .exec_cnd = pco_instr_get_exec_cnd(instr));

   /* Remap the offset for each large hwreg and replace it with the indexed
    * register.
    */
   for (unsigned u = 0; u < large_hwreg_count; ++u) {
      *large_hwregs[u] = pco_ref_offset(*large_hwregs[u], -min_large_offset);
      *large_hwregs[u] = pco_ref_hwreg_idx_from(idx_reg_num, *large_hwregs[u]);
   }

   return true;
}

/**
 * \brief Check whether a DITR/DITRP instruction requires a pre-fence.
 *
 * Reverse-iterates from the DITR/DITRP back to the previous fence pseudo-op
 * (or the start of the function) and returns true if any instruction in that
 * range uses a register that is also used by the DITR/DITRP destination.
 *
 * \param[in] instr DITR or DITRP PCO instr.
 * \return True if a pre-fence must be inserted before the DITR/DITRP.
 */
static bool ditr_needs_pre_fence(pco_instr *instr)
{
   pco_ref ditr_dest = instr->dest[0];

   pco_foreach_instr_in_func_from_rev (prev, instr) {
      if (prev->op == PCO_OP_FENCE)
         break;

      pco_foreach_instr_dest (pprev_dest, prev) {
         if (pco_ref_is_reg(*pprev_dest) || pco_ref_is_idx_reg(*pprev_dest))
            if (pco_refs_are_overlapping_regs(*pprev_dest, ditr_dest))
               return true;
      }

      pco_foreach_instr_src (pprev_src, prev) {
         if (pco_ref_is_reg(*pprev_src) || pco_ref_is_idx_reg(*pprev_src))
            if (pco_refs_are_overlapping_regs(*pprev_src, ditr_dest))
               return true;
      }
   }

   return false;
}

/**
 * \brief Insert pseudo fences around DITR and DITRP instructions.
 *
 * The pre-fence is only inserted when necessary and
 * ensures no registers are shared with the DITR/DITRP destination.
 * A post-fence is always inserted.
 *
 * \param[in,out] instr PCO instr.
 * \return True if progress was made.
 */
static bool try_legalize_ditr_fence(pco_instr *instr)
{
   if (instr->op != PCO_OP_DITR && instr->op != PCO_OP_DITRP)
      return false;

   pco_builder b =
      pco_builder_create(instr->parent_func, pco_cursor_after_instr(instr));

   /* Always insert a post-fence to enforce WDF ordering. */
   pco_fence(&b);

   if (ditr_needs_pre_fence(instr)) {
      b.cursor = pco_cursor_before_instr(instr);
      pco_fence(&b);
   }

   return true;
}

/**
 * \brief Try to legalizes an instruction.
 *
 * \param[in,out] instr PCO instr.
 * \return True if progress was made.
 */
static bool try_legalize(pco_instr *instr)
{
   const struct pco_op_info *info = &pco_op_info[instr->op];
   bool progress = false;

   progress |= try_legalize_large_hwreg_offsets(instr, info);
   progress |= try_legalize_ditr_fence(instr);

   return progress;
}

/**
 * \brief Legalizes instructions where additional restrictions apply.
 * This should be run after register allocation.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_pre_ra_legalize(pco_shader *shader)
{
   bool progress = false;

   assert(!shader->is_grouped);
   assert(!shader->is_legalized);

   const struct pco_op_info *info;

   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (instr, func) {
         info = &pco_op_info[instr->op];
         if (info->type != PCO_OP_TYPE_PSEUDO)
            progress |= try_legalize_src_mappings(instr, info);
         else
            progress |= legalize_pseudo(instr, true);
      }
   }

   return progress;
}

/**
 * \brief Post-RA legalization pass.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_post_ra_legalize(pco_shader *shader)
{
   assert(!shader->is_grouped);

   bool progress = false;

   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (instr, func) {
         progress |= try_legalize(instr);
      }
   }

   const struct pco_op_info *info;

   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (instr, func) {
         info = &pco_op_info[instr->op];
         if (info->type == PCO_OP_TYPE_PSEUDO)
            progress |= legalize_pseudo(instr, false);
      }
   }

   shader->is_legalized = true;
   return progress;
}
