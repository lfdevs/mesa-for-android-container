/*
 * Copyright © 2025 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_nir_compute.c
 *
 * \brief PCO NIR compute-specific passes.
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
 * \brief Inserts the instance check.
 *
 * \param[in,out] entrypoint NIR shader entrypoint.
 * \return The cursor position for where to re-insert the entrypoint.
 */
static nir_cursor insert_instance_check(nir_function_impl *entrypoint)
{
   nir_builder b = nir_builder_at(nir_before_impl(entrypoint));
   nir_cursor cursor;

   /* If the current instance index is greater than the total workgroup size,
    * we don't execute.
    */
   nir_def *local_size = nir_load_workgroup_size(&b);
   nir_def *size_x = nir_channel(&b, local_size, 0);
   nir_def *size_y = nir_channel(&b, local_size, 1);
   nir_def *size_z = nir_channel(&b, local_size, 2);
   nir_def *flat_size = nir_imul(&b, nir_imul(&b, size_x, size_y), size_z);

   nir_def *flat_id = nir_load_local_invocation_index(&b);

   nir_def *cond_inst_valid = nir_ilt(&b, flat_id, flat_size);
   nir_if *nif = nir_push_if(&b, cond_inst_valid);
   {
      cursor = b.cursor;
   }
   nir_pop_if(&b, nif);

   return cursor;
}

/**
 * \brief Inserts an instance check for compute shaders.
 *
 * \param[in,out] shader NIR shader.
 * \return True if the pass made progress.
 */
bool pco_nir_compute_instance_check(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_COMPUTE);

   if (shader->info.internal)
      return false;

   nir_function_impl *entrypoint = nir_shader_get_entrypoint(shader);

   if (nir_cf_list_is_empty_block(&entrypoint->body))
      return false;

   /* Extract the entire entrypoint. */
   nir_cf_list cf_list;
   nir_cf_extract(&cf_list,
                  nir_before_impl(entrypoint),
                  nir_after_impl(entrypoint));

   nir_cursor cursor = insert_instance_check(entrypoint);

   /* Re-insert the entrypoint inside the instance check. */
   nir_cf_reinsert(&cf_list, cursor);

   return true;
}
