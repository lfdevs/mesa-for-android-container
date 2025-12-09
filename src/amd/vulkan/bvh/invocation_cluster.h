/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

/* Helpers for encoding BVH nodes on different HW generations. */

#ifndef RADV_BVH_INVOCATION_CLUSTER_H
#define RADV_BVH_INVOCATION_CLUSTER_H

struct radv_invocation_cluster {
   uint32_t invocation_index;
   uint32_t cluster_index;
   uint32_t cluster_size;
};

/* cluster_size has to be a power of two and <32. */
void
radv_invocation_cluster_init(out radv_invocation_cluster cluster, uint32_t cluster_size)
{
   cluster.invocation_index = gl_SubgroupInvocationID & (cluster_size - 1);
   cluster.cluster_index = gl_SubgroupInvocationID / cluster_size;
   cluster.cluster_size = cluster_size;
}

#define radv_read_invocation(cluster, index, value)                                                                    \
   subgroupShuffle(value, (gl_SubgroupInvocationID & (~(cluster.cluster_size - 1))) + index)

uint32_t
radv_ballot(radv_invocation_cluster cluster, bool value)
{
   uvec4 ballot = subgroupBallot(value);
   uint64_t ballot64 = uint64_t(ballot.x) | (uint64_t(ballot.y) << 32ul);
   uint32_t cluster_shift = gl_SubgroupInvocationID & (~(cluster.cluster_size - 1));
   return uint32_t((ballot64 >> cluster_shift) & ((1u << cluster.cluster_size) - 1));
}

uint32_t
radv_first_active_invocation(radv_invocation_cluster cluster)
{
   return findLSB(radv_ballot(cluster, true));
}

#endif
