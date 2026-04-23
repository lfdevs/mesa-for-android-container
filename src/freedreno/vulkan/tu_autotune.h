/*
 * Copyright © 2021 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_AUTOTUNE_H
#define TU_AUTOTUNE_H

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "tu_cs.h"
#include "tu_suballoc.h"

/* Autotune allows for us to tune rendering parameters (such as GMEM vs SYSMEM, tile size divisor, etc.) based on
 * dynamic analysis of the rendering workload via on-GPU profiling. This lets us make much better decisions than static
 * analysis, since we can adapt to the actual workload rather than relying on heuristics.
 */
struct tu_autotune {
 private:
   bool enabled = true;
   struct tu_device *device;

   /** Configuration **/

   enum class algorithm : uint8_t;
   enum class mod_flag : uint8_t;
   enum class metric_flag : uint8_t;
   /* Container for all autotune configuration options. */
   struct PACKED config_t;
   union PACKED packed_config_t;

   uint32_t supported_mod_flags;

   /* Allows for thread-safe access to the configurations. */
   struct atomic_config_t {
    private:
      std::atomic<uint32_t> config_bits = 0;

    public:
      atomic_config_t(config_t initial_config);

      config_t load() const;

      bool compare_and_store(config_t expected, config_t updated);
   } active_config;

   config_t get_env_config();
   uint32_t get_supported_mod_flags(tu_device *device) const;

   /** Global Fence and Internal CS Management **/

   /* BO suballocator for reducing BO management for small GMEM/SYSMEM autotune result buffers.
    * Synchronized by suballoc_mutex.
    */
   struct tu_suballocator suballoc;
   std::mutex suballoc_mutex;

   /* The next value to assign to tu6_global::autotune_fence, this is incremented during on_submit. */
   uint32_t next_fence = 1;

   /* A wrapper around a CS which sets the global autotune fence to a certain fence value, this allows for ergonomically
    * managing the lifetime of the CS including recycling it after the fence value has been reached.
    */
   struct submission_entry {
    private:
      uint32_t fence;
      struct tu_cs fence_cs;

    public:
      explicit submission_entry(tu_device *device);

      ~submission_entry();

      /* Disable move/copy, since this holds stable pointers to the fence_cs. */
      submission_entry(const submission_entry &) = delete;
      submission_entry &operator=(const submission_entry &) = delete;
      submission_entry(submission_entry &&) = delete;
      submission_entry &operator=(submission_entry &&) = delete;

      /* The current state of the submission entry, this is used to track whether the CS is available for reuse, pending
       * GPU completion or currently being processed.
       */
      bool is_active() const;

      /* If the CS is free, returns the CS which will write out the specified fence value. Otherwise, returns nullptr. */
      struct tu_cs *try_get_cs(uint32_t new_fence);
   };

   /* Unified pool for submission CSes.
    * Note: This is a deque rather than a vector due to the lack of move semantics in the submission_entry.
    */
   std::deque<submission_entry> submission_entries;

   /* Returns a CS which will write out the specified fence value to the global BO's autotune fence. */
   struct tu_cs *get_cs_for_fence(uint32_t fence);

   /** RP Entry Management **/

   struct rp_gpu_data;
   struct tile_gpu_data;
   struct rp_batch_preempt_gpu_data;
   struct rp_entry;

   struct rp_batch_preempt_latency {
      struct tu_device *device;

      bool allocated;
      struct tu_suballoc_bo bo;
      uint8_t *map;

      rp_batch_preempt_latency(struct tu_device *device, bool allocate);
      ~rp_batch_preempt_latency();

      rp_batch_preempt_gpu_data get_gpu_data();
   };

   /* A wrapper over all entries associated with a single command buffer. */
   struct rp_entry_batch {
      bool active;    /* If the entry is ready to be processed, i.e. the entry is submitted to the GPU queue and has a
                         valid fence. */
      uint32_t fence; /* The fence value which is used to signal the completion of the CB submission. This is used to
                         determine when the entries can be processed. */
      std::vector<std::unique_ptr<rp_entry>> entries;
      std::unordered_map<uint64_t, uint32_t> all_renderpasses;

      rp_batch_preempt_latency preempt_latency;

      rp_entry_batch(struct tu_device *device, bool track_preempt_latency);

      /* Disable the copy/move to avoid performance hazards. */
      rp_entry_batch(const rp_entry_batch &) = delete;
      rp_entry_batch &operator=(const rp_entry_batch &) = delete;
      rp_entry_batch(rp_entry_batch &&) = delete;
      rp_entry_batch &operator=(rp_entry_batch &&) = delete;

      bool requires_processing() const;

      void assign_fence(uint32_t new_fence);

      void mark_inactive();

      void snapshot_preempt_data(struct tu_cs *cs);
   };

   std::shared_ptr<rp_entry_batch> create_batch() const;

   /* A deque of entry batches that are strongly ordered by the fence value that was written by the GPU, for efficient
    * iteration and to ensure that we process the entries in the same order they were submitted.
    */
   std::deque<std::shared_ptr<rp_entry_batch>> active_batches;

   /* Handles processing of entry batches that are pending to be processed.
    *
    * Note: This must be called regularly to process the entries that have been written by the GPU. We currently do this
    *       in the on_submit() method, which is called on every submit of a command buffer.
    */
   void process_entries();

   void process_batch_preempt_data(rp_entry_batch &batch);

   /** Renderpass State Tracking **/

   struct rp_history;
   struct rp_history_handle;

   /* A strongly typed key which generates a hash to uniquely identify a renderpass instance. This hash is expected to
    * be stable across runs, so it can be used to identify the same renderpass instance consistently.
    *
    * Note: We can potentially include the vector of data we extract from the parameters to generate the hash into
    *       rp_key, which would lead to true value-based equality rather than just hash-based equality which has a cost
    *       but avoids hash collisions causing issues.
    */
   struct rp_key {
      uint64_t hash;

      rp_key(const struct tu_render_pass *pass,
             const struct tu_framebuffer *framebuffer,
             const struct tu_cmd_buffer *cmd);

      /* Further salt the hash to distinguish between multiple instances of the same RP within a single command buffer. */
      rp_key(const rp_key &key, uint32_t duplicates);

      /* Constructor for hash-only lookup */
      explicit constexpr rp_key(uint64_t hash): hash(hash)
      {
      }

      /* Equality operator, used in unordered_map. */
      constexpr bool operator==(const rp_key &other) const noexcept
      {
         return hash == other.hash;
      }
   };

   /* A thin wrapper to satisfy C++'s Hash named requirement for rp_key.
    *
    * Note: This should *NEVER* be used to calculate the hash itself as it would lead to the hash being calculated
    *       multiple times, rather than being calculated once and reused when there's multiple successive lookups like
    *       with find_or_create_rp_history() and providing the hash to the rp_history constructor.
    */
   struct rp_hash {
      constexpr size_t operator()(const rp_key &key) const noexcept
      {
         /* Note: This will throw away the upper 32-bits on 32-bit architectures. */
         return static_cast<size_t>(key.hash);
      }
   };

   /* A map between the hash of an RP and the historical state of the RP. Synchronized by rp_mutex. */
   using rp_histories_t = std::unordered_map<rp_key, rp_history, rp_hash>;
   rp_histories_t rp_histories;
   std::shared_mutex rp_mutex;
   uint64_t last_reap_ts = 0;

   /* Note: These will internally lock rp_mutex internally, no need to lock it. */
   rp_history_handle find_rp_history(const rp_key &key);
   rp_history_handle find_or_create_rp_history(const rp_key &key);
   void reap_old_rp_histories();

   /** Preemption Latency Tracking **/

   struct rp_latency_info {
      bool seen_latency_spike = false;   /* If a preemption latency spike was seen recently. */
      bool mark_rp_as_sensitive = false; /* Marks RP as latency-sensitive in rp_history */
   };

   /* Tracks recent preemption latency occurrences for a specific RP hash */
   struct rp_latency_tracker {
      std::vector<uint64_t> recent_latency_timestamps_ns; /* Timestamps of recent latency events */
      rp_latency_info info;
   };

   /* Global map tracking RPs that have caused preemption latency */
   std::unordered_map<uint64_t, rp_latency_tracker> rp_latency_tracking;
   std::mutex rp_latency_mutex; /* Protects rp_latency_tracking */
   uint64_t last_latency_cleanup_ts = 0;

   uint32_t preemption_latency_selector_reg;
   uint32_t preemption_latency_selector;
   uint32_t preemption_latency_counter_reg_lo;

   uint32_t always_count_selector_reg;
   uint32_t always_count_selector;
   uint32_t always_count_counter_reg_lo;

   struct tu_draw_state reset_rp_hash_draw_state;

   bool supports_preempt_latency_tracking() const;
   void cleanup_latency_tracking();
   tu_autotune::rp_latency_info get_rp_latency_info(uint64_t rp_hash, bool unmark_sensitive);
   void write_preempt_counters_to_iova(struct tu_cs *cs,
                                       bool emit_selector,
                                       bool emit_wfi,
                                       uint64_t latency_iova,
                                       uint64_t always_count_iova,
                                       uint64_t aon_iova) const;

 public:
   tu_autotune(struct tu_device *device, VkResult &result);

   ~tu_autotune();

   /* Opaque pointer to internal structure with RP context that needs to be preserved across begin/end calls. */
   using rp_ctx_t = rp_entry *;

   /* An internal structure that needs to be held by tu_cmd_buffer to track the state of the autotuner for a given CB.
    *
    * Note: tu_cmd_buffer is only responsible for the lifetime of this object, all the access to the context state is
    *       done through tu_autotune.
    */
   struct cmd_buf_ctx {
    private:
      /* A batch of all entries from RPs within this CB. */
      std::shared_ptr<rp_entry_batch> batch;

      /* Creates a new RP entry attached to this CB. */
      rp_entry *
      attach_rp_entry(struct tu_device *device, rp_history_handle &&history, config_t config, uint32_t draw_count);

      bool tracks_preempt_latency() const;

      friend struct tu_autotune;

    public:
      cmd_buf_ctx(struct tu_autotune &autotune);
      ~cmd_buf_ctx();

      rp_key generate_rp_key(const struct tu_render_pass *pass,
                             const struct tu_framebuffer *framebuffer,
                             const struct tu_cmd_buffer *cmd,
                             bool record_instance = true);

      void snapshot_preempt_data(struct tu_cs *cs);

      /* Resets the internal context, should be called when tu_cmd_buffer state has been reset. */
      void reset(struct tu_autotune &autotune);
   };

   enum class render_mode {
      SYSMEM,
      GMEM,
   };

   /* Wrapper to expose rp_key for passing around publicly. */
   struct rp_key_opt : public std::optional<rp_key> {
      using std::optional<rp_key>::optional;
   };

   /* Note: For preemption latency tracking to work, key_opt from emit_preempt_latency_tracking_rp_hash() must be used. */
   render_mode get_optimal_mode(struct tu_cmd_buffer *cmd_buffer, rp_ctx_t *rp_ctx, rp_key_opt key_opt);

   /* Returns the optimal tile size divisor for the given CB state. */
   uint32_t get_tile_size_divisor(struct tu_cmd_buffer *cmd_buffer);

   /* Disables preemption latency optimization within the autotuner, this is used when high-priority queues are used to
    * ensure that the autotuner does not interfere with the high-priority queue's performance.
    *
    * Note: This should be called before any renderpass is started, otherwise it may lead to undefined behavior.
    */
   void disable_preempt_optimize();

   void
   begin_renderpass(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, bool sysmem, uint32_t tile_count);

   void end_renderpass(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx);

   void begin_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, uint32_t tile_idx);

   void end_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs, rp_ctx_t rp_ctx, uint32_t tile_idx);

   /* The submit-time hook for autotuner, this may return a CS (can be NULL) which must be amended for autotuner
    * tracking to function correctly.
    *
    * Note: This must be called from a single-threaded context. There should never be multiple threads calling this
    *       function at the same time.
    */
   struct tu_cs *on_submit(struct tu_cmd_buffer **cmd_buffers, uint32_t cmd_buffer_count);

   /** Preemption Latency Tracking API **/

   uint32_t get_switch_away_amble_size() const;
   uint32_t get_switch_back_amble_size() const;
   void emit_switch_away_amble(struct tu_cs *cs) const;
   void emit_switch_back_amble(struct tu_cs *cs) const;

   /* Note: MUST be called from a single-threaded context before emit_reset_rp_hash_draw_state(). */
   void init_reset_rp_hash_draw_state();
   void emit_reset_rp_hash_draw_state(struct tu_cmd_buffer *cmd, struct tu_cs *cs) const;

   /* Returns if preemption latency tracking is enabled for this CB. */
   bool emit_preempt_latency_tracking_setup(struct tu_cmd_buffer *cmd, struct tu_cs *cs);
   /* Returns the RP hash only when preemption latency tracking is enabled. */
   rp_key_opt emit_preempt_latency_tracking_rp_hash(struct tu_cmd_buffer *cmd);
};

#endif /* TU_AUTOTUNE_H */