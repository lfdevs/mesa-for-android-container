/*
 * Copyright 2015 Samuel Pitoiset
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nvc0/nvc0_context.h"
#include "nvc0/nvc0_query_hw_metric.h"
#include "nvc0/nvc0_query_hw_sm.h"

/* === PERFORMANCE MONITORING METRICS for NVC0:NVE4 === */
static const char *nvc0_hw_metric_names[] =
{
   "metric-achieved_occupancy",
   "metric-branch_efficiency",
   "metric-inst_issued",
   "metric-inst_per_wrap",
   "metric-inst_replay_overhead",
   "metric-issued_ipc",
   "metric-issue_slots",
   "metric-issue_slot_utilization",
   "metric-ipc",
};

struct nvc0_hw_metric_query_cfg {
   uint32_t queries[8];
   uint32_t num_queries;
};

#define _SM(n) NVC0_HW_SM_QUERY(NVC0_HW_SM_QUERY_ ##n)
#define _M(n, c) [NVC0_HW_METRIC_QUERY_##n] = c

/* ==== Compute capability 2.0 (GF100/GF110) ==== */
static const struct nvc0_hw_metric_query_cfg
sm20_achieved_occupancy =
{
   .queries[0]  = _SM(ACTIVE_WARPS),
   .queries[1]  = _SM(ACTIVE_CYCLES),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm20_branch_efficiency =
{
   .queries[0]  = _SM(BRANCH),
   .queries[1]  = _SM(DIVERGENT_BRANCH),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm20_inst_per_wrap =
{
   .queries[0]  = _SM(INST_EXECUTED),
   .queries[1]  = _SM(WARPS_LAUNCHED),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm20_inst_replay_overhead =
{
   .queries[0]  = _SM(INST_ISSUED),
   .queries[1]  = _SM(INST_EXECUTED),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm20_issued_ipc =
{
   .queries[0]  = _SM(INST_ISSUED),
   .queries[1]  = _SM(ACTIVE_CYCLES),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm20_ipc =
{
   .queries[0]  = _SM(INST_EXECUTED),
   .queries[1]  = _SM(ACTIVE_CYCLES),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg *sm20_hw_metric_queries[] =
{
   _M(ACHIEVED_OCCUPANCY,     &sm20_achieved_occupancy),
   _M(BRANCH_EFFICIENCY,      &sm20_branch_efficiency),
   _M(INST_ISSUED,            NULL),
   _M(INST_PER_WRAP,          &sm20_inst_per_wrap),
   _M(INST_REPLAY_OVERHEAD,   &sm20_inst_replay_overhead),
   _M(ISSUED_IPC,             &sm20_issued_ipc),
   _M(ISSUE_SLOTS,            NULL),
   _M(ISSUE_SLOT_UTILIZATION, &sm20_issued_ipc),
   _M(IPC,                    &sm20_ipc),
};

/* ==== Compute capability 2.1 (GF108+ except GF110) ==== */
static const struct nvc0_hw_metric_query_cfg
sm21_inst_issued =
{
   .queries[0]  = _SM(INST_ISSUED1_0),
   .queries[1]  = _SM(INST_ISSUED1_1),
   .queries[2]  = _SM(INST_ISSUED2_0),
   .queries[3]  = _SM(INST_ISSUED2_1),
   .num_queries = 4,
};

static const struct nvc0_hw_metric_query_cfg
sm21_inst_replay_overhead =
{
   .queries[0]  = _SM(INST_ISSUED1_0),
   .queries[1]  = _SM(INST_ISSUED1_1),
   .queries[2]  = _SM(INST_ISSUED2_0),
   .queries[3]  = _SM(INST_ISSUED2_1),
   .queries[4]  = _SM(INST_EXECUTED),
   .num_queries = 5,
};

static const struct nvc0_hw_metric_query_cfg
sm21_issued_ipc =
{
   .queries[0]  = _SM(INST_ISSUED1_0),
   .queries[1]  = _SM(INST_ISSUED1_1),
   .queries[2]  = _SM(INST_ISSUED2_0),
   .queries[3]  = _SM(INST_ISSUED2_1),
   .queries[4]  = _SM(ACTIVE_CYCLES),
   .num_queries = 5,
};

static const struct nvc0_hw_metric_query_cfg *sm21_hw_metric_queries[] =
{
   _M(ACHIEVED_OCCUPANCY,     &sm20_achieved_occupancy),
   _M(BRANCH_EFFICIENCY,      &sm20_branch_efficiency),
   _M(INST_ISSUED,            &sm21_inst_issued),
   _M(INST_PER_WRAP,          &sm20_inst_per_wrap),
   _M(INST_REPLAY_OVERHEAD,   &sm21_inst_replay_overhead),
   _M(ISSUED_IPC,             &sm21_issued_ipc),
   _M(ISSUE_SLOTS,            &sm21_inst_issued),
   _M(ISSUE_SLOT_UTILIZATION, &sm21_issued_ipc),
   _M(IPC,                    &sm20_ipc),
};

#undef _SM
#undef _M

/* === PERFORMANCE MONITORING METRICS for NVE4+ === */
static const char *nve4_hw_metric_names[] =
{
   "metric-achieved_occupancy",
   "metric-branch_efficiency",
   "metric-inst_issued",
   "metric-inst_per_wrap",
   "metric-inst_replay_overhead",
   "metric-issued_ipc",
   "metric-issue_slots",
   "metric-issue_slot_utilization",
   "metric-ipc",
   "metric-shared_replay_overhead",
};

#define _SM(n) NVE4_HW_SM_QUERY(NVE4_HW_SM_QUERY_ ##n)
#define _M(n, c) [NVE4_HW_METRIC_QUERY_##n] = c

/* ==== Compute capability 3.0 (GK104/GK106/GK107) ==== */
static const struct nvc0_hw_metric_query_cfg
sm30_achieved_occupancy =
{
   .queries[0]  = _SM(ACTIVE_WARPS),
   .queries[1]  = _SM(ACTIVE_CYCLES),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm30_branch_efficiency =
{
   .queries[0]  = _SM(BRANCH),
   .queries[1]  = _SM(DIVERGENT_BRANCH),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm30_inst_issued =
{
   .queries[0]  = _SM(INST_ISSUED1),
   .queries[1]  = _SM(INST_ISSUED2),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm30_inst_per_wrap =
{
   .queries[0]  = _SM(INST_EXECUTED),
   .queries[1]  = _SM(WARPS_LAUNCHED),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm30_inst_replay_overhead =
{
   .queries[0]  = _SM(INST_ISSUED1),
   .queries[1]  = _SM(INST_ISSUED2),
   .queries[2]  = _SM(INST_EXECUTED),
   .num_queries = 3,
};

static const struct nvc0_hw_metric_query_cfg
sm30_issued_ipc =
{
   .queries[0]  = _SM(INST_ISSUED1),
   .queries[1]  = _SM(INST_ISSUED2),
   .queries[2]  = _SM(ACTIVE_CYCLES),
   .num_queries = 3,
};

static const struct nvc0_hw_metric_query_cfg
sm30_ipc =
{
   .queries[0]  = _SM(INST_EXECUTED),
   .queries[1]  = _SM(ACTIVE_CYCLES),
   .num_queries = 2,
};

static const struct nvc0_hw_metric_query_cfg
sm30_shared_replay_overhead =
{
   .queries[0]  = _SM(SHARED_LD_REPLAY),
   .queries[1]  = _SM(SHARED_ST_REPLAY),
   .queries[2]  = _SM(INST_EXECUTED),
   .num_queries = 3,
};

static const struct nvc0_hw_metric_query_cfg *sm30_hw_metric_queries[] =
{
   _M(ACHIEVED_OCCUPANCY,              &sm30_achieved_occupancy),
   _M(BRANCH_EFFICIENCY,               &sm30_branch_efficiency),
   _M(INST_ISSUED,                     &sm30_inst_issued),
   _M(INST_PER_WRAP,                   &sm30_inst_per_wrap),
   _M(INST_REPLAY_OVERHEAD,            &sm30_inst_replay_overhead),
   _M(ISSUED_IPC,                      &sm30_issued_ipc),
   _M(ISSUE_SLOTS,                     &sm30_inst_issued),
   _M(ISSUE_SLOT_UTILIZATION,          &sm30_issued_ipc),
   _M(IPC,                             &sm30_ipc),
   _M(SHARED_REPLAY_OVERHEAD,          &sm30_shared_replay_overhead),
};

#undef _SM
#undef _M

static inline const struct nvc0_hw_metric_query_cfg **
nvc0_hw_metric_get_queries(struct nvc0_screen *screen)
{
   struct nouveau_device *dev = screen->base.device;

   if (dev->chipset == 0xc0 || dev->chipset == 0xc8)
      return sm20_hw_metric_queries;
   return sm21_hw_metric_queries;
}

static const struct nvc0_hw_metric_query_cfg *
nvc0_hw_metric_query_get_cfg(struct nvc0_context *nvc0,
                             struct nvc0_hw_query *hq)
{
   const struct nvc0_hw_metric_query_cfg **queries;
   struct nvc0_screen *screen = nvc0->screen;
   struct nvc0_query *q = &hq->base;

   if (screen->base.class_3d >= NVE4_3D_CLASS)
      return sm30_hw_metric_queries[q->type - NVE4_HW_METRIC_QUERY(0)];

   queries = nvc0_hw_metric_get_queries(screen);
   return queries[q->type - NVC0_HW_METRIC_QUERY(0)];
}

static void
nvc0_hw_metric_destroy_query(struct nvc0_context *nvc0,
                             struct nvc0_hw_query *hq)
{
   struct nvc0_hw_metric_query *hmq = nvc0_hw_metric_query(hq);
   unsigned i;

   for (i = 0; i < hmq->num_queries; i++)
      if (hmq->queries[i]->funcs->destroy_query)
         hmq->queries[i]->funcs->destroy_query(nvc0, hmq->queries[i]);
   FREE(hmq);
}

static boolean
nvc0_hw_metric_begin_query(struct nvc0_context *nvc0, struct nvc0_hw_query *hq)
{
   struct nvc0_hw_metric_query *hmq = nvc0_hw_metric_query(hq);
   boolean ret = false;
   unsigned i;

   for (i = 0; i < hmq->num_queries; i++) {
      ret = hmq->queries[i]->funcs->begin_query(nvc0, hmq->queries[i]);
      if (!ret)
         return ret;
   }
   return ret;
}

static void
nvc0_hw_metric_end_query(struct nvc0_context *nvc0, struct nvc0_hw_query *hq)
{
   struct nvc0_hw_metric_query *hmq = nvc0_hw_metric_query(hq);
   unsigned i;

   for (i = 0; i < hmq->num_queries; i++)
      hmq->queries[i]->funcs->end_query(nvc0, hmq->queries[i]);
}

static uint64_t
sm20_hw_metric_calc_result(struct nvc0_hw_query *hq, uint64_t res64[8])
{
   switch (hq->base.type - NVC0_HW_METRIC_QUERY(0)) {
   case NVC0_HW_METRIC_QUERY_ACHIEVED_OCCUPANCY:
      /* (active_warps / active_cycles) / max. number of warps on a MP */
      if (res64[1])
         return (res64[0] / (double)res64[1]) / 48;
      break;
   case NVC0_HW_METRIC_QUERY_BRANCH_EFFICIENCY:
      /* (branch / (branch + divergent_branch)) * 100 */
      if (res64[0] + res64[1])
         return (res64[0] / (double)(res64[0] + res64[1])) * 100;
      break;
   case NVC0_HW_METRIC_QUERY_INST_PER_WRAP:
      /* inst_executed / warps_launched */
      if (res64[1])
         return res64[0] / (double)res64[1];
      break;
   case NVC0_HW_METRIC_QUERY_INST_REPLAY_OVERHEAD:
      /* (inst_issued - inst_executed) / inst_executed */
      if (res64[1])
         return (res64[0] - res64[1]) / (double)res64[1];
      break;
   case NVC0_HW_METRIC_QUERY_ISSUED_IPC:
      /* inst_issued / active_cycles */
      if (res64[1])
         return res64[0] / (double)res64[1];
      break;
   case NVC0_HW_METRIC_QUERY_ISSUE_SLOT_UTILIZATION:
      /* ((inst_issued / 2) / active_cycles) * 100 */
      if (res64[1])
         return ((res64[0] / 2) / (double)res64[1]) * 100;
      break;
   case NVC0_HW_METRIC_QUERY_IPC:
      /* inst_executed / active_cycles */
      if (res64[1])
         return res64[0] / (double)res64[1];
      break;
   default:
      debug_printf("invalid metric type: %d\n",
                   hq->base.type - NVC0_HW_METRIC_QUERY(0));
      break;
   }
   return 0;
}

static uint64_t
sm21_hw_metric_calc_result(struct nvc0_hw_query *hq, uint64_t res64[8])
{
   switch (hq->base.type - NVC0_HW_METRIC_QUERY(0)) {
   case NVC0_HW_METRIC_QUERY_ACHIEVED_OCCUPANCY:
      return sm20_hw_metric_calc_result(hq, res64);
   case NVC0_HW_METRIC_QUERY_BRANCH_EFFICIENCY:
      return sm20_hw_metric_calc_result(hq, res64);
   case NVC0_HW_METRIC_QUERY_INST_ISSUED:
      /* issued1_0 + issued1_1 + (issued2_0 + issued2_1) * 2 */
      return res64[0] + res64[1] + (res64[2] + res64[3]) * 2;
      break;
   case NVC0_HW_METRIC_QUERY_INST_PER_WRAP:
      return sm20_hw_metric_calc_result(hq, res64);
   case NVC0_HW_METRIC_QUERY_INST_REPLAY_OVERHEAD:
      /* (metric-inst_issued - inst_executed) / inst_executed */
      if (res64[4])
         return (((res64[0] + res64[1] + (res64[2] + res64[3]) * 2) -
                   res64[4]) / (double)res64[4]);
      break;
   case NVC0_HW_METRIC_QUERY_ISSUED_IPC:
      /* metric-inst_issued / active_cycles */
      if (res64[4])
         return (res64[0] + res64[1] + (res64[2] + res64[3]) * 2) /
                (double)res64[4];
      break;
   case NVC0_HW_METRIC_QUERY_ISSUE_SLOTS:
      /* issued1_0 + issued1_1 + issued2_0 + issued2_1 */
      return res64[0] + res64[1] + res64[2] + res64[3];
      break;
   case NVC0_HW_METRIC_QUERY_ISSUE_SLOT_UTILIZATION:
      /* ((metric-issue_slots / 2) / active_cycles) * 100 */
      if (res64[4])
         return (((res64[0] + res64[1] + res64[2] + res64[3]) / 2) /
                 (double)res64[4]) * 100;
      break;
   case NVC0_HW_METRIC_QUERY_IPC:
      return sm20_hw_metric_calc_result(hq, res64);
   default:
      debug_printf("invalid metric type: %d\n",
                   hq->base.type - NVC0_HW_METRIC_QUERY(0));
      break;
   }
   return 0;
}

static uint64_t
sm30_hw_metric_calc_result(struct nvc0_hw_query *hq, uint64_t res64[8])
{
   switch (hq->base.type - NVE4_HW_METRIC_QUERY(0)) {
   case NVE4_HW_METRIC_QUERY_ACHIEVED_OCCUPANCY:
      /* (active_warps / active_cycles) / max. number of warps on a MP */
      if (res64[1])
         return (res64[0] / (double)res64[1]) / 64;
      break;
   case NVE4_HW_METRIC_QUERY_BRANCH_EFFICIENCY:
      return sm20_hw_metric_calc_result(hq, res64);
   case NVE4_HW_METRIC_QUERY_INST_ISSUED:
      /* inst_issued1 + inst_issued2 * 2 */
      return res64[0] + res64[1] * 2;
   case NVE4_HW_METRIC_QUERY_INST_PER_WRAP:
      return sm20_hw_metric_calc_result(hq, res64);
   case NVE4_HW_METRIC_QUERY_INST_REPLAY_OVERHEAD:
      /* (metric-inst_issued - inst_executed) / inst_executed */
      if (res64[2])
         return (((res64[0] + res64[1] * 2) - res64[2]) / (double)res64[2]);
      break;
   case NVE4_HW_METRIC_QUERY_ISSUED_IPC:
      /* metric-inst_issued / active_cycles */
      if (res64[2])
         return (res64[0] + res64[1] * 2) / (double)res64[2];
      break;
   case NVE4_HW_METRIC_QUERY_ISSUE_SLOTS:
      /* inst_issued1 + inst_issued2 */
      return res64[0] + res64[1];
   case NVE4_HW_METRIC_QUERY_ISSUE_SLOT_UTILIZATION:
      /* ((metric-issue_slots / 2) / active_cycles) * 100 */
      if (res64[2])
         return (((res64[0] + res64[1]) / 2) / (double)res64[2]) * 100;
      break;
   case NVE4_HW_METRIC_QUERY_IPC:
      return sm20_hw_metric_calc_result(hq, res64);
   case NVE4_HW_METRIC_QUERY_SHARED_REPLAY_OVERHEAD:
      /* (shared_load_replay + shared_store_replay) / inst_executed */
      if (res64[2])
         return (res64[0] + res64[1]) / (double)res64[2];
      break;
   default:
      debug_printf("invalid metric type: %d\n",
                   hq->base.type - NVE4_HW_METRIC_QUERY(0));
      break;
   }
   return 0;
}

static boolean
nvc0_hw_metric_get_query_result(struct nvc0_context *nvc0,
                                struct nvc0_hw_query *hq, boolean wait,
                                union pipe_query_result *result)
{
   struct nvc0_hw_metric_query *hmq = nvc0_hw_metric_query(hq);
   struct nvc0_screen *screen = nvc0->screen;
   struct nouveau_device *dev = screen->base.device;
   union pipe_query_result results[8] = {};
   uint64_t res64[8] = {};
   uint64_t value = 0;
   boolean ret = false;
   unsigned i;

   for (i = 0; i < hmq->num_queries; i++) {
      ret = hmq->queries[i]->funcs->get_query_result(nvc0, hmq->queries[i],
                                                     wait, &results[i]);
      if (!ret)
         return ret;
      res64[i] = *(uint64_t *)&results[i];
   }

   if (screen->base.class_3d >= NVE4_3D_CLASS) {
      value = sm30_hw_metric_calc_result(hq, res64);
   } else {
      if (dev->chipset == 0xc0 || dev->chipset == 0xc8)
         value = sm20_hw_metric_calc_result(hq, res64);
      else
         value = sm21_hw_metric_calc_result(hq, res64);
   }

   *(uint64_t *)result = value;
   return ret;
}

static const struct nvc0_hw_query_funcs hw_metric_query_funcs = {
   .destroy_query = nvc0_hw_metric_destroy_query,
   .begin_query = nvc0_hw_metric_begin_query,
   .end_query = nvc0_hw_metric_end_query,
   .get_query_result = nvc0_hw_metric_get_query_result,
};

struct nvc0_hw_query *
nvc0_hw_metric_create_query(struct nvc0_context *nvc0, unsigned type)
{
   const struct nvc0_hw_metric_query_cfg *cfg;
   struct nvc0_hw_metric_query *hmq;
   struct nvc0_hw_query *hq;
   unsigned i;

   if ((type < NVE4_HW_METRIC_QUERY(0) || type > NVE4_HW_METRIC_QUERY_LAST) &&
       (type < NVC0_HW_METRIC_QUERY(0) || type > NVC0_HW_METRIC_QUERY_LAST))
      return NULL;

   hmq = CALLOC_STRUCT(nvc0_hw_metric_query);
   if (!hmq)
      return NULL;

   hq = &hmq->base;
   hq->funcs = &hw_metric_query_funcs;
   hq->base.type = type;

   cfg = nvc0_hw_metric_query_get_cfg(nvc0, hq);

   for (i = 0; i < cfg->num_queries; i++) {
      hmq->queries[i] = nvc0_hw_sm_create_query(nvc0, cfg->queries[i]);
      if (!hmq->queries[i]) {
         nvc0_hw_metric_destroy_query(nvc0, hq);
         return NULL;
      }
      hmq->num_queries++;
   }

   return hq;
}

static int
nvc0_hw_metric_get_next_query_id(const struct nvc0_hw_metric_query_cfg **queries,
                                 unsigned id)
{
   unsigned i, next = 0;

   for (i = 0; i < NVC0_HW_METRIC_QUERY_COUNT; i++) {
      if (!queries[i]) {
         next++;
      } else
      if (i >= id && queries[id + next]) {
         break;
      }
   }
   return id + next;
}

int
nvc0_hw_metric_get_driver_query_info(struct nvc0_screen *screen, unsigned id,
                                     struct pipe_driver_query_info *info)
{
   uint16_t class_3d = screen->base.class_3d;
   int count = 0;

   if (screen->base.drm->version >= 0x01000101) {
      if (screen->compute) {
         if (screen->base.class_3d == NVE4_3D_CLASS) {
            count += NVE4_HW_METRIC_QUERY_COUNT;
         } else
         if (class_3d < NVE4_3D_CLASS) {
            const struct nvc0_hw_metric_query_cfg **queries =
               nvc0_hw_metric_get_queries(screen);
            unsigned i;

            for (i = 0; i < NVC0_HW_METRIC_QUERY_COUNT; i++) {
               if (queries[i])
                  count++;
            }
         }
      }
   }

   if (!info)
      return count;

   if (id < count) {
      if (screen->compute) {
         if (screen->base.class_3d == NVE4_3D_CLASS) {
            info->name = nve4_hw_metric_names[id];
            info->query_type = NVE4_HW_METRIC_QUERY(id);
            info->group_id = NVC0_HW_METRIC_QUERY_GROUP;
            return 1;
         } else
         if (class_3d < NVE4_3D_CLASS) {
             const struct nvc0_hw_metric_query_cfg **queries =
               nvc0_hw_metric_get_queries(screen);

            id = nvc0_hw_metric_get_next_query_id(queries, id);
            info->name = nvc0_hw_metric_names[id];
            info->query_type = NVC0_HW_METRIC_QUERY(id);
            info->group_id = NVC0_HW_METRIC_QUERY_GROUP;
            return 1;
         }
      }
   }
   return 0;
}
