/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_io.c
 *
 * \brief PCO NIR I/O lowering pass.
 */

#include "nir.h"
#include "nir_builder.h"
#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * \brief Lowers an I/O instruction.
 *
 * \param[in] b NIR builder.
 * \param[in] intr NIR intrinsic instruction.
 * \param[in] cb_data User callback data.
 * \return True if progress was made.
 */
static bool
lower_io(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *cb_data)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_src *offset_src;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_push_constant:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_shared_atomic_swap:
      offset_src = &intr->src[0];
      break;

   case nir_intrinsic_store_shared:
      offset_src = &intr->src[1];
      break;

   default:
      return false;
   }

   ASSERTED unsigned base = nir_intrinsic_base(intr);
   assert(!base);

   /* Byte offset to DWORD offset. */
   nir_src_rewrite(offset_src, nir_ushr_imm(b, offset_src->ssa, 2));

   return true;
}

/**
 * \brief I/O lowering pass.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_lower_io(nir_shader *shader)
{
   bool progress = false;

   progress |= nir_shader_intrinsics_pass(shader,
                                          lower_io,
                                          nir_metadata_control_flow,
                                          NULL);

   return progress;
}
