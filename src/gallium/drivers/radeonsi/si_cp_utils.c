/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_build_pm4.h"

void si_cp_release_mem_pws(struct si_context *sctx, struct radeon_cmdbuf *cs,
                           unsigned event_type, unsigned gcr_cntl)
{
   ac_emit_cp_release_mem_pws(&cs->current, sctx->gfx_level,
                              sctx->is_gfx_queue ? AMD_IP_GFX : AMD_IP_COMPUTE,
                              event_type, gcr_cntl);
}

void si_cp_acquire_mem_pws(struct si_context *sctx, struct radeon_cmdbuf *cs,
                           unsigned event_type, unsigned stage_sel, unsigned gcr_cntl,
                           unsigned distance, unsigned sqtt_flush_flags)
{
   if (unlikely(sctx->sqtt_enabled))
      si_sqtt_describe_barrier_start(sctx, cs);

   ac_emit_cp_acquire_mem_pws(&cs->current, sctx->gfx_level,
                              sctx->is_gfx_queue ? AMD_IP_GFX : AMD_IP_COMPUTE,
                              event_type, stage_sel, distance, gcr_cntl);

   if (unlikely(sctx->sqtt_enabled))
      si_sqtt_describe_barrier_end(sctx, cs, sqtt_flush_flags);
}

void si_cp_release_acquire_mem_pws(struct si_context *sctx, struct radeon_cmdbuf *cs,
                                   unsigned event_type, unsigned gcr_cntl, unsigned stage_sel,
                                   unsigned sqtt_flush_flags)
{
   si_cp_release_mem_pws(sctx, cs, event_type, gcr_cntl);
   si_cp_acquire_mem_pws(sctx, cs, event_type, stage_sel, 0, 0, sqtt_flush_flags);
}

void si_cp_acquire_mem(struct si_context *sctx, struct radeon_cmdbuf *cs, unsigned gcr_cntl,
                       unsigned engine)
{
   const enum amd_ip_type ip_type = sctx->is_gfx_queue ? AMD_IP_GFX : AMD_IP_COMPUTE;

   if (sctx->gfx_level >= GFX10) {
      ac_emit_cp_acquire_mem(&cs->current, sctx->gfx_level, ip_type, engine,
                             gcr_cntl);
   } else {
      bool compute_ib = !sctx->is_gfx_queue;

      /* This seems problematic with GFX7 (see #4764) */
      if (sctx->gfx_level != GFX7)
         gcr_cntl |= 1u << 31; /* don't sync PFP, i.e. execute the sync in ME */

      ac_emit_cp_acquire_mem(&cs->current, sctx->gfx_level, ip_type, engine,
                             gcr_cntl);

      /* ACQUIRE_MEM & SURFACE_SYNC roll the context if the current context is busy. */
      if (!compute_ib)
         sctx->context_roll = true;

      if (engine == V_580_CP_PFP)
         si_cp_pfp_sync_me(cs);
   }
}

void si_cp_pfp_sync_me(struct radeon_cmdbuf *cs)
{
   ac_emit_cp_pfp_sync_me(&cs->current, false);
}
