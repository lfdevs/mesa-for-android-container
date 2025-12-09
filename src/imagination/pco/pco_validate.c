/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_validate.c
 *
 * \brief PCO validation functions.
 */

#include "pco.h"
#include "pco_internal.h"
#include "util/bitset.h"
#include "util/macros.h"
#include "util/ralloc.h"

#include <inttypes.h>
#include <stdio.h>

#ifndef NDEBUG

enum ref_cursor {
   REF_CURSOR_NONE,
   REF_CURSOR_INSTR_DEST,
   REF_CURSOR_INSTR_SRC,
   REF_CURSOR_IGRP_SRC,
   REF_CURSOR_IGRP_ISS,
   REF_CURSOR_IGRP_DEST,
};

/** Validation state. */
struct val_state {
   const char *when; /** Description of the validation being done. */
   pco_shader *shader; /** The shader being validated. */
   pco_func *func; /** Current function being validated. */
   pco_cf_node *cf_node; /** Current cf node being validated. */
   pco_igrp *igrp; /** Current instruction group being validated. */
   enum pco_op_phase phase; /** Phase of the instruction being validated. */
   pco_instr *instr; /** Current instruction being validated. */
   pco_ref *ref; /** Current reference being validated. */
   enum ref_cursor ref_cursor; /** Current reference cursor. */
};

/**
 * \brief Asserts a condition, printing an error and aborting on failure.
 *
 * \param[in] state Validation state.
 * \param[in] cond Assertion condition.
 * \param[in] cond_str Assertion condition string.
 * \param[in] fmt Format string.
 */
static void pco_assert(struct val_state *state,
                       bool cond,
                       const char *cond_str,
                       const char *fmt,
                       ...)
{
   if (cond)
      return;

   printf("PCO validation failed ");

   if (state->when)
      printf("%s ", state->when);

   printf("with assertion \"%s\" - ", cond_str);

   va_list args;
   va_start(args, fmt);
   vprintf(fmt, args);
   va_end(args);

   printf(" - while validating");

   if (state->ref_cursor != REF_CURSOR_NONE) {
      switch (state->ref_cursor) {
      case REF_CURSOR_INSTR_DEST:
         printf(" instr dest #%" PRIuPTR, state->ref - state->instr->dest);
         break;

      case REF_CURSOR_INSTR_SRC:
         printf(" instr src #%" PRIuPTR, state->ref - state->instr->src);
         break;

      case REF_CURSOR_IGRP_DEST:
         printf(" igrp dest #%" PRIuPTR, state->ref - state->igrp->srcs.s);
         break;

      case REF_CURSOR_IGRP_ISS:
         printf(" igrp iss #%" PRIuPTR, state->ref - state->igrp->iss.is);
         break;

      case REF_CURSOR_IGRP_SRC:
         printf(" igrp src #%" PRIuPTR, state->ref - state->igrp->srcs.s);
         break;

      default:
         UNREACHABLE("");
      }

      printf(" (");
      pco_print_ref(state->shader, *state->ref);
      printf(")");
   }

   if (state->cf_node) {
      printf(" ");
      pco_print_cf_node_name(state->shader, state->cf_node);
   }

   if (state->igrp) {
      printf(" igrp ");
      pco_print_igrp(state->shader, state->igrp);
   }

   if (state->instr) {
      printf(" instr ");
      if (state->shader->is_grouped) {
         printf("(phase ");
         pco_print_phase(state->shader,
                         state->instr->parent_igrp->hdr.alutype,
                         state->instr->phase);
         printf(") ");
      }

      pco_print_instr(state->shader, state->instr);
   }

   if (state->func) {
      printf(" ");
      pco_print_cf_node_name(state->shader, &state->func->cf_node);
   }

   printf(".\n");

   pco_print_shader(state->shader, stdout, state->when);

   abort();
}

#   define PCO_ASSERT(state, cond, fmt, ...) \
      pco_assert(state, cond, #cond, fmt, ##__VA_ARGS__)

/**
 * \brief Validates SSA assignments and uses.
 *
 * \param[in,out] state Validation state.
 */
static void pco_validate_ssa(struct val_state *state)
{
   BITSET_WORD *ssa_writes;
   pco_foreach_func_in_shader (func, state->shader) {
      state->func = func;

      ssa_writes = rzalloc_array_size(NULL,
                                      sizeof(*ssa_writes),
                                      BITSET_WORDS(func->next_ssa));

      /* Ensure sources have been defined before they're used. */
      pco_foreach_instr_in_func (instr, func) {
         state->cf_node = &instr->parent_block->cf_node;
         state->instr = instr;
         state->ref_cursor = REF_CURSOR_INSTR_SRC;
         pco_foreach_instr_src_ssa (psrc, instr) {
            state->ref = psrc;
            PCO_ASSERT(state,
                       BITSET_TEST(ssa_writes, psrc->val),
                       "SSA source used before being defined");
         }

         /* Ensure destinations are only defined once. */
         state->ref_cursor = REF_CURSOR_INSTR_DEST;
         pco_foreach_instr_dest_ssa (pdest, instr) {
            state->ref = pdest;
            PCO_ASSERT(state,
                       !BITSET_TEST(ssa_writes, pdest->val),
                       "SSA destination defined to more than once");
            BITSET_SET(ssa_writes, pdest->val);
         }

         state->instr = NULL;
         state->cf_node = NULL;
      }

      ralloc_free(ssa_writes);

      state->func = NULL;
      state->ref = NULL;
      state->ref_cursor = REF_CURSOR_NONE;
   }
}

/**
 * \brief Validates hardware source mappings.
 *
 * \param[in,out] state Validation state.
 */
static void pco_validate_src_maps(struct val_state *state)
{
   /* Only check after the legalize pass has run. */
   if (!state->shader->is_legalized)
      return;

   bool needs_s124;
   pco_foreach_func_in_shader (func, state->shader) {
      state->func = func;

      pco_foreach_instr_in_func (instr, func) {
         const struct pco_op_info *info = &pco_op_info[instr->op];
         if (info->type == PCO_OP_TYPE_PSEUDO)
            continue;

         state->cf_node = &instr->parent_block->cf_node;
         state->instr = instr;

         state->ref_cursor = REF_CURSOR_INSTR_DEST;
         pco_foreach_instr_dest (pdest, instr) {
            state->ref = pdest;
            unsigned dest_index = pdest - instr->dest;

            if (!info->dest_intrn_map[dest_index])
               continue;

            enum pco_io mapped_src =
               PCO_IO_S0 + info->dest_intrn_map[dest_index] - 1;

            bool valid = ref_src_map_valid(*pdest, mapped_src, &needs_s124);
            PCO_ASSERT(state,
                       valid,
                       "HW register reference should be mapped to %s",
                       needs_s124 ? "S1/S2/S4" : "S0/S2/S3");
         }

         state->ref_cursor = REF_CURSOR_INSTR_SRC;
         pco_foreach_instr_src (psrc, instr) {
            state->ref = psrc;
            unsigned src_index = psrc - instr->src;

            if (!info->src_intrn_map[src_index])
               continue;

            enum pco_io mapped_src =
               PCO_IO_S0 + info->src_intrn_map[src_index] - 1;

            bool valid = ref_src_map_valid(*psrc, mapped_src, &needs_s124);
            PCO_ASSERT(state,
                       valid,
                       "HW register reference should be mapped to %s",
                       needs_s124 ? "S1/S2/S4" : "S0/S2/S3");
         }

         state->instr = NULL;
         state->cf_node = NULL;
      }

      state->func = NULL;
      state->ref = NULL;
      state->ref_cursor = REF_CURSOR_NONE;
   }
}

#   define CHECK_IO(io, ref)                                        \
   case PCO_REF_MAP_##io:                                           \
      if (pco_ref_is_io(ref) && pco_ref_get_io(ref) == PCO_IO_##io) \
         return true;                                               \
      break;

/**
 * \brief Checks whether a ref type corresponds to a supported mapping.
 *
 * \param[in] ref Src/dest reference.
 * \param[in] ref_maps Supported mappings.
 * \return True if the ref type corresponds to a supported mapping.
 */
static inline bool ref_is_in_map(pco_ref ref, enum pco_ref_map ref_maps)
{
   u_foreach_bit (_ref_map, ref_maps) {
      enum pco_ref_map ref_map = _ref_map;
      switch (ref_map) {
      case PCO_REF_MAP__:
         if (pco_ref_is_null(ref))
            return true;
         break;

         CHECK_IO(S0, ref)
         CHECK_IO(S1, ref)
         CHECK_IO(S2, ref)
         CHECK_IO(S3, ref)
         CHECK_IO(S4, ref)
         CHECK_IO(S5, ref)

         CHECK_IO(W0, ref)
         CHECK_IO(W1, ref)

         CHECK_IO(IS0, ref)
         CHECK_IO(IS1, ref)
         CHECK_IO(IS2, ref)
         CHECK_IO(IS3, ref)
         CHECK_IO(IS4, ref)
         CHECK_IO(IS5, ref)

         CHECK_IO(FT0, ref)
         CHECK_IO(FT1, ref)
         CHECK_IO(FT1_INVERT, ref)
         CHECK_IO(FT2, ref)
         CHECK_IO(FTE, ref)
         CHECK_IO(FT3, ref)
         CHECK_IO(FT4, ref)
         CHECK_IO(FT5, ref)

         CHECK_IO(FTT, ref)

      case PCO_REF_MAP_P0:
         if (pco_ref_is_pred(ref) && ref.val == PCO_PRED_P0)
            return true;
         break;

      case PCO_REF_MAP_PE:
         if (pco_ref_is_pred(ref) && ref.val == PCO_PRED_PE)
            return true;
         break;

      case PCO_REF_MAP_IMM:
         if (pco_ref_is_imm(ref))
            return true;
         break;

      case PCO_REF_MAP_DRC:
         if (pco_ref_is_drc(ref))
            return true;
         break;

      case PCO_REF_MAP_TEMP:
         if ((pco_ref_is_reg(ref) || pco_ref_is_idx_reg(ref)) &&
             pco_ref_get_reg_class(ref) == PCO_REG_CLASS_TEMP)
            return true;
         break;

      case PCO_REF_MAP_COEFF:
         if ((pco_ref_is_reg(ref) || pco_ref_is_idx_reg(ref)) &&
             pco_ref_get_reg_class(ref) == PCO_REG_CLASS_COEFF)
            return true;
         break;

      default:
         UNREACHABLE("");
      }
   }

   return false;
}

#   undef CHECK_IO

/**
 * \brief Validates I/O references for igrps.
 *
 * \param[in,out] state Validation state.
 */
static void pco_validate_ref_maps(struct val_state *state)
{
   bool needs_s124;
   pco_foreach_func_in_shader (func, state->shader) {
      state->func = func;

      pco_foreach_igrp_in_func (igrp, func) {
         state->cf_node = &igrp->parent_block->cf_node;
         state->igrp = igrp;

         /* Igrp source mappings. */
         state->ref_cursor = REF_CURSOR_IGRP_SRC;
         for (unsigned s = 0; s < ARRAY_SIZE(igrp->srcs.s); ++s) {
            state->ref = &igrp->srcs.s[s];
            pco_ref src = igrp->srcs.s[s];

            if (pco_ref_is_null(src))
               continue;

            bool valid = ref_src_map_valid(src, PCO_IO_S0 + s, &needs_s124);
            PCO_ASSERT(state,
                       valid,
                       "HW register reference should be mapped to %s",
                       needs_s124 ? "S1/S2/S4" : "S0/S2/S3");
         }

         pco_foreach_instr_in_igrp (instr, igrp) {
            const struct pco_op_info *info = &pco_op_info[instr->op];
            enum pco_op_phase phase = instr->phase;
            state->instr = instr;

            /* Instruction dests. */
            state->ref_cursor = REF_CURSOR_INSTR_DEST;
            for (unsigned d = 0; d < instr->num_dests; ++d) {
               state->ref = &instr->dest[d];
               PCO_ASSERT(state,
                          ref_is_in_map(instr->dest[d],
                                        info->grp_dest_maps[phase][d]),
                          "Invalid dest assignment.");
            }

            /* Instruction sources. */
            state->ref_cursor = REF_CURSOR_INSTR_SRC;
            for (unsigned s = 0; s < instr->num_srcs; ++s) {
               state->ref = &instr->src[s];
               PCO_ASSERT(state,
                          ref_is_in_map(instr->src[s],
                                        info->grp_src_maps[phase][s]),
                          "Invalid src assignment.");
            }

            state->instr = NULL;
         }

         state->igrp = NULL;
         state->cf_node = NULL;
      }

      state->func = NULL;
      state->ref = NULL;
      state->ref_cursor = REF_CURSOR_NONE;
   }
}

/**
 * \brief Validates a PCO shader.
 *
 * \param[in] shader PCO shader.
 * \param[in] when When the validation check is being run.
 */
void pco_validate_shader(pco_shader *shader, const char *when)
{
   if (PCO_DEBUG(VAL_SKIP))
      return;

   struct val_state state = {
      .when = when,
      .shader = shader,
      .phase = -1,
   };

   if (!shader->is_grouped) {
      pco_validate_ssa(&state);
      pco_validate_src_maps(&state);
   } else {
      PCO_ASSERT(&state,
                 shader->is_legalized,
                 "Legalize pass should have been run before grouping");
      pco_validate_ref_maps(&state);
   }
}
#else /* NDEBUG */
void pco_validate_shader(UNUSED pco_shader *shader, UNUSED const char *when) {}
#endif /* NDEBUG */
