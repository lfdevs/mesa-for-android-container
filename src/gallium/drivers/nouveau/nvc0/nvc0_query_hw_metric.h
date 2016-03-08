#ifndef __NVC0_QUERY_HW_METRIC_H__
#define __NVC0_QUERY_HW_METRIC_H__

#include "nvc0_query_hw.h"

struct nvc0_hw_metric_query {
   struct nvc0_hw_query base;
   struct nvc0_hw_query *queries[8];
   unsigned num_queries;
};

static inline struct nvc0_hw_metric_query *
nvc0_hw_metric_query(struct nvc0_hw_query *hq)
{
   return (struct nvc0_hw_metric_query *)hq;
}

/*
 * Driver metrics queries:
 */
#define NVE4_HW_METRIC_QUERY(i)   (PIPE_QUERY_DRIVER_SPECIFIC + 3072 + (i))
#define NVE4_HW_METRIC_QUERY_LAST  NVE4_HW_METRIC_QUERY(NVE4_HW_METRIC_QUERY_COUNT - 1)
enum nve4_hw_metric_queries
{
    NVE4_HW_METRIC_QUERY_ACHIEVED_OCCUPANCY = 0,
    NVE4_HW_METRIC_QUERY_BRANCH_EFFICIENCY,
    NVE4_HW_METRIC_QUERY_INST_ISSUED,
    NVE4_HW_METRIC_QUERY_INST_PER_WRAP,
    NVE4_HW_METRIC_QUERY_INST_REPLAY_OVERHEAD,
    NVE4_HW_METRIC_QUERY_ISSUED_IPC,
    NVE4_HW_METRIC_QUERY_ISSUE_SLOTS,
    NVE4_HW_METRIC_QUERY_ISSUE_SLOT_UTILIZATION,
    NVE4_HW_METRIC_QUERY_IPC,
    NVE4_HW_METRIC_QUERY_SHARED_REPLAY_OVERHEAD,
    NVE4_HW_METRIC_QUERY_COUNT
};

#define NVC0_HW_METRIC_QUERY(i)   (PIPE_QUERY_DRIVER_SPECIFIC + 3072 + (i))
#define NVC0_HW_METRIC_QUERY_LAST  NVC0_HW_METRIC_QUERY(NVC0_HW_METRIC_QUERY_COUNT - 1)
enum nvc0_hw_metric_queries
{
    NVC0_HW_METRIC_QUERY_ACHIEVED_OCCUPANCY = 0,
    NVC0_HW_METRIC_QUERY_BRANCH_EFFICIENCY,
    NVC0_HW_METRIC_QUERY_INST_ISSUED,
    NVC0_HW_METRIC_QUERY_INST_PER_WRAP,
    NVC0_HW_METRIC_QUERY_INST_REPLAY_OVERHEAD,
    NVC0_HW_METRIC_QUERY_ISSUED_IPC,
    NVC0_HW_METRIC_QUERY_ISSUE_SLOTS,
    NVC0_HW_METRIC_QUERY_ISSUE_SLOT_UTILIZATION,
    NVC0_HW_METRIC_QUERY_IPC,
    NVC0_HW_METRIC_QUERY_COUNT
};

struct nvc0_hw_query *
nvc0_hw_metric_create_query(struct nvc0_context *, unsigned);
int
nvc0_hw_metric_get_driver_query_info(struct nvc0_screen *, unsigned,
                                     struct pipe_driver_query_info *);
#endif
