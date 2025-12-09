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

static bool legalize_pseudo(pco_instr *instr)
{
   switch (instr->op) {
   case PCO_OP_MOV:
      if (pco_ref_is_reg(instr->src[0]) &&
          pco_ref_get_reg_class(instr->src[0]) == PCO_REG_CLASS_SPEC)
         instr->op = PCO_OP_MOVS1;
      else
         instr->op = PCO_OP_MBYP;

      return true;

   case PCO_OP_MOV_OFFSET: {
      pco_builder b =
         pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

      pco_ref dest = instr->dest[0];
      pco_ref src = instr->src[0];
      pco_ref offset = instr->src[1];

      unsigned idx_reg_num = 0;
      pco_ref idx_reg =
         pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

      pco_mbyp(&b, idx_reg, offset, .exec_cnd = pco_instr_get_exec_cnd(instr));

      if (pco_instr_get_offset_sd(instr) == PCO_OFFSET_SD_SRC)
         src = pco_ref_hwreg_idx_from(idx_reg_num, src);
      else
         dest = pco_ref_hwreg_idx_from(idx_reg_num, dest);

      pco_instr *mbyp = pco_mbyp(&b, dest, src);
      xfer_op_mods(mbyp, instr);

      pco_instr_delete(instr);

      return true;
   }

   case PCO_OP_OP_ATOMIC_OFFSET: {
      pco_builder b =
         pco_builder_create(instr->parent_func, pco_cursor_before_instr(instr));

      pco_ref dest = instr->dest[0];
      pco_ref shmem_dest = instr->dest[1];

      pco_ref shmem_src = instr->src[0];
      pco_ref value = instr->src[1];
      pco_ref value_swap = instr->src[2];
      pco_ref offset = instr->src[3];

      unsigned idx_reg_num = 0;
      pco_ref idx_reg =
         pco_ref_hwreg_idx(idx_reg_num, idx_reg_num, PCO_REG_CLASS_INDEX);

      pco_mbyp(&b, idx_reg, offset, .exec_cnd = pco_instr_get_exec_cnd(instr));

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

   default:
      break;
   }

   return false;
}

static bool try_legalize_large_hwreg_offsets(pco_instr *instr,
                                             const struct pco_op_info *info)
{
   unsigned large_hwreg_count = 0;
   pco_ref *large_hwregs[_PCO_OP_MAX_SRCS + _PCO_OP_MAX_DESTS] = { 0 };

   enum pco_reg_class hwreg_class = ~0;
   unsigned min_large_offset = ~0;
   ASSERTED unsigned max_large_offset = 0;

   /* Check dests. */
   pco_foreach_instr_dest_hwreg (pdest, instr) {
      if (pco_ref_get_reg_index(*pdest) < 256)
         continue;

      large_hwregs[large_hwreg_count++] = pdest;
      assert(hwreg_class == ~0 || hwreg_class == pco_ref_get_reg_class(*pdest));
      hwreg_class = pco_ref_get_reg_class(*pdest);

      min_large_offset = MIN2(min_large_offset, pco_ref_get_reg_index(*pdest));
      max_large_offset = MAX2(max_large_offset, pco_ref_get_reg_index(*pdest));
   }

   /* Check srcs. */
   pco_foreach_instr_src_hwreg (psrc, instr) {
      if (pco_ref_get_reg_index(*psrc) < 256)
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
   assert((max_large_offset - min_large_offset) < 256);

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

   /* Skip pseudo instructions. */
   if (info->type == PCO_OP_TYPE_PSEUDO) {
      progress |= legalize_pseudo(instr);
   } else {
      progress |= try_legalize_src_mappings(instr, info);
   }

   return progress;
}

/**
 * \brief Legalizes instructions where additional restrictions apply.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_legalize(pco_shader *shader)
{
   bool progress = false;

   assert(!shader->is_grouped);
   assert(!shader->is_legalized);

   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (instr, func) {
         progress |= try_legalize(instr);
      }
   }

   shader->is_legalized = true;
   return progress;
}
