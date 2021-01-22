/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */


#include "si_pipe.h"
#include "si_build_pm4.h"

#include "ac_rgp.h"
#include "ac_sqtt.h"
#include "util/u_memory.h"

static void
si_emit_spi_config_cntl(struct si_context* sctx,
                        struct radeon_cmdbuf *cs, bool enable);

static inline void
radeon_set_privileged_config_reg(struct radeon_cmdbuf *cs,
                                 unsigned reg,
                                 unsigned value)
{
   assert(reg < CIK_UCONFIG_REG_OFFSET);

   radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
   radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_IMM) |
               COPY_DATA_DST_SEL(COPY_DATA_PERF));
   radeon_emit(cs, value);
   radeon_emit(cs, 0); /* unused */
   radeon_emit(cs, reg >> 2);
   radeon_emit(cs, 0); /* unused */
}

static bool
si_thread_trace_init_bo(struct si_context *sctx)
{
   struct radeon_winsys *ws = sctx->ws;
   uint64_t size;

   /* The buffer size and address need to be aligned in HW regs. Align the
    * size as early as possible so that we do all the allocation & addressing
    * correctly. */
   sctx->thread_trace->buffer_size = align64(sctx->thread_trace->buffer_size,
                                             1u << SQTT_BUFFER_ALIGN_SHIFT);

   /* Compute total size of the thread trace BO for 4 SEs. */
   size = align64(sizeof(struct ac_thread_trace_info) * 4,
                  1 << SQTT_BUFFER_ALIGN_SHIFT);
   size += sctx->thread_trace->buffer_size * 4;

   sctx->thread_trace->bo =
      ws->buffer_create(ws, size, 4096,
                        RADEON_DOMAIN_VRAM,
                        RADEON_FLAG_NO_INTERPROCESS_SHARING |
                        RADEON_FLAG_GTT_WC |
                        RADEON_FLAG_NO_SUBALLOC);
   if (!sctx->thread_trace->bo)
      return false;

   return true;
}

static void
si_emit_thread_trace_start(struct si_context* sctx,
                           struct radeon_cmdbuf *cs,
                           uint32_t queue_family_index)
{
   struct si_screen *sscreen = sctx->screen;
   uint32_t shifted_size = sctx->thread_trace->buffer_size >> SQTT_BUFFER_ALIGN_SHIFT;
   unsigned max_se = sscreen->info.max_se;

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t va = sctx->ws->buffer_get_virtual_address(sctx->thread_trace->bo);
      uint64_t data_va = ac_thread_trace_get_data_va(sctx->thread_trace, va, se);
      uint64_t shifted_va = data_va >> SQTT_BUFFER_ALIGN_SHIFT;

      /* Target SEx and SH0. */
      radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                             S_030800_SE_INDEX(se) |
                             S_030800_SH_INDEX(0) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));

      if (sctx->chip_class == GFX10) {
         /* Order seems important for the following 2 registers. */
         radeon_set_privileged_config_reg(cs, R_008D04_SQ_THREAD_TRACE_BUF0_SIZE,
                                          S_008D04_SIZE(shifted_size) |
                                          S_008D04_BASE_HI(shifted_va >> 32));

         radeon_set_privileged_config_reg(cs, R_008D00_SQ_THREAD_TRACE_BUF0_BASE,
                                          S_008D00_BASE_LO(shifted_va));

         radeon_set_privileged_config_reg(cs, R_008D14_SQ_THREAD_TRACE_MASK,
                                          S_008D14_WTYPE_INCLUDE(0x7f) | /* all shader stages */
                                          S_008D14_SA_SEL(0) |
                                          S_008D14_WGP_SEL(0) |
                                          S_008D14_SIMD_SEL(0));

         radeon_set_privileged_config_reg(cs, R_008D18_SQ_THREAD_TRACE_TOKEN_MASK,
                      S_008D18_REG_INCLUDE(V_008D18_REG_INCLUDE_SQDEC |
                                           V_008D18_REG_INCLUDE_SHDEC |
                                           V_008D18_REG_INCLUDE_GFXUDEC |
                                           V_008D18_REG_INCLUDE_CONTEXT |
                                           V_008D18_REG_INCLUDE_COMP |
                                           V_008D18_REG_INCLUDE_CONTEXT |
                                           V_008D18_REG_INCLUDE_CONFIG) |
                      S_008D18_TOKEN_EXCLUDE(V_008D18_TOKEN_EXCLUDE_PERF));

         /* Should be emitted last (it enables thread traces). */
         radeon_set_privileged_config_reg(cs, R_008D1C_SQ_THREAD_TRACE_CTRL,
                                          S_008D1C_MODE(1) |
                                          S_008D1C_HIWATER(5) |
                                          S_008D1C_UTIL_TIMER(1) |
                                          S_008D1C_RT_FREQ(2) | /* 4096 clk */
                                          S_008D1C_DRAW_EVENT_EN(1) |
                                          S_008D1C_REG_STALL_EN(1) |
                                          S_008D1C_SPI_STALL_EN(1) |
                                          S_008D1C_SQ_STALL_EN(1) |
                                          S_008D1C_REG_DROP_ON_STALL(0));
      } else {
         /* Order seems important for the following 4 registers. */
         radeon_set_uconfig_reg(cs, R_030CDC_SQ_THREAD_TRACE_BASE2,
                                S_030CDC_ADDR_HI(shifted_va >> 32));

         radeon_set_uconfig_reg(cs, R_030CC0_SQ_THREAD_TRACE_BASE,
                                S_030CC0_ADDR(shifted_va));

         radeon_set_uconfig_reg(cs, R_030CC4_SQ_THREAD_TRACE_SIZE,
                                S_030CC4_SIZE(shifted_size));

         radeon_set_uconfig_reg(cs, R_030CD4_SQ_THREAD_TRACE_CTRL,
                                S_030CD4_RESET_BUFFER(1));

         uint32_t thread_trace_mask = S_030CC8_CU_SEL(2) |
                                      S_030CC8_SH_SEL(0) |
                                      S_030CC8_SIMD_EN(0xf) |
                                      S_030CC8_VM_ID_MASK(0) |
                                      S_030CC8_REG_STALL_EN(1) |
                                      S_030CC8_SPI_STALL_EN(1) |
                                      S_030CC8_SQ_STALL_EN(1);

         radeon_set_uconfig_reg(cs, R_030CC8_SQ_THREAD_TRACE_MASK,
                                thread_trace_mask);

         /* Trace all tokens and registers. */
         radeon_set_uconfig_reg(cs, R_030CCC_SQ_THREAD_TRACE_TOKEN_MASK,
                                S_030CCC_TOKEN_MASK(0xbfff) |
                                S_030CCC_REG_MASK(0xff) |
                                S_030CCC_REG_DROP_ON_STALL(0));

         /* Enable SQTT perf counters for all CUs. */
         radeon_set_uconfig_reg(cs, R_030CD0_SQ_THREAD_TRACE_PERF_MASK,
                                S_030CD0_SH0_MASK(0xffff) |
                                S_030CD0_SH1_MASK(0xffff));

         radeon_set_uconfig_reg(cs, R_030CE0_SQ_THREAD_TRACE_TOKEN_MASK2,
                                S_030CE0_INST_MASK(0xffffffff));

         radeon_set_uconfig_reg(cs, R_030CEC_SQ_THREAD_TRACE_HIWATER,
                                S_030CEC_HIWATER(4));

         if (sctx->chip_class == GFX9) {
            /* Reset thread trace status errors. */
            radeon_set_uconfig_reg(cs, R_030CE8_SQ_THREAD_TRACE_STATUS,
                                   S_030CE8_UTC_ERROR(0));
         }

         /* Enable the thread trace mode. */
         uint32_t thread_trace_mode =
            S_030CD8_MASK_PS(1) |
            S_030CD8_MASK_VS(1) |
            S_030CD8_MASK_GS(1) |
            S_030CD8_MASK_ES(1) |
            S_030CD8_MASK_HS(1) |
            S_030CD8_MASK_LS(1) |
            S_030CD8_MASK_CS(1) |
            S_030CD8_AUTOFLUSH_EN(1) | /* periodically flush SQTT data to memory */
            S_030CD8_MODE(1);

         if (sctx->chip_class == GFX9) {
            /* Count SQTT traffic in TCC perf counters. */
            thread_trace_mode |= S_030CD8_TC_PERF_EN(1);
         }

         radeon_set_uconfig_reg(cs, R_030CD8_SQ_THREAD_TRACE_MODE,
                                thread_trace_mode);
      }
   }

   /* Restore global broadcasting. */
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                          S_030800_SE_BROADCAST_WRITES(1) |
                             S_030800_SH_BROADCAST_WRITES(1) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));

   /* Start the thread trace with a different event based on the queue. */
   if (queue_family_index == RING_COMPUTE) {
      radeon_set_sh_reg(cs, R_00B878_COMPUTE_THREAD_TRACE_ENABLE,
                        S_00B878_THREAD_TRACE_ENABLE(1));
   } else {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_START) | EVENT_INDEX(0));
   }
}

static const uint32_t gfx9_thread_trace_info_regs[] =
{
   R_030CE4_SQ_THREAD_TRACE_WPTR,
   R_030CE8_SQ_THREAD_TRACE_STATUS,
   R_030CF0_SQ_THREAD_TRACE_CNTR,
};

static const uint32_t gfx10_thread_trace_info_regs[] =
{
   R_008D10_SQ_THREAD_TRACE_WPTR,
   R_008D20_SQ_THREAD_TRACE_STATUS,
   R_008D24_SQ_THREAD_TRACE_DROPPED_CNTR,
};

static void
si_copy_thread_trace_info_regs(struct si_context* sctx,
             struct radeon_cmdbuf *cs,
             unsigned se_index)
{
   const uint32_t *thread_trace_info_regs = NULL;

   switch (sctx->chip_class) {
   case GFX10:
      thread_trace_info_regs = gfx10_thread_trace_info_regs;
      break;
   case GFX9:
      thread_trace_info_regs = gfx9_thread_trace_info_regs;
      break;
   default:
      unreachable("Unsupported chip_class");
   }

   /* Get the VA where the info struct is stored for this SE. */
   uint64_t va = sctx->ws->buffer_get_virtual_address(sctx->thread_trace->bo);
   uint64_t info_va = ac_thread_trace_get_info_va(va, se_index);

   /* Copy back the info struct one DWORD at a time. */
   for (unsigned i = 0; i < 3; i++) {
      radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
      radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_PERF) |
                      COPY_DATA_DST_SEL(COPY_DATA_TC_L2) |
                  COPY_DATA_WR_CONFIRM);
      radeon_emit(cs, thread_trace_info_regs[i] >> 2);
      radeon_emit(cs, 0); /* unused */
      radeon_emit(cs, (info_va + i * 4));
      radeon_emit(cs, (info_va + i * 4) >> 32);
   }
}



static void
si_emit_thread_trace_stop(struct si_context *sctx,
                          struct radeon_cmdbuf *cs,
                          uint32_t queue_family_index)
{
   unsigned max_se = sctx->screen->info.max_se;

   /* Stop the thread trace with a different event based on the queue. */
   if (queue_family_index == RING_COMPUTE) {
      radeon_set_sh_reg(cs, R_00B878_COMPUTE_THREAD_TRACE_ENABLE,
                        S_00B878_THREAD_TRACE_ENABLE(0));
   } else {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_STOP) | EVENT_INDEX(0));
   }

   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_FINISH) | EVENT_INDEX(0));

   for (unsigned se = 0; se < max_se; se++) {
      /* Target SEi and SH0. */
      radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                             S_030800_SE_INDEX(se) |
                             S_030800_SH_INDEX(0) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));

      if (sctx->chip_class == GFX10) {
         /* Make sure to wait for the trace buffer. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(cs, WAIT_REG_MEM_NOT_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_008D20_SQ_THREAD_TRACE_STATUS >> 2);  /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0); /* reference value */
         radeon_emit(cs, S_008D20_FINISH_DONE(1)); /* mask */
         radeon_emit(cs, 4); /* poll interval */

         /* Disable the thread trace mode. */
         radeon_set_privileged_config_reg(cs, R_008D1C_SQ_THREAD_TRACE_CTRL,
                                          S_008D1C_MODE(0));

         /* Wait for thread trace completion. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(cs, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_008D20_SQ_THREAD_TRACE_STATUS >> 2);  /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0); /* reference value */
         radeon_emit(cs, S_008D20_BUSY(1)); /* mask */
         radeon_emit(cs, 4); /* poll interval */
      } else {
         /* Disable the thread trace mode. */
         radeon_set_uconfig_reg(cs, R_030CD8_SQ_THREAD_TRACE_MODE,
                                S_030CD8_MODE(0));

         /* Wait for thread trace completion. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(cs, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_030CE8_SQ_THREAD_TRACE_STATUS >> 2);  /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0); /* reference value */
         radeon_emit(cs, S_030CE8_BUSY(1)); /* mask */
         radeon_emit(cs, 4); /* poll interval */
      }

      si_copy_thread_trace_info_regs(sctx, cs, se);
   }

   /* Restore global broadcasting. */
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                          S_030800_SE_BROADCAST_WRITES(1) |
                             S_030800_SH_BROADCAST_WRITES(1) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));
}

static void
si_thread_trace_start(struct si_context *sctx, int family, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys *ws = sctx->ws;

   switch (family) {
      case RING_GFX:
         radeon_emit(cs, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         radeon_emit(cs, CC0_UPDATE_LOAD_ENABLES(1));
         radeon_emit(cs, CC1_UPDATE_SHADOW_ENABLES(1));
         break;
      case RING_COMPUTE:
         radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
         radeon_emit(cs, 0);
         break;
      }

   ws->cs_add_buffer(cs,
                     sctx->thread_trace->bo,
                     RADEON_USAGE_READWRITE,
                     RADEON_DOMAIN_VRAM,
                     0);

   si_cp_dma_wait_for_idle(sctx, cs);

   /* Make sure to wait-for-idle before starting SQTT. */
   sctx->flags |=
      SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH |
      SI_CONTEXT_INV_ICACHE | SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE |
      SI_CONTEXT_INV_L2;
   sctx->emit_cache_flush(sctx, cs);

   si_inhibit_clockgating(sctx, cs, true);

   /* Enable SQG events that collects thread trace data. */
   si_emit_spi_config_cntl(sctx, cs, true);

   si_emit_thread_trace_start(sctx, cs, family);
}

static void
si_thread_trace_stop(struct si_context *sctx, int family, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys *ws = sctx->ws;
   switch (family) {
      case RING_GFX:
         radeon_emit(sctx->thread_trace->stop_cs[family], PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         radeon_emit(sctx->thread_trace->stop_cs[family], CC0_UPDATE_LOAD_ENABLES(1));
         radeon_emit(sctx->thread_trace->stop_cs[family], CC1_UPDATE_SHADOW_ENABLES(1));
         break;
      case RING_COMPUTE:
         radeon_emit(sctx->thread_trace->stop_cs[family], PKT3(PKT3_NOP, 0, 0));
         radeon_emit(sctx->thread_trace->stop_cs[family], 0);
         break;
   }
   ws->cs_add_buffer(cs,
                     sctx->thread_trace->bo,
                     RADEON_USAGE_READWRITE,
                     RADEON_DOMAIN_VRAM,
                     0);

   si_cp_dma_wait_for_idle(sctx, cs);

   /* Make sure to wait-for-idle before stopping SQTT. */
   sctx->flags |=
      SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH |
      SI_CONTEXT_INV_ICACHE | SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE |
      SI_CONTEXT_INV_L2;
   sctx->emit_cache_flush(sctx, cs);

   si_emit_thread_trace_stop(sctx, cs, family);

   /* Restore previous state by disabling SQG events. */
   si_emit_spi_config_cntl(sctx, cs, false);

   si_inhibit_clockgating(sctx, cs, false);
}


static void
si_thread_trace_init_cs(struct si_context *sctx)
{
   struct radeon_winsys *ws = sctx->ws;

   /* Thread trace start CS (only handles RING_GFX). */
   sctx->thread_trace->start_cs[RING_GFX] = CALLOC_STRUCT(radeon_cmdbuf);
   if (!ws->cs_create(sctx->thread_trace->start_cs[RING_GFX],
                      sctx->ctx, RING_GFX, NULL, NULL, 0)) {
      free(sctx->thread_trace->start_cs[RING_GFX]);
      sctx->thread_trace->start_cs[RING_GFX] = NULL;
      return;
   }

   si_thread_trace_start(sctx, RING_GFX, sctx->thread_trace->start_cs[RING_GFX]);

   /* Thread trace stop CS. */
   sctx->thread_trace->stop_cs[RING_GFX] = CALLOC_STRUCT(radeon_cmdbuf);
   if (!ws->cs_create(sctx->thread_trace->stop_cs[RING_GFX],
                      sctx->ctx, RING_GFX, NULL, NULL, 0)) {
      free(sctx->thread_trace->start_cs[RING_GFX]);
      sctx->thread_trace->start_cs[RING_GFX] = NULL;
      free(sctx->thread_trace->stop_cs[RING_GFX]);
      sctx->thread_trace->stop_cs[RING_GFX] = NULL;
      return;
   }

   si_thread_trace_stop(sctx, RING_GFX, sctx->thread_trace->stop_cs[RING_GFX]);
}

static void
si_begin_thread_trace(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   struct radeon_cmdbuf *cs = sctx->thread_trace->start_cs[RING_GFX];
   sctx->ws->cs_flush(cs, 0, NULL);
}

static void
si_end_thread_trace(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   struct radeon_cmdbuf *cs = sctx->thread_trace->stop_cs[RING_GFX];
   sctx->ws->cs_flush(cs, 0, &sctx->last_sqtt_fence);
}

static bool
si_get_thread_trace(struct si_context *sctx,
                    struct ac_thread_trace *thread_trace)
{
   unsigned max_se = sctx->screen->info.max_se;

   memset(thread_trace, 0, sizeof(*thread_trace));
   thread_trace->num_traces = max_se;

   sctx->thread_trace->ptr = sctx->ws->buffer_map(sctx->thread_trace->bo,
                                                          NULL,
                                                          PIPE_MAP_READ);

   if (!sctx->thread_trace->ptr)
      return false;

   void *thread_trace_ptr = sctx->thread_trace->ptr;

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t info_offset = ac_thread_trace_get_info_offset(se);
      uint64_t data_offset = ac_thread_trace_get_data_offset(sctx->thread_trace, se);
      void *info_ptr = thread_trace_ptr + info_offset;
      void *data_ptr = thread_trace_ptr + data_offset;
      struct ac_thread_trace_info *info =
         (struct ac_thread_trace_info *)info_ptr;

      struct ac_thread_trace_se thread_trace_se = {0};

      if (!ac_is_thread_trace_complete(&sctx->screen->info, info)) {
         uint32_t expected_size =
            ac_get_expected_buffer_size(&sctx->screen->info, info);
         uint32_t available_size = (info->cur_offset * 32) / 1024;

         fprintf(stderr, "Failed to get the thread trace "
                 "because the buffer is too small. The "
                 "hardware needs %d KB but the "
                 "buffer size is %d KB.\n",
                 expected_size, available_size);
         fprintf(stderr, "Please update the buffer size with "
                 "AMD_THREAD_TRACE_BUFFER_SIZE=<size_in_kbytes>\n");
         return false;
      }

      thread_trace_se.data_ptr = data_ptr;
      thread_trace_se.info = *info;
      thread_trace_se.shader_engine = se;
      thread_trace_se.compute_unit = 0;

      thread_trace->traces[se] = thread_trace_se;
   }

   return true;
}


bool
si_init_thread_trace(struct si_context *sctx)
{
   static bool warn_once = true;
   if (warn_once) {
      fprintf(stderr, "*************************************************\n");
      fprintf(stderr, "* WARNING: Thread trace support is experimental *\n");
      fprintf(stderr, "*************************************************\n");
      warn_once = false;
   }

   sctx->thread_trace = CALLOC_STRUCT(ac_thread_trace_data);

   if (sctx->chip_class < GFX8) {
      fprintf(stderr, "GPU hardware not supported: refer to "
              "the RGP documentation for the list of "
              "supported GPUs!\n");
      return false;
   }

   if (sctx->chip_class > GFX10) {
      fprintf(stderr, "radeonsi: Thread trace is not supported "
              "for that GPU!\n");
      return false;
   }

   /* Default buffer size set to 1MB per SE. */
   sctx->thread_trace->buffer_size = debug_get_num_option("AMD_THREAD_TRACE_BUFFER_SIZE", 1024) * 1024;
   sctx->thread_trace->start_frame = 10;

   const char *trigger_file = getenv("AMD_THREAD_TRACE_TRIGGER");
   if (trigger_file) {
      sctx->thread_trace->trigger_file = strdup(trigger_file);
      sctx->thread_trace->start_frame = -1;
   }

   if (!si_thread_trace_init_bo(sctx))
      return false;

   si_thread_trace_init_cs(sctx);

   return true;
}

void
si_destroy_thread_trace(struct si_context *sctx)
{
  struct si_screen *sscreen = sctx->screen;
   struct pb_buffer *bo = sctx->thread_trace->bo;
   pb_reference(&bo, NULL);

   if (sctx->thread_trace->trigger_file)
      free(sctx->thread_trace->trigger_file);
   sscreen->ws->cs_destroy(sctx->thread_trace->start_cs[RING_GFX]);
   sscreen->ws->cs_destroy(sctx->thread_trace->stop_cs[RING_GFX]);
   free(sctx->thread_trace);
   sctx->thread_trace = NULL;
}

static uint64_t num_frames = 0;

void
si_handle_thread_trace(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   /* Should we enable SQTT yet? */
   if (!sctx->thread_trace_enabled) {
      bool frame_trigger = num_frames == sctx->thread_trace->start_frame;
      bool file_trigger = false;
      if (sctx->thread_trace->trigger_file &&
          access(sctx->thread_trace->trigger_file, W_OK) == 0) {
         if (unlink(sctx->thread_trace->trigger_file) == 0) {
            file_trigger = true;
         } else {
            /* Do not enable tracing if we cannot remove the file,
             * because by then we'll trace every frame.
             */
            fprintf(stderr, "radeonsi: could not remove thread trace trigger file, ignoring\n");
         }
      }

      if (frame_trigger || file_trigger) {
         /* Wait for last submission */
         sctx->ws->fence_wait(sctx->ws, sctx->last_gfx_fence, PIPE_TIMEOUT_INFINITE);

         /* Start SQTT */
         si_begin_thread_trace(sctx, rcs);

         sctx->thread_trace_enabled = true;
         sctx->thread_trace->start_frame = -1;
      }
   } else {
      struct ac_thread_trace thread_trace = {0};

      /* Stop SQTT */
      si_end_thread_trace(sctx, rcs);
      sctx->thread_trace_enabled = false;
      sctx->thread_trace->start_frame = -1;
      assert (sctx->last_sqtt_fence);

      /* Wait for SQTT to finish and read back the bo */
      if (sctx->ws->fence_wait(sctx->ws, sctx->last_sqtt_fence, PIPE_TIMEOUT_INFINITE) &&
          si_get_thread_trace(sctx, &thread_trace)) {
         ac_dump_thread_trace(&sctx->screen->info, &thread_trace);
      } else {
         fprintf(stderr, "Failed to read the trace\n");
      }
   }

   num_frames++;
}


static void
si_emit_thread_trace_userdata(struct si_context* sctx,
                              struct radeon_cmdbuf *cs,
                              const void *data, uint32_t num_dwords)
{
   const uint32_t *dwords = (uint32_t *)data;

   while (num_dwords > 0) {
      uint32_t count = MIN2(num_dwords, 2);

      /* Without the perfctr bit the CP might not always pass the
       * write on correctly. */
      radeon_set_uconfig_reg_seq(cs, R_030D08_SQ_THREAD_TRACE_USERDATA_2, count, sctx->chip_class >= GFX10);

      radeon_emit_array(cs, dwords, count);

      dwords += count;
      num_dwords -= count;
   }
}

static void
si_emit_spi_config_cntl(struct si_context* sctx,
           struct radeon_cmdbuf *cs, bool enable)
{
   if (sctx->chip_class >= GFX9) {
      uint32_t spi_config_cntl = S_031100_GPR_WRITE_PRIORITY(0x2c688) |
                                 S_031100_EXP_PRIORITY_ORDER(3) |
                                 S_031100_ENABLE_SQG_TOP_EVENTS(enable) |
                                 S_031100_ENABLE_SQG_BOP_EVENTS(enable);

      if (sctx->chip_class == GFX10)
         spi_config_cntl |= S_031100_PS_PKR_PRIORITY_CNTL(3);

      radeon_set_uconfig_reg(cs, R_031100_SPI_CONFIG_CNTL, spi_config_cntl);
   } else {
      /* SPI_CONFIG_CNTL is a protected register on GFX6-GFX8. */
      radeon_set_privileged_config_reg(cs, R_009100_SPI_CONFIG_CNTL,
                                       S_009100_ENABLE_SQG_TOP_EVENTS(enable) |
                                       S_009100_ENABLE_SQG_BOP_EVENTS(enable));
   }
}

void
si_sqtt_write_event_marker(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                           enum rgp_sqtt_marker_event_type api_type,
                           uint32_t vertex_offset_user_data,
                           uint32_t instance_offset_user_data,
                           uint32_t draw_index_user_data)
{
   static uint32_t num_events = 0;
   struct rgp_sqtt_marker_event marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.api_type = api_type;
   marker.cmd_id = num_events++;
   marker.cb_id = 0;

   if (vertex_offset_user_data == UINT_MAX ||
       instance_offset_user_data == UINT_MAX) {
      vertex_offset_user_data = 0;
      instance_offset_user_data = 0;
   }

   if (draw_index_user_data == UINT_MAX)
      draw_index_user_data = vertex_offset_user_data;

   marker.vertex_offset_reg_idx = vertex_offset_user_data;
   marker.instance_offset_reg_idx = instance_offset_user_data;
   marker.draw_index_reg_idx = draw_index_user_data;

   si_emit_thread_trace_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
}
