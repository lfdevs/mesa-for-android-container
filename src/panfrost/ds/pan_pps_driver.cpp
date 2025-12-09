/*
 * Copyright © 2019-2021 Collabora, Ltd.
 * Author: Antonio Caggiano <antonio.caggiano@collabora.com>
 * Author: Rohan Garg <rohan.garg@collabora.com>
 * Author: Robert Beckett <bob.beckett@collabora.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "pan_pps_driver.h"

#include <cstring>
#include <xf86drm.h>

#ifndef ANDROID_LIBPERFETTO
#include <perfetto.h>
#else
#include <perfetto/tracing.h>
#endif

#include <drm-uapi/panfrost_drm.h>
#include <perf/pan_perf.h>
#include <util/macros.h>

#include <pps/pps.h>
#include <pps/pps_algorithm.h>

namespace pps {
PanfrostDriver::PanfrostDriver()
{
}

PanfrostDriver::~PanfrostDriver()
{
}

uint64_t
PanfrostDriver::get_min_sampling_period_ns()
{
   return 1000000;
}

std::pair<std::vector<CounterGroup>, std::vector<Counter>>
PanfrostDriver::create_available_counters(const PanfrostPerf &perf)
{
   std::pair<std::vector<CounterGroup>, std::vector<Counter>> ret;
   auto &[groups, counters] = ret;

   size_t cid = 0;

   for (uint32_t gid = 0; gid < perf.perf->cfg->n_categories; ++gid) {
      const auto &category = perf.perf->cfg->categories[gid];
      CounterGroup group = {};
      group.id = gid;
      group.name = category.name;

      for (size_t id = 0; id < category.n_counters; ++id) {
         Counter counter = {};
         counter.id = cid;
         counter.group = gid;

         counter.name = category.counters[id].name;

         counter.set_getter([=](const Counter &c, const Driver &d) {
            auto &pan_driver = PanfrostDriver::into(d);
            struct pan_perf *perf = pan_driver.perf->perf;
            const auto counter = &perf->cfg->categories[gid].counters[id];
            return int64_t(pan_perf_counter_read(counter, perf));
         });

         group.counters.push_back(cid++);

         counters.emplace_back(counter);
      }

      groups.push_back(group);
   }

   return ret;
}

bool
PanfrostDriver::init_perfcnt()
{
   if (!dev) {
      dev = std::make_unique<PanfrostDevice>(drm_device.fd);
   }
   if (!perf) {
      perf = std::make_unique<PanfrostPerf>(*dev);
   }
   if (groups.empty() && counters.empty()) {
      std::tie(groups, counters) = create_available_counters(*perf);
   }
   return true;
}

void
PanfrostDriver::enable_counter(const uint32_t counter_id)
{
   enabled_counters.push_back(counters[counter_id]);
}

void
PanfrostDriver::enable_all_counters()
{
   enabled_counters.resize(counters.size());
   for (size_t i = 0; i < counters.size(); ++i) {
      enabled_counters[i] = counters[i];
   }
}

void
PanfrostDriver::enable_perfcnt(const uint64_t /* sampling_period_ns */)
{
   auto res = perf->enable();
   if (!check(res, "Failed to enable performance counters")) {
      if (res == -ENOSYS) {
         PERFETTO_FATAL(
            "Please enable unstable ioctls with: modprobe panfrost unstable_ioctls=1");
      }
      PERFETTO_FATAL("Please verify graphics card");
   }
}

bool
PanfrostDriver::dump_perfcnt()
{
   last_dump_ts = perfetto::base::GetBootTimeNs().count();

   // Dump performance counters to buffer
   if (!check(perf->dump(), "Failed to dump performance counters")) {
      PERFETTO_ELOG("Skipping sample");
      return false;
   }

   return true;
}

uint64_t
PanfrostDriver::next()
{
   auto ret = last_dump_ts;
   last_dump_ts = 0;
   return ret;
}

void
PanfrostDriver::disable_perfcnt()
{
   perf->disable();
   perf.reset();
   dev.reset();
   groups.clear();
   counters.clear();
   enabled_counters.clear();
}

uint32_t
PanfrostDriver::gpu_clock_id() const
{
   return perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
}

uint64_t
PanfrostDriver::gpu_timestamp() const
{
   return perfetto::base::GetBootTimeNs().count();
}

bool
PanfrostDriver::cpu_gpu_timestamp(uint64_t &, uint64_t &) const
{
   /* Not supported */
   return false;
}

} // namespace pps
