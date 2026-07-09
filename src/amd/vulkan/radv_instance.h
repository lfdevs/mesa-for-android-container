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
#include "radv_drirc.h"
#include "radv_radeon_winsys.h"
#include "vk_instance.h"

#ifdef ANDROID_STRICT
#define RADV_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)
#else
#define RADV_API_VERSION     VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION)
#define RADV_API_VERSION_1_3 VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION)
#endif

/* Please keep docs/envvars.rst up-to-date when you add/remove options. */
enum {
   RADV_DEBUG_NO_FAST_CLEARS = 1ull << 0,
   RADV_DEBUG_NO_DCC = 1ull << 1,
   RADV_DEBUG_NO_CACHE_COMPAT = 1ull << 2,
   RADV_DEBUG_NO_CACHE = 1ull << 3,
   RADV_DEBUG_DUMP_SHADER_STATS = 1ull << 4,
   RADV_DEBUG_NO_HIZ = 1ull << 5,
   RADV_DEBUG_NO_COMPUTE_QUEUE = 1ull << 6,
   RADV_DEBUG_ALL_BOS = 1ull << 7,
   RADV_DEBUG_NO_IB_CHAINING = 1ull << 8,
   RADV_DEBUG_DUMP_SPIRV = 1ull << 9,
   RADV_DEBUG_ZERO_VRAM = 1ull << 10,
   RADV_DEBUG_SYNC_SHADERS = 1ull << 11,
   RADV_DEBUG_DUMP_PREOPT_IR = 1ull << 12,
   RADV_DEBUG_INFO = 1ull << 13,
   RADV_DEBUG_STARTUP = 1ull << 14,
   RADV_DEBUG_CHECKIR = 1ull << 15,
   RADV_DEBUG_NOBINNING = 1ull << 16,
   RADV_DEBUG_NO_NGG = 1ull << 17,
   RADV_DEBUG_DUMP_META_SHADERS = 1ull << 18,
   RADV_DEBUG_LLVM = 1ull << 19,
   RADV_DEBUG_FORCE_COMPRESS = 1ull << 20,
   RADV_DEBUG_HANG = 1ull << 21,
   RADV_DEBUG_IMG = 1ull << 22,
   RADV_DEBUG_NO_UMR = 1ull << 23,
   RADV_DEBUG_NO_DISPLAY_DCC = 1ull << 24,
   RADV_DEBUG_NO_TC_COMPAT_CMASK = 1ull << 25,
   RADV_DEBUG_NO_VRS_FLAT_SHADING = 1ull << 26,
   RADV_DEBUG_NO_ATOC_DITHERING = 1ull << 27,
   RADV_DEBUG_NO_NGGC = 1ull << 28,
   RADV_DEBUG_DUMP_PROLOGS = 1ull << 29,
   RADV_DEBUG_NO_DMA_BLIT = 1ull << 30,
   RADV_DEBUG_DUMP_EPILOGS = 1ull << 31,
   RADV_DEBUG_NO_FMASK = 1ull << 32,
   RADV_DEBUG_SHADOW_REGS = 1ull << 33,
   RADV_DEBUG_EXTRA_MD = 1ull << 34,
   RADV_DEBUG_NO_GPL = 1ull << 35,
   RADV_DEBUG_NO_RT = 1ull << 36,
   RADV_DEBUG_NO_MESH_SHADER = 1ull << 37,
   RADV_DEBUG_NO_ESO = 1ull << 38,
   RADV_DEBUG_PSO_CACHE_STATS = 1ull << 39,
   RADV_DEBUG_NIR_DEBUG_INFO = 1ull << 40,
   RADV_DEBUG_DUMP_TRAP_HANDLER = 1ull << 41,
   RADV_DEBUG_DUMP_VS = 1ull << 42,
   RADV_DEBUG_DUMP_TCS = 1ull << 43,
   RADV_DEBUG_DUMP_TES = 1ull << 44,
   RADV_DEBUG_DUMP_GS = 1ull << 45,
   RADV_DEBUG_DUMP_PS = 1ull << 46,
   RADV_DEBUG_DUMP_TASK = 1ull << 47,
   RADV_DEBUG_DUMP_MESH = 1ull << 48,
   RADV_DEBUG_DUMP_CS = 1ull << 49,
   RADV_DEBUG_DUMP_NIR = 1ull << 50,
   RADV_DEBUG_DUMP_ASM = 1ull << 51,
   RADV_DEBUG_DUMP_BACKEND_IR = 1ull << 52,
   RADV_DEBUG_PSO_HISTORY = 1ull << 53,
   RADV_DEBUG_BVH4 = 1ull << 54,
   RADV_DEBUG_NO_VIDEO = 1ull << 55,
   RADV_DEBUG_VALIDATE_VAS = 1ull << 56,
   RADV_DEBUG_DUMP_BO_HISTORY = 1ull << 57,
   RADV_DEBUG_DUMP_IBS = 1ull << 58,
   RADV_DEBUG_VM = 1ull << 59,
   RADV_DEBUG_NO_SMEM_MITIGATION = 1ull << 60,
   RADV_DEBUG_FULL_SYNC = 1ull << 61,
   RADV_DEBUG_NO_TMZ = 1ull << 62,
   RADV_DEBUG_NO_HEAP = 1ull << 63,
   RADV_DEBUG_DUMP_SHADERS = RADV_DEBUG_DUMP_VS | RADV_DEBUG_DUMP_TCS | RADV_DEBUG_DUMP_TES | RADV_DEBUG_DUMP_GS |
                             RADV_DEBUG_DUMP_PS | RADV_DEBUG_DUMP_TASK | RADV_DEBUG_DUMP_MESH | RADV_DEBUG_DUMP_CS |
                             RADV_DEBUG_DUMP_NIR | RADV_DEBUG_DUMP_ASM | RADV_DEBUG_DUMP_BACKEND_IR,
};

/* emulate_rt, video_decode, transfer_queue, video_encode, hic, sparse and bfloat16 are deprecated,
 * use RADV_EXPERIMENTAL instead.
 */
enum {
   RADV_PERFTEST_LOCAL_BOS = 1u << 0,
   RADV_PERFTEST_DCC_MSAA = 1u << 1,
   RADV_PERFTEST_CS_WAVE_32 = 1u << 2,
   RADV_PERFTEST_PS_WAVE_32 = 1u << 3,
   RADV_PERFTEST_GE_WAVE_32 = 1u << 4,
   RADV_PERFTEST_NO_SAM = 1u << 5,
   RADV_PERFTEST_SAM = 1u << 6,
   RADV_PERFTEST_NGGC = 1u << 7,
   RADV_PERFTEST_EMULATE_RT = 1u << 8,
   RADV_PERFTEST_RT_WAVE_64 = 1u << 9,
   RADV_PERFTEST_VIDEO_DECODE = 1u << 10,
   RADV_PERFTEST_DMA_SHADERS = 1u << 11,
   RADV_PERFTEST_TRANSFER_QUEUE = 1u << 12,
   RADV_PERFTEST_NIR_CACHE = 1u << 13,
   RADV_PERFTEST_VIDEO_ENCODE = 1u << 14,
   RADV_PERFTEST_NO_GTT_SPILL = 1u << 15,
   RADV_PERFTEST_HIC = 1u << 16,
   RADV_PERFTEST_SPARSE = 1u << 17,
   RADV_PERFTEST_RT_CPS = 1u << 18,
   RADV_PERFTEST_BFLOAT16 = 1u << 19,
   RADV_PERFTEST_LOWLATENCYDEC = 1u << 20,
   RADV_PERFTEST_LOWLATENCYENC = 1u << 21,
};

enum {
   RADV_EXPERIMENTAL_EMULATE_RT = 1u << 0,
   RADV_EXPERIMENTAL_VIDEO_DECODE = 1u << 1,
   RADV_EXPERIMENTAL_TRANSFER_QUEUE = 1u << 2,
   RADV_EXPERIMENTAL_VIDEO_ENCODE = 1u << 3,
   RADV_EXPERIMENTAL_HIC = 1u << 4,
   RADV_EXPERIMENTAL_SPARSE = 1u << 5,
   RADV_EXPERIMENTAL_BFLOAT16 = 1u << 6,
   RADV_EXPERIMENTAL_MSRTSS = 1u << 7,
};

enum {
   RADV_TRAP_EXCP_MEM_VIOL = 1u << 0,
   RADV_TRAP_EXCP_FLOAT_DIV_BY_ZERO = 1u << 1,
   RADV_TRAP_EXCP_FLOAT_OVERFLOW = 1u << 2,
   RADV_TRAP_EXCP_FLOAT_UNDERFLOW = 1u << 3,
};

enum {
   RADV_QUEUE_DISABLE_GENERAL = 1u << 0,
   RADV_QUEUE_DISABLE_COMPUTE = 1u << 1,
   RADV_QUEUE_DISABLE_VIDEO_DEC = 1u << 2,
   RADV_QUEUE_DISABLE_VIDEO_ENC = 1u << 3,
   RADV_QUEUE_DISABLE_TRANSFER = 1u << 4,
   RADV_QUEUE_DISABLE_SPARSE = 1u << 5,
};

enum radv_trace_mode {
   /** Radeon GPU Profiler */
   RADV_TRACE_MODE_RGP = 1 << VK_TRACE_MODE_COUNT,

   /** Radeon Raytracing Analyzer */
   RADV_TRACE_MODE_RRA = 1 << (VK_TRACE_MODE_COUNT + 1),

   RADV_TRACE_MODE_GAMMA = 1 << (VK_TRACE_MODE_COUNT + 2),

   /** Gather context rolls of submitted command buffers */
   RADV_TRACE_MODE_CTX_ROLLS = 1 << (VK_TRACE_MODE_COUNT + 3),

   RADV_TRACE_MODE_RANGES = 1 << (VK_TRACE_MODE_COUNT + 4),
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
   return (instance->vk.trace_mode & (RADV_TRACE_MODE_RRA | RADV_TRACE_MODE_GAMMA)) || radv_bvh_stats_file();
}

#endif /* RADV_INSTANCE_H */
