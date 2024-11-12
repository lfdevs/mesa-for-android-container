/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "nvk_mme.h"

#include "nvk_private.h"

#include "mme_sim.h"

static const nvk_mme_builder_func mme_builders[NVK_MME_COUNT] = {
   [NVK_MME_SELECT_CB0]                    = nvk_mme_select_cb0,
   [NVK_MME_BIND_CBUF_DESC]                = nvk_mme_bind_cbuf_desc,
   [NVK_MME_CLEAR]                         = nvk_mme_clear,
   [NVK_MME_BIND_IB]                       = nvk_mme_bind_ib,
   [NVK_MME_BIND_VB]                       = nvk_mme_bind_vb,
   [NVK_MME_SET_VB_ENABLES]                = nvk_mme_set_vb_enables,
   [NVK_MME_SET_VB_STRIDE]                 = nvk_mme_set_vb_stride,
   [NVK_MME_SET_TESS_PARAMS]               = nvk_mme_set_tess_params,
   [NVK_MME_SET_SHADING_RATE_CONTROL]      = nvk_mme_set_shading_rate_control,
   [NVK_MME_SET_ANTI_ALIAS]                = nvk_mme_set_anti_alias,
   [NVK_MME_DRAW]                          = nvk_mme_draw,
   [NVK_MME_DRAW_INDEXED]                  = nvk_mme_draw_indexed,
   [NVK_MME_DRAW_INDIRECT]                 = nvk_mme_draw_indirect,
   [NVK_MME_DRAW_INDEXED_INDIRECT]         = nvk_mme_draw_indexed_indirect,
   [NVK_MME_DRAW_INDIRECT_COUNT]           = nvk_mme_draw_indirect_count,
   [NVK_MME_DRAW_INDEXED_INDIRECT_COUNT]   = nvk_mme_draw_indexed_indirect_count,
   [NVK_MME_ADD_CS_INVOCATIONS]            = nvk_mme_add_cs_invocations,
   [NVK_MME_DISPATCH_INDIRECT]             = nvk_mme_dispatch_indirect,
   [NVK_MME_WRITE_CS_INVOCATIONS]          = nvk_mme_write_cs_invocations,
   [NVK_MME_XFB_COUNTER_LOAD]              = nvk_mme_xfb_counter_load,
   [NVK_MME_XFB_DRAW_INDIRECT]             = nvk_mme_xfb_draw_indirect,
   [NVK_MME_SET_PRIV_REG]                  = nvk_mme_set_priv_reg,
   [NVK_MME_SET_WRITE_MASK]                = nvk_mme_set_write_mask,
   [NVK_MME_SET_CONSERVATIVE_RASTER_STATE] = nvk_mme_set_conservative_raster_state,
   [NVK_MME_SET_VIEWPORT_MIN_MAX_Z]        = nvk_mme_set_viewport_min_max_z,
   [NVK_MME_SET_Z_CLAMP]                   = nvk_mme_set_z_clamp,
};

static const struct nvk_mme_test_case *mme_tests[NVK_MME_COUNT] = {
   [NVK_MME_CLEAR]                         = nvk_mme_clear_tests,
   [NVK_MME_BIND_VB]                       = nvk_mme_bind_vb_tests,
   [NVK_MME_SET_TESS_PARAMS]               = nvk_mme_set_tess_params_tests,
   [NVK_MME_SET_SHADING_RATE_CONTROL]      = nvk_mme_set_shading_rate_control_tests,
   [NVK_MME_SET_ANTI_ALIAS]                = nvk_mme_set_anti_alias_tests,
};

uint32_t *
nvk_build_mme(const struct nv_device_info *devinfo,
              enum nvk_mme mme, size_t *size_out)
{
   struct mme_builder b;
   mme_builder_init(&b, devinfo);

   mme_builders[mme](&b);

   return mme_builder_finish(&b, size_out);
}

struct nvk_mme_test_state {
   const struct nvk_mme_test_case *test;
   struct nvk_mme_mthd_data results[32];
   uint32_t pi, ei;
};

static uint32_t
nvk_mme_test_state_load(void *_ts)
{
   struct nvk_mme_test_state *ts = _ts;
   return ts->test->params[ts->pi++];
}

static uint32_t
nvk_mme_test_state_state(void *_ts, uint16_t addr)
{
   struct nvk_mme_test_state *ts = _ts;

   /* First, look backwards through the expected data that we've already
    * written.  This ensures that mthd() impacts state().
    */
   for (int32_t i = ts->ei - 1; i >= 0; i--) {
      if (ts->test->expected[i].mthd == addr)
         return ts->test->expected[i].data;
   }

   /* Now look at init.  We assume the init data is unique */
   assert(ts->test->init != NULL && "Read uninitialized state");
   for (uint32_t i = 0;; i++) {
      if (ts->test->init[i].mthd == 0)
         unreachable("Read uninitialized state");

      if (ts->test->init[i].mthd == addr)
         return ts->test->init[i].data;
   }
}

static void
nvk_mme_test_state_mthd(void *_ts, uint16_t addr, uint32_t data)
{
   struct nvk_mme_test_state *ts = _ts;

   assert(ts->ei < ARRAY_SIZE(ts->results));
   ts->results[ts->ei] = (struct nvk_mme_mthd_data) {
      .mthd = addr,
      .data = data,
   };

   if (ts->test->expected != NULL) {
      assert(ts->test->expected[ts->ei].mthd != 0);
      assert(ts->test->expected[ts->ei].mthd == addr);
      assert(ts->test->expected[ts->ei].data == data);
   }

   ts->ei++;
}

const struct mme_sim_state_ops nvk_mme_test_state_ops = {
   .load = nvk_mme_test_state_load,
   .state = nvk_mme_test_state_state,
   .mthd = nvk_mme_test_state_mthd,
};

void
nvk_test_all_mmes(const struct nv_device_info *devinfo)
{
   for (uint32_t mme = 0; mme < NVK_MME_COUNT; mme++) {
      size_t size;
      uint32_t *dw = nvk_build_mme(devinfo, mme, &size);
      assert(dw != NULL);

      if (mme_tests[mme] != NULL) {
         for (uint32_t i = 0;; i++) {
            if (mme_tests[mme][i].params == NULL)
               break;

            struct nvk_mme_test_state ts = {
               .test = &mme_tests[mme][i],
            };
            mme_sim_core(devinfo, size, dw, &nvk_mme_test_state_ops, &ts);
            if (ts.test->expected != NULL)
               assert(ts.test->expected[ts.ei].mthd == 0);
            if (ts.test->check != NULL)
               ts.test->check(devinfo, ts.test, ts.results);
         }
      }

      free(dw);
   }
}
