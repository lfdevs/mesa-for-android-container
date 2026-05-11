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
FreedrenoDriver::setup_a6xx_counters()
{
   /* TODO is there a reason to want more than one group? */
   CounterGroup group = {};
   group.name = "counters";
   groups.clear();
   counters.clear();
   countables.clear();
   enabled_counters.clear();
   groups.emplace_back(std::move(group));

   /*
    * Create the countables that we'll be using.
    */

   auto PERF_CP_ALWAYS_COUNT = countable("CP", "PERF_CP_ALWAYS_COUNT");
   auto PERF_CP_BUSY_CYCLES  = countable("CP", "PERF_CP_BUSY_CYCLES");
   auto PERF_RB_3D_PIXELS    = countable("RB", "PERF_RB_3D_PIXELS");
   auto PERF_TP_L1_CACHELINE_MISSES = countable("TP", "PERF_TP_L1_CACHELINE_MISSES");
   auto PERF_TP_L1_CACHELINE_REQUESTS = countable("TP", "PERF_TP_L1_CACHELINE_REQUESTS");

   auto PERF_TP_OUTPUT_PIXELS  = countable("TP", "PERF_TP_OUTPUT_PIXELS");
   auto PERF_TP_OUTPUT_PIXELS_ANISO  = countable("TP", "PERF_TP_OUTPUT_PIXELS_ANISO");
   auto PERF_TP_OUTPUT_PIXELS_BILINEAR = countable("TP", "PERF_TP_OUTPUT_PIXELS_BILINEAR");
   auto PERF_TP_OUTPUT_PIXELS_POINT = countable("TP", "PERF_TP_OUTPUT_PIXELS_POINT");
   auto PERF_TP_OUTPUT_PIXELS_ZERO_LOD = countable("TP", "PERF_TP_OUTPUT_PIXELS_ZERO_LOD");

   auto PERF_TSE_INPUT_PRIM  = countable("TSE", "PERF_TSE_INPUT_PRIM");
   auto PERF_TSE_CLIPPED_PRIM  = countable("TSE", "PERF_TSE_CLIPPED_PRIM");
   auto PERF_TSE_TRIVAL_REJ_PRIM  = countable("TSE", "PERF_TSE_TRIVAL_REJ_PRIM");
   auto PERF_TSE_OUTPUT_VISIBLE_PRIM = countable("TSE", "PERF_TSE_OUTPUT_VISIBLE_PRIM");

   auto PERF_SP_BUSY_CYCLES  = countable("SP", "PERF_SP_BUSY_CYCLES");
   auto PERF_SP_ALU_WORKING_CYCLES = countable("SP", "PERF_SP_ALU_WORKING_CYCLES");
   auto PERF_SP_EFU_WORKING_CYCLES = countable("SP", "PERF_SP_EFU_WORKING_CYCLES");
   auto PERF_SP_VS_STAGE_EFU_INSTRUCTIONS = countable("SP", "PERF_SP_VS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_VS_STAGE_TEX_INSTRUCTIONS = countable("SP", "PERF_SP_VS_STAGE_TEX_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_EFU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_EFU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS = countable("SP", "PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS");
   auto PERF_SP_STALL_CYCLES_TP = countable("SP", "PERF_SP_STALL_CYCLES_TP");
   auto PERF_SP_ANY_EU_WORKING_FS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_FS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_VS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_VS_STAGE");
   auto PERF_SP_ANY_EU_WORKING_CS_STAGE = countable("SP", "PERF_SP_ANY_EU_WORKING_CS_STAGE");

   auto PERF_UCHE_STALL_CYCLES_ARBITER = countable("UCHE", "PERF_UCHE_STALL_CYCLES_ARBITER");
   auto PERF_UCHE_VBIF_READ_BEATS_TP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_TP");
   auto PERF_UCHE_VBIF_READ_BEATS_VFD = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_VFD");
   auto PERF_UCHE_VBIF_READ_BEATS_SP = countable("UCHE", "PERF_UCHE_VBIF_READ_BEATS_SP");
   auto PERF_UCHE_READ_REQUESTS_TP = countable("UCHE", "PERF_UCHE_READ_REQUESTS_TP");

   auto PERF_PC_STALL_CYCLES_VFD = countable("PC", "PERF_PC_STALL_CYCLES_VFD");
   auto PERF_PC_VS_INVOCATIONS = countable("PC", "PERF_PC_VS_INVOCATIONS");
   auto PERF_PC_VERTEX_HITS = countable("PC", "PERF_PC_VERTEX_HITS");

   auto PERF_HLSQ_QUADS = countable("HLSQ", "PERF_HLSQ_QUADS"); /* Quads (fragments / 4) produced */

   auto PERF_CP_NUM_PREEMPTIONS = countable("CP", "PERF_CP_NUM_PREEMPTIONS");
   auto PERF_CP_PREEMPTION_REACTION_DELAY = countable("CP", "PERF_CP_PREEMPTION_REACTION_DELAY");

   /* LRZ: 4/4 counters */
   auto PERF_LRZ_TOTAL_PIXEL = countable("LRZ", "PERF_LRZ_TOTAL_PIXEL");
   auto PERF_LRZ_VISIBLE_PIXEL_AFTER_LRZ = countable("LRZ", "PERF_LRZ_VISIBLE_PIXEL_AFTER_LRZ");
   auto PERF_LRZ_TILE_KILLED = countable("LRZ", "PERF_LRZ_TILE_KILLED");
   auto PERF_LRZ_PRIM_KILLED_BY_LRZ = countable("LRZ", "PERF_LRZ_PRIM_KILLED_BY_LRZ");

   /* TODO: resolve() tells there is no PERF_CMPDECMP_VBIF_READ_DATA */
   // auto PERF_CMPDECMP_VBIF_READ_DATA = countable("PERF_CMPDECMP_VBIF_READ_DATA");

   /*
    * And then setup the derived counters that we are exporting to
    * pps based on the captured countable values.
    *
    * We try to expose the same counters as blob:
    * https://gpuinspector.dev/docs/gpu-counters/qualcomm
    */

   counter("GPU Frequency", Counter::Units::Hertz, [=]() {
         return PERF_CP_ALWAYS_COUNT / time;
      }
   );

   counter("GPU % Utilization", Counter::Units::Percent, [=]() {
         return percent(PERF_CP_BUSY_CYCLES / time, max_freq);
      }
   );

   counter("TP L1 Cache Misses", Counter::Units::None, [=]() {
         return PERF_TP_L1_CACHELINE_MISSES / time;
      }
   );

   counter("Shader Core Utilization", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_BUSY_CYCLES / time, max_freq * info->num_sp_cores);
      }
   );

   /* TODO: verify */
   counter("(?) % Texture Fetch Stall", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_STALL_CYCLES_TP / time, max_freq * info->num_sp_cores);
      }
   );

   /* TODO: verify */
   counter("(?) % Vertex Fetch Stall", Counter::Units::Percent, [=]() {
         return percent(PERF_PC_STALL_CYCLES_VFD / time, max_freq * info->num_sp_cores);
      }
   );

   counter("L1 Texture Cache Miss Per Pixel", Counter::Units::None, [=]() {
         return safe_div(PERF_TP_L1_CACHELINE_MISSES, PERF_HLSQ_QUADS * 4);
      }
   );

   counter("% Texture L1 Miss", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_L1_CACHELINE_MISSES, PERF_TP_L1_CACHELINE_REQUESTS);
      }
   );

   counter("% Texture L2 Miss", Counter::Units::Percent, [=]() {
         return percent(PERF_UCHE_VBIF_READ_BEATS_TP / 2, PERF_UCHE_READ_REQUESTS_TP);
      }
   );

   /* TODO: verify */
   counter("(?) % Stalled on System Memory", Counter::Units::Percent, [=]() {
         return percent(PERF_UCHE_STALL_CYCLES_ARBITER / time, max_freq * info->num_sp_cores);
      }
   );

   counter("Pre-clipped Polygons / Second", Counter::Units::None, [=]() {
         return PERF_TSE_INPUT_PRIM * (1.f / time);
      }
   );

   counter("% Prims Trivially Rejected", Counter::Units::Percent, [=]() {
         return percent(PERF_TSE_TRIVAL_REJ_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );

   counter("% Prims Clipped", Counter::Units::Percent, [=]() {
         return percent(PERF_TSE_CLIPPED_PRIM, PERF_TSE_INPUT_PRIM);
      }
   );

   counter("Average Vertices / Polygon", Counter::Units::None, [=]() {
         return PERF_PC_VS_INVOCATIONS / PERF_TSE_INPUT_PRIM;
      }
   );

   counter("Reused Vertices / Second", Counter::Units::None, [=]() {
         return PERF_PC_VERTEX_HITS * (1.f / time);
      }
   );

   counter("Average Polygon Area", Counter::Units::None, [=]() {
         return safe_div(PERF_HLSQ_QUADS * 4, PERF_TSE_OUTPUT_VISIBLE_PRIM);
      }
   );

   /* TODO: find formula */
   // counter("% Shaders Busy", Counter::Units::Percent, [=]() {
   //       return 100.0 * 0;
   //    }
   // );

   counter("Vertices Shaded / Second", Counter::Units::None, [=]() {
         return PERF_PC_VS_INVOCATIONS * (1.f / time);
      }
   );

   counter("Fragments Shaded / Second", Counter::Units::None, [=]() {
         return PERF_HLSQ_QUADS * 4 * (1.f / time);
      }
   );

   counter("Vertex Instructions / Second", Counter::Units::None, [=]() {
         return (PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS +
                 PERF_SP_VS_STAGE_EFU_INSTRUCTIONS) * (1.f / time);
      }
   );

   counter("Fragment Instructions / Second", Counter::Units::None, [=]() {
         return (PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
                 PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2 +
                 PERF_SP_FS_STAGE_EFU_INSTRUCTIONS) * (1.f / time);
      }
   );

   counter("Fragment ALU Instructions / Sec (Full)", Counter::Units::None, [=]() {
         return PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS * (1.f / time);
      }
   );

   counter("Fragment ALU Instructions / Sec (Half)", Counter::Units::None, [=]() {
         return PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS * (1.f / time);
      }
   );

   counter("Fragment EFU Instructions / Second", Counter::Units::None, [=]() {
         return PERF_SP_FS_STAGE_EFU_INSTRUCTIONS * (1.f / time);
      }
   );

   counter("Textures / Vertex", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_VS_STAGE_TEX_INSTRUCTIONS, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("Textures / Fragment", Counter::Units::None, [=]() {
         return safe_div(PERF_TP_OUTPUT_PIXELS, PERF_HLSQ_QUADS * 4);
      }
   );

   counter("ALU / Vertex", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("EFU / Vertex", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_VS_STAGE_EFU_INSTRUCTIONS, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("ALU / Fragment", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
                         PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2, PERF_HLSQ_QUADS);
      }
   );

   counter("EFU / Fragment", Counter::Units::None, [=]() {
         return safe_div(PERF_SP_FS_STAGE_EFU_INSTRUCTIONS, PERF_HLSQ_QUADS);
      }
   );

   counter("% Time Shading Vertices", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ANY_EU_WORKING_VS_STAGE,
                        (PERF_SP_ANY_EU_WORKING_VS_STAGE +
                         PERF_SP_ANY_EU_WORKING_FS_STAGE +
                         PERF_SP_ANY_EU_WORKING_CS_STAGE));
      }
   );

   counter("% Time Shading Fragments", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ANY_EU_WORKING_FS_STAGE,
                        (PERF_SP_ANY_EU_WORKING_VS_STAGE +
                         PERF_SP_ANY_EU_WORKING_FS_STAGE +
                         PERF_SP_ANY_EU_WORKING_CS_STAGE));
      }
   );

   counter("% Time Compute", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ANY_EU_WORKING_CS_STAGE,
                        (PERF_SP_ANY_EU_WORKING_VS_STAGE +
                         PERF_SP_ANY_EU_WORKING_FS_STAGE +
                         PERF_SP_ANY_EU_WORKING_CS_STAGE));
      }
   );

   counter("% Shader ALU Capacity Utilized", Counter::Units::Percent, [=]() {
         return percent((PERF_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS +
                         PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
                         PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2) / 64,
                        PERF_SP_BUSY_CYCLES);
      }
   );

   counter("% Time ALUs Working", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_ALU_WORKING_CYCLES / 2, PERF_SP_BUSY_CYCLES);
      }
   );

   counter("% Time EFUs Working", Counter::Units::Percent, [=]() {
         return percent(PERF_SP_EFU_WORKING_CYCLES / 2, PERF_SP_BUSY_CYCLES);
      }
   );

   counter("% Anisotropic Filtered", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_ANISO, PERF_TP_OUTPUT_PIXELS);
      }
   );

   counter("% Linear Filtered", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_BILINEAR, PERF_TP_OUTPUT_PIXELS);
      }
   );

   counter("% Nearest Filtered", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_POINT, PERF_TP_OUTPUT_PIXELS);
      }
   );

   counter("% Non-Base Level Textures", Counter::Units::Percent, [=]() {
         return percent(PERF_TP_OUTPUT_PIXELS_ZERO_LOD, PERF_TP_OUTPUT_PIXELS);
      }
   );

   /* Reads from KGSL_PERFCOUNTER_GROUP_VBIF countable=63 */
   // counter("Read Total (Bytes/sec)", Counter::Units::Byte, [=]() {
   //       return  * (1.f / time);
   //    }
   // );

   /* Reads from KGSL_PERFCOUNTER_GROUP_VBIF countable=84 */
   // counter("Write Total (Bytes/sec)", Counter::Units::Byte, [=]() {
   //       return  * (1.f / time);
   //    }
   // );

   /* Cannot get PERF_CMPDECMP_VBIF_READ_DATA countable */
   // counter("Texture Memory Read BW (Bytes/Second)", Counter::Units::Byte, [=]() {
   //       return (PERF_CMPDECMP_VBIF_READ_DATA + PERF_UCHE_VBIF_READ_BEATS_TP) * (1.f / time);
   //    }
   // );

   /* TODO: verify */
   counter("(?) Vertex Memory Read (Bytes/Second)", Counter::Units::Byte, [=]() {
         return PERF_UCHE_VBIF_READ_BEATS_VFD * 32 * (1.f / time);
      }
   );

   /* TODO: verify */
   counter("SP Memory Read (Bytes/Second)", Counter::Units::Byte, [=]() {
         return PERF_UCHE_VBIF_READ_BEATS_SP * 32 * (1.f / time);
      }
   );

   counter("Avg Bytes / Fragment", Counter::Units::Byte, [=]() {
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_TP * 32, PERF_HLSQ_QUADS * 4);
      }
   );

   counter("Avg Bytes / Vertex", Counter::Units::Byte, [=]() {
         return safe_div(PERF_UCHE_VBIF_READ_BEATS_VFD * 32, PERF_PC_VS_INVOCATIONS);
      }
   );

   counter("Preemptions / second", Counter::Units::None, [=]() {
         return PERF_CP_NUM_PREEMPTIONS * (1.f / time);
      }
   );

   counter("Avg Preemption Delay", Counter::Units::None, [=]() {
         return PERF_CP_PREEMPTION_REACTION_DELAY * (1.f / time);
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
