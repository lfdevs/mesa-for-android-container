/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "fd_pps_driver.h"

#include <cstring>
#include <iostream>

#include <err.h>
#include "util/perf/u_perfetto.h"
#include <poll.h>

#include <xf86drm.h>

#include "common/freedreno_common.h"
#include "common/freedreno_dev_info.h"
#include "drm-uapi/msm_drm.h"
#include "drm/freedreno_drmif.h"
#include "drm/freedreno_ringbuffer.h"
#include "perfcntrs/freedreno_dt.h"
#include "perfcntrs/freedreno_perfcntr.h"
#include "util/hash_table.h"

#include "pps/pps.h"
#include "pps/pps_algorithm.h"

namespace pps
{

bool
FreedrenoDriver::is_dump_perfcnt_preemptible() const
{
   return false;
}

uint64_t
FreedrenoDriver::get_min_sampling_period_ns()
{
   return 100000;
}

/**
 * Generate an submit the cmdstream to configure the counter/countable
 * muxing
 */
void
FreedrenoDriver::configure_counters(bool reset, bool wait)
{
   struct fd_submit *submit = fd_submit_new(pipe);
   enum fd_ringbuffer_flags flags =
      (enum fd_ringbuffer_flags)(FD_RINGBUFFER_PRIMARY | FD_RINGBUFFER_GROWABLE);
   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(submit, 0x1000, flags);

   assert(io);  /* This is legacy path only */

   for (const auto &countable : countables)
      countable.configure(ring, reset);

   struct fd_fence *fence = fd_submit_flush(submit, -1, false);

   fd_fence_flush(fence);
   fd_fence_del(fence);

   fd_ringbuffer_del(ring);
   fd_submit_del(submit);

   if (wait)
      fd_pipe_wait(pipe, fence);
}

/**
 * Read the current counter values and record the time.
 */
void
FreedrenoDriver::collect_countables()
{
   assert(io);  /* This is legacy path only */

   last_dump_ts = gpu_timestamp();

   for (const auto &countable : countables)
      countable.collect();
}

int
FreedrenoDriver::configure_counters_stream()
{
   if (perfcntr_stream_fd >= 0) {
      close(perfcntr_stream_fd);
      perfcntr_stream_fd = -1;
   }

   unsigned sample_size = sizeof(uint64_t) * (2 + countables.size());
   unsigned bufsz = 2 * sample_size;
   unsigned bufsz_shift = ffs(util_next_power_of_two(bufsz)) - 1;

   struct drm_msm_perfcntr_group groups[num_perfcntrs];
   memset(groups, 0, sizeof(groups));

   struct drm_msm_perfcntr_config req = {
      .flags = MSM_PERFCNTR_STREAM,
      .groups = VOID2U64(groups),
      .period = sampling_period_ns_,
      .bufsz_shift = bufsz_shift,
      .group_stride = sizeof(struct drm_msm_perfcntr_group),
   };

   assert(req.period);

   for (const auto &countable : countables)
      countable.configure_stream(&req);

   /* Now that the groups are fully populated, resolve the sample indices: */
   for (const auto &countable : countables)
      countable.resolve_sample_idx(&req);

   int fd = drmIoctl(fd_device_fd(dev), DRM_IOCTL_MSM_PERFCNTR_CONFIG, &req);
   if (fd < 0)
      return fd;

   sample_buf = malloc(sample_size);

   perfcntr_stream_fd = fd;

   /* Unlike the legacy path, the kernel handles reconfiguring counters
    * after power collapse for us, so we won't need to configure the
    * stream again.  So cleanup allocated memory now:
    */
   for (unsigned i = 0; i < num_perfcntrs; i++) {
      if (!groups[i].countables)
         break;
      free(U642VOID(groups[i].countables));
   }

   return 0;
}

static bool
perfcntr_stream_ready(int perfcntr_stream_fd)
{
   struct pollfd pfd;

   pfd.fd = perfcntr_stream_fd;
   pfd.events = POLLIN;
   pfd.revents = 0;

   if (poll(&pfd, 1, 0) < 0)
      return false;

   if (!(pfd.revents & POLLIN))
      return false;

   return true;
}

static uint64_t
ticks_to_ns(uint64_t ticks)
{
   constexpr uint64_t ALWAYS_ON_FREQUENCY_HZ = 19200000;
   constexpr double GPU_TICKS_PER_NS = ALWAYS_ON_FREQUENCY_HZ / 1000000000.0;

   return ticks / GPU_TICKS_PER_NS;
}

bool
FreedrenoDriver::collect_countables_stream()
{
   unsigned nsamples = 0;
   bool discontinuity = false;

   assert(perfcntr_stream_fd >= 0);

   while (perfcntr_stream_ready(perfcntr_stream_fd)) {
      unsigned sample_size = sizeof(uint64_t) * (2 + countables.size());
      size_t sz = sample_size;
      void *ptr = sample_buf;

      while (sz > 0) {
         ssize_t ret = read(perfcntr_stream_fd, ptr, sz);

         if (ret < 0)
            ret = -errno;

         if (ret == -EINTR || ret == -EAGAIN)
            continue;

         if (ret < 0)
            errx(ret, "read failed");

         sz -= ret;
         ptr = static_cast<char *>(ptr) + ret;
      }

      uint64_t *buf = (uint64_t *)sample_buf;
      uint64_t ts = buf[0];
      uint32_t seqno = buf[1] & 0xffffffff;

      discontinuity = seqno == 0;

      /* Capture the timestamp from the *start* of the sampling period: */
      last_capture_ts = last_dump_ts;
      last_dump_ts = ts;

      auto elapsed_time_ns = ticks_to_ns(last_dump_ts - last_capture_ts);

      time = (float)elapsed_time_ns / 1000000000.0;

      /* advance past header: */
      buf += 2;

      for (const auto &countable : countables)
         countable.collect_stream(buf);

      nsamples++;
   }

   return (nsamples > 0) && !discontinuity;
}

bool
FreedrenoDriver::init_perfcnt()
{
   uint64_t val;

   if (dev)
      fd_device_del(dev);

   dev = fd_device_new(drm_device.fd);
   pipe = fd_pipe_new2(dev, FD_PIPE_3D, 0);
   dev_id = fd_pipe_dev_id(pipe);

   if (fd_pipe_get_param(pipe, FD_MAX_FREQ, &val)) {
      PERFETTO_FATAL("Could not get MAX_FREQ");
      return false;
   }
   max_freq = val;

   if (fd_pipe_get_param(pipe, FD_SUSPEND_COUNT, &val)) {
      PERFETTO_ILOG("Could not get SUSPEND_COUNT");
   } else {
      suspend_count = val;
      has_suspend_count = true;
   }

   perfcntrs = fd_perfcntrs(dev_id, &num_perfcntrs);
   if (num_perfcntrs == 0) {
      PERFETTO_FATAL("No hw counters available");
      return false;
   }

   assigned_counters.resize(num_perfcntrs);
   assigned_counters.assign(assigned_counters.size(), 0);

   info = fd_dev_info_raw(dev_id);

   switch (fd_dev_gen(dev_id)) {
   case 6:
      setup_a6xx_counters();
      break;
   case 7:
      setup_a7xx_counters();
      break;
   case 8:
      setup_a8xx_counters();
      break;
   default:
      PERFETTO_FATAL("Unsupported GPU: a%03u", fd_dev_gpu_id(dev_id));
      return false;
   }

   state.resize(next_countable_id);

   for (const auto &countable : countables)
      countable.resolve();

   if (!configure_counters_stream()) {
      close(perfcntr_stream_fd);
      perfcntr_stream_fd = -1;
      return true;
   }

   io = fd_dt_find_io();
   if (!io) {
      PERFETTO_FATAL("Could not map GPU I/O space");
      return false;
   }

   fd_pipe_set_param(pipe, FD_SYSPROF, 1);

   configure_counters(true, true);
   collect_countables();

   return true;
}

void
FreedrenoDriver::enable_counter(const uint32_t counter_id)
{
   enabled_counters.push_back(counters[counter_id]);
}

void
FreedrenoDriver::enable_all_counters()
{
   enabled_counters.reserve(counters.size());
   for (auto &counter : counters) {
      enabled_counters.push_back(counter);
   }
}

void
FreedrenoDriver::enable_perfcnt(const uint64_t sampling_period_ns)
{
   sampling_period_ns_ = sampling_period_ns;

   if (!io) {
      /* reconfigure counter stream: */
      configure_counters_stream();
      collect_countables_stream();
   }
}

bool
FreedrenoDriver::dump_perfcnt()
{
   /* Note, when using perfcntr stream instead of mmio basec counter
    * reads, we can skip this (since the seqno in the data read from
    * the stream will tell us if there is a discontinuity, and the
    * kernel will handle reconfiguring counters on resume)
    */
   if (has_suspend_count && io) {
      uint64_t val;

      fd_pipe_get_param(pipe, FD_SUSPEND_COUNT, &val);

      if (suspend_count != val) {
         PERFETTO_ILOG("Device had suspended!");

         suspend_count = val;

         configure_counters(true, true);
         collect_countables();

         /* We aren't going to have anything sensible by comparing
          * current values to values from prior to the suspend, so
          * just skip this sampling period.
          */
         return false;
      }
   }

   if (!io)
      return collect_countables_stream();

   auto last_ts = last_dump_ts;

   /* Capture the timestamp from the *start* of the sampling period: */
   last_capture_ts = last_dump_ts;

   collect_countables();

   auto elapsed_time_ns = ticks_to_ns(last_dump_ts - last_ts);

   time = (float)elapsed_time_ns / 1000000000.0;

   /* On older kernels that dont' support querying the suspend-
    * count, just send configuration cmdstream regularly to keep
    * the GPU alive and correctly configured for the countables
    * we want
    */
   if (!has_suspend_count) {
      configure_counters(false, false);
   }

   return true;
}

uint64_t FreedrenoDriver::next()
{
   auto ret = last_capture_ts;
   last_capture_ts = 0;
   return ret;
}

void
FreedrenoDriver::disable_perfcnt()
{
   if (perfcntr_stream_fd >= 0) {
      close(perfcntr_stream_fd);
      perfcntr_stream_fd = -1;
   }
}

/*
 * Countable
 */

FreedrenoDriver::Countable
FreedrenoDriver::countable(std::string group, std::string name)
{
   auto countable = Countable(this, group, name);
   countables.emplace_back(countable);
   return countable;
}

FreedrenoDriver::Countable::Countable(FreedrenoDriver *d, std::string group, std::string name)
   : id {d->next_countable_id++}, d {d}, group {group}, name {name}
{
}

/* Emit register writes on ring to configure counter/countable muxing: */
void
FreedrenoDriver::Countable::configure(struct fd_ringbuffer *ring, bool reset) const
{
   const struct fd_perfcntr_countable *countable = d->state[id].countable;
   const struct fd_perfcntr_counter   *counter   = d->state[id].counter;

   OUT_PKT7(ring, CP_WAIT_FOR_IDLE, 0);

   if (counter->enable && reset) {
      OUT_PKT4(ring, counter->enable, 1);
      OUT_RING(ring, 0);
   }

   if (counter->clear && reset) {
      OUT_PKT4(ring, counter->clear, 1);
      OUT_RING(ring, 1);

      OUT_PKT4(ring, counter->clear, 1);
      OUT_RING(ring, 0);
   }

   OUT_PKT4(ring, counter->select_reg, 1);
   OUT_RING(ring, countable->selector);

   if (counter->enable && reset) {
      OUT_PKT4(ring, counter->enable, 1);
      OUT_RING(ring, 1);
   }
}

void
FreedrenoDriver::Countable::configure_stream(struct drm_msm_perfcntr_config *req) const
{
   const struct fd_perfcntr_countable *countable = d->state[id].countable;
   struct drm_msm_perfcntr_group *groups =
      (struct drm_msm_perfcntr_group *)U642VOID(req->groups);

   /* Find group: */
   struct drm_msm_perfcntr_group *g = NULL;

   for (unsigned i = 0; i < req->nr_groups; i++) {
      if (!strcmp(groups[i].group_name, group.c_str())) {
         g = &groups[i];
         break;
      }
   }

   /* If not found, append a new group: */
   if (!g) {
      g = &groups[req->nr_groups++];
      strcpy(g->group_name, group.c_str());

      /* allocate countables for max # of counters in the group */
      for (unsigned i = 0; i < d->num_perfcntrs; i++) {
         if (!strcmp(d->perfcntrs[i].name, group.c_str())) {
            void *countables = calloc(sizeof(uint32_t), d->perfcntrs[i].num_counters);
            g->countables = VOID2U64(countables);
            break;
         }
      }

      assert(g->countables);
   }

   /* Initially, just store the index within the group, since earlier groups
    * are not yet fully populated (ie. we don't yet know the offset of the
    * first sample in the group)
    */
   d->state[id].idx = g->nr_countables;

   /* And last, append the countable: */
   uint32_t *countables = (uint32_t *)U642VOID(g->countables);
   countables[g->nr_countables++] = countable->selector;
}

static unsigned
find_group_offset(const struct drm_msm_perfcntr_config *req, const char *group)
{
   struct drm_msm_perfcntr_group *groups =
      (struct drm_msm_perfcntr_group *)U642VOID(req->groups);
   unsigned off = 0;

   for (unsigned i = 0; i < req->nr_groups; i++) {
      if (!strcmp(groups[i].group_name, group))
         break;
      off += groups[i].nr_countables;
   }

   return off;
}

void
FreedrenoDriver::Countable::resolve_sample_idx(const struct drm_msm_perfcntr_config *req) const
{
   d->state[id].idx += find_group_offset(req, group.c_str());
}

void
FreedrenoDriver::Countable::collect_stream(const uint64_t *buf) const
{
   d->state[id].last_value = d->state[id].value;
   d->state[id].value = buf[d->state[id].idx];
}

/* Collect current counter value and calculate delta since last sample: */
void
FreedrenoDriver::Countable::collect() const
{
   const struct fd_perfcntr_counter *counter = d->state[id].counter;

   d->state[id].last_value = d->state[id].value;

   /* this is true on a5xx and later */
   assert(counter->counter_reg_lo + 1 == counter->counter_reg_hi);
   uint64_t *reg = (uint64_t *)((uint32_t *)d->io + counter->counter_reg_lo);

   d->state[id].value = *reg;
}

/* Resolve the countable and assign the next counter from its group. */
void
FreedrenoDriver::Countable::resolve() const
{
   for (unsigned i = 0; i < d->num_perfcntrs; i++) {
      const struct fd_perfcntr_group *g = &d->perfcntrs[i];
      if (group != g->name)
         continue;

      const struct fd_perfcntr_countable *c =
         fd_perfcntrs_countable(g, name.c_str());

      if (c) {
         d->state[id].countable = c;

         /* Assign counters from high to low to reduce conflicts with UMD-owned
          * slots. */
         assert(d->assigned_counters[i] < g->num_counters);
         unsigned counter_index =
            (g->num_counters - 1) - d->assigned_counters[i]++;
         d->state[id].counter = &g->counters[counter_index];

         std::cout << "Countable: " << name << ", group=" << g->name
                   << ", counter=" << counter_index << "\n";

         return;
      }
   }
   UNREACHABLE("no such countable!");
}

uint64_t
FreedrenoDriver::Countable::get_value() const
{
   return d->state[id].value - d->state[id].last_value;
}

/*
 * DerivedCounter
 */

FreedrenoDriver::DerivedCounter::DerivedCounter(FreedrenoDriver *d, std::string name,
                                                Counter::Units units,
                                                std::function<int64_t()> derive)
   : Counter(d->next_counter_id++, name, 0)
{
   std::cout << "DerivedCounter: " << name << ", id=" << id << "\n";
   this->units = units;
   set_getter([=](const Counter &c, const Driver &d) {
         return derive();
      }
   );
}

FreedrenoDriver::DerivedCounter
FreedrenoDriver::counter(std::string name, Counter::Units units,
                         std::function<int64_t()> derive)
{
   auto counter = DerivedCounter(this, name, units, derive);
   counters.emplace_back(counter);
   return counter;
}

uint32_t
FreedrenoDriver::gpu_clock_id() const
{
   static uint32_t gpu_clock_id;

   if (!gpu_clock_id) {
      /* Note: clock_id's below 128 are reserved.. for custom clock sources,
       * using the hash of a namespaced string is the recommended approach.
       * See: https://perfetto.dev/docs/concepts/clock-sync
       */
      gpu_clock_id =
         _mesa_hash_string("org.freedesktop.pps.freedreno") | 0x80000000;
   }

   return gpu_clock_id;
}

uint64_t
FreedrenoDriver::gpu_timestamp() const
{
   uint64_t ts;
   fd_pipe_get_param(pipe, FD_TIMESTAMP, &ts);
   return ts;
}

bool
FreedrenoDriver::cpu_gpu_timestamp(uint64_t &, uint64_t &) const
{
   /* Not supported */
   return false;
}

} // namespace pps
