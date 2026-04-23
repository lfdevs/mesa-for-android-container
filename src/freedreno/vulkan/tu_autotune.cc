/*
 * Copyright © 2021 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "tu_autotune.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <string_view>

#include "util/rand_xor.h"

#define XXH_INLINE_ALL
#include "util/xxhash.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_image.h"
#include "tu_pass.h"

/** Compile-time debug options **/

#define TU_AUTOTUNE_DEBUG_LOG_BASE      0
#define TU_AUTOTUNE_DEBUG_LOG_BANDWIDTH 0
#define TU_AUTOTUNE_DEBUG_LOG_PROFILED  0
#define TU_AUTOTUNE_DEBUG_LOG_PREEMPT   0

#if TU_AUTOTUNE_DEBUG_LOG_BASE
#define at_log_base(fmt, ...)         mesa_logi("autotune: " fmt, ##__VA_ARGS__)
#define at_log_base_h(fmt, hash, ...) mesa_logi("autotune %016" PRIx64 ": " fmt, hash, ##__VA_ARGS__)
#else
#define at_log_base(fmt, ...)
#define at_log_base_h(fmt, hash, ...)
#endif

#if TU_AUTOTUNE_DEBUG_LOG_BANDWIDTH
#define at_log_bandwidth_h(fmt, hash, ...) mesa_logi("autotune-bw %016" PRIx64 ": " fmt, hash, ##__VA_ARGS__)
#else
#define at_log_bandwidth_h(fmt, hash, ...)
#endif

#if TU_AUTOTUNE_DEBUG_LOG_PROFILED
#define at_log_profiled_h(fmt, hash, ...) mesa_logi("autotune-prof %016" PRIx64 ": " fmt, hash, ##__VA_ARGS__)
#else
#define at_log_profiled_h(fmt, hash, ...)
#endif

#if TU_AUTOTUNE_DEBUG_LOG_PREEMPT
#define at_log_preempt_h(fmt, hash, ...) mesa_logi("autotune-preempt %016" PRIx64 ": " fmt, hash, ##__VA_ARGS__)
#else
#define at_log_preempt_h(fmt, hash, ...)
#endif

/* Process any pending entries on autotuner finish, could be used to gather data from traces. */
#define TU_AUTOTUNE_FLUSH_AT_FINISH 0

/** Global constants and helpers **/

/* GPU always-on timer constants */
constexpr uint64_t ALWAYS_ON_FREQUENCY_HZ = 19'200'000;
constexpr double GPU_TICKS_PER_US = ALWAYS_ON_FREQUENCY_HZ / 1'000'000.0;

constexpr uint64_t
ticks_to_us(uint64_t ticks)
{
   return ticks / GPU_TICKS_PER_US;
}

constexpr bool
fence_before(uint32_t a, uint32_t b)
{
   /* Essentially a < b, but handles wrapped values. */
   return (int32_t) (a - b) < 0;
}

constexpr const char *
render_mode_str(tu_autotune::render_mode mode)
{
   switch (mode) {
   case tu_autotune::render_mode::SYSMEM:
      return "SYSMEM";
   case tu_autotune::render_mode::GMEM:
      return "GMEM";
   default:
      return "UNKNOWN";
   }
}

/** Configuration **/

enum class tu_autotune::algorithm : uint8_t {
   BANDWIDTH = 0,     /* Uses estimated BW for determining rendering mode. */
   PROFILED = 1,      /* Uses dynamically profiled results for determining rendering mode. */
   PROFILED_IMM = 2,  /* Same as PROFILED but immediately resolves the SYSMEM/GMEM probability. */
   PREFER_SYSMEM = 3, /* Always use SYSMEM unless we have strong evidence that GMEM is better. */
   PREFER_GMEM = 4,   /* Always use GMEM unless we have strong evidence that SYSMEM is better. */

   DEFAULT = BANDWIDTH, /* Default algorithm, used if no other is specified. */
};

/* Modifier flags, these modify the behavior of the autotuner in a user-defined way. */
enum class tu_autotune::mod_flag : uint8_t {
   BIG_GMEM = BIT(1),         /* All RPs with >= 10 draws use GMEM. */
   TUNE_SMALL = BIT(2),       /* Try tuning all RPs with <= 5 draws, ignored by default. */
   PREEMPT_OPTIMIZE = BIT(3),  /* Attempts to minimize the preemption latency. */
};

/* Metric flags, for internal tracking of enabled metrics. */
enum class tu_autotune::metric_flag : uint8_t {
   SAMPLES = BIT(1), /* Enable tracking samples passed metric. */
   TS = BIT(2),      /* Enable tracking per-RP timestamp metric. */
   TS_TILE = BIT(3), /* Enable tracking per-tile timestamp metric. */
};

struct PACKED tu_autotune::config_t {
 private:
   algorithm algo = algorithm::DEFAULT;
   uint8_t mod_flags = 0;    /* See mod_flag enum. */
   uint8_t metric_flags = 0; /* See metric_flag enum. */

   constexpr void update_metric_flags()
   {
      /* Note: Always keep in sync with rp_history to prevent UB. */
      if (algo == algorithm::BANDWIDTH) {
         metric_flags |= (uint8_t) metric_flag::SAMPLES;
      } else if (algo == algorithm::PROFILED || algo == algorithm::PROFILED_IMM) {
         metric_flags |= (uint8_t) metric_flag::TS;
      }

      if (mod_flags & (uint8_t) mod_flag::PREEMPT_OPTIMIZE) {
         metric_flags |= (uint8_t) metric_flag::TS | (uint8_t) metric_flag::TS_TILE;
      }
   }

 public:
   constexpr config_t() = default;

   constexpr config_t(algorithm algo, uint8_t mod_flags): algo(algo), mod_flags(mod_flags)
   {
      update_metric_flags();
   }

   constexpr bool is_enabled(algorithm a) const
   {
      return algo == a;
   }

   constexpr bool test(mod_flag f) const
   {
      return mod_flags & (uint32_t) f;
   }

   constexpr bool test(metric_flag f) const
   {
      return metric_flags & (uint32_t) f;
   }

   constexpr bool set_algo(algorithm a)
   {
      if (algo == a)
         return false;

      algo = a;
      update_metric_flags();
      return true;
   }

   constexpr bool disable(mod_flag f)
   {
      if (!(mod_flags & (uint8_t) f))
         return false;

      mod_flags &= ~(uint8_t) f;
      update_metric_flags();
      return true;
   }

   constexpr bool enable(mod_flag f)
   {
      if (mod_flags & (uint8_t) f)
         return false;

      mod_flags |= (uint8_t) f;
      update_metric_flags();
      return true;
   }

   std::string to_string() const
   {
#define ALGO_STR(algo_name)                                                                                            \
   if (algo == algorithm::algo_name)                                                                                   \
      str += #algo_name;
#define MODF_STR(flag)                                                                                                 \
   if (mod_flags & (uint8_t) mod_flag::flag) {                                                                         \
      str += #flag " ";                                                                                                \
   }
#define METRICF_STR(flag)                                                                                              \
   if (metric_flags & (uint8_t) metric_flag::flag) {                                                                   \
      str += #flag " ";                                                                                                \
   }

      std::string str = "Algorithm: ";

      ALGO_STR(BANDWIDTH);
      ALGO_STR(PROFILED);
      ALGO_STR(PROFILED_IMM);
      ALGO_STR(PREFER_SYSMEM);
      ALGO_STR(PREFER_GMEM);

      str += ", Mod Flags: 0x" + std::to_string(mod_flags) + " (";
      MODF_STR(BIG_GMEM);
      MODF_STR(TUNE_SMALL);
      MODF_STR(PREEMPT_OPTIMIZE);
      str += ")";

      str += ", Metric Flags: 0x" + std::to_string(metric_flags) + " (";
      METRICF_STR(SAMPLES);
      METRICF_STR(TS);
      METRICF_STR(TS_TILE);
      str += ")";

      return str;

#undef ALGO_STR
#undef MODF_STR
#undef METRICF_STR
   }
};

union PACKED tu_autotune::packed_config_t {
   config_t config;
   uint32_t bits = 0;
   static_assert(sizeof(bits) >= sizeof(config));
   static_assert(std::is_trivially_copyable<config_t>::value,
                 "config_t must be trivially copyable to be automatically packed");

   constexpr packed_config_t(config_t p_config): bits(0)
   {
      config = p_config; /* Set after bits(0) to avoid UB in sizeof(bits) > sizeof(config) case.*/
   }

   constexpr packed_config_t(uint32_t bits): bits(bits)
   {
   }
};

tu_autotune::atomic_config_t::atomic_config_t(config_t initial): config_bits(packed_config_t { initial }.bits)
{
}

tu_autotune::config_t
tu_autotune::atomic_config_t::load() const
{
   return config_t(packed_config_t { config_bits.load(std::memory_order_relaxed) }.config);
}

bool
tu_autotune::atomic_config_t::compare_and_store(config_t expected, config_t updated)
{
   uint32_t expected_bits = packed_config_t { expected }.bits;
   return config_bits.compare_exchange_strong(expected_bits, packed_config_t { updated }.bits,
                                              std::memory_order_acquire, std::memory_order_relaxed);
}

tu_autotune::config_t
tu_autotune::get_env_config()
{
   static std::once_flag once;
   static config_t at_config;
   std::call_once(once, [&] {
      algorithm algo = algorithm::DEFAULT;
      const char *algo_str = os_get_option("TU_AUTOTUNE_ALGO");
      std::string_view algo_strv;

      if (algo_str)
         algo_strv = algo_str;
      else if (device->instance->autotune_algo)
         algo_strv = device->instance->autotune_algo;

      if (!algo_strv.empty()) {
         if (algo_strv == "bandwidth") {
            algo = algorithm::BANDWIDTH;
         } else if (algo_strv == "profiled") {
            algo = algorithm::PROFILED;
         } else if (algo_strv == "profiled_imm") {
            algo = algorithm::PROFILED_IMM;
         } else if (algo_strv == "prefer_sysmem") {
            algo = algorithm::PREFER_SYSMEM;
         } else if (algo_strv == "prefer_gmem") {
            algo = algorithm::PREFER_GMEM;
         } else {
            mesa_logw("Unknown TU_AUTOTUNE_ALGO '%s', using default", algo_strv.data());
         }

         if (TU_DEBUG(STARTUP))
            mesa_logi("TU_AUTOTUNE_ALGO=%u (%s)", (uint8_t) algo, algo_strv.data());
      }

      /* Parse the flags from the environment variable. */
      const char *flags_env_str = os_get_option("TU_AUTOTUNE_FLAGS");
      uint32_t mod_flags = 0;
      if (flags_env_str) {
         static const struct debug_control tu_at_flags_control[] = {
            { "big_gmem", (uint32_t) mod_flag::BIG_GMEM },
            { "tune_small", (uint32_t) mod_flag::TUNE_SMALL },
            { "preempt_optimize", (uint32_t) mod_flag::PREEMPT_OPTIMIZE },
            { NULL, 0 }
         };

         mod_flags = parse_debug_string(flags_env_str, tu_at_flags_control);
         if (TU_DEBUG(STARTUP))
            mesa_logi("TU_AUTOTUNE_FLAGS=0x%x (%s)", mod_flags, flags_env_str);

         if ((mod_flags & ~supported_mod_flags) != 0) {
            mesa_logw("Unsupported TU_AUTOTUNE_FLAGS=0x%x, supported flags: 0x%x", mod_flags, supported_mod_flags);
            mod_flags &= supported_mod_flags;
         }
      }

      assert((uint8_t) mod_flags == mod_flags);
      at_config = config_t(algo, (uint8_t) mod_flags);
   });

   if (TU_DEBUG(STARTUP))
      mesa_logi("TU_AUTOTUNE: %s", at_config.to_string().c_str());

   return at_config;
}

uint32_t
tu_autotune::get_supported_mod_flags(tu_device *device) const
{
   uint32_t supported_mod_flags = (uint32_t) mod_flag::BIG_GMEM | (uint32_t) mod_flag::TUNE_SMALL;
   if (device->physical_device->info->props.max_draw_states > TU_DRAW_STATE_AT_WRITE_RP_HASH &&
       device->physical_device->is_perf_cntr_selectable) {
      supported_mod_flags |= (uint32_t) mod_flag::PREEMPT_OPTIMIZE;
   }
   return supported_mod_flags;
}

/** Global Fence and Internal CS Management **/

tu_autotune::submission_entry::submission_entry(tu_device *device): fence(0)
{
   tu_cs_init(&fence_cs, device, TU_CS_MODE_GROW, 5, "autotune fence cs");
}

tu_autotune::submission_entry::~submission_entry()
{
   assert(!is_active());
   tu_cs_finish(&fence_cs);
}

bool
tu_autotune::submission_entry::is_active() const
{
   return fence_cs.device->global_bo_map->autotune_fence < fence;
}

template <chip CHIP>
static void
write_fence_cs(struct tu_device *dev, struct tu_cs *cs, uint32_t fence)
{
   uint64_t dst_iova = dev->global_bo->iova + gb_offset(autotune_fence);
   if (CHIP >= A7XX) {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = CACHE_FLUSH_TS, .write_src = EV_WRITE_USER_32B, .write_dst = EV_DST_RAM,
                                       .write_enabled = true)
                        .value);
   } else {
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS));
   }

   tu_cs_emit_qw(cs, dst_iova);
   tu_cs_emit(cs, fence);
}

struct tu_cs *
tu_autotune::submission_entry::try_get_cs(uint32_t new_fence)
{
   if (is_active()) {
      /* If the CS is already active, we cannot write to it. */
      return nullptr;
   }

   struct tu_device *device = fence_cs.device;
   tu_cs_reset(&fence_cs);
   tu_cs_begin(&fence_cs);
   TU_CALLX(device, write_fence_cs)(device, &fence_cs, new_fence);
   tu_cs_end(&fence_cs);
   assert(fence_cs.entry_count == 1); /* We expect the initial allocation to be large enough. */
   fence = new_fence;

   return &fence_cs;
}

struct tu_cs *
tu_autotune::get_cs_for_fence(uint32_t fence)
{
   for (submission_entry &entry : submission_entries) {
      struct tu_cs *cs = entry.try_get_cs(fence);
      if (cs)
         return cs;
   }

   /* If we reach here, we have to allocate a new entry. */
   submission_entry &entry = submission_entries.emplace_back(device);
   struct tu_cs *cs = entry.try_get_cs(fence);
   assert(cs); /* We just allocated it, so it should be available. */
   return cs;
}

/** RP Entry Management **/

/* The part of the per-RP entry which is written by the GPU. */
struct PACKED tu_autotune::rp_gpu_data {
   /* HW requires the sample start/stop locations to be 128b aligned. */
   alignas(16) uint64_t samples_start;
   alignas(16) uint64_t samples_end;
   uint64_t ts_start;
   uint64_t ts_end;
};

/* Per-tile values for GMEM rendering, this structure is appended to the end of rp_gpu_data for each tile. */
struct PACKED tu_autotune::tile_gpu_data {
   uint64_t ts_start;
   uint64_t ts_end;

   /* A helper for the offset of this relative to BO start. */
   static constexpr uint64_t offset(uint32_t tile_index)
   {
      return sizeof(rp_gpu_data) + (tile_index * sizeof(tile_gpu_data));
   }
};

/* ALl of these values correspond to the RP in the batch with the max preemption latency. */
struct PACKED tu_autotune::rp_batch_preempt_gpu_data {
   uint64_t preemption_latency; /* in CP clock ticks. */
   uint64_t preemption_latency_rp_hash;
   uint64_t always_count_delta;
   uint64_t aon_delta;
};

/* A small wrapper around rp_history to provide ref-counting and usage timestamps. */
struct tu_autotune::rp_history_handle {
   rp_history *history;

   /* Note: Must be called with rp_mutex held. */
   rp_history_handle(rp_history &history);

   constexpr rp_history_handle(std::nullptr_t): history(nullptr)
   {
   }

   rp_history_handle(const rp_history_handle &) = delete;
   rp_history_handle &operator=(const rp_history_handle &) = delete;

   constexpr rp_history_handle(rp_history_handle &&other): history(other.history)
   {
      other.history = nullptr;
   }

   constexpr rp_history_handle &operator=(rp_history_handle &&other)
   {
      if (this != &other) {
         history = other.history;
         other.history = nullptr;
      }
      return *this;
   }

   constexpr operator bool() const
   {
      return history != nullptr;
   }

   constexpr rp_history &operator*() const
   {
      assert(history);
      return *history;
   }

   constexpr operator rp_history *() const
   {
      return history;
   }

   constexpr rp_history *operator->() const
   {
      assert(history);
      return history;
   }

   ~rp_history_handle();
};

/* An "entry" of renderpass autotune results, which is used to store the results of a renderpass autotune run for a
 * given command buffer. */
struct tu_autotune::rp_entry {
 private:
   struct tu_device *device;

   struct tu_suballoc_bo bo;
   uint8_t *map; /* A direct pointer to the BO's CPU mapping. */

   static_assert(alignof(rp_gpu_data) == 16);
   static_assert(offsetof(rp_gpu_data, samples_start) == 0);
   static_assert(offsetof(rp_gpu_data, samples_end) == 16);
   static_assert(sizeof(rp_gpu_data) % alignof(tile_gpu_data) == 0);

 public:
   rp_history_handle history;
   config_t config; /* Configuration at the time of entry creation. */
   bool sysmem;
   uint32_t tile_count;
   uint32_t draw_count;

   rp_entry(struct tu_device *device, rp_history_handle &&history, config_t config, uint32_t draw_count)
       : device(device), map(nullptr), history(std::move(history)), config(config), draw_count(draw_count)
   {
   }

   ~rp_entry()
   {
      if (map) {
         std::scoped_lock lock(device->autotune->suballoc_mutex);
         tu_suballoc_bo_free(&device->autotune->suballoc, &bo);
      }
   }

   /* Disable the copy/move operators as that shouldn't be done. */
   rp_entry(const rp_entry &) = delete;
   rp_entry &operator=(const rp_entry &) = delete;
   rp_entry(rp_entry &&) = delete;
   rp_entry &operator=(rp_entry &&) = delete;

   void allocate(bool sysmem, uint32_t tile_count)
   {
      this->sysmem = sysmem;
      this->tile_count = tile_count;
      size_t total_size = sizeof(rp_gpu_data) + (tile_count * sizeof(tile_gpu_data));

      std::scoped_lock lock(device->autotune->suballoc_mutex);
      VkResult result = tu_suballoc_bo_alloc(&bo, &device->autotune->suballoc, total_size, alignof(rp_gpu_data));
      if (result != VK_SUCCESS) {
         mesa_loge("Failed to allocate BO for autotune rp_entry: %u", result);
         return;
      }

      map = (uint8_t *) tu_suballoc_bo_map(&bo);
      memset(map, 0, total_size);
   }

   rp_gpu_data &get_gpu_data()
   {
      assert(map);
      return *(rp_gpu_data *) map;
   }

   tile_gpu_data &get_tile_gpu_data(uint32_t tile_index)
   {
      assert(map);
      assert(tile_index < tile_count);
      uint64_t offset = tile_gpu_data::offset(tile_index);
      return *(tile_gpu_data *) (map + offset);
   }

   /** Samples-Passed Metric **/

   uint64_t get_samples_passed()
   {
      assert(config.test(metric_flag::SAMPLES));
      rp_gpu_data &gpu = get_gpu_data();
      return gpu.samples_end - gpu.samples_start;
   }

   void emit_metric_samples_start(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint64_t start_iova)
   {
      tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));
      if (cmd->device->physical_device->info->props.has_event_write_sample_count) {
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
         tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true).value);
         tu_cs_emit_qw(cs, start_iova);

         /* If the renderpass contains an occlusion query with its own ZPASS_DONE, we have to provide a fake ZPASS_DONE
          * event here to logically close the previous one, preventing firmware from misbehaving due to nested events.
          * This writes into the samples_end field, which will be overwritten in tu_autotune_end_renderpass.
          */
         if (cmd->state.rp.has_zpass_done_sample_count_write_in_rp) {
            tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
            tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true,
                                             .sample_count_end_offset = true, .write_accum_sample_count_diff = true)
                              .value);
            tu_cs_emit_qw(cs, start_iova);
         }
      } else {
         tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_BASE(.qword = start_iova));
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
         tu_cs_emit(cs, ZPASS_DONE);
      }
   }

   void emit_metric_samples_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint64_t start_iova, uint64_t end_iova)
   {
      tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_CNTL(.copy = true));
      if (cmd->device->physical_device->info->props.has_event_write_sample_count) {
         /* If the renderpass contains ZPASS_DONE events we emit a fake ZPASS_DONE event here, composing a pair of these
          * events that firmware handles without issue. This first event writes into the samples_end field and the
          * second event overwrites it. The second event also enables the accumulation flag even when we don't use that
          * result because the blob always sets it.
          */
         if (cmd->state.rp.has_zpass_done_sample_count_write_in_rp) {
            tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
            tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true).value);
            tu_cs_emit_qw(cs, end_iova);
         }

         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE7, 3);
         tu_cs_emit(cs, CP_EVENT_WRITE7_0(.event = ZPASS_DONE, .write_sample_count = true,
                                          .sample_count_end_offset = true, .write_accum_sample_count_diff = true)
                           .value);
         tu_cs_emit_qw(cs, start_iova);
      } else {
         tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_COUNTER_BASE(.qword = end_iova));
         tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
         tu_cs_emit(cs, ZPASS_DONE);
      }
   }

   /** RP/Tile Timestamp Metric **/

   uint64_t get_rp_duration()
   {
      assert(config.test(metric_flag::TS));
      rp_gpu_data &gpu = get_gpu_data();
      return gpu.ts_end - gpu.ts_start;
   }

   /* The amount of cycles spent in the longest tile. This is used to calculate the average draw duration for
    * determining the largest non-preemptible duration for GMEM rendering.
    */
   uint64_t get_max_tile_duration()
   {
      assert(config.test(metric_flag::TS_TILE));
      uint64_t max_duration = 0;
      for (uint32_t i = 0; i < tile_count; i++) {
         tile_gpu_data &tile = get_tile_gpu_data(i);
         max_duration = MAX2(max_duration, tile.ts_end - tile.ts_start);
      }
      return max_duration;
   }

   void emit_metric_timestamp(struct tu_cs *cs, uint64_t timestamp_iova)
   {
      tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
      tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(TU_CALLX(device, __CP_ALWAYS_ON_COUNTER)({}).reg) | CP_REG_TO_MEM_0_CNT(2) |
                        CP_REG_TO_MEM_0_64B);
      tu_cs_emit_qw(cs, timestamp_iova);
   }

   /** CS Emission **/

   void emit_rp_start(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
   {
      assert(map && bo.iova);
      uint64_t bo_iova = bo.iova;
      if (config.test(metric_flag::SAMPLES))
         emit_metric_samples_start(cmd, cs, bo_iova + offsetof(rp_gpu_data, samples_start));

      if (config.test(metric_flag::TS))
         emit_metric_timestamp(cs, bo_iova + offsetof(rp_gpu_data, ts_start));
   }

   void emit_rp_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
   {
      assert(map && bo.iova);
      uint64_t bo_iova = bo.iova;
      if (config.test(metric_flag::SAMPLES))
         emit_metric_samples_end(cmd, cs, bo_iova + offsetof(rp_gpu_data, samples_start),
                                 bo_iova + offsetof(rp_gpu_data, samples_end));

      if (config.test(metric_flag::TS))
         emit_metric_timestamp(cs, bo_iova + offsetof(rp_gpu_data, ts_end));
   }

   void emit_tile_start(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint32_t tile_index)
   {
      assert(map && bo.iova);
      assert(!sysmem);
      assert(tile_index < tile_count);
      if (config.test(metric_flag::TS_TILE))
         emit_metric_timestamp(cs, bo.iova + tile_gpu_data::offset(tile_index) + offsetof(tile_gpu_data, ts_start));
   }

   void emit_tile_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint32_t tile_index)
   {
      assert(map && bo.iova);
      assert(!sysmem);
      assert(tile_index < tile_count);
      if (config.test(metric_flag::TS_TILE))
         emit_metric_timestamp(cs, bo.iova + tile_gpu_data::offset(tile_index) + offsetof(tile_gpu_data, ts_end));
   }
};

tu_autotune::rp_batch_preempt_latency::rp_batch_preempt_latency(struct tu_device *device, bool allocate)
    : device(device), allocated(allocate)
{
   if (!allocate)
      return;

   {
      std::scoped_lock lock(device->autotune->suballoc_mutex);
      VkResult result = tu_suballoc_bo_alloc(&bo, &device->autotune->suballoc, sizeof(rp_batch_preempt_gpu_data),
                                             alignof(rp_batch_preempt_gpu_data));

      if (result != VK_SUCCESS) {
         mesa_loge("Failed to allocate BO for autotune rp_batch_preempt_gpu_data: %u", result);
         allocated = false;
         return;
      }
   }

   map = (uint8_t *) tu_suballoc_bo_map(&bo);
   memset(map, 0, sizeof(rp_batch_preempt_gpu_data));
}

tu_autotune::rp_batch_preempt_latency::~rp_batch_preempt_latency()
{
   if (!allocated)
      return;

   std::scoped_lock lock(device->autotune->suballoc_mutex);
   tu_suballoc_bo_free(&device->autotune->suballoc, &bo);
}

tu_autotune::rp_batch_preempt_gpu_data
tu_autotune::rp_batch_preempt_latency::get_gpu_data()
{
   assert(allocated);
   return *(rp_batch_preempt_gpu_data *) map;
}

tu_autotune::rp_entry_batch::rp_entry_batch(struct tu_device *device, bool track_preempt_latency)
    : active(false), fence(0), entries(), preempt_latency(device, track_preempt_latency)
{
}

bool
tu_autotune::rp_entry_batch::requires_processing() const
{
   return !entries.empty() || (preempt_latency.allocated && !all_renderpasses.empty());
}

void
tu_autotune::rp_entry_batch::assign_fence(uint32_t new_fence)
{
   assert(!active); /* Cannot assign a fence to an active entry batch. */
   fence = new_fence;
   active = true;
}

void
tu_autotune::rp_entry_batch::mark_inactive()
{
   assert(active);
   active = false;
   fence = 0;
}

void
tu_autotune::rp_entry_batch::snapshot_preempt_data(struct tu_cs *cs)
{
   if (!preempt_latency.allocated)
      return;

   constexpr size_t base_offset = gb_offset(max_preemption_latency);
   static_assert(gb_offset(max_preemption_latency) ==
                 base_offset + offsetof(rp_batch_preempt_gpu_data, preemption_latency));
   static_assert(gb_offset(max_preemption_latency_rp_hash) ==
                 base_offset + offsetof(rp_batch_preempt_gpu_data, preemption_latency_rp_hash));
   static_assert(gb_offset(max_always_count_delta) ==
                 base_offset + offsetof(rp_batch_preempt_gpu_data, always_count_delta));
   static_assert(gb_offset(max_aon_delta) == base_offset + offsetof(rp_batch_preempt_gpu_data, aon_delta));
   static_assert(sizeof(rp_batch_preempt_gpu_data) == 32);

   tu_cs_emit_pkt7(cs, CP_MEMCPY, 5);
   tu_cs_emit(cs, sizeof(rp_batch_preempt_gpu_data) / sizeof(uint32_t));
   tu_cs_emit_qw(cs, preempt_latency.device->global_bo->iova + base_offset);
   tu_cs_emit_qw(cs, preempt_latency.bo.iova);
}

/** Renderpass state tracking. **/

tu_autotune::rp_key::rp_key(const struct tu_render_pass *pass,
                            const struct tu_framebuffer *framebuffer,
                            const struct tu_cmd_buffer *cmd)
{
   /* It may be hard to match the same renderpass between frames, or rather it's hard to strike a
    * balance between being too lax with identifying different renderpasses as the same one, and
    * not recognizing the same renderpass between frames when only a small thing changed.
    *
    * This is mainly an issue with translation layers (particularly DXVK), because a layer may
    * break a "renderpass" into smaller ones due to some heuristic that isn't consistent between
    * frames.
    *
    * Note: Not using image IOVA leads to too many false matches.
    */

   struct PACKED packed_att_properties {
      uint64_t iova;
      bool load;
      bool store;
      bool load_stencil;
      bool store_stencil;
   };

   auto get_hash = [&](uint32_t *data, size_t size) {
      uint32_t *ptr = data;
      *ptr++ = framebuffer->width;
      *ptr++ = framebuffer->height;
      *ptr++ = framebuffer->layers;

      for (unsigned i = 0; i < pass->attachment_count; i++) {
         packed_att_properties props = {
            .iova = cmd->state.attachments[i]->image->iova + cmd->state.attachments[i]->view.offset,
            .load = pass->attachments[i].load,
            .store = pass->attachments[i].store,
            .load_stencil = pass->attachments[i].load_stencil,
            .store_stencil = pass->attachments[i].store_stencil,
         };

         memcpy(ptr, &props, sizeof(packed_att_properties));
         ptr += sizeof(packed_att_properties) / sizeof(uint32_t);
      }
      assert(ptr == data + size);

      return XXH3_64bits(data, size * sizeof(uint32_t));
   };

   /* We do a manual Boost-style "small vector" optimization here where the stack is used for the vast majority of
    * cases, while only extreme cases need to allocate on the heap.
    */
   size_t data_count = 3 + (pass->attachment_count * sizeof(packed_att_properties) / sizeof(uint32_t));
   constexpr size_t STACK_MAX_DATA_COUNT = 3 + (5 * 3); /* in u32 units. */

   if (data_count <= STACK_MAX_DATA_COUNT) {
      /* If the data is small enough, we can use the stack. */
      std::array<uint32_t, STACK_MAX_DATA_COUNT> arr;
      hash = get_hash(arr.data(), data_count);
   } else {
      /* If the data is too large, we have to allocate it on the heap. */
      std::vector<uint32_t> vec(data_count);
      hash = get_hash(vec.data(), vec.size());
   }
}

tu_autotune::rp_key::rp_key(const rp_key &key, uint32_t duplicates)
{
   hash = XXH3_64bits_withSeed(&key.hash, sizeof(key.hash), duplicates);
}

/* Exponential moving average (EMA) calculator for smoothing successive values of any metric. An alpha (smoothing
 * factor) of 0.1 means 10% weight to new values (slow adaptation), while 0.9 means 90% weight (fast adaptation).
 */
template <typename T = double> class exponential_average {
 private:
   std::atomic<double> average = std::numeric_limits<double>::quiet_NaN();
   double alpha;

 public:
   explicit exponential_average(double alpha = 0.1) noexcept: alpha(alpha)
   {
   }

   bool empty() const noexcept
   {
      double current = average.load(std::memory_order_relaxed);
      return std::isnan(current);
   }

   void add(T value) noexcept
   {
      double v = static_cast<double>(value);
      double current = average.load(std::memory_order_relaxed);
      double new_avg;
      do {
         new_avg = std::isnan(current) ? v : (1.0 - alpha) * current + alpha * v;
      } while (!average.compare_exchange_weak(current, new_avg, std::memory_order_relaxed, std::memory_order_relaxed));
   }

   void clear() noexcept
   {
      average.store(std::numeric_limits<double>::quiet_NaN(), std::memory_order_relaxed);
   }

   T get() const noexcept
   {
      double current = average.load(std::memory_order_relaxed);
      return std::isnan(current) ? T {} : static_cast<T>(current);
   }
};

/* An improvement over pure EMA to filter out spikes by using two EMAs:
 * - A "slow" EMA with a low alpha to track the long-term average.
 * - A "fast" EMA with a high alpha to track short-term changes.
 * When retrieving the average, if the fast EMA deviates significantly from the slow EMA, it indicates a spike, and we
 * fall back to the slow EMA.
 */
template <typename T = double> class adaptive_average {
 private:
   static constexpr double DEFAULT_SLOW_ALPHA = 0.1, DEFAULT_FAST_ALPHA = 0.5, DEFAULT_DEVIATION_THRESHOLD = 0.3;
   exponential_average<T> slow;
   exponential_average<T> fast;
   double deviationThreshold;

 public:
   size_t count = 0;

   explicit adaptive_average(double slow_alpha = DEFAULT_SLOW_ALPHA,
                             double fast_alpha = DEFAULT_FAST_ALPHA,
                             double deviation_threshold = DEFAULT_DEVIATION_THRESHOLD) noexcept
       : slow(slow_alpha), fast(fast_alpha), deviationThreshold(deviation_threshold)
   {
   }

   void add(T value) noexcept
   {
      slow.add(value);
      fast.add(value);
      count++;
   }

   T get() const noexcept
   {
      double s = slow.get();
      double f = fast.get();
      /* Use fast if it's close to slow (normal variation).
       * Use slow if fast deviates too much (likely a spike).
       */
      double deviation = std::abs(f - s) / s;
      return (deviation < deviationThreshold) ? f : s + (f - s) * deviationThreshold;
   }

   void clear() noexcept
   {
      slow.clear();
      fast.clear();
      count = 0;
   }
};

/* All historical state pertaining to a uniquely identified RP. This integrates data from RP entries, accumulating
 * metrics over the long-term and providing autotune algorithms using the data.
 */
struct tu_autotune::rp_history {
 private:
   /* Amount of duration samples for profiling before we start averaging. */
   static constexpr uint32_t MIN_PROFILE_DURATION_COUNT = 5;

   adaptive_average<uint64_t> sysmem_rp_average;
   adaptive_average<uint64_t> gmem_rp_average;

 public:
   uint64_t hash; /* The hash of the renderpass, just for debug output. */
   uint32_t duplicates; /* The amount of times we've seen this RP, used for identifying repeated RPs. */

   std::atomic<uint32_t> refcount = 0; /* Reference count to prevent deletion when active. */
   std::atomic<uint64_t> last_use_ts;  /* Last time the reference count was updated, in monotonic nanoseconds. */

   rp_history(uint64_t hash): hash(hash), last_use_ts(os_time_get_nano()), profiled(hash)
   {
   }

   /** Bandwidth Estimation Algorithm **/
   struct bandwidth_algo {
    private:
      exponential_average<uint32_t> mean_samples_passed;

    public:
      void update(uint32_t samples)
      {
         mean_samples_passed.add(samples);
      }

      render_mode get_optimal_mode(rp_history &history,
                                   const struct tu_cmd_state *cmd_state,
                                   const struct tu_render_pass *pass,
                                   const struct tu_framebuffer *framebuffer,
                                   const struct tu_render_pass_state *rp_state)
      {
         uint32_t pass_pixel_count = 0;
         if (cmd_state->per_layer_render_area) {
            for (unsigned i = 0; i < cmd_state->pass->num_views; i++) {
               const VkExtent2D &extent = cmd_state->render_areas[i].extent;
               pass_pixel_count += extent.width * extent.height;
            }
         } else {
            const VkExtent2D &extent = cmd_state->render_areas[0].extent;
            pass_pixel_count =
               extent.width * extent.height * MAX2(cmd_state->pass->num_views, cmd_state->framebuffer->layers);
         }

         uint64_t sysmem_bandwidth = (uint64_t) pass->sysmem_bandwidth_per_pixel * pass_pixel_count;
         uint64_t gmem_bandwidth = (uint64_t) pass->gmem_bandwidth_per_pixel * pass_pixel_count;

         uint64_t total_draw_call_bandwidth = 0;
         uint64_t mean_samples = mean_samples_passed.get();
         if (rp_state->drawcall_count && mean_samples > 0.0) {
            /* The total draw call bandwidth is estimated as the average samples (collected via tracking samples passed
             * within the CS) multiplied by the drawcall bandwidth per sample, divided by the amount of draw calls.
             *
             * This is a rough estimate of the bandwidth used by the draw calls in the renderpass for FB writes which
             * is used to determine whether to use SYSMEM or GMEM.
             */
            total_draw_call_bandwidth =
               (mean_samples * rp_state->drawcall_bandwidth_per_sample_sum) / rp_state->drawcall_count;
         }

         /* Drawcalls access the memory in SYSMEM rendering (ignoring CCU). */
         sysmem_bandwidth += total_draw_call_bandwidth;

         /* Drawcalls access GMEM in GMEM rendering, but we do not want to ignore them completely.  The state changes
          * between tiles also have an overhead.  The magic numbers of 11 and 10 are randomly chosen.
          */
         gmem_bandwidth = (gmem_bandwidth * 11 + total_draw_call_bandwidth) / 10;

         bool select_sysmem = sysmem_bandwidth <= gmem_bandwidth;
         render_mode mode = select_sysmem ? render_mode::SYSMEM : render_mode::GMEM;

         UNUSED const VkExtent2D &extent = cmd_state->render_areas[0].extent;
         at_log_bandwidth_h(
            "%" PRIu32 " selecting %s\n"
            "   mean_samples=%" PRIu64 ", draw_bandwidth_per_sample=%.2f, total_draw_call_bandwidth=%" PRIu64
            ", render_areas[0]=%" PRIu32 "x%" PRIu32 ", sysmem_bandwidth_per_pixel=%" PRIu32
            ", gmem_bandwidth_per_pixel=%" PRIu32 ", sysmem_bandwidth=%" PRIu64 ", gmem_bandwidth=%" PRIu64,
            history.hash, rp_state->drawcall_count, render_mode_str(mode), mean_samples,
            (float) rp_state->drawcall_bandwidth_per_sample_sum / rp_state->drawcall_count, total_draw_call_bandwidth,
            extent.width, extent.height, pass->sysmem_bandwidth_per_pixel, pass->gmem_bandwidth_per_pixel,
            sysmem_bandwidth, gmem_bandwidth);

         return mode;
      }
   } bandwidth;

   /** Profiled Algorithms **/
   struct profiled_algo {
    private:
      /* Range [0 (GMEM), 100 (SYSMEM)], where 50 means no preference. */
      constexpr static uint32_t PROBABILITY_MAX = 100, PROBABILITY_MID = 50;
      constexpr static uint32_t PROBABILITY_PREFER_SYSMEM = 80, PROBABILITY_PREFER_GMEM = 20;

      std::atomic<uint32_t> sysmem_probability = PROBABILITY_MID;
      bool should_reset = false; /* If true, will reset sysmem_probability before next update. */
      bool locked = false;       /* If true, the probability will no longer be updated. */
      uint64_t seed[2] { 0x3bffb83978e24f88, 0x9238d5d56c71cd35 };

      bool is_sysmem_winning = false;
      uint64_t winning_since_ts = 0;

    public:
      profiled_algo(uint64_t hash)
      {
         seed[1] = hash;
      }

      void update(rp_history &history, bool immediate)
      {
         if (locked)
            return;

         auto &sysmem_ema = history.sysmem_rp_average;
         auto &gmem_ema = history.gmem_rp_average;
         uint32_t sysmem_prob = sysmem_probability.load(std::memory_order_relaxed);
         if (immediate) {
            /* Try to immediately resolve the probability, this is useful for CI running a single trace of frames where
             * the probabilites aren't expected to change from run to run. This environment also gives us a best case
             * scenario for autotune performance, since we know the optimal decisions.
             */

            if (sysmem_ema.count < 1) {
               sysmem_prob = PROBABILITY_MAX;
            } else if (gmem_ema.count < 1) {
               sysmem_prob = 0;
            } else {
               sysmem_prob = gmem_ema.get() < sysmem_ema.get() ? 0 : PROBABILITY_MAX;
               locked = true;
            }
         } else {
            if (sysmem_ema.count < MIN_PROFILE_DURATION_COUNT || gmem_ema.count < MIN_PROFILE_DURATION_COUNT) {
               /* Not enough data to make a decision, bias towards least used. */
               sysmem_prob = sysmem_ema.count < gmem_ema.count ? PROBABILITY_PREFER_SYSMEM : PROBABILITY_PREFER_GMEM;
               should_reset = true;
            } else {
               if (should_reset) {
                  sysmem_prob = PROBABILITY_MID;
                  should_reset = false;
               }

               /* Adjust probability based on timing results. */
               constexpr uint32_t FAST_STEP_DELTA = 5, FAST_MIN_PROBABILITY = 5, FAST_MAX_PROBABILITY = 95;
               constexpr uint32_t SLOW_STEP_DELTA = 1, SLOW_MIN_PROBABILITY = 1, SLOW_MAX_PROBABILITY = 99;

               uint64_t avg_sysmem = sysmem_ema.get();
               uint64_t avg_gmem = gmem_ema.get();

               if (avg_gmem < avg_sysmem) {
                  if (sysmem_prob > FAST_MIN_PROBABILITY && sysmem_prob <= FAST_MAX_PROBABILITY)
                     sysmem_prob = MAX2(sysmem_prob - FAST_STEP_DELTA, FAST_MIN_PROBABILITY);
                  else if (sysmem_prob > SLOW_MIN_PROBABILITY)
                     sysmem_prob = MAX2(sysmem_prob - SLOW_STEP_DELTA, SLOW_MIN_PROBABILITY);
               } else if (avg_sysmem < avg_gmem) {
                  if (sysmem_prob >= FAST_MIN_PROBABILITY && sysmem_prob < FAST_MAX_PROBABILITY)
                     sysmem_prob = MIN2(sysmem_prob + FAST_STEP_DELTA, FAST_MAX_PROBABILITY);
                  else if (sysmem_prob < SLOW_MAX_PROBABILITY)
                     sysmem_prob = MIN2(sysmem_prob + SLOW_STEP_DELTA, SLOW_MAX_PROBABILITY);
               }

               /* If the RP duration exceeds a certain minimum duration threshold (i.e. has a large impact on frametime)
                * and the percentage difference between the modes is large enough, we lock into the optimal mode. This
                * avoids performance hazards from switching to an extremely suboptimal mode even if done very rarely.
                * Note: Due to the potentially huge negative impact of a bad lock, this is a very conservative check.
                */
               constexpr uint32_t MIN_LOCK_DURATION_COUNT = 15;
               constexpr uint64_t MIN_LOCK_THRESHOLD = GPU_TICKS_PER_US * 1'000; /* 1ms */
               constexpr uint32_t LOCK_PERCENT_DIFF = 30;
               constexpr uint64_t LOCK_TIME_WINDOW_NS = 30'000'000'000; /* 30s */

               uint64_t now = os_time_get_nano();
               bool current_sysmem_winning = avg_sysmem < avg_gmem;

               if (winning_since_ts == 0 || current_sysmem_winning != is_sysmem_winning) {
                  winning_since_ts = now;
                  is_sysmem_winning = current_sysmem_winning;
               }

               bool has_resolved = sysmem_prob == SLOW_MAX_PROBABILITY || sysmem_prob == SLOW_MIN_PROBABILITY;
               bool enough_samples =
                  sysmem_ema.count >= MIN_LOCK_DURATION_COUNT && gmem_ema.count >= MIN_LOCK_DURATION_COUNT;
               uint64_t min_avg = MIN2(avg_sysmem, avg_gmem);
               uint64_t max_avg = MAX2(avg_sysmem, avg_gmem);
               uint64_t percent_diff = (100 * (max_avg - min_avg)) / min_avg;

               if (has_resolved && enough_samples && max_avg >= MIN_LOCK_THRESHOLD &&
                   percent_diff >= LOCK_PERCENT_DIFF && (now - winning_since_ts) >= LOCK_TIME_WINDOW_NS) {
                  if (avg_gmem < avg_sysmem)
                     sysmem_prob = 0;
                  else
                     sysmem_prob = 100;
                  locked = true;
               }
            }
         }

         sysmem_probability.store(sysmem_prob, std::memory_order_relaxed);

         at_log_profiled_h("update%s avg_gmem: %" PRIu64 " us (%" PRIu64 " samples) avg_sysmem: %" PRIu64
                           " us (%" PRIu64 " samples) = sysmem_probability: %" PRIu32 " locked: %u",
                           history.hash, immediate ? "-imm" : "", ticks_to_us(gmem_ema.get()), gmem_ema.count,
                           ticks_to_us(sysmem_ema.get()), sysmem_ema.count, sysmem_prob, locked);
      }

    public:
      render_mode get_optimal_mode(rp_history &history)
      {
         uint32_t l_sysmem_probability = sysmem_probability.load(std::memory_order_relaxed);
         bool select_sysmem = (rand_xorshift128plus(seed) % PROBABILITY_MAX) < l_sysmem_probability;
         render_mode mode = select_sysmem ? render_mode::SYSMEM : render_mode::GMEM;

         at_log_profiled_h("%" PRIu32 "%% sysmem chance, using %s", history.hash, l_sysmem_probability,
                           render_mode_str(mode));

         return mode;
      }
   } profiled;

   /** Preemption Latency Optimization Mode **/
   struct preempt_optimize_mode {
    private:
      adaptive_average<uint64_t> gmem_tile_average;

      /* If the renderpass has long draws which are at risk of causing high preemptible latency. */
      std::atomic<bool> latency_risk = false;
      /* The factor by which the tile size should be divided to reduce preemption latency. */
      std::atomic<uint32_t> tile_size_divisor = 1;

      /* The next timestamp to update the latency sensitivity parameters at. */
      uint64_t latency_update_ts = 0;
      /* The next timestamp where it's allowed to decrement the divisor. */
      uint64_t divisor_decrement_ts = 0;
      /* The next timestamp where it's allowed to mark the RP as no longer latency sensitive. */
      uint64_t latency_switch_ts = 0;

      /* Threshold of longest non-preemptible duration before activating latency optimization: 1.5ms */
      static constexpr uint64_t TARGET_THRESHOLD = GPU_TICKS_PER_US * 1500;

    public:
      void update_gmem(rp_history &history, uint64_t tile_duration)
      {
         constexpr uint64_t default_update_duration_ns = 100'000'000;         /* 100ms */
         constexpr uint64_t change_update_duration_ns = 500'000'000;          /* 500ms */
         constexpr uint64_t downward_update_duration_ns = 10'000'000'000;     /* 10s */
         constexpr uint64_t latency_insensitive_duration_ns = 30'000'000'000; /* 30s */

         gmem_tile_average.add(tile_duration);

         uint64_t now = os_time_get_nano();
         if (latency_update_ts > now)
            return; /* No need to update yet. */

         /* If the RP is latency sensitive and we're using GMEM, we should check if it's worth reducing the tile size to
          * reduce the latency risk further or if it's already low enough that it's not worth the performance hit.
          */

         uint64_t update_duration_ns = default_update_duration_ns;
         if (gmem_tile_average.count > MIN_PROFILE_DURATION_COUNT) {
            uint64_t avg_gmem_tile = gmem_tile_average.get();
            bool l_latency_risk = latency_risk.load(std::memory_order_relaxed);
            if (!l_latency_risk) {
               if (avg_gmem_tile > TARGET_THRESHOLD) {
                  latency_risk.store(true, std::memory_order_relaxed);
                  latency_switch_ts = now + latency_insensitive_duration_ns;

                  at_log_preempt_h("high gmem tile duration %" PRIu64 ", marking as latency sensitive", history.hash,
                                   avg_gmem_tile);
               }
            } else {
               uint32_t l_tile_size_divisor = tile_size_divisor.load(std::memory_order_relaxed);
               at_log_preempt_h("avg_gmem_tile: %" PRIu64 " us (%u), latency_risk: %u, tile_size_divisor: %" PRIu32,
                                history.hash, ticks_to_us(avg_gmem_tile), avg_gmem_tile > TARGET_THRESHOLD,
                                l_latency_risk, l_tile_size_divisor);

               int delta = 0;
               if (avg_gmem_tile > TARGET_THRESHOLD && l_tile_size_divisor < TU_GMEM_LAYOUT_DIVISOR_MAX) {
                  /* If the average tile duration is high, we should reduce the tile size to reduce the latency risk. */
                  delta = 1;

                  divisor_decrement_ts = now + downward_update_duration_ns;
               } else if (avg_gmem_tile * 4 < TARGET_THRESHOLD && l_tile_size_divisor > 1 &&
                          divisor_decrement_ts <= now) {
                  /* If the average tile duration is low enough that we can get away with a larger tile size, we should
                   * increase the tile size to reduce the performance hit of the smaller tiles.
                   *
                   * Note: The 4x factor is to account for the tile duration being halved when we increase the tile size
                   * divisor by 1, with an additional 2x factor to generally be conservative about reducing the divisor
                   * since it can lead to oscillation between tile sizes.
                   *
                   * Similarly, divisor_decrement_ts is used to limit how often we can reduce the divisor to avoid
                   * oscillation.
                   */
                  delta = -1;
                  latency_switch_ts = now + latency_insensitive_duration_ns;
               } else if (avg_gmem_tile * 10 < TARGET_THRESHOLD && l_tile_size_divisor == 1 &&
                          latency_switch_ts <= now) {
                  /* If the average tile duration is low enough that we no longer consider the RP latency sensitive, we
                   * can switch it back to non-latency sensitive.
                   */
                  latency_risk.store(false, std::memory_order_relaxed);
               }

               if (delta != 0) {
                  /* Clear all the results to avoid biasing the decision based on the old tile size. */
                  gmem_tile_average.clear();

                  uint32_t new_tile_size_divisor = l_tile_size_divisor + delta;
                  at_log_preempt_h("updating tile size divisor: %" PRIu32 " -> %" PRIu32, history.hash,
                                   l_tile_size_divisor, new_tile_size_divisor);

                  tile_size_divisor.store(new_tile_size_divisor, std::memory_order_relaxed);

                  update_duration_ns = change_update_duration_ns;
               }
            }

            latency_update_ts = now + update_duration_ns;
         }
      }

      /* If this RP has a risk of causing high preemption latency. */
      bool is_latency_sensitive() const
      {
         return latency_risk.load(std::memory_order_relaxed);
      }

      void mark_as_latency_sensitive()
      {
         latency_risk.store(true, std::memory_order_relaxed);
      }

      uint32_t get_tile_size_divisor() const
      {
         return tile_size_divisor.load(std::memory_order_relaxed);
      }
   } preempt_optimize;

   void process(rp_entry &entry, tu_autotune &at)
   {
      /* We use entry config to know what metrics it has, autotune config to know what algorithms are enabled. */
      config_t entry_config = entry.config;
      config_t at_config = at.active_config.load();

      if (entry_config.test(metric_flag::SAMPLES) && at_config.is_enabled(algorithm::BANDWIDTH))
         bandwidth.update(entry.get_samples_passed());
      if (entry_config.test(metric_flag::TS)) {
         if (entry.sysmem) {
            uint64_t rp_duration = entry.get_rp_duration();

            sysmem_rp_average.add(rp_duration);
         } else {
            gmem_rp_average.add(entry.get_rp_duration());

            if (entry_config.test(metric_flag::TS_TILE) && at_config.test(mod_flag::PREEMPT_OPTIMIZE))
               preempt_optimize.update_gmem(*this, entry.get_max_tile_duration());
         }

         if (at_config.is_enabled(algorithm::PROFILED) || at_config.is_enabled(algorithm::PROFILED_IMM)) {
            profiled.update(*this, at_config.is_enabled(algorithm::PROFILED_IMM));
         }
      }
   }
};

tu_autotune::rp_history_handle::~rp_history_handle()
{
   if (!history)
      return;

   history->last_use_ts.store(os_time_get_nano(), std::memory_order_relaxed);
   ASSERTED uint32_t old_refcount = history->refcount.fetch_sub(1, std::memory_order_relaxed);
   assert(old_refcount != 0); /* Underflow check. */
}

tu_autotune::rp_history_handle::rp_history_handle(rp_history &history): history(&history)
{
   history.refcount.fetch_add(1, std::memory_order_relaxed);
   history.last_use_ts.store(os_time_get_nano(), std::memory_order_relaxed);
}

tu_autotune::rp_history_handle
tu_autotune::find_rp_history(const rp_key &key)
{
   std::shared_lock lock(rp_mutex);
   auto it = rp_histories.find(key);
   if (it != rp_histories.end())
      return rp_history_handle(it->second);

   return rp_history_handle(nullptr);
}

tu_autotune::rp_history_handle
tu_autotune::find_or_create_rp_history(const rp_key &key)
{
   rp_history *existing = find_rp_history(key);
   if (existing)
      return *existing;

   /* If we reach here, we have to create a new history. */
   std::unique_lock lock(rp_mutex);
   auto it = rp_histories.find(key);
   if (it != rp_histories.end())
      return it->second; /* Another thread created the history while we were waiting for the lock. */
   auto history = rp_histories.emplace(std::make_pair(key, key.hash));
   return rp_history_handle(history.first->second);
}

void
tu_autotune::reap_old_rp_histories()
{
   constexpr uint64_t REAP_INTERVAL_NS = 10'000'000'000; /* 10s */
   uint64_t now = os_time_get_nano();
   if (last_reap_ts + REAP_INTERVAL_NS > now)
      return;
   last_reap_ts = now;

   constexpr size_t MAX_RP_HISTORIES = 1024; /* Not a hard limit, we might exceed this if there's many active RPs. */
   {
      /* Quicker non-unique lock, should hit this path mostly. */
      std::shared_lock lock(rp_mutex);
      if (rp_histories.size() <= MAX_RP_HISTORIES)
         return;
   }

   std::unique_lock lock(rp_mutex);
   size_t og_size = rp_histories.size();
   if (og_size <= MAX_RP_HISTORIES)
      return;

   std::vector<rp_histories_t::iterator> candidates;
   candidates.reserve(og_size);
   for (auto it = rp_histories.begin(); it != rp_histories.end(); ++it) {
      if (it->second.refcount.load(std::memory_order_relaxed) == 0)
         candidates.push_back(it);
   }

   size_t to_purge = std::min(candidates.size(), og_size - MAX_RP_HISTORIES);
   if (to_purge == 0) {
      at_log_base("no RP histories to reap at size %zu, all are active", og_size);
      return;
   }

   /* Partition candidates by last use timestamp, oldest first. */
   auto partition_end = candidates.begin() + to_purge;
   if (to_purge < candidates.size()) {
      std::nth_element(candidates.begin(), partition_end, candidates.end(),
                       [](rp_histories_t::iterator a, rp_histories_t::iterator b) {
                          return a->second.last_use_ts.load(std::memory_order_relaxed) <
                                 b->second.last_use_ts.load(std::memory_order_relaxed);
                       });
   }

   for (auto it = candidates.begin(); it != partition_end; ++it) {
      rp_history &history = (*it)->second;
      if (history.refcount.load(std::memory_order_relaxed) == 0) {
         at_log_base("reaping RP history %016" PRIx64, history.hash);
         rp_histories.erase(*it);
      }
   }

   at_log_base("reaped old RP histories %zu -> %zu", og_size, rp_histories.size());
}

std::shared_ptr<tu_autotune::rp_entry_batch>
tu_autotune::create_batch() const
{
   return std::make_shared<rp_entry_batch>(device, active_config.load().test(mod_flag::PREEMPT_OPTIMIZE));
}

bool
tu_autotune::supports_preempt_latency_tracking() const
{
   return supported_mod_flags & (uint32_t) mod_flag::PREEMPT_OPTIMIZE;
}

void
tu_autotune::cleanup_latency_tracking()
{
   constexpr uint64_t CLEANUP_INTERVAL_NS = 10'000'000'000;         /* 10s */
   constexpr uint64_t FORGET_ABOUT_DELAY_AFTER_NS = 20'000'000'000; /* 20s */

   if (!active_config.load().test(mod_flag::PREEMPT_OPTIMIZE))
      return;

   uint64_t now = os_time_get_nano();
   if (last_latency_cleanup_ts + CLEANUP_INTERVAL_NS > now)
      return;
   last_latency_cleanup_ts = now;

   std::scoped_lock delay_lock(rp_latency_mutex);
   std::shared_lock history_lock(rp_mutex);

   for (auto it = rp_latency_tracking.begin(); it != rp_latency_tracking.end();) {
      auto &tracker = it->second;
      uint64_t rp_hash = it->first;

      /* Check if corresponding rp_history has cleared its latency_risk flag. */
      if (tracker.info.seen_latency_spike) {
         auto history_it = rp_histories.find(rp_key { rp_hash });
         if (history_it == rp_histories.end() || !history_it->second.preempt_optimize.is_latency_sensitive() ||
             history_it->second.last_use_ts.load(std::memory_order_relaxed) + FORGET_ABOUT_DELAY_AFTER_NS < now) {
            tracker.info.seen_latency_spike = false;
            at_log_preempt_h("clearing latency-sensitive flag", rp_hash);
         }
      }

      /* Remove tracking entry if no recent latency events and flag is cleared. */
      if (tracker.recent_latency_timestamps_ns.empty() && !tracker.info.seen_latency_spike) {
         it = rp_latency_tracking.erase(it);
      } else {
         ++it;
      }
   }
}

tu_autotune::rp_latency_info
tu_autotune::get_rp_latency_info(uint64_t rp_hash, bool unmark_sensitive)
{
   std::scoped_lock lock(rp_latency_mutex);
   auto it = rp_latency_tracking.find(rp_hash);
   if (it == rp_latency_tracking.end())
      return {};

   rp_latency_info info = it->second.info;
   if (unmark_sensitive)
      it->second.info.mark_rp_as_sensitive = false;
   return info;
}

void
tu_autotune::process_entries()
{
   uint32_t current_fence = device->global_bo_map->autotune_fence;

   while (!active_batches.empty()) {
      auto &batch = active_batches.front();
      assert(batch->active);

      if (fence_before(current_fence, batch->fence))
         break; /* Entries are allocated in sequence, next will be newer and
                   also fail so we can just directly break out of the loop. */

      process_batch_preempt_data(*batch);

      for (auto &entry : batch->entries)
         entry->history->process(*entry, *this);

      batch->mark_inactive();
      active_batches.pop_front();
   }

   if (active_batches.size() > 10) {
      at_log_base("high amount of active batches: %zu, fence: %" PRIu32 " < %" PRIu32, active_batches.size(),
                  current_fence, active_batches.front()->fence);
   }
}

void
tu_autotune::process_batch_preempt_data(rp_entry_batch &batch)
{
   constexpr uint64_t LATENCY_THRESHOLD_US = 1500;       /* 1.5ms */
   constexpr uint64_t LATENCY_WINDOW_NS = 2'000'000'000; /* 2s */

   if (!batch.preempt_latency.allocated)
      return;

   rp_batch_preempt_gpu_data gpu_data = batch.preempt_latency.get_gpu_data();
   if (gpu_data.preemption_latency == 0 || gpu_data.preemption_latency_rp_hash == 0 ||
       gpu_data.always_count_delta == 0 || gpu_data.aon_delta == 0)
      return;

   /* Convert preemption latency from CP clock ticks to microseconds.
    *
    * The always_count_delta and aon_delta represent the number of ticks that have passed in the CP and AON clock
    * domains, respectively, during the interval where counters were active. By using the CP-to-AON clock ratio, we can
    * convert the preemption latency from CP ticks to AON ticks (which runs at ALWAYS_ON_FREQUENCY_HZ), and then to wall
    * clock microseconds.
    *
    * Note: This clock ratio averages over the whole execution interval, rather than strictly when the preemption_latency
    *       counter was ticking, so it's not perfectly accurate, but it should be good enough for our purposes.
    */
   uint64_t delay_aon_ticks = (gpu_data.preemption_latency * gpu_data.aon_delta) / gpu_data.always_count_delta;
   uint64_t delay_us = delay_aon_ticks / GPU_TICKS_PER_US;

   if (delay_us < LATENCY_THRESHOLD_US)
      return;

   at_log_preempt_h("preemption latency spike detected: %" PRIu64 " us (always_count_delta: %" PRIu64
                    ", aon_delta: %" PRIu64 ", delay_aon_ticks: %" PRIu64 ", preemption_latency: %" PRIu64
                    ", estimated_cp_mhz: %" PRIu64 ")",
                    gpu_data.preemption_latency_rp_hash, delay_us, gpu_data.always_count_delta, gpu_data.aon_delta,
                    delay_aon_ticks, gpu_data.preemption_latency,
                    (gpu_data.always_count_delta * ALWAYS_ON_FREQUENCY_HZ) / gpu_data.aon_delta / 1'000'000);

   std::scoped_lock lock(rp_latency_mutex);
   uint64_t now = os_time_get_nano();

   auto &tracker = rp_latency_tracking[gpu_data.preemption_latency_rp_hash];
   tracker.recent_latency_timestamps_ns.push_back(now);

   /* Remove old timestamps outside the window. */
   tracker.recent_latency_timestamps_ns.erase(
      std::remove_if(tracker.recent_latency_timestamps_ns.begin(), tracker.recent_latency_timestamps_ns.end(),
                     [&](uint64_t ts) { return (now - ts) > LATENCY_WINDOW_NS; }),
      tracker.recent_latency_timestamps_ns.end());

   /* Mark as latency-sensitive if 2+ occurrences in window. */
   if (tracker.recent_latency_timestamps_ns.size() >= 2) {
      tracker.info.seen_latency_spike = true;
      tracker.info.mark_rp_as_sensitive = true;
      at_log_preempt_h("marking RP as latency-sensitive after %zu long preemption events",
                       gpu_data.preemption_latency_rp_hash, tracker.recent_latency_timestamps_ns.size());
   } else {
      tracker.info.seen_latency_spike = true;
      at_log_preempt_h("RP preemption latency spike seen, but not marking as sensitive yet at %zu events",
                       gpu_data.preemption_latency_rp_hash, tracker.recent_latency_timestamps_ns.size());
   }
}

struct tu_cs *
tu_autotune::on_submit(struct tu_cmd_buffer **cmd_buffers, uint32_t cmd_buffer_count)
{

   /* This call occurs regularly and we are single-threaded here, so we use this opportunity to process any available
    * entries. It's also important that any entries are processed here because we always want to ensure that we've
    * processed all entries from prior CBs before we submit any new CBs with the same RP to the GPU.
    */
   process_entries();
   cleanup_latency_tracking();
   reap_old_rp_histories();

   bool has_results = false;
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      auto &batch = cmd_buffers[i]->autotune_ctx.batch;
      if (batch->requires_processing()) {
         has_results = true;
         break;
      }
   }
   if (!has_results)
      return nullptr; /* No results to process, return early. */

   /* Generate a new fence and the CS for it. */
   const uint32_t new_fence = next_fence++;
   auto fence_cs = get_cs_for_fence(new_fence);
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      /* Transfer the entries from the command buffers to the active queue. */
      struct tu_cmd_buffer *cmdbuf = cmd_buffers[i];
      auto &batch = cmdbuf->autotune_ctx.batch;
      if (!batch->requires_processing())
         continue;

      batch->assign_fence(new_fence);
      if (cmdbuf->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) {
         /* If the command buffer is one-time submit, we can move the batch directly into the active batches, as it
          * won't be used again. This would lead to it being deallocated as early as possible.
          */
         active_batches.push_back(std::move(batch));
      } else {
         active_batches.push_back(batch);
      }
   }

   return fence_cs;
}

tu_autotune::tu_autotune(struct tu_device *device, VkResult &result)
    : device(device), supported_mod_flags(get_supported_mod_flags(device)), active_config(get_env_config())
{
   tu_bo_suballocator_init(&suballoc, device, 128 * 1024, TU_BO_ALLOC_INTERNAL_RESOURCE, "autotune_suballoc");

   if (supports_preempt_latency_tracking()) {
      uint32_t group_count;
      const struct fd_perfcntr_group *groups = fd_perfcntrs(&device->physical_device->dev_id, &group_count);
      const char *fail_reason = nullptr;

      const fd_perfcntr_group *cp_group = nullptr;
      for (uint32_t i = 0; i < group_count; i++) {
         if (strcmp(groups[i].name, "CP") == 0) {
            cp_group = &groups[i];
            break;
         }
      }

      if (cp_group) {
         auto get_perfcntr_countable = [](const struct fd_perfcntr_group *group,
                                          const char *name) -> const struct fd_perfcntr_countable * {
            for (uint32_t i = 0; i < group->num_countables; i++) {
               if (strcmp(group->countables[i].name, name) == 0)
                  return &group->countables[i];
            }

            return nullptr;
         };

         auto preemption_latency_countable = get_perfcntr_countable(cp_group, "PERF_CP_PREEMPTION_REACTION_DELAY");
         auto always_count_countable = get_perfcntr_countable(cp_group, "PERF_CP_ALWAYS_COUNT");
         if (preemption_latency_countable && always_count_countable) {
            if (cp_group->num_counters >= 2) {
               preemption_latency_selector_reg = cp_group->counters[0].select_reg;
               preemption_latency_selector = preemption_latency_countable->selector;
               preemption_latency_counter_reg_lo = cp_group->counters[0].counter_reg_lo;

               always_count_selector_reg = cp_group->counters[1].select_reg;
               always_count_selector = always_count_countable->selector;
               always_count_counter_reg_lo = cp_group->counters[1].counter_reg_lo;
            } else {
               fail_reason = "not enough counters in CP group for preemption latency tracking";
            }
         } else {
            fail_reason = "required countables not found in CP group";
         }
      } else {
         fail_reason = "CP counter group not found";
      }

      if (fail_reason) {
         if (TU_DEBUG(STARTUP) || active_config.load().test(mod_flag::PREEMPT_OPTIMIZE))
            mesa_logw("autotune: %s, preemption optimization not supported", fail_reason);

         supported_mod_flags &= ~((uint32_t) mod_flag::PREEMPT_OPTIMIZE);
         disable_preempt_optimize();
      }
   }

   result = VK_SUCCESS;
   return;
}

tu_autotune::~tu_autotune()
{
   if (TU_AUTOTUNE_FLUSH_AT_FINISH) {
      while (!active_batches.empty())
         process_entries();
      at_log_base("finished processing all entries");
   }

   tu_bo_suballocator_finish(&suballoc);
}

tu_autotune::cmd_buf_ctx::cmd_buf_ctx(struct tu_autotune &autotune): batch(autotune.create_batch())
{
}

tu_autotune::cmd_buf_ctx::~cmd_buf_ctx()
{
   /* This is empty but it causes the implicit destructor to be compiled within this compilation unit with access to
    * internal structures. Otherwise, we would need to expose the full definition of autotuner internals in the header
    * file, which is not desirable.
    */
}

bool
tu_autotune::cmd_buf_ctx::tracks_preempt_latency() const
{
   return batch->preempt_latency.allocated;
}

void
tu_autotune::cmd_buf_ctx::snapshot_preempt_data(struct tu_cs *cs)
{
   batch->snapshot_preempt_data(cs);
}

void
tu_autotune::cmd_buf_ctx::reset(struct tu_autotune &autotune)
{
   batch = autotune.create_batch();
}

tu_autotune::rp_entry *
tu_autotune::cmd_buf_ctx::attach_rp_entry(struct tu_device *device,
                                          rp_history_handle &&history,
                                          config_t config,
                                          uint32_t drawcall_count)
{
   std::unique_ptr<rp_entry> &new_entry =
      batch->entries.emplace_back(std::make_unique<rp_entry>(device, std::move(history), config, drawcall_count));
   return new_entry.get();
}

tu_autotune::rp_key
tu_autotune::cmd_buf_ctx::generate_rp_key(const struct tu_render_pass *pass,
                                          const struct tu_framebuffer *framebuffer,
                                          const struct tu_cmd_buffer *cmd,
                                          bool record_instance)
{
   rp_key key(pass, framebuffer, cmd);
   /* When nearly identical renderpasses appear multiple times within the same command buffer, we need to generate a
    * unique hash for each instance to distinguish them. While this approach doesn't address identical renderpasses
    * across different command buffers, it is good enough in most cases.
    */
   auto it = this->batch->all_renderpasses.find(key.hash);
   if (it != this->batch->all_renderpasses.end()) {
      key = rp_key(key, it->second);
      if (record_instance)
         it->second++;
   } else {
      if (record_instance)
         this->batch->all_renderpasses[key.hash] = 1;
   }

   return key;
}

tu_autotune::render_mode
tu_autotune::get_optimal_mode(struct tu_cmd_buffer *cmd_buffer, rp_ctx_t *rp_ctx, rp_key_opt key_opt)
{
   const struct tu_cmd_state *cmd_state = &cmd_buffer->state;
   const struct tu_render_pass *pass = cmd_state->pass;
   const struct tu_framebuffer *framebuffer = cmd_state->framebuffer;
   const struct tu_render_pass_state *rp_state = &cmd_state->rp;
   cmd_buf_ctx &cb_ctx = cmd_buffer->autotune_ctx;
   config_t config = active_config.load();

   /* Just to ensure a segfault for accesses, in case we don't set it. */
   *rp_ctx = nullptr;

   /* If a feedback loop in the subpass caused one of the pipelines used to set
    * SINGLE_PRIM_MODE(FLUSH_PER_OVERLAP_AND_OVERWRITE) or even SINGLE_PRIM_MODE(FLUSH), then that should cause
    * significantly increased SYSMEM bandwidth (though we haven't quantified it).
    */
   if (rp_state->sysmem_single_prim_mode)
      return render_mode::GMEM;

   /* If the user is using a fragment density map, then this will cause less FS invocations with GMEM, which has a
    * hard-to-measure impact on performance because it depends on how heavy the FS is in addition to how many
    * invocations there were and the density. Let's assume the user knows what they're doing when they added the map,
    * because if SYSMEM is actually faster then they could've just not used the fragment density map.
    */
   if (pass->has_fdm)
      return render_mode::GMEM;

   /* SYSMEM is always a safe default mode when we can't fully engage the autotuner. From testing, we know that for an
    * incorrect decision towards SYSMEM tends to be far less impactful than an incorrect decision towards GMEM, which
    * can cause significant performance issues.
    */
   constexpr render_mode default_mode = render_mode::SYSMEM;

   /* For VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT buffers, we would have to allocate GPU memory at the submit time
    * and copy results into it. We just disable complex autotuner in this case, which isn't a big issue since native
    * games usually don't use it, Zink and DXVK don't use it, while D3D12 doesn't even have such concept.
    *
    * We combine this with processing entries at submit time, to avoid a race where the CPU hasn't processed the results
    * from an earlier submission of the CB while a second submission of the CB is on the GPU queue.
    */
   bool simultaneous_use = cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

   std::optional<rp_latency_info> latency_info;
   if (key_opt && config.test(mod_flag::PREEMPT_OPTIMIZE))
      latency_info = get_rp_latency_info(key_opt->hash, true);

   /* These smaller RPs with few draws are too difficult to create a balanced hash for that can independently identify
    * them while not being so unique to not properly identify them across CBs. They're generally insigificant outside of
    * a few edge cases such as during deferred rendering G-buffer passes, as we don't have a good way to deal with those
    * edge cases yet, we just disable the autotuner for small RPs entirely for now unless TUNE_SMALL is specified.
    *
    * Note: If we detect a small RP to be latency sensitive, we enable the autotuner for it anyway.
    */
   bool ignore_small_rp = !config.test(mod_flag::TUNE_SMALL) && rp_state->drawcall_count < 5 &&
                          (!latency_info || !latency_info->seen_latency_spike);

   if (!enabled || simultaneous_use || ignore_small_rp)
      return default_mode;

   /* We can return early with the decision based on the draw call count, instead of needing to hash the renderpass
    * instance and look up the history, which is far more expensive.
    *
    * However, certain options such as latency sensitive mode take precedence over any of the other autotuner options
    * and we cannot do so in those cases.
    */
   bool can_early_return = !config.test(mod_flag::PREEMPT_OPTIMIZE);
   auto early_return_mode = [&]() -> std::optional<render_mode> {
      if ((config.test(mod_flag::BIG_GMEM) && rp_state->drawcall_count >= 10) ||
          config.is_enabled(algorithm::PREFER_GMEM))
         return render_mode::GMEM;
      if (config.is_enabled(algorithm::PREFER_SYSMEM))
         return render_mode::SYSMEM;
      return std::nullopt;
   }();

   if (can_early_return && early_return_mode) {
      at_log_base_h("%" PRIu32 " draw calls, using %s (early)",
                    key_opt ? key_opt->hash : rp_key(pass, framebuffer, cmd_buffer).hash, rp_state->drawcall_count,
                    render_mode_str(*early_return_mode));
      return *early_return_mode;
   }

   rp_key key(0);
   if (key_opt)
      key = *key_opt;
   else
      key = cb_ctx.generate_rp_key(pass, framebuffer, cmd_buffer);

   rp_history &history = *find_or_create_rp_history(key);
   if (config.test(mod_flag::PREEMPT_OPTIMIZE)) {
      if (!latency_info && !key_opt)
         latency_info = get_rp_latency_info(key.hash, true);
      assert(latency_info); /* Should always have it at this point. */

      if (!latency_info->seen_latency_spike) {
         /* If the RP isn't latency sensitive according to the latency tracking, disable the preemption optimization
          * to avoid unnecessary performance hit from the predictive latency sensitive heuristics for RPs that
          * haven't seen any real latency spikes.
          */
         at_log_base_h("no latency spike seen for RP, disabling preempt optimization", key.hash);
         config.disable(mod_flag::PREEMPT_OPTIMIZE);
      }

      if (latency_info->mark_rp_as_sensitive) {
         at_log_base_h("marking RP as latency sensitive based on latency tracking", key.hash);
         history.preempt_optimize.mark_as_latency_sensitive();
      }
   }
   *rp_ctx = cb_ctx.attach_rp_entry(device, history, config, rp_state->drawcall_count);

   if (config.test(mod_flag::PREEMPT_OPTIMIZE) && history.preempt_optimize.is_latency_sensitive()) {
      /* Try to mitigate the risk of high preemption latency by always using GMEM, which should break up any larger
       * draws into smaller ones with tiling.
       */
      at_log_base_h("high preemption latency risk, using GMEM", key.hash);
      return render_mode::GMEM;
   }

   if (early_return_mode) {
      at_log_base_h("%" PRIu32 " draw calls, using %s (late)", key.hash, rp_state->drawcall_count,
                    render_mode_str(*early_return_mode));
      return *early_return_mode;
   }

   if (config.is_enabled(algorithm::PROFILED) || config.is_enabled(algorithm::PROFILED_IMM))
      return history.profiled.get_optimal_mode(history);

   if (config.is_enabled(algorithm::BANDWIDTH))
      return history.bandwidth.get_optimal_mode(history, cmd_state, pass, framebuffer, rp_state);

   return default_mode;
}

uint32_t
tu_autotune::get_tile_size_divisor(struct tu_cmd_buffer *cmd_buffer)
{
   const struct tu_cmd_state *cmd_state = &cmd_buffer->state;
   const struct tu_render_pass *pass = cmd_state->pass;
   const struct tu_framebuffer *framebuffer = cmd_state->framebuffer;
   const struct tu_render_pass_state *rp_state = &cmd_state->rp;

   if (!enabled || !active_config.load().test(mod_flag::PREEMPT_OPTIMIZE) || rp_state->sysmem_single_prim_mode ||
       pass->has_fdm || cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)
      return 1;

   rp_key key = cmd_buffer->autotune_ctx.generate_rp_key(pass, framebuffer, cmd_buffer, false);

   rp_latency_info latency_info = get_rp_latency_info(key.hash, false);
   if (!latency_info.seen_latency_spike) {
      at_log_base_h("no RP latency spike seen, using tile_size_divisor=1", key.hash);
      return 1;
   }

   tu_autotune::rp_history_handle history = find_rp_history(key);
   if (!history) {
      at_log_base_h("no RP history found, using tile_size_divisor=1", key.hash);
      return 1;
   }

   uint32_t tile_size_divisor = history->preempt_optimize.get_tile_size_divisor();

   return tile_size_divisor;
}

void
tu_autotune::disable_preempt_optimize()
{
   config_t original, updated;
   do {
      original = updated = active_config.load();
      if (!original.test(mod_flag::PREEMPT_OPTIMIZE))
         return; /* Already disabled, nothing to do. */
      updated.disable(mod_flag::PREEMPT_OPTIMIZE);
   } while (!active_config.compare_and_store(original, updated));
}

void
tu_autotune::write_preempt_counters_to_iova(struct tu_cs *cs,
                                            bool emit_selector,
                                            bool emit_wfi,
                                            uint64_t latency_iova,
                                            uint64_t always_count_iova,
                                            uint64_t aon_iova) const
{
   if (emit_selector) {
      tu_cs_emit_pkt4(cs, preemption_latency_selector_reg, 1);
      tu_cs_emit(cs, preemption_latency_selector);

      tu_cs_emit_pkt4(cs, always_count_selector_reg, 1);
      tu_cs_emit(cs, always_count_selector);
   }

   if (emit_wfi)
      tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
   tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(preemption_latency_counter_reg_lo) | CP_REG_TO_MEM_0_64B);
   tu_cs_emit_qw(cs, latency_iova);

   tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
   tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(always_count_counter_reg_lo) | CP_REG_TO_MEM_0_64B);
   tu_cs_emit_qw(cs, always_count_iova);

   tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
   tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(TU_CALLX(device, __CP_ALWAYS_ON_COUNTER)({}).reg) | CP_REG_TO_MEM_0_CNT(2) |
                     CP_REG_TO_MEM_0_64B);
   tu_cs_emit_qw(cs, aon_iova);
}

/** RP-level CS emissions **/

void
tu_autotune::begin_renderpass(
   struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, bool sysmem, uint32_t tile_count)
{
   if (!rp_ctx)
      return;

   assert(sysmem || tile_count > 0);
   assert(!sysmem || tile_count == 0);

   rp_ctx->allocate(sysmem, tile_count);
   rp_ctx->emit_rp_start(cmd, cs);
}

void
tu_autotune::end_renderpass(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx)
{
   if (!rp_ctx)
      return;

   rp_ctx->emit_rp_end(cmd, cs);
}

/** Tile-level CS emissions **/

void
tu_autotune::begin_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, uint32_t tile_idx)
{
   if (!rp_ctx)
      return;

   rp_ctx->emit_tile_start(cmd, cs, tile_idx);
}

void
tu_autotune::end_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, uint32_t tile_idx)
{
   if (!rp_ctx)
      return;

   rp_ctx->emit_tile_end(cmd, cs, tile_idx);
}

/** Preemption Latency Tracking API **/

uint32_t
tu_autotune::get_switch_away_amble_size() const
{
   return supports_preempt_latency_tracking() ? 32 : 0;
}

uint32_t
tu_autotune::get_switch_back_amble_size() const
{
   return supports_preempt_latency_tracking() ? 128 : 0;
}

void
tu_autotune::emit_switch_away_amble(struct tu_cs *cs) const
{
   if (!supports_preempt_latency_tracking())
      return;

   const uint64_t mem = device->global_bo->iova;

   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) | CP_COND_REG_EXEC_0_BR);

   write_preempt_counters_to_iova(cs, false, false, mem + gb_offset(new_preemption_latency),
                                  mem + gb_offset(new_always_count), mem + gb_offset(new_aon));

   /* We need to account for accumulation of PERF_CP_PREEMPTION_REACTION_DELAY, so we always has the last preemption
    * latency stored to subtract. preemption_latency = new_preemption_latency - base_preemption_latency.
    */
   tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
   tu_cs_emit(cs, CP_MEM_TO_MEM_0_DOUBLE | CP_MEM_TO_MEM_0_NEG_B | CP_MEM_TO_MEM_0_WAIT_FOR_MEM_WRITES);
   tu_cs_emit_qw(cs, mem + gb_offset(preemption_latency));
   tu_cs_emit_qw(cs, mem + gb_offset(new_preemption_latency));
   tu_cs_emit_qw(cs, mem + gb_offset(base_preemption_latency));
   tu_cs_emit_qw(cs, mem + gb_offset(zero_64b));

   static size_t counter = 0;
   if (counter++ % 2 == 0) {
      tu_cs_emit_pkt4(cs, preemption_latency_selector_reg, 1);
      tu_cs_emit(cs, always_count_selector);

      tu_cs_emit_pkt4(cs, always_count_selector_reg, 1);
      tu_cs_emit(cs, preemption_latency_selector);
   }

   tu_cond_exec_end(cs);
}

void
tu_autotune::emit_switch_back_amble(struct tu_cs *cs) const
{
   if (!supports_preempt_latency_tracking())
      return;

   const uint64_t mem = device->global_bo->iova;

   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(THREAD_MODE) | CP_COND_REG_EXEC_0_BR);

   /* Update max_preemption_latency and max_preemption_latency_rp_hash if this preemption had a longer preemption delay
    * than the previous one in the current cmdbuffer.
    */
   {
      uint32_t scratch_reg = TU_CALLX(device, tu_scratch_reg)(5, 0).reg;

      /* scratch = max_preemption_latency - preemption_reaction_delay. */
      tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
      tu_cs_emit(cs, CP_MEM_TO_MEM_0_DOUBLE | CP_MEM_TO_MEM_0_NEG_B);
      tu_cs_emit_qw(cs, mem + gb_offset(preemption_latency_cmp_scratch));
      tu_cs_emit_qw(cs, mem + gb_offset(max_preemption_latency));
      tu_cs_emit_qw(cs, mem + gb_offset(preemption_latency));
      tu_cs_emit_qw(cs, mem + gb_offset(zero_64b));

      /* Wait for the mem_op to complete. */
      tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

      /* Load high 32 bits of difference into scratch register. */
      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(scratch_reg) | CP_MEM_TO_REG_0_CNT(1));
      tu_cs_emit_qw(cs, mem + gb_offset(preemption_latency_cmp_scratch) + sizeof(uint32_t));

      /* Test bit 31 (sign bit). */
      tu_cs_emit_pkt7(cs, CP_REG_TEST, 1);
      tu_cs_emit(cs, A6XX_CP_REG_TEST_0_REG(scratch_reg) | A6XX_CP_REG_TEST_0_BIT(31));

      /* If negative (preemption_reaction_delay > max_preemption_latency), update. */
      tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(PRED_TEST));
      {
         /* max_preemption_latency = preemption_reaction_delay .*/
         tu_cs_emit_pkt7(cs, CP_MEMCPY, 5);
         tu_cs_emit(cs, 2);
         tu_cs_emit_qw(cs, mem + gb_offset(preemption_latency));
         tu_cs_emit_qw(cs, mem + gb_offset(max_preemption_latency));

         /* max_preemption_latency_rp_hash = cur_rp_hash. */
         tu_cs_emit_pkt7(cs, CP_MEMCPY, 5);
         tu_cs_emit(cs, 2);
         tu_cs_emit_qw(cs, mem + gb_offset(cur_rp_hash));
         tu_cs_emit_qw(cs, mem + gb_offset(max_preemption_latency_rp_hash));

         /* max_always_count_delta = new_always_count - base_always_count. */
         tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
         tu_cs_emit(cs, CP_MEM_TO_MEM_0_DOUBLE | CP_MEM_TO_MEM_0_NEG_B);
         tu_cs_emit_qw(cs, mem + gb_offset(max_always_count_delta));
         tu_cs_emit_qw(cs, mem + gb_offset(new_always_count));
         tu_cs_emit_qw(cs, mem + gb_offset(base_always_count));
         tu_cs_emit_qw(cs, mem + gb_offset(zero_64b));

         /* max_aon_delta = new_aon - base_aon. */
         tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
         tu_cs_emit(cs, CP_MEM_TO_MEM_0_DOUBLE | CP_MEM_TO_MEM_0_NEG_B);
         tu_cs_emit_qw(cs, mem + gb_offset(max_aon_delta));
         tu_cs_emit_qw(cs, mem + gb_offset(new_aon));
         tu_cs_emit_qw(cs, mem + gb_offset(base_aon));
         tu_cs_emit_qw(cs, mem + gb_offset(zero_64b));

         /* Ensures that base_{always_count, aon} are read before the REG_TO_MEM. */
         tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
      }
      tu_cond_exec_end(cs);
   }

   /* We still need to re-emit the selectors since another context may have changed them.
    * Note: Emitting WFI in PREAMBLE_AMBLE_TYPE leads to a GPU hang for some reason, so we skip the WFI which seems to
    *       work even when selectors are intentionally scrambled at the end of switch_away_amble to simulate another
    *       context changing the selectors.
    */
   write_preempt_counters_to_iova(cs, true, false, mem + gb_offset(base_preemption_latency),
                                  mem + gb_offset(base_always_count), mem + gb_offset(base_aon));

   tu_cond_exec_end(cs);
}

void
tu_autotune::init_reset_rp_hash_draw_state()
{
   if (!supports_preempt_latency_tracking()) {
      memset(&reset_rp_hash_draw_state, 0, sizeof(reset_rp_hash_draw_state));
      return;
   }

   struct tu_cs cs;
   reset_rp_hash_draw_state = tu_cs_draw_state(&device->sub_cs, &cs, 5);

   tu_cs_emit_pkt7(&cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(&cs, device->global_bo->iova + gb_offset(cur_rp_hash));
   tu_cs_emit_qw(&cs, 0);
}

void
tu_autotune::emit_reset_rp_hash_draw_state(struct tu_cmd_buffer *cmd, struct tu_cs *cs) const
{
   if (!cmd->autotune_ctx.tracks_preempt_latency())
      return;

   assert(reset_rp_hash_draw_state.size != 0); /* init_reset_rp_hash_draw_state() not called. */

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(reset_rp_hash_draw_state.size) | CP_SET_DRAW_STATE__0_SYSMEM |
                     CP_SET_DRAW_STATE__0_GROUP_ID(TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM));
   tu_cs_emit_qw(cs, reset_rp_hash_draw_state.iova);
}

bool
tu_autotune::emit_preempt_latency_tracking_setup(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   if (!cmd->autotune_ctx.tracks_preempt_latency())
      return false;

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, global_iova(cmd, max_preemption_latency));
   tu_cs_emit_qw(cs, 0);

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, global_iova(cmd, max_preemption_latency_rp_hash));
   tu_cs_emit_qw(cs, ~0ull);

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, global_iova(cmd, max_always_count_delta));
   tu_cs_emit_qw(cs, 0);

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, global_iova(cmd, max_aon_delta));
   tu_cs_emit_qw(cs, 0);

   write_preempt_counters_to_iova(cs, true, true, global_iova(cmd, base_preemption_latency),
                                  global_iova(cmd, base_always_count), global_iova(cmd, base_aon));

   return true;
}

tu_autotune::rp_key_opt
tu_autotune::emit_preempt_latency_tracking_rp_hash(struct tu_cmd_buffer *cmd)
{
   if (!cmd->autotune_ctx.tracks_preempt_latency())
      return std::nullopt;

   tu_autotune::rp_key rp_key = cmd->autotune_ctx.generate_rp_key(cmd->state.pass, cmd->state.framebuffer, cmd);

   struct tu_cs cs;
   struct tu_draw_state ds = tu_cs_draw_state(&cmd->sub_cs, &cs, 5);

   tu_cs_emit_pkt7(&cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(&cs, global_iova(cmd, cur_rp_hash));
   tu_cs_emit_qw(&cs, rp_key.hash);

   tu_cs_emit_pkt7(&cmd->cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit_draw_state(&cmd->cs, TU_DRAW_STATE_AT_WRITE_RP_HASH, ds);

   return rp_key;
}