/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_tests.h"
#include "si_tests_private.h"

#include "si_pipe.h"
#include "util/u_tests.h"

void si_test_vmfault(struct si_screen *sscreen, uint64_t test_flags)
{
   struct pipe_context *ctx = sscreen->aux_context.general.ctx;
   struct si_context *sctx = (struct si_context *)ctx;
   struct pipe_resource *buf = pipe_buffer_create_const0(&sscreen->b, 0, PIPE_USAGE_DEFAULT, 64);

   if (!buf) {
      puts("Buffer allocation failed.");
      exit(1);
   }

   si_resource(buf)->gpu_address = 0; /* cause a VM fault */

   if (test_flags & DBG(TEST_VMFAULT_CP)) {
      si_cp_dma_copy_buffer(sctx, buf, buf, 0, 4, 4);
      ctx->flush(ctx, NULL, 0);
      puts("VM fault test: CP - done.");
   }
   if (test_flags & DBG(TEST_VMFAULT_SHADER)) {
      util_test_constant_buffer(ctx, buf);
      puts("VM fault test: Shader - done.");
   }
}
