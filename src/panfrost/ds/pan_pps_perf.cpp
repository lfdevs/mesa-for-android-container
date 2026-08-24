/*
 * Copyright © 2021 Collabora, Ltd.
 * Copyright © 2026 Arm, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "pan_pps_perf.h"
#include "pan_pps_driver.h"

#include <lib/kmod/pan_kmod.h>
#include <perf/pan_perf.h>

#include <pps/pps.h>
#include <util/ralloc.h>
#include <util/timespec.h>

#include <sstream>

namespace pps {

PanfrostPerf::PanfrostPerf(const PanfrostDevice &dev)
{
   assert(dev.fd >= 0);
   perf = pan_perf_create(dev.fd);
   assert(perf);
}

PanfrostPerf::~PanfrostPerf()
{
   if (perf) {
      pan_perf_disable(perf);
      pan_perf_destroy(perf);
   }
}

PanfrostPerf::PanfrostPerf(PanfrostPerf &&o): perf{o.perf}
{
   o.perf = nullptr;
}

PanfrostPerf &
PanfrostPerf::operator=(PanfrostPerf &&o)
{
   std::swap(perf, o.perf);
   return *this;
}

int
PanfrostPerf::enable(uint64_t sampling_period_ns)
{
   assert(perf);
   return pan_perf_enable(perf, sampling_period_ns);
}

void
PanfrostPerf::disable()
{
   assert(perf);
   pan_perf_disable(perf);
}

bool
PanfrostPerf::dump()
{
   assert(perf);

   int ret = pan_perf_dump(perf);

   last_dump_ts = pan_perf_get_dump_timestamp(perf);
   return !!(ret >= 0);
}

uint64_t
PanfrostPerf::get_min_sampling_period_ns()
{
   assert(perf);
   return pan_perf_get_min_sampling_period(perf);
}

void *
PanfrostPerf::get_subinstance() {
   return perf;
}

static std::string
get_counter_name(const char *base_name, enum mali_perf_block_type blk_type,
                 uint8_t blk_idx)
{
   std::stringstream ss;

   switch (blk_type) {
   case MALI_PERF_BLOCK_MEMSYS:
      ss << base_name << " (slice " << (unsigned)blk_idx << ")";
      break;
   case MALI_PERF_BLOCK_SHADER_CORE:
      ss << base_name << " (core " << (unsigned)blk_idx << ")";
      break;
   default:
      assert(!blk_idx);
      ss << base_name;
      break;
   }

   return ss.str();
}

Counter::Units
convert_pan_units(enum mali_perf_counter_units unit)
{
   switch (unit) {
   case MALI_PERF_COUNTER_UNITS_PRIMITIVES:
      return Counter::Units::Primitive;
   case MALI_PERF_COUNTER_UNITS_INSTRUCTIONS:
      return Counter::Units::Instruction;
   case MALI_PERF_COUNTER_UNITS_BYTES:
      return Counter::Units::Byte;
   case MALI_PERF_COUNTER_UNITS_PIXELS:
      return Counter::Units::Pixel;
   default:
      return Counter::Units::None;
   }
}

static uint32_t
convert_classes(uint64_t classes)
{
   static_assert((int)MALI_PERF_UNCLASSIFIED == (int)Perfettogrp::UNCLASSIFIED);
   static_assert((int)MALI_PERF_SYSTEM == (int)Perfettogrp::SYSTEM);
   static_assert((int)MALI_PERF_VERTICES == (int)Perfettogrp::VERTICES);
   static_assert((int)MALI_PERF_FRAGMENTS == (int)Perfettogrp::FRAGMENTS);
   static_assert((int)MALI_PERF_PRIMITIVES == (int)Perfettogrp::PRIMITIVES);
   static_assert((int)MALI_PERF_MEMORY == (int)Perfettogrp::MEMORY);
   static_assert((int)MALI_PERF_COMPUTE == (int)Perfettogrp::COMPUTE);

   /* RAY_TRACING not yet exposed by pps_counter. */
   if (classes & MALI_PERF_RAY_TRACING)
      classes &= ~MALI_PERF_RAY_TRACING;

   assert(!(classes &
            ~(BITFIELD64_BIT(MALI_PERF_UNCLASSIFIED) |
              BITFIELD64_BIT(MALI_PERF_SYSTEM) |
              BITFIELD64_BIT(MALI_PERF_VERTICES) |
              BITFIELD64_BIT(MALI_PERF_FRAGMENTS) |
              BITFIELD64_BIT(MALI_PERF_PRIMITIVES) |
              BITFIELD64_BIT(MALI_PERF_MEMORY) |
              BITFIELD64_BIT(MALI_PERF_COMPUTE) |
              BITFIELD64_BIT(MALI_PERF_RAY_TRACING))));

   if (!classes)
      classes = BITFIELD64_BIT(MALI_PERF_UNCLASSIFIED);

   assert(classes <= UINT32_MAX);
   return classes;
}

std::pair<std::vector<CounterGroup>, std::vector<Counter>>
PanfrostPerf::create_available_counters() const
{
   std::pair<std::vector<CounterGroup>, std::vector<Counter>> ret;
   auto &[groups, counters] = ret;
   uint32_t global_counter_id = 0;

   for (const struct mali_perf_counter *cinfo = perf->info->counters;
        cinfo->name; cinfo++) {
      for (uint32_t j = 0;
           j < mali_perf_block_count(cinfo->block, &perf->constants); j++) {

         Counter counter = {};
         counter.id = global_counter_id++;
         counter.name = get_counter_name(cinfo->name, cinfo->block, j);
         counter.description = cinfo->desc;
         counter.group = cinfo->block;
         counter.group_mask = convert_classes(cinfo->classes);
         counter.units = convert_pan_units(cinfo->units);
         counter.set_getter([=](const Counter &c, const Driver &d) {
            auto &pan_driver = PanfrostDriver::into(d);
            struct pan_perf *perf = static_cast<struct pan_perf *>(
               pan_driver.perf->get_subinstance());
            return pan_perf_counter_read(perf, cinfo, j);
         });
         counters.emplace_back(counter);
      }
   }

   for (uint32_t i = 0; i < MALI_PERF_BLOCK_TYPE_COUNT; i++) {
      CounterGroup group = {};

      group.id = i;
      group.name = mali_perf_block_type_str((enum mali_perf_block_type)i);

      for (auto &counter : counters) {
         if (counter.group == (int32_t)group.id)
            group.counters.push_back(counter.id);
      }

      if (group.counters.size() > 0)
         groups.push_back(group);
   }

   return ret;
}

uint64_t
PanfrostPerf::next()
{
   auto ret = last_dump_ts;
   last_dump_ts = 0;
   return ret;
}

uint32_t
PanfrostPerf::gpu_clock_id() const
{
   assert(pan_perf_gpu_clock_id(perf) == CLOCK_MONOTONIC_RAW);
   return perfetto::protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW;
}

uint64_t
PanfrostPerf::gpu_timestamp() const
{
   return pan_perf_get_gpu_timestamp(perf);
}

bool
PanfrostPerf::cpu_gpu_timestamp(uint64_t &, uint64_t &) const
{
   // TODO (panthor) Start using the appropriate IOCTL to get these values
   return false;
}

} // namespace pps
