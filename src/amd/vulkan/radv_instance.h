/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_INSTANCE_H
#define RADV_INSTANCE_H

#include "util/simple_mtx.h"
#include "util/xmlconfig.h"
#include "radv_drirc.h"
#include "radv_radeon_winsys.h"
#include "vk_instance.h"

#ifdef ANDROID_STRICT
#define RADV_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)
#else
#define RADV_API_VERSION     VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION)
#define RADV_API_VERSION_1_3 VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION)
#endif

enum radv_trace_mode {
   /** Radeon GPU Profiler */
   RADV_TRACE_MODE_RGP = 1 << VK_TRACE_MODE_COUNT,

   /** Radeon Raytracing Analyzer */
   RADV_TRACE_MODE_RRA = 1 << (VK_TRACE_MODE_COUNT + 1),

   /** Gather context rolls of submitted command buffers */
   RADV_TRACE_MODE_CTX_ROLLS = 1 << (VK_TRACE_MODE_COUNT + 2),
};

struct radv_instance {
   struct vk_instance vk;

   VkAllocationCallbacks alloc;

   simple_mtx_t shader_dump_mtx;

   uint64_t debug_flags;
   uint64_t perftest_flags;
   uint64_t experimental_flags;
   uint64_t trap_excp_flags;
   uint32_t queue_disable_flags;

   enum radeon_ctx_pstate profile_pstate;

   struct radv_drirc drirc;

   FILE *pso_history_logfile;
};

VK_DEFINE_HANDLE_CASTS(radv_instance, vk.base, VkInstance, VK_OBJECT_TYPE_INSTANCE)

const char *radv_get_debug_option_name(int id);

const char *radv_get_perftest_option_name(int id);

bool radv_is_rt_wave64_enabled(const struct radv_instance *instance);

static const char *
radv_bvh_stats_file()
{
   return os_get_option_secure("RADV_BVH_STATS_FILE");
}

static bool
radv_bvh_dumping_enabled(const struct radv_instance *instance)
{
   /* Gathering bvh stats uses a large part of the rra code for dumping bvhs. */
   return (instance->vk.trace_mode & RADV_TRACE_MODE_RRA) || radv_bvh_stats_file();
}

#endif /* RADV_INSTANCE_H */
