/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_cmdbuf.h"
#include "ac_pm4.h"

#include "sid.h"

static void
gfx6_init_compute_preamble_state(const struct ac_preamble_state *state,
                                 struct ac_pm4_state *pm4)
{
   const struct radeon_info *info = pm4->info;
   const uint32_t compute_cu_en = S_00B858_SH0_CU_EN(info->spi_cu_en) |
                                  S_00B858_SH1_CU_EN(info->spi_cu_en);

   ac_pm4_set_reg(pm4, R_00B834_COMPUTE_PGM_HI, S_00B834_DATA(info->address32_hi >> 8));

   for (unsigned i = 0; i < 2; ++i)
      ac_pm4_set_reg(pm4, R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE0 + i * 4,
                     i < info->max_se ? compute_cu_en : 0x0);

   if (info->gfx_level >= GFX7) {
      for (unsigned i = 2; i < 4; ++i)
         ac_pm4_set_reg(pm4, R_00B864_COMPUTE_STATIC_THREAD_MGMT_SE2 + (i - 2) * 4,
                        i < info->max_se ? compute_cu_en : 0x0);
   }

   if (info->gfx_level >= GFX9)
      ac_pm4_set_reg(pm4, R_0301EC_CP_COHER_START_DELAY, 0);

   /* Set the pointer to border colors. */
   if (info->gfx_level >= GFX7) {
      ac_pm4_set_reg(pm4, R_030E00_TA_CS_BC_BASE_ADDR, state->border_color_va >> 8);
      ac_pm4_set_reg(pm4, R_030E04_TA_CS_BC_BASE_ADDR_HI,
                     S_030E04_ADDRESS(state->border_color_va >> 40));
   } else if (info->gfx_level == GFX6) {
      ac_pm4_set_reg(pm4, R_00950C_TA_CS_BC_BASE_ADDR, state->border_color_va >> 8);
   }
}

static void
gfx10_init_compute_preamble_state(const struct ac_preamble_state *state,
                                  struct ac_pm4_state *pm4)
{
   const struct radeon_info *info = pm4->info;
   const uint32_t compute_cu_en = S_00B858_SH0_CU_EN(info->spi_cu_en) |
                                  S_00B858_SH1_CU_EN(info->spi_cu_en);

   if (info->gfx_level < GFX11)
      ac_pm4_set_reg(pm4, R_0301EC_CP_COHER_START_DELAY, 0x20);
   ac_pm4_set_reg(pm4, R_030E00_TA_CS_BC_BASE_ADDR, state->border_color_va >> 8);
   ac_pm4_set_reg(pm4, R_030E04_TA_CS_BC_BASE_ADDR_HI, S_030E04_ADDRESS(state->border_color_va >> 40));

   ac_pm4_set_reg(pm4, R_00B834_COMPUTE_PGM_HI, S_00B834_DATA(info->address32_hi >> 8));

   for (unsigned i = 0; i < 2; ++i)
      ac_pm4_set_reg(pm4, R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE0 + i * 4,
                     i < info->max_se ? compute_cu_en : 0x0);

   for (unsigned i = 2; i < 4; ++i)
      ac_pm4_set_reg(pm4, R_00B864_COMPUTE_STATIC_THREAD_MGMT_SE2 + (i - 2) * 4,
                     i < info->max_se ? compute_cu_en : 0x0);

   ac_pm4_set_reg(pm4, R_00B890_COMPUTE_USER_ACCUM_0, 0);
   ac_pm4_set_reg(pm4, R_00B894_COMPUTE_USER_ACCUM_1, 0);
   ac_pm4_set_reg(pm4, R_00B898_COMPUTE_USER_ACCUM_2, 0);
   ac_pm4_set_reg(pm4, R_00B89C_COMPUTE_USER_ACCUM_3, 0);

   if (info->gfx_level >= GFX11) {
      for (unsigned i = 4; i < 8; ++i)
         ac_pm4_set_reg(pm4, R_00B8AC_COMPUTE_STATIC_THREAD_MGMT_SE4 + (i - 4) * 4,
                        i < info->max_se ? compute_cu_en : 0x0);

      /* How many threads should go to 1 SE before moving onto the next. Think of GL1 cache hits.
       * Only these values are valid: 0 (disabled), 64, 128, 256, 512
       * Recommendation: 64 = RT, 256 = non-RT (run benchmarks to be sure)
       */
      ac_pm4_set_reg(pm4, R_00B8BC_COMPUTE_DISPATCH_INTERLEAVE,
                     S_00B8BC_INTERLEAVE(state->gfx11.compute_dispatch_interleave));
   }

   ac_pm4_set_reg(pm4, R_00B9F4_COMPUTE_DISPATCH_TUNNEL, 0);
}

static void
gfx12_init_compute_preamble_state(const struct ac_preamble_state *state,
                                  struct ac_pm4_state *pm4)
{
   const struct radeon_info *info = pm4->info;
   const uint32_t compute_cu_en = S_00B858_SH0_CU_EN(info->spi_cu_en) |
                                  S_00B858_SH1_CU_EN(info->spi_cu_en);
   const uint32_t num_se = info->max_se;

   ac_pm4_set_reg(pm4, R_030E00_TA_CS_BC_BASE_ADDR, state->border_color_va >> 8);
   ac_pm4_set_reg(pm4, R_030E04_TA_CS_BC_BASE_ADDR_HI, S_030E04_ADDRESS(state->border_color_va >> 40));

   ac_pm4_set_reg(pm4, R_00B82C_COMPUTE_PERFCOUNT_ENABLE, 0);
   ac_pm4_set_reg(pm4, R_00B834_COMPUTE_PGM_HI, S_00B834_DATA(info->address32_hi >> 8));
   ac_pm4_set_reg(pm4, R_00B838_COMPUTE_DISPATCH_PKT_ADDR_LO, 0);
   ac_pm4_set_reg(pm4, R_00B83C_COMPUTE_DISPATCH_PKT_ADDR_HI, 0);
   ac_pm4_set_reg(pm4, R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE0, compute_cu_en);
   ac_pm4_set_reg(pm4, R_00B85C_COMPUTE_STATIC_THREAD_MGMT_SE1, num_se > 1 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B864_COMPUTE_STATIC_THREAD_MGMT_SE2, num_se > 2 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B868_COMPUTE_STATIC_THREAD_MGMT_SE3, num_se > 3 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B88C_COMPUTE_STATIC_THREAD_MGMT_SE8, num_se > 8 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B890_COMPUTE_USER_ACCUM_0, 0);
   ac_pm4_set_reg(pm4, R_00B894_COMPUTE_USER_ACCUM_1, 0);
   ac_pm4_set_reg(pm4, R_00B898_COMPUTE_USER_ACCUM_2, 0);
   ac_pm4_set_reg(pm4, R_00B89C_COMPUTE_USER_ACCUM_3, 0);
   ac_pm4_set_reg(pm4, R_00B8AC_COMPUTE_STATIC_THREAD_MGMT_SE4, num_se > 4 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B8B0_COMPUTE_STATIC_THREAD_MGMT_SE5, num_se > 5 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B8B4_COMPUTE_STATIC_THREAD_MGMT_SE6, num_se > 6 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B8B8_COMPUTE_STATIC_THREAD_MGMT_SE7, num_se > 7 ? compute_cu_en : 0);
   ac_pm4_set_reg(pm4, R_00B9F4_COMPUTE_DISPATCH_TUNNEL, 0);
}

void
ac_init_compute_preamble_state(const struct ac_preamble_state *state,
                               struct ac_pm4_state *pm4)
{
   const struct radeon_info *info = pm4->info;

   if (info->gfx_level >= GFX12) {
      gfx12_init_compute_preamble_state(state, pm4);
   } else if (info->gfx_level >= GFX10) {
      gfx10_init_compute_preamble_state(state, pm4);
   } else {
      gfx6_init_compute_preamble_state(state, pm4);
   }
}
