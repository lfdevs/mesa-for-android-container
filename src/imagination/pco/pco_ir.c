/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_ir.c
 *
 * \brief PCO IR-specific functions.
 */

#include "pco.h"
#include "pco_internal.h"

#include <stdbool.h>
#include <stdio.h>

/**
 * \brief Runs passes on a PCO shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in,out] shader PCO shader.
 */
void pco_process_ir(pco_ctx *ctx, pco_shader *shader)
{
   pco_validate_shader(shader, "before passes");

   PCO_PASS(_, shader, pco_const_imms);
   PCO_PASS(_, shader, pco_opt);

   bool progress;
   do {
      progress = false;
      PCO_PASS(progress, shader, pco_dce);
   } while (progress);

   PCO_PASS(_, shader, pco_bool);
   PCO_PASS(_, shader, pco_cf);

   PCO_PASS(_, shader, pco_shrink_vecs);

   PCO_PASS(_, shader, pco_const_imms);
   PCO_PASS(_, shader, pco_opt_comp_only_vecs);
   PCO_PASS(_, shader, pco_opt);

   do {
      progress = false;
      PCO_PASS(progress, shader, pco_dce);
   } while (progress);

   /* TODO: schedule after RA instead as e.g. vecs may no longer be the first
    * time a drc result is used.
    */
   PCO_PASS(_, shader, pco_schedule);
   PCO_PASS(_, shader, pco_legalize);
   PCO_PASS(_, shader, pco_ra);
   PCO_PASS(_, shader, pco_end);
   PCO_PASS(_, shader, pco_group_instrs);

   pco_validate_shader(shader, "after passes");

   if (pco_should_print_shader(shader))
      pco_print_shader(shader, stdout, "after passes");
}
