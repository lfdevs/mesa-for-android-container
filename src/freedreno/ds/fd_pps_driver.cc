/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "fd_pps_driver.h"

#include <cstring>
#include <iostream>
#include <perfetto.h>

#include "common/freedreno_dev_info.h"
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
   last_dump_ts = gpu_timestamp();

   for (const auto &countable : countables)
      countable.collect();
}

static uint64_t
ticks_to_ns(uint64_t ticks)
{
   constexpr uint64_t ALWAYS_ON_FREQUENCY_HZ = 19200000;
   constexpr double GPU_TICKS_PER_NS = ALWAYS_ON_FREQUENCY_HZ / 1000000000.0;

   return ticks / GPU_TICKS_PER_NS;
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

   fd_pipe_set_param(pipe, FD_SYSPROF, 1);

   perfcntrs = fd_perfcntrs(fd_pipe_dev_id(pipe), &num_perfcntrs);
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
   default:
      PERFETTO_FATAL("Unsupported GPU: a%03u", fd_dev_gpu_id(dev_id));
      return false;
   }

   state.resize(next_countable_id);

   for (const auto &countable : countables)
      countable.resolve();

   io = fd_dt_find_io();
   if (!io) {
      PERFETTO_FATAL("Could not map GPU I/O space");
      return false;
   }

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
FreedrenoDriver::enable_perfcnt(const uint64_t /* sampling_period_ns */)
{
}

bool
FreedrenoDriver::dump_perfcnt()
{
   if (has_suspend_count) {
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

void FreedrenoDriver::disable_perfcnt()
{
   /* There isn't really any disable, only reconfiguring which countables
    * get muxed to which counters
    */
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

      for (unsigned j = 0; j < g->num_countables; j++) {
         const struct fd_perfcntr_countable *c = &g->countables[j];
         if (name != c->name)
            continue;

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
