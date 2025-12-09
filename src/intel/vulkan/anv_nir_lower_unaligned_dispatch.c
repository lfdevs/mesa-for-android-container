/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_nir.h"
#include "nir_builder.h"
#include "compiler/brw/brw_nir.h"

bool
anv_nir_lower_unaligned_dispatch(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_builder b = nir_builder_at(nir_before_impl(impl));

   nir_def *global_idx = nir_channel(&b, nir_load_global_invocation_id(&b, 32), 0);
   nir_def *max_unaligned_invocations_x =
      nir_load_inline_data_intel(&b, 1, 32,
                                 .base = ANV_INLINE_PARAM_UNALIGNED_INVOCATIONS_X_OFFSET);

   nir_push_if(&b, nir_uge(&b, global_idx, max_unaligned_invocations_x));
   {
      nir_jump(&b, nir_jump_return);
   }
   nir_pop_if(&b, NULL);

   return nir_progress(true, impl, nir_metadata_none);
}
