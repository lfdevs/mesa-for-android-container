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

#include "pps/pps.h"
#include "pps/pps_algorithm.h"

namespace pps
{

void
FreedrenoDriver::setup_a7xx_counters()
{
   /* TODO is there a reason to want more than one group? */
   CounterGroup group = {};
   group.name = "counters";
   groups.clear();
   counters.clear();
   countables.clear();
   enabled_counters.clear();
   groups.emplace_back(std::move(group));

   /* So far, all a7xx devices seem to have two uSPTPs in each SP core
    * and 128 ALUs in each uSPTP.
    */
   const unsigned number_of_usptp = info->num_sp_cores * 2;
   const unsigned number_of_alus_per_usptp = 128;

   /* The enumeration and two helper lambdas serve to handle countables
    * that can be sampled from either rendering or visibility bins.
    */
   enum {
      BR = 0,
      BV = 1,
   };

   auto cbCountable = [=](std::string group, std::string name) {
      return std::array<Countable, 2> {
         countable(group, name),
         countable("BV_" + group, name),
      };
   };

   auto cbSum = [](const std::array<Countable, 2>& countable) {
      return countable[BR] + countable[BV];
   };

   /* This is a helper no-op lambda to handle known and understood counters
    * that we can't currently implement for a variety of reasons.
    */
   auto disabledCounter = [](std::string, Counter::Units, std::function<int64_t()>) { };

   /* CP: 3/14 counters */
   auto PERF_CP_ALWAYS_COUNT = countable("CP", "PERF_CP_ALWAYS_COUNT");
   auto PERF_CP_NUM_PREEMPTIONS = countable("CP", "PERF_CP_NUM_PREEMPTIONS");
   auto PERF_CP_PREEMPTION_REACTION_DELAY = countable("CP", "PERF_CP_PREEMPTION_REACTION_DELAY");

   /* RBBM: 1/4 counters */
   auto PERF_RBBM_STATUS_MASKED = countable("RBBM", "PERF_RBBM_STATUS_MASKED");

   /* PC: 3/8 counters, BV_PC: 3/8 counters */
   auto PERF_PC_STALL_CYCLES_VFD = cbCountable("PC", "PERF_PC_STALL_CYCLES_VFD");
   auto PERF_PC_VERTEX_HITS = cbCountable("PC", "PERF_PC_VERTEX_HITS");
   auto PERF_PC_VS_INVOCATIONS = cbCountable("PC", "PERF_PC_VS_INVOCATIONS");

   /* TSE: 4/8 counters */
   auto PERF_TSE_INPUT_PRIM = countable("TSE", "PERF_TSE_INPUT_PRIM");
   auto PERF_TSE_TRIVAL_REJ_PRIM = countable("TSE", "PERF_TSE_TRIVAL_REJ_PRIM");
   auto PERF_TSE_CLIPPED_PRIM = countable("TSE", "PERF_TSE_CLIPPED_PRIM");
   auto PERF_TSE_OUTPUT_VISIBLE_PRIM = countable("TSE", "PERF_TSE_OUTPUT_VISIBLE_PRIM");

   /* UCHE: 8/12 counters */
   auto PERF_UCHE_STALL_CYCLES_ARBITER = countable("UCHE", "PERF_UCHE_STALL_CYCLES_ARBITER");
   auto PERF_UCHE_VBIF_READ_BEATS_TP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_TP");
   auto PERF_UCHE_VBIF_READ_BEATS_VFD = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_VFD");
   auto PERF_UCHE_VBIF_READ_BEATS_SP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_SP");
   auto PERF_UCHE_READ_REQUESTS_TP = countable("UCHE", "PERF_UCHE_READ_REQUESTS_TP");
   auto PERF_UCHE_READ_REQUESTS_SP = countable("UCHE", "PERF_UCHE_READ_REQUESTS_SP");
   auto PERF_UCHE_WRITE_REQUESTS_SP = countable("UCHE", "PERF_UCHE_WRITE_REQUESTS_SP");
   auto PERF_UCHE_EVICTS = countable("UCHE", "PERF_UCHE_EVICTS");

   /* TP: 7/12 counters, BV_TP: 6/6 counters */
   auto PERF_TP_BUSY_CYCLES = countable("TP", "PERF_TP_BUSY_CYCLES");
   auto PERF_TP_L1_CACHELINE_REQUESTS = cbCountable("TP", "PERF_TP_L1_CACHELINE_REQUESTS");
   auto PERF_TP_L1_CACHELINE_MISSES = cbCountable("TP", "PERF_TP_L1_CACHELINE_MISSES");
   auto PERF_TP_OUTPUT_PIXELS = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS");
   auto PERF_TP_OUTPUT_PIXELS_POINT = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS_POINT");
   auto PERF_TP_OUTPUT_PIXELS_BILINEAR = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS_BILINEAR");
   auto PERF_TP_OUTPUT_PIXELS_ANISO = cbCountable("TP", "PERF_TP_OUTPUT_PIXELS_ANISO");

   /* SP: 24/24 counters, BV_SP: 7/12 counters */
   auto PERF_SP_BUSY_CYCLES = countable("SP", "PERF_SP_BUSY_CYCLES");
   auto PERF_SP_ALU_WORKING_CYCLES = countable("SP", "PERF_SP_ALU_WORKING_CYCLES");
   auto PERF_SP_EFU_WORKING_CYCLES = countable("SP", "PERF_SP_EFU_WORKING_CYCLES");
   auto PERF_SP_STALL_CYCLES_TP = cbCountable("SP", "PERF_SP_STALL_CYCLES_TP");
   auto PERF_SP_NON_EXECUTION_CYCLES = countable("SP", "PERF_SP_NON_EXECUTION_CYCLES");
   auto PERF_SP_VS_STAGE_TEX_INSTRUCTIONS = cbCountable("SP", "PERF_SP_VS_STAGE_TEX_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_EFU_INSTRUCTIONS = cbCountable("SP", "PERF_SP_VS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS = cbCountable("SP", "PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_EFU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS");
   auto PERF_SP_ICL1_REQUESTS = cbCountable("SP", "PERF_SP_ICL1_REQUESTS");
   auto PERF_SP_ICL1_MISSES = cbCountable("SP", "PERF_SP_ICL1_MISSES");
   auto PERF_SP_ANY_EU_WORKING_FS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_FS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_VS_STAGE = cbCountable("SP", "PERF_SP_ANY_EU_WORKING_VS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_CS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_CS_STAGE");
   auto PERF_SP_PIXELS = countable("SP", "PERF_SP_PIXELS");
   auto PERF_SP_RAY_QUERY_INSTRUCTIONS = countable("SP", "PERF_SP_RAY_QUERY_INSTRUCTIONS");
   auto PERF_SP_RTU_BUSY_CYCLES = countable("SP", "PERF_SP_RTU_BUSY_CYCLES");
   auto PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES = countable("SP", "PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES");
   auto PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES = countable("SP", "PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES");
   auto PERF_SP_RTU_RAY_BOX_INTERSECTIONS = countable("SP", "PERF_SP_RTU_RAY_BOX_INTERSECTIONS");
   auto PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS = countable("SP", "PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS");
   auto PERF_SP_SCH_STALL_CYCLES_RTU = countable("SP", "PERF_SP_SCH_STALL_CYCLES_RTU");

   /* CMP: 1/4 counters */
   auto PERF_CMPDECMP_VBIF_READ_DATA = countable("CMP", "PERF_CMPDECMP_VBIF_READ_DATA");

   /* LRZ: 4/4 counters */
   auto PERF_LRZ_TOTAL_PIXEL = countable("LRZ", "PERF_LRZ_TOTAL_PIXEL");
   auto PERF_LRZ_VISIBLE_PIXEL_AFTER_LRZ = countable("LRZ", "PERF_LRZ_VISIBLE_PIXEL_AFTER_LRZ");
   auto PERF_LRZ_TILE_KILLED = countable("LRZ", "PERF_LRZ_TILE_KILLED");
   auto PERF_LRZ_PRIM_KILLED_BY_LRZ = countable("LRZ", "PERF_LRZ_PRIM_KILLED_BY_LRZ");

   /**
    * GPU Compute
    */
   disabledCounter("Avg Load-Store Instructions Per Cycle", Counter::Units::None, [=]() {
         /* Number of average Load-Store instructions per cycle. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_27 = PERF_SP_LM_LOAD_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_28 = PERF_SP_LM_STORE_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_30 = PERF_SP_GM_LOAD_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_31 = PERF_SP_GM_STORE_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: 4*sum(PERF_SP_{LM,GM}_{LOAD,STORE}_INSTRUCTIONS) / PERF_SP_BUSY_CYCLES
          */
         return 42;
      }
   );
   counter("Bytes Data Actually Written", Counter::Units::Byte, [=]() {
         /* Number of bytes requested to be written by the GPU. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_18 = PERF_UCHE_EVICTS
          * Notes:
          *   - Equation: PERF_UCHE_EVICTS * 64
          */
         return PERF_UCHE_EVICTS * 64;
      }
   );
   counter("Bytes Data Write Requested", Counter::Units::Byte, [=]() {
         /* Number of bytes requested to be written by the GPU. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_15 = PERF_UCHE_WRITE_REQUESTS_SP
          * Notes:
          *   - Equation: PERF_UCHE_WRITE_REQUESTS_SP * 16
          */
         return PERF_UCHE_WRITE_REQUESTS_SP * 16;
      }
   );
   counter("Global Buffer Data Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global buffer data read in by the GPU, per second from the system memory (when the data is not found in L2 cache). */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_8 = PERF_UCHE_VBIF_READ_BEATS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_VBIF_READ_BEATS_SP * 32) / time
          */
         return (PERF_UCHE_VBIF_READ_BEATS_SP * 32) / time;
      }
   );
   counter("Global Buffer Data Read Request BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global buffer read requests, made by a compute kernel to the L2 cache, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_13 = PERF_UCHE_READ_REQUESTS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_SP * 16) / time
          */
         return (PERF_UCHE_READ_REQUESTS_SP * 16) / time;
      }
   );
   counter("% Global Buffer Read L2 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of total global buffer read requests that were fulfilled by L2 cache hit which is populated by looking at the number of read requests that were forwarded to VBIF to read from the system memory. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_8 = PERF_UCHE_VBIF_READ_BEATS_SP
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_13 = PERF_UCHE_READ_REQUESTS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_SP - (PERF_UCHE_VBIF_READ_BEATS_SP / 2)) / PERF_UCHE_READ_REQUESTS_SP
          */
         return percent(PERF_UCHE_READ_REQUESTS_SP - (PERF_UCHE_VBIF_READ_BEATS_SP / 2), PERF_UCHE_READ_REQUESTS_SP);
      }
   );
   counter("% Global Buffer Write L2 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of global write L2 Hit. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_18 = PERF_UCHE_EVICTS
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_15 = PERF_UCHE_WRITE_REQUESTS_SP
          * Notes:
          *   - Equation: (PERF_UCHE_WRITE_REQUESTS_SP - PERF_UCHE_EVICTS) / PERF_UCHE_WRITE_REQUESTS_SP
          */
         return percent(PERF_UCHE_WRITE_REQUESTS_SP - PERF_UCHE_EVICTS, PERF_UCHE_WRITE_REQUESTS_SP);
      }
   );
   counter("Global Image Compressed Data Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global Image data (compressed) read in by the GPU per second from the system memory (when the data is not found in L2 cache). */
         /* Countables:
          * PERFCOUNTER_GROUP_CMP::COUNTABLE_7 = PERF_CMPDECMP_VBIF_READ_DATA
          * Notes:
          *   - Equation: (PERF_CMPDECMP_VBIF_READ_DATA * 32) / time
          */
         return (PERF_CMPDECMP_VBIF_READ_DATA * 32) / time;
      }
   );
   counter("Global Image Data Read Request BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of image buffer read requests, made by a compute kernel to the L2 cache, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_9 = PERF_UCHE_READ_REQUESTS_TP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_TP * 16) / time
          */
         return (PERF_UCHE_READ_REQUESTS_TP * 16) / time;
      }
   );
   counter("Global Image Uncompressed Data Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Number of bytes of global Image data (uncompressed) read in by the GPU per second from the system memory (when the data is not found in L2 cache). */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * Notes:
          *   - Equation: (PERF_UCHE_VBIF_READ_BEATS_TP * 32) / time
          */
         return (PERF_UCHE_VBIF_READ_BEATS_TP * 32) / time;
      }
   );
   disabledCounter("Global Memory Atomic Instructions", Counter::Units::None, [=]() {
         /* Number of Global Memory Atomic Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_32 = PERF_SP_GM_ATOMICS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_GM_ATOMICS * 4
          */
         return 42;
      }
   );
   disabledCounter("Global Memory Load Instructions", Counter::Units::None, [=]() {
         /* Number of Global Memory Load Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_30 = PERF_SP_GM_LOAD_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_GM_LOAD_INSTRUCTIONS * 4
          */
         return 42;
      }
   );
   disabledCounter("Global Memory Store Instructions", Counter::Units::None, [=]() {
         /* Number of Global Memory Store Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_31 = PERF_SP_GM_STORE_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_GM_STORE_INSTRUCTIONS * 4
          */
         return 42;
      }
   );
   counter("% Image Read L2 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of total image read requests that were fulfilled by L2 cache hit which is populated by looking at the number of read requests that were forwarded to VBIF to read from the system memory. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_9 = PERF_UCHE_READ_REQUESTS_TP
          * Notes:
          *   - Equation: (PERF_UCHE_READ_REQUESTS_TP - (PERF_UCHE_VBIF_READ_BEATS_TP / 2)) / PERF_UCHE_READ_REQUESTS_TP
          */
         return percent(PERF_UCHE_READ_REQUESTS_TP - (PERF_UCHE_VBIF_READ_BEATS_TP / 2), PERF_UCHE_READ_REQUESTS_TP);
      }
   );
   counter("% Kernel Load Cycles", Counter::Units::Percent, [=]() {
         /* Percentage of cycles used for a compute kernel loading; excludes execution cycles. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_0 = PERF_CP_ALWAYS_COUNT
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - Equation: (PERF_RBBM_STATUS_MASKED - (PERF_SP_BUSY_CYCLES * #uSPTP)) / PERF_CP_ALWAYS_COUNT
          */
         return percent(PERF_RBBM_STATUS_MASKED - (PERF_SP_BUSY_CYCLES * number_of_usptp), PERF_CP_ALWAYS_COUNT);
      }
   );
   counter("% L1 Hit", Counter::Units::Percent, [=]() {
         /* Percentage of L1 texture cache requests that were hits. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_6 = PERF_TP_L1_CACHELINE_REQUESTS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * Notes:
          *   - Equation: (PERF_TP_L1_CACHELINE_REQUESTS - PERF_TP_L1_CACHELINE_MISSES) / PERF_TP_L1_CACHELINE_REQUESTS
          */
         return percent(PERF_TP_L1_CACHELINE_REQUESTS[BR] - PERF_TP_L1_CACHELINE_MISSES[BR], PERF_TP_L1_CACHELINE_REQUESTS[BR]);
      }
   );
   disabledCounter("Load-Store Utilization", Counter::Units::Percent, [=]() {
         /* Percentage of the Load-Store unit is utilized compared to theoretical Load/Store throughput. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_63 = PERF_SP_LOAD_CONTROL_WORKING_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LOAD_CONTROL_WORKING_CYCLES / PERF_SP_BUSY_CYCLES
          */
         return 42;
      }
   );
   disabledCounter("Local Memory Atomic Instructions", Counter::Units::None, [=]() {
         /* Number of Local Memory Atomic Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_29 = PERF_SP_LM_ATOMICS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LM_ATOMICS * 4
          */
         return 42;
      }
   );
   disabledCounter("Local Memory Load Instructions", Counter::Units::None, [=]() {
         /* Number of Local Memory Load Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_27 = PERF_SP_LM_LOAD_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LM_LOAD_INSTRUCTIONS * 4
          */
         return 42;
      }
   );
   disabledCounter("Local Memory Store Instructions", Counter::Units::None, [=]() {
         /* Number of Local Memory Store Instructions executed by SP during a given sample period. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_28 = PERF_SP_LM_STORE_INSTRUCTIONS
          * Notes:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - Equation: PERF_SP_LM_STORE_INSTRUCTIONS * 4
          */
         return 42;
      }
   );

   /**
    * GPU General
    */
   disabledCounter("Clocks / Second", Counter::Units::None, [=]() {
         /* Number of GPU clocks per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_0 = PERF_CP_ALWAYS_COUNT
          * Notes:
          *   - TODO: with Adaptive Clock Distribution, the measured values are much more varied
          *     than the constant GPU frequency value we currently get, so this counter is disabled
          *     for now in favor of the GPU Frequency counter below.
          *   - Equation: PERF_CP_ALWAYS_COUNT / time
          */
         return 42;
      }
   );
   disabledCounter("GPU % Bus Busy", Counter::Units::Percent, [=]() {
         /* Approximate Percentage of time the GPU's bus to system memory is busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_1 = PERF_UCHE_STALL_CYCLES_ARBITER
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_34 = PERF_GBIF_AXI0_READ_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_35 = PERF_GBIF_AXI1_READ_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_46 = PERF_GBIF_AXI0_WRITE_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_47 = PERF_GBIF_AXI1_WRITE_DATA_BEATS_TOTAL
          * Notes:
          *   - TODO: requires VBIF perfcounter group exposure which isn't trivial because of
          *     more complex way that those counters are enabled
          *   - Equation: (PERF_UCHE_STALL_CYCLES_ARBITER + sum(PERF_GBIF_AXI{0,1}_{READ,WRITE}_DATA_BEATS_TOTAL)) / (4 * PERF_RBBM_STATUS_MASKED)
          */
         return 42;
      }
   );
   counter("GPU Frequency", Counter::Units::None, [=]() {
         /* Notes:
          *   - TODO: Should read from (an equivalent of) /sys/class/kgsl/kgsl-3d0/gpuclk
          *   - Same value can be retrieved through PERF_CP_ALWAYS_COUNT, until ACD enables adaptive
          *     GPU frequencies that would be covered by the Clocks / Second counter above.
          */
         return PERF_CP_ALWAYS_COUNT / time;
      }
   );
   disabledCounter("GPU Temperature", Counter::Units::None, [=]() {
         /* TODO: Should read from (an equivalent of) /sys/class/kgsl/kgsl-3d0/temp */
         return 42;
      }
   );
   counter("GPU % Utilization", Counter::Units::Percent, [=]() {
         /* Percentage utilization of the GPU. */
         /* Countables:
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(PERF_RBBM_STATUS_MASKED, max_freq);
      }
   );

   /**
    * GPU Memory Stats
    */
   counter("Avg Bytes / Fragment", Counter::Units::Byte, [=]() {
         /* Average number of bytes transferred from main memory for each fragment. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_TP * 32, PERF_SP_PIXELS);
      }
   );
   counter("Avg Bytes / Vertex", Counter::Units::Byte, [=]() {
         /* Average number of bytes transferred from main memory for each vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_5 = PERF_UCHE_VBIF_READ_BEATS_VFD
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          */
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_VFD * 32, cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   disabledCounter("Read Total (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Total number of bytes read by the GPU from memory, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_34 = PERF_GBIF_AXI0_READ_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_35 = PERF_GBIF_AXI1_READ_DATA_BEATS_TOTAL
          * Notes:
          *   - TODO: requires VBIF perfcounter group exposure which isn't trivial because of
          *     more complex way that those counters are enabled
          *   - Equation: (PERF_GBIF_AXI0_READ_DATA_BEATS_TOTAL + PERF_GBIF_AXI1_READ_DATA_BEATS_TOTAL) * 32 / time
          */
         return 42;
      }
   );
   counter("SP Memory Read (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Bytes of data read from memory by the Shader Processors, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_8 = PERF_UCHE_VBIF_READ_BEATS_SP
          */
         return (PERF_UCHE_VBIF_READ_BEATS_SP * 32) / time;
      }
   );
   counter("Texture Memory Read BW (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Bytes of texture data read from memory per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_CMP::COUNTABLE_7 = PERF_CMPDECMP_VBIF_READ_DATA
          */
         return ((PERF_UCHE_VBIF_READ_BEATS_TP + PERF_CMPDECMP_VBIF_READ_DATA) * 32) / time;
      }
   );
   counter("Vertex Memory Read (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Bytes of vertex data read from memory per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_5 = PERF_UCHE_VBIF_READ_BEATS_VFD
          */
         return (PERF_UCHE_VBIF_READ_BEATS_VFD * 32) / time;
      }
   );
   disabledCounter("Write Total (Bytes/sec)", Counter::Units::Byte, [=]() {
         /* Total number of bytes written by the GPU to memory, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_46 = PERF_GBIF_AXI0_WRITE_DATA_BEATS_TOTAL
          * PERFCOUNTER_GROUP_VBIF::COUNTABLE_47 = PERF_GBIF_AXI1_WRITE_DATA_BEATS_TOTAL
          * Notes:
          *   - TODO: requires VBIF perfcounter group exposure which isn't trivial because of
          *     more complex way that those counters are enabled
          *   - Equation: (PERF_GBIF_AXI0_WRITE_DATA_BEATS_TOTAL + PERF_GBIF_AXI1_WRITE_DATA_BEATS_TOTAL) * 32 / time
          */
         return 42;
      }
   );

   /**
    * GPU Preemption
    */
   counter("Avg Preemption Delay", Counter::Units::None, [=]() {
         /* Average time (us) from the preemption request to preemption start. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_4 = PERF_CP_PREEMPTION_REACTION_DELAY
          * PERFCOUNTER_GROUP_CP::COUNTABLE_3 = PERF_CP_NUM_PREEMPTIONS
          * PERFCOUNTER_GROUP_CP::COUNTABLE_0 = PERF_CP_ALWAYS_COUNT
          * Note:
          *   - PERF_CP_NUM_PREEMPTIONS has to be divided by 2
          */
         if (!PERF_CP_ALWAYS_COUNT || !PERF_CP_NUM_PREEMPTIONS)
            return 0.0;

         double clocks_per_us = (double)PERF_CP_ALWAYS_COUNT / (time * 1000000);
         double delay_us = PERF_CP_PREEMPTION_REACTION_DELAY / clocks_per_us;
         return delay_us / ((double)PERF_CP_NUM_PREEMPTIONS / 2);
      }
   );
   counter("Preemptions / second", Counter::Units::None, [=]() {
         /* The number of GPU preemptions that occurred, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_CP::COUNTABLE_3 = PERF_CP_NUM_PREEMPTIONS
          * Note:
          *   - PERF_CP_NUM_PREEMPTIONS has to be divided by 2
          */
         return PERF_CP_NUM_PREEMPTIONS / (2 * time);
      }
   );

   /**
    * GPU Primitive Processing
    */
   counter("Average Polygon Area", Counter::Units::None, [=]() {
         /* Average number of pixels per polygon. */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_14 = PERF_TSE_OUTPUT_VISIBLE_PRIM
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(PERF_SP_PIXELS, PERF_TSE_OUTPUT_VISIBLE_PRIM);
      }
   );
   counter("Average Vertices / Polygon", Counter::Units::None, [=]() {
         /* Average number of vertices per polygon. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return safe_div(cbSum(PERF_PC_VS_INVOCATIONS), PERF_TSE_INPUT_PRIM);
      }
   );
   counter("Pre-clipped Polygons / Second", Counter::Units::None, [=]() {
         /* Number of polygons submitted to the GPU, per second, before any hardware clipping. */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return PERF_TSE_INPUT_PRIM / time;
      }
   );
   counter("% Prims Clipped", Counter::Units::Percent, [=]() {
         /* Percentage of primitives clipped by the GPU (where new primitives are generated). */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_9 = PERF_TSE_CLIPPED_PRIM
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return percent(PERF_TSE_CLIPPED_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );
   counter("% Prims Trivially Rejected", Counter::Units::Percent, [=]() {
         /* Percentage of primitives that are trivially rejected. */
         /* Countables:
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_8 = PERF_TSE_TRIVAL_REJ_PRIM
          * PERFCOUNTER_GROUP_TSE::COUNTABLE_6 = PERF_TSE_INPUT_PRIM
          */
         return percent(PERF_TSE_TRIVAL_REJ_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );
   counter("Reused Vertices / Second", Counter::Units::None, [=]() {
         /* Number of vertices used from the post-transform vertex buffer cache, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_19 = PERF_PC_VERTEX_HITS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_19 = PERF_PC_VERTEX_HITS
          */
         return cbSum(PERF_PC_VERTEX_HITS) / time;
      }
   );

   /**
    * GPU Shader Processing
    */
   counter("ALU / Fragment", Counter::Units::None, [=]() {
         /* Average number of scalar fragment shader ALU instructions issued per shaded fragment, expressed as full precision ALUs (2 mediump = 1 fullp). */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_99 = PERF_SP_QUADS
          * Notes:
          *   - PERF_SP_PIXELS is used instead of PERF_SP_QUADS to avoid SP counter group overcapacity.
          *   - PERF_SP_PIXELS ~ PERF_SP_QUADS * 4
          *   - original equation uses unmultiplied QUADS as denominator, we use PIXELS ~ QUADS * 4
          *     to match other per-fragment counters.
          */
         return safe_div(PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS + PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2,
            PERF_SP_PIXELS);
      }
   );
   counter("ALU / Vertex", Counter::Units::None, [=]() {
         /* Average number of vertex scalar shader ALU instructions issued per shaded vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          *   - For some reason half-precision ALUs are not counted.
          */
         return safe_div(4 * cbSum(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS), cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   counter("% Anisotropic Filtered", Counter::Units::Percent, [=]() {
         /* Percent of texels filtered using the 'Anisotropic' sampling method. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_28 = PERF_TP_OUTPUT_PIXELS_ANISO
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_28 = PERF_TP_OUTPUT_PIXELS_ANISO
          */
         return safe_div(cbSum(PERF_TP_OUTPUT_PIXELS_ANISO), cbSum(PERF_TP_OUTPUT_PIXELS));
      }
   );
   counter("Average BVH Fetch Latency Cycles", Counter::Units::None, [=]() {
         /* The Average BVH Fetch Latency cycles is the latency counted from start of BVH query request till getting BVH Query result back. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_139 = PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_140 = PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return safe_div(PERF_SP_RTU_BVH_FETCH_LATENCY_CYCLES, PERF_SP_RTU_BVH_FETCH_LATENCY_SAMPLES);
      }
   );
   counter("EFU / Fragment", Counter::Units::None, [=]() {
         /* Average number of scalar fragment shader EFU instructions issued per shaded fragment. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_39 = PERF_SP_FS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_99 = PERF_SP_QUADS
          * Notes:
          *   - PERF_SP_PIXELS is used instead of PERF_SP_QUADS to avoid SP counter group overcapacity.
          *   - PERF_SP_PIXELS ~ PERF_SP_QUADS * 4
          *   - original equation uses unmultiplied QUADS as denominator, we use PIXELS ~ QUADS * 4
          *     to match other per-fragment counters.
          */
         return safe_div(PERF_SP_FS_STAGE_EFU_INSTRUCTIONS, PERF_SP_PIXELS);
      }
   );
   counter("EFU / Vertex", Counter::Units::None, [=]() {
         /* Average number of scalar vertex shader EFU instructions issued per shaded vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return safe_div(4 * cbSum(PERF_SP_VS_STAGE_EFU_INSTRUCTIONS), cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   counter("Fragment ALU Instructions / Sec (Full)", Counter::Units::None, [=]() {
         /* Total number of full precision fragment shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS * 4) / time;
      }
   );
   counter("Fragment ALU Instructions / Sec (Half)", Counter::Units::None, [=]() {
         /* Total number of half precision Scalar fragment shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS * 4) / time;
      }
   );
   counter("Fragment EFU Instructions / Second", Counter::Units::None, [=]() {
         /* Total number of Scalar fragment shader Elementary Function Unit (EFU) instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_39 = PERF_SP_FS_STAGE_EFU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (PERF_SP_FS_STAGE_EFU_INSTRUCTIONS * 4) / time;
      }
   );
   counter("Fragment Instructions / Second", Counter::Units::None, [=]() {
         /* Total number of fragment shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_39 = PERF_SP_FS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return (4 * (PERF_SP_FS_STAGE_EFU_INSTRUCTIONS + PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
            + PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2)) / time;
      }
   );
   counter("Fragments Shaded / Second", Counter::Units::None, [=]() {
         /* Number of fragments submitted to the shader engine, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return PERF_SP_PIXELS / time;
      }
   );
   counter("% Linear Filtered", Counter::Units::Percent, [=]() {
         /* Percent of texels filtered using the 'Linear' sampling method. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_26 = PERF_TP_OUTPUT_PIXELS_BILINEAR
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_26 = PERF_TP_OUTPUT_PIXELS_BILINEAR
          */
         return safe_div(cbSum(PERF_TP_OUTPUT_PIXELS_BILINEAR), cbSum(PERF_TP_OUTPUT_PIXELS));
      }
   );
   counter("% Nearest Filtered", Counter::Units::Percent, [=]() {
         /* Percent of texels filtered using the 'Nearest' sampling method. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_25 = PERF_TP_OUTPUT_PIXELS_POINT
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_25 = PERF_TP_OUTPUT_PIXELS_POINT
          */
         return safe_div(cbSum(PERF_TP_OUTPUT_PIXELS_POINT), cbSum(PERF_TP_OUTPUT_PIXELS));
      }
   );
   disabledCounter("% Non-Base Level Textures", Counter::Units::Percent, [=]() {
         /* Percent of texels coming from a non-base MIP level. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_29 = PERF_TP_OUTPUT_PIXELS_ZERO_LOD
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_29 = PERF_TP_OUTPUT_PIXELS_ZERO_LOD
          * Notes:
          *   - FIXME: disabled due to lack of TP counter capacity
          *   - Equation: 100.0 - percent(cbSum(PERF_TP_OUTPUT_PIXELS_ZERO_LOD), cbSum(PERF_TP_OUTPUT_PIXELS));
          */
         return 42;
      }
   );
   counter("% RTU Busy", Counter::Units::Percent, [=]() {
         /* Percentage of time that Ray Tracing Unit in SP is busy compared to whole SP. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_125 = PERF_SP_RTU_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return percent(PERF_SP_RTU_BUSY_CYCLES, PERF_SP_BUSY_CYCLES);
      }
   );
   counter("RTU Ray Box Intersections Per Instruction", Counter::Units::None, [=]() {
         /* Number of Ray Box intersections per instruction. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_148 = PERF_SP_RTU_RAY_BOX_INTERSECTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_122 = PERF_SP_RAY_QUERY_INSTRUCTIONS
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return safe_div(PERF_SP_RTU_RAY_BOX_INTERSECTIONS, PERF_SP_RAY_QUERY_INSTRUCTIONS);
      }
   );
   counter("RTU Ray Triangle Intersections Per Instruction", Counter::Units::None, [=]() {
         /* Number of Ray Triangle intersections per instruction. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_149 = PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_122 = PERF_SP_RAY_QUERY_INSTRUCTIONS
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return safe_div(PERF_SP_RTU_RAY_TRIANGLE_INTERSECTIONS, PERF_SP_RAY_QUERY_INSTRUCTIONS);
      }
   );
   counter("% Shader ALU Capacity Utilized", Counter::Units::Percent, [=]() {
         /* Percent of maximum shader capacity (ALU operations) utilized. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_40 = PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_41 = PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         int64_t numerator = cbSum(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS) +
            PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS + PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2;
         int64_t denominator = PERF_SP_BUSY_CYCLES * number_of_alus_per_usptp;
         return percent(numerator, denominator);
      }
   );
   counter("% Shaders Busy", Counter::Units::Percent, [=]() {
         /* Percentage of time that all Shader cores are busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_TP::COUNTABLE_0 = PERF_TP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - SP_BUSY_CYCLES seems to be used as the numerator -- unless it's zero,
          *     at which point TP_BUSY_CYLCES seems to be used instead.
          */

         int64_t numerator = PERF_SP_BUSY_CYCLES;
         if (!numerator)
            numerator = PERF_TP_BUSY_CYCLES;
         return percent(numerator, number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Shaders Stalled", Counter::Units::Percent, [=]() {
         /* Percentage of time that all shader cores are idle with at least one active wave. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_7 = PERF_SP_NON_EXECUTION_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(PERF_SP_NON_EXECUTION_CYCLES, number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Texture Pipes Busy", Counter::Units::Percent, [=]() {
         /* Percentage of time that any texture pipe is busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_0 = PERF_TP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(PERF_TP_BUSY_CYCLES, number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("Textures / Fragment", Counter::Units::None, [=]() {
         /* Average number of textures referenced per fragment. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_33 = PERF_SP_VS_STAGE_TEX_INSTRUCTIONS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_10 = PERF_TP_OUTPUT_PIXELS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(PERF_TP_OUTPUT_PIXELS[BR], PERF_SP_PIXELS);
      }
   );
   counter("Textures / Vertex", Counter::Units::None, [=]() {
         /* Average number of textures referenced per vertex. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_33 = PERF_SP_VS_STAGE_TEX_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_33 = PERF_SP_VS_STAGE_TEX_INSTRUCTIONS
          * Notes:
          *   - Numerator has to be multiplied by four.
          */
         return safe_div(4 * cbSum(PERF_SP_VS_STAGE_TEX_INSTRUCTIONS), cbSum(PERF_PC_VS_INVOCATIONS));
      }
   );
   counter("% Time ALUs Working", Counter::Units::Percent, [=]() {
         /* Percentage of time the ALUs are working while the Shaders are busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_1 = PERF_SP_ALU_WORKING_CYCLES
          * Notes:
          *   - ALU working cycles have to be halved.
          */
         return percent(PERF_SP_ALU_WORKING_CYCLES / 2, PERF_SP_BUSY_CYCLES);
      }
   );
   counter("% Time Compute", Counter::Units::Percent, [=]() {
         /* Amount of time spent in compute work compared to the total time spent shading everything. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_78 = PERF_SP_ANY_EU_WORKING_CS_STAGE
          * CS_STAGE amount is also counted in FS_STAGE, so it shouldn't be summed into the total value.
          */
         int64_t total = PERF_SP_ANY_EU_WORKING_FS_STAGE +
            cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE);
         return percent(PERF_SP_ANY_EU_WORKING_CS_STAGE, total);
      }
   );
   counter("% Time EFUs Working", Counter::Units::Percent, [=]() {
         /* Percentage of time the EFUs are working while the Shaders are busy. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_0 = PERF_SP_BUSY_CYCLES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_2 = PERF_SP_EFU_WORKING_CYCLES
          */
         return percent(PERF_SP_EFU_WORKING_CYCLES, PERF_SP_BUSY_CYCLES);
      }
   );
   counter("% Time Shading Fragments", Counter::Units::Percent, [=]() {
         /* Amount of time spent shading fragments compared to the total time spent shading everything. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_78 = PERF_SP_ANY_EU_WORKING_CS_STAGE
          * Notes:
          *   - CS_STAGE amount is also counted in FS_STAGE, so fragment time has to be retrieved
          *     through subtraction and the compute time shouldn't be summed into the total value.
          */
         int64_t fragments = PERF_SP_ANY_EU_WORKING_FS_STAGE - PERF_SP_ANY_EU_WORKING_CS_STAGE;
         int64_t total = PERF_SP_ANY_EU_WORKING_FS_STAGE +
            cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE);
         return percent(fragments, total);
      }
   );
   counter("% Time Shading Vertices", Counter::Units::Percent, [=]() {
         /* Amount of time spent shading vertices compared to the total time spent shading everything. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_74 = PERF_SP_ANY_EU_WORKING_FS_STAGE
          * PERFCOUNTER_GROUP_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_76 = PERF_SP_ANY_EU_WORKING_VS_STAGE
          * Notes:
          *   - CS_STAGE amount is also counted in FS_STAGE, so it shouldn't be summed into the total value.
          */
         int64_t total = PERF_SP_ANY_EU_WORKING_FS_STAGE +
            cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE);
         return percent(cbSum(PERF_SP_ANY_EU_WORKING_VS_STAGE), total);
      }
   );
   counter("Vertex Instructions / Second", Counter::Units::None, [=]() {
         /* Total number of scalar vertex shader instructions issued, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_34 = PERF_SP_VS_STAGE_EFU_INSTRUCTIONS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_35 = PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS
          * Notes:
              - Numerator has to be multiplied by four.
          */
         return (4 * (cbSum(PERF_SP_VS_STAGE_EFU_INSTRUCTIONS) + cbSum(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS))) / time;
      }
   );
   counter("Vertices Shaded / Second", Counter::Units::None, [=]() {
         /* Number of vertices submitted to the shader engine, per second. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_25 = PERF_PC_VS_INVOCATIONS
          */
         return cbSum(PERF_PC_VS_INVOCATIONS) / time;
      }
   );
   disabledCounter("% Wave Context Occupancy", Counter::Units::Percent, [=]() {
         /* Average percentage of wave context occupancy per cycle. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_8 = PERF_SP_WAVE_CONTEXTS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_9 = PERF_SP_WAVE_CONTEXT_CYCLES
          * Note:
          *   - FIXME: disabled due to lack of SP counter capacity
          *   - the quotient has to be divided by the number of execution wave slots per SP (16 on a7xx)
          *   - Equation: (PERF_SP_WAVE_CONTEXTS / PERF_SP_WAVE_CONTEXT_CYCLES) / number_of_execution_wave_slots_per_sp;
          */
         return 42;
      }
   );

   /**
    * GPU Stalls
    */
   counter("% BVH Fetch Stall", Counter::Units::Percent, [=]() {
         /* Percentage of clock cycles where the RTU could not make any more requests for BVH fetch from scheduler. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_150 = PERF_SP_SCH_STALL_CYCLES_RTU
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - TODO: provisional implementation, wasn't able to verify.
          */
         return percent(PERF_SP_SCH_STALL_CYCLES_RTU, PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Instruction Cache Miss", Counter::Units::Percent, [=]() {
         /* Number of L1 instruction cache misses divided by L1 instruction cache requests. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_51 = PERF_SP_ICL1_REQUESTS
          * PERFCOUNTER_GROUP_SP::COUNTABLE_52 = PERF_SP_ICL1_MISSES
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_51 = PERF_SP_ICL1_REQUESTS
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_52 = PERF_SP_ICL1_MISSES
          */
         return percent(cbSum(PERF_SP_ICL1_MISSES), cbSum(PERF_SP_ICL1_REQUESTS));
      }
   );
   counter("L1 Texture Cache Miss Per Pixel", Counter::Units::None, [=]() {
         /* Average number of Texture L1 cache misses per pixel. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * PERFCOUNTER_GROUP_SP::COUNTABLE_101 = PERF_SP_PIXELS
          */
         return safe_div(cbSum(PERF_TP_L1_CACHELINE_MISSES), PERF_SP_PIXELS);
      }
   );
   counter("% Stalled On System Memory", Counter::Units::Percent, [=]() {
         /* Percentage of cycles the L2 cache is stalled waiting for data from system memory. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_1 = PERF_UCHE_STALL_CYCLES_ARBITER
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          * Notes:
          *   - denominator has to be multiplied by four, for unknown reasons.
          */
         return safe_div(PERF_UCHE_STALL_CYCLES_ARBITER, 4 * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Texture Fetch Stall", Counter::Units::Percent, [=]() {
         /* Percentage of clock cycles where the shader processors cannot make any more requests for texture data. */
         /* Countables:
          * PERFCOUNTER_GROUP_SP::COUNTABLE_4 = PERF_SP_STALL_CYCLES_TP
          * PERFCOUNTER_GROUP_BV_SP::COUNTABLE_4 = PERF_SP_STALL_CYCLES_TP
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(cbSum(PERF_SP_STALL_CYCLES_TP), number_of_usptp * PERF_RBBM_STATUS_MASKED);
      }
   );
   counter("% Texture L1 Miss", Counter::Units::Percent, [=]() {
         /* Number of L1 texture cache misses divided by L1 texture cache requests. */
         /* Countables:
          * PERFCOUNTER_GROUP_TP::COUNTABLE_6 = PERF_TP_L1_CACHELINE_REQUESTS
          * PERFCOUNTER_GROUP_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_6 = PERF_TP_L1_CACHELINE_REQUESTS
          * PERFCOUNTER_GROUP_BV_TP::COUNTABLE_7 = PERF_TP_L1_CACHELINE_MISSES
          */
         return percent(cbSum(PERF_TP_L1_CACHELINE_MISSES), cbSum(PERF_TP_L1_CACHELINE_REQUESTS));
      }
   );
   counter("% Texture L2 Miss", Counter::Units::Percent, [=]() {
         /* Number of L2 texture cache misses divided by L2 texture cache requests. */
         /* Countables:
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_4 = PERF_UCHE_VBIF_READ_BEATS_TP
          * PERFCOUNTER_GROUP_UCHE::COUNTABLE_9 = PERF_UCHE_READ_REQUESTS_TP
          * Notes:
          *   - ratio has to be multiplied by two. Unsure how this constant comes up.
          */
         return percent(2 * PERF_UCHE_VBIF_READ_BEATS_TP, PERF_UCHE_READ_REQUESTS_TP);
      }
   );
   counter("% Vertex Fetch Stall", Counter::Units::Percent, [=]() {
         /* Percentage of clock cycles where the GPU cannot make any more requests for vertex data. */
         /* Countables:
          * PERFCOUNTER_GROUP_PC::COUNTABLE_2 = PERF_PC_STALL_CYCLES_VFD
          * PERFCOUNTER_GROUP_BV_PC::COUNTABLE_2 = PERF_PC_STALL_CYCLES_VFD
          * PERFCOUNTER_GROUP_RBBM::COUNTABLE_6 = PERF_RBBM_STATUS_MASKED
          */
         return percent(cbSum(PERF_PC_STALL_CYCLES_VFD), PERF_RBBM_STATUS_MASKED);
      }
   );

   counter("% LRZ Pixel Killed", Counter::Units::Percent, [=]() {
      return percent(PERF_LRZ_TOTAL_PIXEL - PERF_LRZ_VISIBLE_PIXEL_AFTER_LRZ,
                     PERF_LRZ_TOTAL_PIXEL);
   });

   counter("LRZ Primitives Killed", Counter::Units::None, [=]() {
      return PERF_LRZ_PRIM_KILLED_BY_LRZ;
   });

   counter("LRZ Tiles Killed", Counter::Units::None, [=]() {
      return PERF_LRZ_TILE_KILLED;
   });
}

} // namespace pps
