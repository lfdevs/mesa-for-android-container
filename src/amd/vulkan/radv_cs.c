/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based on si_state.c
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_cs.h"
#include "radv_buffer.h"
#include "radv_debug.h"
#include "radv_sdma.h"
#include "radv_shader.h"
#include "radv_sqtt.h"
#include "sid.h"

void
radv_cs_emit_write_event_eop(struct radv_cmd_stream *cs, enum amd_gfx_level gfx_level, unsigned event,
                             unsigned event_flags, unsigned dst_sel, unsigned int_sel, unsigned data_sel, uint64_t va,
                             uint32_t new_fence, uint64_t gfx9_eop_bug_va)
{
   assert(cs->hw_ip == AMD_IP_GFX || cs->hw_ip == AMD_IP_COMPUTE);

   /* The EOP bug is specific to GFX9. Though, RadeonSI also implements it for GFX6-8 but it
    * shouldn't be necessary because it's using SURFACE_SYNC to flush L2. See
    * waEventWriteEopPrematureL2Inv in PAL.
    */
   const uint64_t eop_bug_va = gfx_level >= GFX9 ? gfx9_eop_bug_va : va;

   ac_emit_cp_release_mem(cs->b, gfx_level, cs->hw_ip, event, event_flags, dst_sel, int_sel, data_sel, va, new_fence,
                          eop_bug_va);
}

static void
gfx10_cs_emit_cache_flush(struct radv_cmd_stream *cs, enum amd_gfx_level gfx_level, uint32_t *flush_cnt,
                          uint64_t flush_va, enum radv_cmd_flush_bits flush_bits, enum rgp_flush_bits *sqtt_flush_bits)
{
   const bool is_mec = cs->hw_ip == AMD_IP_COMPUTE;
   uint32_t gcr_cntl = 0;
   unsigned cb_db_event = 0;

   /* We don't need these. */
   assert(!(flush_bits & (RADV_CMD_FLAG_VGT_STREAMOUT_SYNC)));

   if (flush_bits & RADV_CMD_FLAG_INV_ICACHE) {
      gcr_cntl |= S_586_GLI_INV(V_586_GLI_ALL);

      *sqtt_flush_bits |= RGP_FLUSH_INVAL_ICACHE;
   }
   if (flush_bits & RADV_CMD_FLAG_INV_SCACHE) {
      gcr_cntl |= S_586_GLK_INV(1);

      *sqtt_flush_bits |= RGP_FLUSH_INVAL_SMEM_L0;
   }
   if (flush_bits & RADV_CMD_FLAG_INV_VCACHE) {
      gcr_cntl |= S_586_GLV_INV(1);

      *sqtt_flush_bits |= RGP_FLUSH_INVAL_VMEM_L0;
   }
   if (flush_bits & (RADV_CMD_FLAG_INV_SCACHE | RADV_CMD_FLAG_INV_VCACHE) && gfx_level < GFX12) {
      gcr_cntl |= S_586_GL1_INV(1);

      *sqtt_flush_bits |= RGP_FLUSH_INVAL_L1;
   }

   if (flush_bits & RADV_CMD_FLAG_INV_L2) {
      /* Writeback and invalidate everything in L2. */
      gcr_cntl |= S_586_GL2_INV(1) | S_586_GL2_WB(1);

      *sqtt_flush_bits |= RGP_FLUSH_INVAL_L2;
   } else if (flush_bits & RADV_CMD_FLAG_WB_L2) {
      /* Writeback but do not invalidate.
       * GLM doesn't support WB alone. If WB is set, INV must be set too.
       */
      gcr_cntl |= S_586_GL2_WB(1);

      *sqtt_flush_bits |= RGP_FLUSH_FLUSH_L2;
   }

   if (gfx_level < GFX12 &&
       (flush_bits & (RADV_CMD_FLAG_INV_L2 | RADV_CMD_FLAG_WB_L2 | RADV_CMD_FLAG_INV_L2_METADATA))) {
      gcr_cntl |= S_586_GLM_INV(1) | S_586_GLM_WB(1);
   }

   if (flush_bits & (RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_DB)) {
      /* TODO: trigger on RADV_CMD_FLAG_FLUSH_AND_INV_CB_META */
      if (gfx_level < GFX12 && flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB) {
         /* Flush CMASK/FMASK/DCC. Will wait for idle later. */
         radeon_begin(cs);
         radeon_event_write(V_028A90_FLUSH_AND_INV_CB_META);
         radeon_end();

         *sqtt_flush_bits |= RGP_FLUSH_FLUSH_CB | RGP_FLUSH_INVAL_CB;
      }

      /* GFX11 can't flush DB_META and should use a TS event instead. */
      /* TODO: trigger on RADV_CMD_FLAG_FLUSH_AND_INV_DB_META ? */
      if (gfx_level < GFX12 && gfx_level != GFX11 && (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB)) {
         /* Flush HTILE. Will wait for idle later. */
         radeon_begin(cs);
         radeon_event_write(V_028A90_FLUSH_AND_INV_DB_META);
         radeon_end();

         *sqtt_flush_bits |= RGP_FLUSH_FLUSH_DB | RGP_FLUSH_INVAL_DB;
      }

      /* First flush CB/DB, then L1/L2. */
      gcr_cntl |= S_586_SEQ(V_586_SEQ_FORWARD);

      if ((flush_bits & (RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_DB)) ==
          (RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_DB)) {
         cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
      } else if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB) {
         cb_db_event = V_028A90_FLUSH_AND_INV_CB_DATA_TS;
      } else if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB) {
         if (gfx_level == GFX11) {
            cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
         } else {
            cb_db_event = V_028A90_FLUSH_AND_INV_DB_DATA_TS;
         }
      } else {
         assert(0);
      }
   } else {
      /* Wait for graphics shaders to go idle if requested. */
      if (flush_bits & RADV_CMD_FLAG_PS_PARTIAL_FLUSH) {
         radeon_begin(cs);
         radeon_event_write(V_028A90_PS_PARTIAL_FLUSH);
         radeon_end();

         *sqtt_flush_bits |= RGP_FLUSH_PS_PARTIAL_FLUSH;
      } else if (flush_bits & RADV_CMD_FLAG_VS_PARTIAL_FLUSH) {
         radeon_begin(cs);
         radeon_event_write(V_028A90_VS_PARTIAL_FLUSH);
         radeon_end();

         *sqtt_flush_bits |= RGP_FLUSH_VS_PARTIAL_FLUSH;
      }
   }

   if (flush_bits & RADV_CMD_FLAG_CS_PARTIAL_FLUSH) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_CS_PARTIAL_FLUSH);
      radeon_end();

      *sqtt_flush_bits |= RGP_FLUSH_CS_PARTIAL_FLUSH;
   }

   if (cb_db_event) {
      if (gfx_level >= GFX11) {
         /* Send an event that flushes caches. */
         ac_emit_cp_release_mem_pws(cs->b, gfx_level, cs->hw_ip, cb_db_event, gcr_cntl);

         gcr_cntl &= C_586_GLK_WB & C_586_GLK_INV & C_586_GLV_INV & C_586_GL2_INV & C_586_GL2_WB; /* keep SEQ */

         if (gfx_level < GFX12)
            gcr_cntl &= C_586_GLM_WB & C_586_GLM_INV & C_586_GL1_INV;

         /* Wait for the event and invalidate remaining caches if needed. */
         ac_emit_cp_acquire_mem_pws(cs->b, gfx_level, cs->hw_ip, cb_db_event, V_580_CP_PFP, 0, gcr_cntl);

         gcr_cntl = 0; /* all done */
      } else {
         /* CB/DB flush and invalidate (or possibly just a wait for a
          * meta flush) via RELEASE_MEM.
          *
          * Combine this with other cache flushes when possible; this
          * requires affected shaders to be idle, so do it after the
          * CS_PARTIAL_FLUSH before (VS/PS partial flushes are always
          * implied).
          */
         /* Get GCR_CNTL fields, because the encoding is different in RELEASE_MEM. */
         unsigned glm_wb = G_586_GLM_WB(gcr_cntl);
         unsigned glm_inv = G_586_GLM_INV(gcr_cntl);
         unsigned glv_inv = G_586_GLV_INV(gcr_cntl);
         unsigned gl1_inv = G_586_GL1_INV(gcr_cntl);
         assert(G_586_GL2_US(gcr_cntl) == 0);
         assert(G_586_GL2_RANGE(gcr_cntl) == 0);
         assert(G_586_GL2_DISCARD(gcr_cntl) == 0);
         unsigned gl2_inv = G_586_GL2_INV(gcr_cntl);
         unsigned gl2_wb = G_586_GL2_WB(gcr_cntl);
         unsigned gcr_seq = G_586_SEQ(gcr_cntl);

         gcr_cntl &=
            C_586_GLM_WB & C_586_GLM_INV & C_586_GLV_INV & C_586_GL1_INV & C_586_GL2_INV & C_586_GL2_WB; /* keep SEQ */

         assert(flush_cnt);
         (*flush_cnt)++;

         radv_cs_emit_write_event_eop(
            cs, gfx_level, cb_db_event,
            S_490_GLM_WB(glm_wb) | S_490_GLM_INV(glm_inv) | S_490_GLV_INV(glv_inv) | S_490_GL1_INV(gl1_inv) |
               S_490_GL2_INV(gl2_inv) | S_490_GL2_WB(gl2_wb) | S_490_SEQ(gcr_seq),
            EOP_DST_SEL_MEM, EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM, EOP_DATA_SEL_VALUE_32BIT, flush_va, *flush_cnt, 0);

         radv_cp_wait_mem(cs, WAIT_REG_MEM_EQUAL, flush_va, *flush_cnt, 0xffffffff);
      }
   }

   /* VGT state sync */
   if (flush_bits & RADV_CMD_FLAG_VGT_FLUSH) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_VGT_FLUSH);
      radeon_end();
   }

   /* Ignore fields that only modify the behavior of other fields. */
   if (gcr_cntl & C_586_GL2_RANGE & C_586_SEQ & (gfx_level >= GFX12 ? ~0 : C_586_GL1_RANGE)) {
      ac_emit_cp_acquire_mem(cs->b, gfx_level, cs->hw_ip, V_580_CP_PFP, gcr_cntl);
   } else if ((cb_db_event || (flush_bits & (RADV_CMD_FLAG_VS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
                                             RADV_CMD_FLAG_CS_PARTIAL_FLUSH))) &&
              !is_mec) {
      /* We need to ensure that PFP waits as well. */
      ac_emit_cp_pfp_sync_me(cs->b, false);

      *sqtt_flush_bits |= RGP_FLUSH_PFP_SYNC_ME;
   }

   radeon_begin(cs);

   if (flush_bits & RADV_CMD_FLAG_START_PIPELINE_STATS) {
      if (!is_mec) {
         radeon_event_write(V_028A90_PIPELINESTAT_START);
      } else {
         radeon_set_sh_reg(R_00B828_COMPUTE_PIPELINESTAT_ENABLE, S_00B828_PIPELINESTAT_ENABLE(1));
      }
   } else if (flush_bits & RADV_CMD_FLAG_STOP_PIPELINE_STATS) {
      if (!is_mec) {
         radeon_event_write(V_028A90_PIPELINESTAT_STOP);
      } else {
         radeon_set_sh_reg(R_00B828_COMPUTE_PIPELINESTAT_ENABLE, S_00B828_PIPELINESTAT_ENABLE(0));
      }
   }

   radeon_end();
}

void
radv_cs_emit_cache_flush(struct radeon_winsys *ws, struct radv_cmd_stream *cs, enum amd_gfx_level gfx_level,
                         uint32_t *flush_cnt, uint64_t flush_va, enum radv_cmd_flush_bits flush_bits,
                         enum rgp_flush_bits *sqtt_flush_bits, uint64_t gfx9_eop_bug_va)
{
   unsigned cp_coher_cntl = 0;
   uint32_t flush_cb_db = flush_bits & (RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_DB);

   radeon_check_space(ws, cs->b, 128);

   if (gfx_level >= GFX10) {
      /* GFX10 cache flush handling is quite different. */
      gfx10_cs_emit_cache_flush(cs, gfx_level, flush_cnt, flush_va, flush_bits, sqtt_flush_bits);
      return;
   }

   const bool is_mec = cs->hw_ip == AMD_IP_COMPUTE && gfx_level >= GFX7;

   if (flush_bits & RADV_CMD_FLAG_INV_ICACHE) {
      cp_coher_cntl |= S_0085F0_SH_ICACHE_ACTION_ENA(1);
      *sqtt_flush_bits |= RGP_FLUSH_INVAL_ICACHE;
   }
   if (flush_bits & RADV_CMD_FLAG_INV_SCACHE) {
      cp_coher_cntl |= S_0085F0_SH_KCACHE_ACTION_ENA(1);
      *sqtt_flush_bits |= RGP_FLUSH_INVAL_SMEM_L0;
   }

   if (gfx_level <= GFX8) {
      if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB) {
         cp_coher_cntl |= S_0085F0_CB_ACTION_ENA(1) | S_0085F0_CB0_DEST_BASE_ENA(1) | S_0085F0_CB1_DEST_BASE_ENA(1) |
                          S_0085F0_CB2_DEST_BASE_ENA(1) | S_0085F0_CB3_DEST_BASE_ENA(1) |
                          S_0085F0_CB4_DEST_BASE_ENA(1) | S_0085F0_CB5_DEST_BASE_ENA(1) |
                          S_0085F0_CB6_DEST_BASE_ENA(1) | S_0085F0_CB7_DEST_BASE_ENA(1);

         /* Necessary for DCC */
         if (gfx_level >= GFX8) {
            radv_cs_emit_write_event_eop(cs, gfx_level, V_028A90_FLUSH_AND_INV_CB_DATA_TS, 0, EOP_DST_SEL_MEM,
                                         EOP_INT_SEL_NONE, EOP_DATA_SEL_DISCARD, 0, 0, gfx9_eop_bug_va);
         }

         *sqtt_flush_bits |= RGP_FLUSH_FLUSH_CB | RGP_FLUSH_INVAL_CB;
      }
      if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB) {
         cp_coher_cntl |= S_0085F0_DB_ACTION_ENA(1) | S_0085F0_DB_DEST_BASE_ENA(1);

         *sqtt_flush_bits |= RGP_FLUSH_FLUSH_DB | RGP_FLUSH_INVAL_DB;
      }
   }

   if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB_META) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_FLUSH_AND_INV_CB_META);
      radeon_end();

      *sqtt_flush_bits |= RGP_FLUSH_FLUSH_CB | RGP_FLUSH_INVAL_CB;
   }

   if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB_META) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_FLUSH_AND_INV_DB_META);
      radeon_end();

      *sqtt_flush_bits |= RGP_FLUSH_FLUSH_DB | RGP_FLUSH_INVAL_DB;
   }

   if (flush_bits & RADV_CMD_FLAG_PS_PARTIAL_FLUSH) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_PS_PARTIAL_FLUSH);
      radeon_end();

      *sqtt_flush_bits |= RGP_FLUSH_PS_PARTIAL_FLUSH;
   } else if (flush_bits & RADV_CMD_FLAG_VS_PARTIAL_FLUSH) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_VS_PARTIAL_FLUSH);
      radeon_end();

      *sqtt_flush_bits |= RGP_FLUSH_VS_PARTIAL_FLUSH;
   }

   if (flush_bits & RADV_CMD_FLAG_CS_PARTIAL_FLUSH) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_CS_PARTIAL_FLUSH);
      radeon_end();

      *sqtt_flush_bits |= RGP_FLUSH_CS_PARTIAL_FLUSH;
   }

   if (gfx_level == GFX9 && flush_cb_db) {
      unsigned cb_db_event, tc_flags;

      /* Set the CB/DB flush event. */
      cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;

      /* These are the only allowed combinations. If you need to
       * do multiple operations at once, do them separately.
       * All operations that invalidate L2 also seem to invalidate
       * metadata. Volatile (VOL) and WC flushes are not listed here.
       *
       * TC    | TC_WB         = writeback & invalidate L2
       * TC    | TC_WB | TC_NC = writeback & invalidate L2 for MTYPE == NC
       *         TC_WB | TC_NC = writeback L2 for MTYPE == NC
       * TC            | TC_NC = invalidate L2 for MTYPE == NC
       * TC    | TC_MD         = writeback & invalidate L2 metadata (DCC, etc.)
       * TCL1                  = invalidate L1
       */
      tc_flags = EVENT_TC_ACTION_ENA | EVENT_TC_MD_ACTION_ENA;

      *sqtt_flush_bits |= RGP_FLUSH_FLUSH_CB | RGP_FLUSH_INVAL_CB | RGP_FLUSH_FLUSH_DB | RGP_FLUSH_INVAL_DB;

      /* Ideally flush TC together with CB/DB. */
      if (flush_bits & RADV_CMD_FLAG_INV_L2) {
         /* Writeback and invalidate everything in L2. */
         tc_flags = EVENT_TC_ACTION_ENA | EVENT_TC_WB_ACTION_ENA;

         /* Clear the flags. */
         flush_bits &= ~(RADV_CMD_FLAG_INV_L2 | RADV_CMD_FLAG_WB_L2);

         *sqtt_flush_bits |= RGP_FLUSH_INVAL_L2;
      }

      assert(flush_cnt);
      (*flush_cnt)++;

      radv_cs_emit_write_event_eop(cs, gfx_level, cb_db_event, tc_flags, EOP_DST_SEL_MEM,
                                   EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM, EOP_DATA_SEL_VALUE_32BIT, flush_va,
                                   *flush_cnt, gfx9_eop_bug_va);
      radv_cp_wait_mem(cs, WAIT_REG_MEM_EQUAL, flush_va, *flush_cnt, 0xffffffff);
   }

   /* VGT state sync */
   if (flush_bits & RADV_CMD_FLAG_VGT_FLUSH) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_VGT_FLUSH);
      radeon_end();
   }

   /* VGT streamout state sync */
   if (flush_bits & RADV_CMD_FLAG_VGT_STREAMOUT_SYNC) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_VGT_STREAMOUT_SYNC);
      radeon_end();
   }

   /* Make sure ME is idle (it executes most packets) before continuing.
    * This prevents read-after-write hazards between PFP and ME.
    */
   if ((cp_coher_cntl || (flush_bits & (RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                                        RADV_CMD_FLAG_INV_L2 | RADV_CMD_FLAG_WB_L2))) &&
       !is_mec) {
      ac_emit_cp_pfp_sync_me(cs->b, false);

      *sqtt_flush_bits |= RGP_FLUSH_PFP_SYNC_ME;
   }

   if ((flush_bits & RADV_CMD_FLAG_INV_L2) || (gfx_level <= GFX7 && (flush_bits & RADV_CMD_FLAG_WB_L2))) {
      ac_emit_cp_acquire_mem(cs->b, gfx_level, cs->hw_ip, V_580_CP_PFP,
                             cp_coher_cntl | S_0085F0_TC_ACTION_ENA(1) | S_0085F0_TCL1_ACTION_ENA(1) |
                                S_0301F0_TC_WB_ACTION_ENA(gfx_level >= GFX8));
      cp_coher_cntl = 0;

      *sqtt_flush_bits |= RGP_FLUSH_INVAL_L2 | RGP_FLUSH_INVAL_VMEM_L0;
   } else {
      if (flush_bits & RADV_CMD_FLAG_WB_L2) {
         /* WB = write-back
          * NC = apply to non-coherent MTYPEs
          *      (i.e. MTYPE <= 1, which is what we use everywhere)
          *
          * WB doesn't work without NC.
          */
         ac_emit_cp_acquire_mem(cs->b, gfx_level, cs->hw_ip, V_580_CP_PFP,
                                cp_coher_cntl | S_0301F0_TC_WB_ACTION_ENA(1) | S_0301F0_TC_NC_ACTION_ENA(1));
         cp_coher_cntl = 0;

         *sqtt_flush_bits |= RGP_FLUSH_FLUSH_L2 | RGP_FLUSH_INVAL_VMEM_L0;
      }
      if (flush_bits & RADV_CMD_FLAG_INV_VCACHE) {
         ac_emit_cp_acquire_mem(cs->b, gfx_level, cs->hw_ip, V_580_CP_PFP, cp_coher_cntl | S_0085F0_TCL1_ACTION_ENA(1));
         cp_coher_cntl = 0;

         *sqtt_flush_bits |= RGP_FLUSH_INVAL_VMEM_L0;
      }
   }

   /* When one of the DEST_BASE flags is set, SURFACE_SYNC waits for idle.
    * Therefore, it should be last. Done in PFP.
    */
   if (cp_coher_cntl)
      ac_emit_cp_acquire_mem(cs->b, gfx_level, cs->hw_ip, V_580_CP_PFP, cp_coher_cntl);

   radeon_begin(cs);

   if (flush_bits & RADV_CMD_FLAG_START_PIPELINE_STATS) {
      if (!is_mec) {
         radeon_event_write(V_028A90_PIPELINESTAT_START);
      } else {
         radeon_set_sh_reg(R_00B828_COMPUTE_PIPELINESTAT_ENABLE, S_00B828_PIPELINESTAT_ENABLE(1));
      }
   } else if (flush_bits & RADV_CMD_FLAG_STOP_PIPELINE_STATS) {
      if (!is_mec) {
         radeon_event_write(V_028A90_PIPELINESTAT_STOP);
      } else {
         radeon_set_sh_reg(R_00B828_COMPUTE_PIPELINESTAT_ENABLE, S_00B828_PIPELINESTAT_ENABLE(0));
      }
   }

   radeon_end();
}

static void
radv_init_tracked_regs(struct radv_cmd_stream *cs)
{
   struct radv_tracked_regs *tracked_regs = &cs->tracked_regs;

   /* Mark all registers as unknown. */
   memset(tracked_regs->reg_value, 0, RADV_NUM_ALL_TRACKED_REGS * sizeof(uint32_t));
   BITSET_ZERO(tracked_regs->reg_saved_mask);

   /* 0xffffffff is an impossible value for these registers */
   memset(tracked_regs->spi_ps_input_cntl, 0xff, sizeof(uint32_t) * 32);
   memset(tracked_regs->cb_blend_control, 0xff, sizeof(uint32_t) * MAX_RTS);
   memset(tracked_regs->sx_mrt_blend_opt, 0xff, sizeof(uint32_t) * MAX_RTS);
}

void
radv_init_cmd_stream(struct radv_cmd_stream *cs, const enum amd_ip_type ip_type)
{
   cs->context_roll_without_scissor_emitted = false;
   cs->buffered_sh_regs.num = 0;
   cs->hw_ip = ip_type;

   radv_init_tracked_regs(cs);
}

VkResult
radv_create_cmd_stream(const struct radv_device *device, const enum amd_ip_type ip_type, const bool is_secondary,
                       struct radv_cmd_stream **cs_out)
{
   struct radeon_winsys *ws = device->ws;
   struct radv_cmd_stream *cs;

   cs = malloc(sizeof(*cs));
   if (!cs)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   radv_init_cmd_stream(cs, ip_type);

   cs->b = ws->cs_create(ws, ip_type, is_secondary);
   if (!cs->b) {
      free(cs);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   *cs_out = cs;
   return VK_SUCCESS;
}

void
radv_reset_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   struct radeon_winsys *ws = device->ws;

   radv_init_cmd_stream(cs, cs->hw_ip);

   ws->cs_reset(cs->b);
}

VkResult
radv_finalize_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   struct radeon_winsys *ws = device->ws;

   return ws->cs_finalize(cs->b);
}

void
radv_destroy_cmd_stream(const struct radv_device *device, struct radv_cmd_stream *cs)
{
   struct radeon_winsys *ws = device->ws;

   ws->cs_destroy(cs->b);
   free(cs);
}
