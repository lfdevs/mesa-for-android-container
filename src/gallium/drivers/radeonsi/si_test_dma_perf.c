/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "si_query.h"
#include "util/streaming-load-memcpy.h"

#define MIN_SIZE   512
#define MAX_SIZE   (128 * 1024 * 1024)
#define SIZE_SHIFT 1
#define WARMUP_RUNS 16
#define NUM_RUNS   32

enum {
   TEST_FILL_VRAM,
   TEST_FILL_VRAM_12B,
   TEST_FILL_GTT,
   TEST_FILL_GTT_12B,
   TEST_COPY_VRAM_VRAM,
   TEST_COPY_VRAM_GTT,
   TEST_COPY_GTT_VRAM,
   NUM_TESTS,
};

static const char *test_strings[] = {
   [TEST_FILL_VRAM] = "fill->VRAM",
   [TEST_FILL_VRAM_12B] = "fill->VRAM 12B",
   [TEST_FILL_GTT] = "fill->GTT",
   [TEST_FILL_GTT_12B] = "fill->GTT 12B",
   [TEST_COPY_VRAM_VRAM] = "VRAM->VRAM",
   [TEST_COPY_VRAM_GTT] = "VRAM->GTT",
   [TEST_COPY_GTT_VRAM] = "GTT->VRAM",
};

enum {
   METHOD_DEFAULT,
   METHOD_CP_DMA,
   METHOD_COMPUTE_2DW,
   METHOD_COMPUTE_3DW,
   METHOD_COMPUTE_4DW,
   NUM_METHODS,
};

static const char *method_strings[] = {
   [METHOD_DEFAULT] = "Default",
   [METHOD_CP_DMA] = "CP DMA",
   [METHOD_COMPUTE_2DW] = "CS 2dw",
   [METHOD_COMPUTE_3DW] = "CS 3dw",
   [METHOD_COMPUTE_4DW] = "CS 4dw",
};

enum {
   ALIGN_MAX,
   ALIGN_256,
   ALIGN_128,
   ALIGN_64,
   ALIGN_4,
   ALIGN_2,
   ALIGN_1,
   ALIGN_SRC4_DST2,
   ALIGN_SRC4_DST1,
   ALIGN_SRC2_DST4,
   ALIGN_SRC2_DST1,
   ALIGN_SRC1_DST4,
   ALIGN_SRC1_DST2,
   NUM_ALIGNMENTS,
};

struct align_info_t {
   const char *string;
   unsigned src_offset;
   unsigned dst_offset;
};

static const struct align_info_t align_info[] = {
   [ALIGN_MAX] = {"both=max", 0, 0},
   [ALIGN_256] = {"both=256", 256, 256},
   [ALIGN_128] = {"both=128", 128, 128},
   [ALIGN_64] = {"both=64", 64, 64},
   [ALIGN_4] = {"both=4", 4, 4},
   [ALIGN_2] = {"both=2", 2, 2},
   [ALIGN_1] = {"both=1", 1, 1},
   [ALIGN_SRC4_DST2] = {"src=4 dst=2", 4, 2},
   [ALIGN_SRC4_DST1] = {"src=4 dst=1", 4, 1},
   [ALIGN_SRC2_DST4] = {"src=2 dst=4", 2, 4},
   [ALIGN_SRC2_DST1] = {"src=2 dst=1", 2, 1},
   [ALIGN_SRC1_DST4] = {"src=1 dst=4", 1, 4},
   [ALIGN_SRC1_DST2] = {"src=1 dst=2", 1, 2},
};

void si_test_dma_perf(struct si_screen *sscreen)
{
   struct pipe_screen *screen = &sscreen->b;
   struct pipe_context *ctx = screen->context_create(screen, NULL, 0);
   struct si_context *sctx = (struct si_context *)ctx;

   sscreen->ws->cs_set_pstate(&sctx->gfx_cs, RADEON_CTX_PSTATE_PEAK);

   printf("Test          , Method , Alignment  ,");
   for (unsigned size = MIN_SIZE; size <= MAX_SIZE; size <<= SIZE_SHIFT) {
      if (size >= 1024 * 1024)
         printf("%6uMB,", size / (1024 * 1024));
      else if (size >= 1024)
         printf("%6uKB,", size / 1024);
      else
         printf(" %6uB,", size);
   }
   printf("\n");

   /* Run benchmarks. */
   for (unsigned test_flavor = 0; test_flavor < NUM_TESTS; test_flavor++) {
      bool is_copy = test_flavor >= TEST_COPY_VRAM_VRAM;

      if (test_flavor)
         puts("");

      for (unsigned method = 0; method < NUM_METHODS; method++) {
         for (unsigned align = 0; align < NUM_ALIGNMENTS; align++) {
            unsigned dwords_per_thread, clear_value_size;
            unsigned src_offset = align_info[align].src_offset;
            unsigned dst_offset = align_info[align].dst_offset;

            /* offset > 0 && offset < 4 is the only case when the compute shader performs the same
             * as offset=0 without any alignment optimizations, so shift the offset by 4 to get
             * unaligned performance.
             */
            if (src_offset && src_offset < 4)
               src_offset += 4;
            if (dst_offset && dst_offset < 4)
               dst_offset += 4;

            if (!is_copy && dst_offset != src_offset)
               continue;

            if (test_flavor == TEST_FILL_VRAM_12B || test_flavor == TEST_FILL_GTT_12B) {
               if ((method != METHOD_DEFAULT && method != METHOD_COMPUTE_3DW &&
                    method != METHOD_COMPUTE_4DW) || dst_offset % 4)
                  continue;

               dwords_per_thread = method == METHOD_COMPUTE_3DW ? 3 : 4;
               clear_value_size = 12;
            } else {
               if (method == METHOD_COMPUTE_3DW)
                  continue;

               dwords_per_thread = method == METHOD_COMPUTE_2DW ? 2 : 4;
               clear_value_size = dst_offset % 4 ? 1 : 4;
            }

            printf("%-14s, %-7s, %-11s,", test_strings[test_flavor], method_strings[method],
                   align_info[align].string);

            for (unsigned size = MIN_SIZE; size <= MAX_SIZE; size <<= SIZE_SHIFT) {
               struct pipe_resource *dst, *src;
               enum pipe_resource_usage dst_usage = PIPE_USAGE_DEFAULT;
               enum pipe_resource_usage src_usage = PIPE_USAGE_DEFAULT;

               if (test_flavor == TEST_FILL_GTT || test_flavor == TEST_FILL_GTT_12B ||
                   test_flavor == TEST_COPY_VRAM_GTT)
                  dst_usage = PIPE_USAGE_STREAM;

               if (test_flavor == TEST_COPY_GTT_VRAM)
                  src_usage = PIPE_USAGE_STREAM;

               /* Don't test large sizes with GTT because it's slow. */
               if ((dst_usage == PIPE_USAGE_STREAM || src_usage == PIPE_USAGE_STREAM) &&
                   size > 32 * 1024 * 1024) {
                  printf("%8s,", "n/a");
                  continue;
               }

               dst = pipe_aligned_buffer_create(screen, 0, dst_usage, dst_offset + size, 256);
               src = is_copy ? pipe_aligned_buffer_create(screen, 0, src_usage, src_offset + size, 256) : NULL;

               struct pipe_query *q = ctx->create_query(ctx, PIPE_QUERY_TIME_ELAPSED, 0);
               bool success = true;

               /* Run tests. */
               for (unsigned iter = 0; iter < WARMUP_RUNS + NUM_RUNS; iter++) {
                  const uint32_t clear_value[4] = {0x12345678, 0x23456789, 0x34567890, 0x45678901};

                  if (iter == WARMUP_RUNS)
                     ctx->begin_query(ctx, q);

                  if (method == METHOD_DEFAULT) {
                     if (is_copy) {
                        si_copy_buffer(sctx, dst, src, dst_offset, src_offset, size,
                                       SI_OP_SYNC_BEFORE_AFTER);
                     } else {
                        sctx->b.clear_buffer(&sctx->b, dst, dst_offset, size, &clear_value,
                                             clear_value_size);
                     }
                  } else if (method == METHOD_CP_DMA) {
                     /* CP DMA */
                     if (is_copy) {
                        si_cp_dma_copy_buffer(sctx, dst, src, dst_offset, src_offset, size,
                                              SI_OP_SYNC_BEFORE_AFTER, SI_COHERENCY_SHADER, L2_LRU);
                     } else {
                        /* CP DMA clears must be aligned to 4 bytes. */
                        if (dst_offset % 4 || size % 4) {
                           success = false;
                           continue;
                        }
                        assert(clear_value_size == 4);
                        si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, dst, dst_offset, size,
                                               clear_value[0], SI_OP_SYNC_BEFORE_AFTER,
                                               SI_COHERENCY_SHADER, L2_LRU);
                     }
                  } else {
                     /* Compute */
                     success &=
                        si_compute_clear_copy_buffer(sctx, dst, dst_offset, src, src_offset,
                                                     size, clear_value, clear_value_size,
                                                     SI_OP_SYNC_BEFORE_AFTER, SI_COHERENCY_SHADER,
                                                     dwords_per_thread, false);
                  }

                  sctx->flags |= SI_CONTEXT_INV_L2;
               }

               ctx->end_query(ctx, q);

               pipe_resource_reference(&dst, NULL);
               pipe_resource_reference(&src, NULL);

               /* Get results. */
               union pipe_query_result result;

               ctx->get_query_result(ctx, q, true, &result);
               ctx->destroy_query(ctx, q);

               if (success) {
                  double GB = 1024.0 * 1024.0 * 1024.0;
                  double seconds = result.u64 / (double)NUM_RUNS / (1000.0 * 1000.0 * 1000.0);
                  double GBps = (size / GB) / seconds * (test_flavor == TEST_COPY_VRAM_VRAM ? 2 : 1);
                  printf("%8.2f,", GBps);
               } else {
                  printf("%8s,", "n/a");
               }
            }
            puts("");
         }
      }
   }

   ctx->destroy(ctx);
   exit(0);
}

void
si_test_mem_perf(struct si_screen *sscreen)
{
   struct radeon_winsys *ws = sscreen->ws;
   const size_t buffer_size = 16 * 1024 * 1024;
   const enum radeon_bo_domain domains[] = { 0, RADEON_DOMAIN_VRAM, RADEON_DOMAIN_GTT };
   const uint64_t flags[] = { 0, RADEON_FLAG_GTT_WC };
   const int n_loops = 2;
   char *title[] = { "Write To", "Read From", "Stream From" };
   char *domain_str[] = { "RAM", "VRAM", "GTT" };

   for (int i = 0; i < 3; i++) {
      printf("| %12s", title[i]);

      printf(" | Size (kB) | Flags |");
      for (int l = 0; l < n_loops; l++)
          printf(" Run %d (MB/s) |", l + 1);
      printf("\n");

      printf("|--------------|-----------|-------|");
      for (int l = 0; l < n_loops; l++)
          printf("--------------|");
      printf("\n");
      for (int j = 0; j < ARRAY_SIZE(domains); j++) {
         enum radeon_bo_domain domain = domains[j];
         for (int k = 0; k < ARRAY_SIZE(flags); k++) {
            if (k && domain != RADEON_DOMAIN_GTT)
               continue;

            struct pb_buffer_lean *bo = NULL;
            void *ptr = NULL;

            if (domains[j]) {
               bo = ws->buffer_create(ws, buffer_size, 4096, domains[j],
                                      RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_NO_SUBALLOC |
                                      flags[k]);
               if (!bo)
                  continue;

               ptr = ws->buffer_map(ws, bo, NULL, RADEON_MAP_TEMPORARY | (i ? PIPE_MAP_READ : PIPE_MAP_WRITE));
               if (!ptr) {
                  radeon_bo_reference(ws, &bo, NULL);
                  continue;
               }
            } else {
               ptr = malloc(buffer_size);
            }

            printf("| %12s |", domain_str[j]);

            printf("%10zu |", buffer_size / 1024);

            printf(" %5s |", domain == RADEON_DOMAIN_VRAM ? "(WC)" : (k == 0 ? "" : "WC "));

            int *cpu = calloc(1, buffer_size);
            memset(cpu, 'c', buffer_size);
            fflush(stdout);

            int64_t before, after;

            for (int loop = 0; loop < n_loops; loop++) {
               before = os_time_get_nano();

               switch (i) {
               case 0:
                  memcpy(ptr, cpu, buffer_size);
                  break;
               case 1:
                  memcpy(cpu, ptr, buffer_size);
                  break;
               case 2:
               default:
                  util_streaming_load_memcpy(cpu, ptr, buffer_size);
                  break;
               }

               after = os_time_get_nano();

               /* Pretend to do something with the result to make sure it's
                * not skipped.
                */
               if (debug_get_num_option("AMD_DEBUG", 0) == 0x123)
                   assert(memcmp(ptr, cpu, buffer_size));

               float dt = (after - before) / (1000000000.0);
               float bandwidth = (buffer_size / (1024 * 1024)) / dt;

               printf("%13.3f |", bandwidth);
            }
            printf("\n");

            free(cpu);
            if (bo) {
               ws->buffer_unmap(ws, bo);
               radeon_bo_reference(ws, &bo, NULL);
            } else {
               free(ptr);
            }
         }
      }
      printf("\n");
   }


   exit(0);
}
