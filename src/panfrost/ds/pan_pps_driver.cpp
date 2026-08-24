/*
 * Copyright © 2019-2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_pps_driver.h"

#include <cstring>
#include <xf86drm.h>

#include "util/perf/u_perfetto.h"

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
   return perf->get_min_sampling_period_ns();
}

std::pair<std::vector<CounterGroup>, std::vector<Counter>>
PanfrostDriver::create_available_counters(const PanfrostPerf &perf)
{
   return perf.create_available_counters();
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
PanfrostDriver::enable_perfcnt(const uint64_t sampling_period_ns)
{
   auto res = perf->enable(sampling_period_ns);
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
   return perf->next();
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
   return perf->gpu_clock_id();
}

uint64_t
PanfrostDriver::gpu_timestamp() const
{
   return perf->gpu_timestamp();
}

bool
PanfrostDriver::cpu_gpu_timestamp(uint64_t &cpu_timestamp, uint64_t &gpu_timestamp) const
{
   return perf->cpu_gpu_timestamp(cpu_timestamp, gpu_timestamp);
}

} // namespace pps
