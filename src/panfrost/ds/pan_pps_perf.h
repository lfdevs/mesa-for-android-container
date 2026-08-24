/*
 * Copyright © 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <vector>
#include <utility>
#include <pps/pps.h>
#include <pps/pps_counter.h>

struct pan_perf;

namespace pps {
class PanfrostDevice {
 public:
   PanfrostDevice(int fd);
   ~PanfrostDevice();

   PanfrostDevice(const PanfrostDevice &) = delete;
   PanfrostDevice &operator=(const PanfrostDevice &) = delete;

   PanfrostDevice(PanfrostDevice &&);
   PanfrostDevice &operator=(PanfrostDevice &&);

   int fd = -1;
};

class PanfrostPerf {
 public:
   PanfrostPerf(const PanfrostDevice &dev);
   ~PanfrostPerf();

   PanfrostPerf(const PanfrostPerf &) = delete;
   PanfrostPerf &operator=(const PanfrostPerf &) = delete;

   PanfrostPerf(PanfrostPerf &&);
   PanfrostPerf &operator=(PanfrostPerf &&);

   int enable(uint64_t sampling_period_ns);
   void disable();
   bool dump();

   std::pair<std::vector<CounterGroup>, std::vector<Counter>>
   create_available_counters() const;

   uint64_t get_min_sampling_period_ns();
   uint64_t next();
   uint32_t gpu_clock_id() const;
   uint64_t gpu_timestamp() const;
   bool cpu_gpu_timestamp(uint64_t &cpu_timestamp,
                          uint64_t &gpu_timestamp) const;
   void *get_subinstance();

 private:
   struct pan_perf *perf = nullptr;
   uint64_t last_dump_ts = 0;
};

} // namespace pps
