/* Copyright © 2026 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "si_tests.h"
#include "si_tests_private.h"
#include "si_pipe.h"
#include "nir_builder.h"
#include "ac_nir_helpers.h"

void
si_test_oa_bandwidth(struct si_screen *sscreen)
{
   struct pipe_context *ctx = sscreen->b.context_create(&sscreen->b, NULL, 0);
   struct si_context *sctx = (struct si_context *)ctx;
   assert(sctx->gfx_level >= GFX12);

   /* Only ac_nir_to_llvm implements ordered_add_loop_gfx12_amd. */
   sscreen->use_aco = false;

   struct pipe_query *q = ctx->create_query(ctx, PIPE_QUERY_TIME_ELAPSED, 0);

   const unsigned size = debug_get_num_option("mb", 64) * 1024 * 1024;
   struct pipe_shader_buffer sb = {};
   sb.buffer = pipe_buffer_create(&sscreen->b, 0, PIPE_USAGE_DEFAULT, size);
   sb.buffer_size = size;
   ctx->set_shader_buffers(ctx, MESA_SHADER_COMPUTE, 0, 1, &sb, 0x1);

   struct pipe_shader_buffer sb2 = {};
   sb2.buffer = pipe_buffer_create(&sscreen->b, 0, PIPE_USAGE_DEFAULT, 8);
   sb2.buffer_size = 8;
   ctx->set_shader_buffers(ctx, MESA_SHADER_COMPUTE, 1, 1, &sb2, 0x1);

   union pipe_query_result total[5][6] = {};

   for (unsigned num_dwords_per_thread = 1; num_dwords_per_thread <= 16;
        num_dwords_per_thread *= 2) {
      for (unsigned block_size = 32; block_size <= 1024; block_size *= 2) {
         nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, sscreen->nir_options,
                                                        "ordered_append_test");
         b.shader->info.workgroup_size[0] = block_size;
         b.shader->info.workgroup_size[1] = 1;
         b.shader->info.workgroup_size[2] = 1;
         b.shader->info.num_ssbos = 2;
         b.shader->info.shared_size = 4;

         nir_def *local_id = nir_channel(&b, nir_load_local_invocation_id(&b), 0);
         nir_def *wg_id = nir_channel(&b, nir_load_workgroup_id(&b), 0);
         nir_def *global_id = ac_get_global_ids(&b, 1, 32);
         nir_def *atomic_address = nir_load_ssbo_address(&b, 1, 64, nir_imm_int(&b, 1), nir_imm_int(&b, 0));

         nir_if *if_tid0 = nir_push_if(&b, nir_ieq(&b, local_id, nir_imm_int(&b, 0)));

         nir_def *ordered_id = nir_iand_imm(&b, wg_id, 0xfff);
         nir_def *atomic_src = nir_pack_64_2x32_split(&b, ordered_id, nir_imm_int(&b, 0));
         nir_def *count = nir_ordered_add_loop_gfx12_amd(&b, atomic_address, nir_imm_int(&b, 0),
                                                         ordered_id, atomic_src);

         if (SHADER_DEBUG_LOG) {
            ac_nir_store_debug_log_amd(&b, nir_vec4(&b, ordered_id, count,
                                                    nir_imm_int(&b, 0), nir_imm_int(&b, 0)));
         }

         nir_store_shared(&b, nir_imm_int(&b, 0), nir_imm_int(&b, 0));
         nir_pop_if(&b, if_tid0);

         nir_barrier(&b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                     .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);
         nir_def *loaded_zero = nir_load_shared(&b, 1, 32, nir_imm_int(&b, 0));

         /* Convert the global thread ID into bytes. */
         for (unsigned i = 0; i < DIV_ROUND_UP(num_dwords_per_thread, 4); i++) {
            nir_def *offset = nir_iadd_imm(&b, nir_imul_imm(&b, global_id, 4 * num_dwords_per_thread), i * 16);
            offset = nir_iadd(&b, offset, loaded_zero);
            nir_store_ssbo(&b, nir_imm_zero(&b, MIN2(4, num_dwords_per_thread), 32), nir_imm_int(&b, 0), offset,
                           .access = ACCESS_RESTRICT);
         }

         void *shader = si_create_shader_state(sctx, b.shader);
         ctx->bind_compute_state(ctx, shader);

         unsigned wave_size = ((struct si_compute*)shader)->shader.wave_size;
         sctx->cs_max_waves_per_sh = debug_get_num_option("max_wg_per_sa", 8) * (block_size / wave_size);
         assert(sctx->cs_max_waves_per_sh);

         struct pipe_grid_info info = {};
         info.block[0] = block_size;
         info.block[1] = 1;
         info.block[2] = 1;
         info.grid[0] = size / (info.block[0] * MAX2(1, num_dwords_per_thread) * 4);
         info.grid[1] = 1;
         info.grid[2] = 1;

         union pipe_query_result result;
         const unsigned num_warmup_repeats = SHADER_DEBUG_LOG ? 1 : 5;
         const unsigned num_repeats = SHADER_DEBUG_LOG ? 1 : 32;

         for (unsigned i = 0; i < num_warmup_repeats + num_repeats; i++) {
            uint32_t clear_value = 0;
            si_barrier_before_simple_buffer_op(sctx, 0, sb2.buffer, NULL);
            si_clear_buffer(sctx, sb2.buffer, 0, 8, &clear_value, 4, SI_AUTO_SELECT_CLEAR_METHOD, false);
            si_barrier_after_simple_buffer_op(sctx, 0, sb2.buffer, NULL);

            if (i >= num_warmup_repeats)
               ctx->begin_query(ctx, q);

            ctx->launch_grid(ctx, &info);

            if (i >= num_warmup_repeats) {
               ctx->end_query(ctx, q);
               ctx->get_query_result(ctx, q, true, &result);

               total[util_logbase2(num_dwords_per_thread)][util_logbase2(block_size) - 5].u64 += result.u64;
            }
         }

         total[util_logbase2(num_dwords_per_thread)][util_logbase2(block_size) - 5].u64 /= num_repeats;
      }
   }

   printf("Printing GB/s ordered append store bandwidth.\n");
   printf(" Stored dw/lane,   Workgroup sizes\n");
   printf("               ,");
   for (unsigned j = 0; j < 6; j++) {
      unsigned wg_size = 32 << j;
      unsigned spaces = 4 - (int)(log(wg_size) / log(10));
      for (unsigned a = 0; a < spaces; a++)
         printf("_");

      printf("%u,", wg_size);
   }
   printf("\n");

   for (unsigned i = 0; i < 5; i++) {
      printf("           %4u| ", 4 << i);

      for (unsigned j = 0; j < 6; j++)
         printf("%4u, ", (unsigned)((double)size / total[i][j].u64));

      printf("\n");
   }
}
