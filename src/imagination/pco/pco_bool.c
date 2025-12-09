/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_bool.c
 *
 * \brief PCO bool passes.
 */

#include "pco.h"
#include "pco_builder.h"
#include "util/bitscan.h"
#include "util/bitset.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"

#include <assert.h>
#include <stdbool.h>

static bool lower_bools(pco_func *func)
{
   bool progress = false;

   /* Update to 32-bit. */
   pco_foreach_instr_in_func (instr, func) {
      pco_foreach_instr_dest_vreg_ssa (pdest, instr) {
         if (pco_ref_get_bits(*pdest) != 1)
            continue;

         *pdest = pco_ref_bits(*pdest, 32);

         progress = true;
      }

      pco_foreach_instr_src_vreg_ssa (psrc, instr) {
         if (pco_ref_get_bits(*psrc) != 1)
            continue;

         *psrc = pco_ref_bits(*psrc, 32);

         progress = true;
      }
   }

   pco_foreach_ssa_bool_if_in_func (pif, func) {
      pif->cond = pco_ref_bits(pif->cond, 32);

      progress = true;
   }

   return progress;
}

/**
 * \brief Bool lowering pass.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_bool(pco_shader *shader)
{
   bool progress = false;

   pco_foreach_func_in_shader (func, shader) {
      progress |= lower_bools(func);
   }

   return progress;
}
