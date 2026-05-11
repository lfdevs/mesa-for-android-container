/*
 * Copyright © 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "util/u_math.h"

#include "fd6_hw.h"

#include "freedreno_perfcntr.h"
#include "a7xx_perfcntrs.json.h"

enum {
   DERIVED_COUNTER_PERFCNTR_CP_ALWAYS_COUNT,
   DERIVED_COUNTER_PERFCNTR_CP_NUM_PREEMPTIONS,
   DERIVED_COUNTER_PERFCNTR_CP_PREEMPTION_REACTION_DELAY,

   DERIVED_COUNTER_PERFCNTR_RBBM_STATUS_MASKED,

   DERIVED_COUNTER_PERFCNTR_PC_STALL_CYCLES_VFD,
   DERIVED_COUNTER_PERFCNTR_PC_VERTEX_HITS,
   DERIVED_COUNTER_PERFCNTR_PC_VS_INVOCATIONS,

   DERIVED_COUNTER_PERFCNTR_TSE_INPUT_PRIM,
   DERIVED_COUNTER_PERFCNTR_TSE_TRIVAL_REJ_PRIM,
   DERIVED_COUNTER_PERFCNTR_TSE_CLIPPED_PRIM,
   DERIVED_COUNTER_PERFCNTR_TSE_OUTPUT_VISIBLE_PRIM,

   DERIVED_COUNTER_PERFCNTR_UCHE_STALL_CYCLES_ARBITER,
   DERIVED_COUNTER_PERFCNTR_UCHE_VBIF_READ_BEATS_TP,
   DERIVED_COUNTER_PERFCNTR_UCHE_VBIF_READ_BEATS_VFD,
   DERIVED_COUNTER_PERFCNTR_UCHE_VBIF_READ_BEATS_SP,
   DERIVED_COUNTER_PERFCNTR_UCHE_READ_REQUESTS_TP,

   DERIVED_COUNTER_PERFCNTR_TP_BUSY_CYCLES,
   DERIVED_COUNTER_PERFCNTR_TP_L1_CACHELINE_REQUESTS,
   DERIVED_COUNTER_PERFCNTR_TP_L1_CACHELINE_MISSES,
   DERIVED_COUNTER_PERFCNTR_TP_OUTPUT_PIXELS,
   DERIVED_COUNTER_PERFCNTR_TP_OUTPUT_PIXELS_POINT,
   DERIVED_COUNTER_PERFCNTR_TP_OUTPUT_PIXELS_BILINEAR,
   DERIVED_COUNTER_PERFCNTR_TP_OUTPUT_PIXELS_ANISO,

   DERIVED_COUNTER_PERFCNTR_SP_BUSY_CYCLES,
   DERIVED_COUNTER_PERFCNTR_SP_ALU_WORKING_CYCLES,
   DERIVED_COUNTER_PERFCNTR_SP_EFU_WORKING_CYCLES,
   DERIVED_COUNTER_PERFCNTR_SP_STALL_CYCLES_TP,
   DERIVED_COUNTER_PERFCNTR_SP_NON_EXECUTION_CYCLES,
   DERIVED_COUNTER_PERFCNTR_SP_VS_STAGE_TEX_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_VS_STAGE_EFU_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_FS_STAGE_EFU_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_ICL1_REQUESTS,
   DERIVED_COUNTER_PERFCNTR_SP_ICL1_MISSES,
   DERIVED_COUNTER_PERFCNTR_SP_ANY_EU_WORKING_FS_STAGE,
   DERIVED_COUNTER_PERFCNTR_SP_ANY_EU_WORKING_VS_STAGE,
   DERIVED_COUNTER_PERFCNTR_SP_ANY_EU_WORKING_CS_STAGE,
   DERIVED_COUNTER_PERFCNTR_SP_PIXELS,
   DERIVED_COUNTER_PERFCNTR_SP_RAY_QUERY_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_RTU_BUSY_CYCLES,
   DERIVED_COUNTER_PERFCNTR_SP_RTU_BVH_FETCH_LATENCY_CYCLES,
   DERIVED_COUNTER_PERFCNTR_SP_RTU_BVH_FETCH_LATENCY_SAMPLES,
   DERIVED_COUNTER_PERFCNTR_SP_RTU_RAY_BOX_INTERSECTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_RTU_RAY_TRIANGLE_INTERSECTIONS,
   DERIVED_COUNTER_PERFCNTR_SP_SCH_STALL_CYCLES_RTU,

   DERIVED_COUNTER_PERFCNTR_CMPDECMP_VBIF_READ_DATA,

   DERIVED_COUNTER_PERFCNTR_BV_PC_STALL_CYCLES_VFD,
   DERIVED_COUNTER_PERFCNTR_BV_PC_VERTEX_HITS,
   DERIVED_COUNTER_PERFCNTR_BV_PC_VS_INVOCATIONS,

   DERIVED_COUNTER_PERFCNTR_BV_TP_L1_CACHELINE_REQUESTS,
   DERIVED_COUNTER_PERFCNTR_BV_TP_L1_CACHELINE_MISSES,
   DERIVED_COUNTER_PERFCNTR_BV_TP_OUTPUT_PIXELS,
   DERIVED_COUNTER_PERFCNTR_BV_TP_OUTPUT_PIXELS_POINT,
   DERIVED_COUNTER_PERFCNTR_BV_TP_OUTPUT_PIXELS_BILINEAR,
   DERIVED_COUNTER_PERFCNTR_BV_TP_OUTPUT_PIXELS_ANISO,

   DERIVED_COUNTER_PERFCNTR_BV_SP_STALL_CYCLES_TP,
   DERIVED_COUNTER_PERFCNTR_BV_SP_VS_STAGE_TEX_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_BV_SP_VS_STAGE_EFU_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_BV_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS,
   DERIVED_COUNTER_PERFCNTR_BV_SP_ICL1_REQUESTS,
   DERIVED_COUNTER_PERFCNTR_BV_SP_ICL1_MISSES,
   DERIVED_COUNTER_PERFCNTR_BV_SP_ANY_EU_WORKING_FS_STAGE,
   DERIVED_COUNTER_PERFCNTR_BV_SP_ANY_EU_WORKING_VS_STAGE,

   DERIVED_COUNTER_PERFCNTR_LRZ_TOTAL_PIXEL,
   DERIVED_COUNTER_PERFCNTR_LRZ_VISIBLE_PIXEL_AFTER_LRZ,
   DERIVED_COUNTER_PERFCNTR_LRZ_PRIM_KILLED_BY_LRZ,
   DERIVED_COUNTER_PERFCNTR_LRZ_TILE_KILLED,

   DERIVED_COUNTER_PERFCNTR_MAX_VALUE,
};

static_assert(DERIVED_COUNTER_PERFCNTR_MAX_VALUE <= FD_DERIVED_COUNTER_COLLECTION_MAX_ENABLED_PERFCNTRS, "");

#define DERIVED_COUNTER_PERFCNTR(_enum, _counter) \
   [DERIVED_COUNTER_PERFCNTR_##_enum] = { .counter = _counter, .countable = A7XX_PERF_##_enum }
#define DERIVED_COUNTER_PERFCNTR_BV(_enum, _counter) \
   [DERIVED_COUNTER_PERFCNTR_BV_##_enum] = { .counter = _counter, .countable = A7XX_PERF_##_enum }

static const struct {
   const struct fd_perfcntr_counter *counter;
   unsigned countable;
} a7xx_derived_counter_perfcntrs[] = {
   /* CP: 3/14 counters */
   DERIVED_COUNTER_PERFCNTR(CP_ALWAYS_COUNT,              &cp_counters[0]),
   DERIVED_COUNTER_PERFCNTR(CP_NUM_PREEMPTIONS,           &cp_counters[1]),
   DERIVED_COUNTER_PERFCNTR(CP_PREEMPTION_REACTION_DELAY, &cp_counters[2]),

   /* RBBM: 1/4 counters */
   DERIVED_COUNTER_PERFCNTR(RBBM_STATUS_MASKED, &rbbm_counters[0]),

   /* PC: 3/8 counters */
   DERIVED_COUNTER_PERFCNTR(PC_STALL_CYCLES_VFD, &pc_counters[0]),
   DERIVED_COUNTER_PERFCNTR(PC_VERTEX_HITS,      &pc_counters[1]),
   DERIVED_COUNTER_PERFCNTR(PC_VS_INVOCATIONS,   &pc_counters[2]),

   /* TSE: 4/4 counters */
   DERIVED_COUNTER_PERFCNTR(TSE_INPUT_PRIM,          &tse_counters[0]),
   DERIVED_COUNTER_PERFCNTR(TSE_TRIVAL_REJ_PRIM,     &tse_counters[1]),
   DERIVED_COUNTER_PERFCNTR(TSE_CLIPPED_PRIM,        &tse_counters[2]),
   DERIVED_COUNTER_PERFCNTR(TSE_OUTPUT_VISIBLE_PRIM, &tse_counters[3]),

   /* UCHE: 5/12 counters */
   DERIVED_COUNTER_PERFCNTR(UCHE_STALL_CYCLES_ARBITER, &uche_counters[0]),
   DERIVED_COUNTER_PERFCNTR(UCHE_VBIF_READ_BEATS_TP,   &uche_counters[1]),
   DERIVED_COUNTER_PERFCNTR(UCHE_VBIF_READ_BEATS_VFD,  &uche_counters[2]),
   DERIVED_COUNTER_PERFCNTR(UCHE_VBIF_READ_BEATS_SP,   &uche_counters[3]),
   DERIVED_COUNTER_PERFCNTR(UCHE_READ_REQUESTS_TP,     &uche_counters[4]),

   /* TP: 7/12 counters */
   DERIVED_COUNTER_PERFCNTR(TP_BUSY_CYCLES,            &tp_counters[0]),
   DERIVED_COUNTER_PERFCNTR(TP_L1_CACHELINE_REQUESTS,  &tp_counters[1]),
   DERIVED_COUNTER_PERFCNTR(TP_L1_CACHELINE_MISSES,    &tp_counters[2]),
   DERIVED_COUNTER_PERFCNTR(TP_OUTPUT_PIXELS,          &tp_counters[3]),
   DERIVED_COUNTER_PERFCNTR(TP_OUTPUT_PIXELS_POINT,    &tp_counters[4]),
   DERIVED_COUNTER_PERFCNTR(TP_OUTPUT_PIXELS_BILINEAR, &tp_counters[5]),
   DERIVED_COUNTER_PERFCNTR(TP_OUTPUT_PIXELS_ANISO,    &tp_counters[6]),

   /* SP: 24/24 counters */
   DERIVED_COUNTER_PERFCNTR(SP_BUSY_CYCLES,                    &sp_counters[ 0]),
   DERIVED_COUNTER_PERFCNTR(SP_ALU_WORKING_CYCLES,             &sp_counters[ 1]),
   DERIVED_COUNTER_PERFCNTR(SP_EFU_WORKING_CYCLES,             &sp_counters[ 2]),
   DERIVED_COUNTER_PERFCNTR(SP_STALL_CYCLES_TP,                &sp_counters[ 3]),
   DERIVED_COUNTER_PERFCNTR(SP_NON_EXECUTION_CYCLES,           &sp_counters[ 4]),
   DERIVED_COUNTER_PERFCNTR(SP_VS_STAGE_TEX_INSTRUCTIONS,      &sp_counters[ 5]),
   DERIVED_COUNTER_PERFCNTR(SP_VS_STAGE_EFU_INSTRUCTIONS,      &sp_counters[ 6]),
   DERIVED_COUNTER_PERFCNTR(SP_VS_STAGE_FULL_ALU_INSTRUCTIONS, &sp_counters[ 7]),
   DERIVED_COUNTER_PERFCNTR(SP_FS_STAGE_EFU_INSTRUCTIONS,      &sp_counters[ 8]),
   DERIVED_COUNTER_PERFCNTR(SP_FS_STAGE_FULL_ALU_INSTRUCTIONS, &sp_counters[ 9]),
   DERIVED_COUNTER_PERFCNTR(SP_FS_STAGE_HALF_ALU_INSTRUCTIONS, &sp_counters[10]),
   DERIVED_COUNTER_PERFCNTR(SP_ICL1_REQUESTS,                  &sp_counters[11]),
   DERIVED_COUNTER_PERFCNTR(SP_ICL1_MISSES,                    &sp_counters[12]),
   DERIVED_COUNTER_PERFCNTR(SP_ANY_EU_WORKING_FS_STAGE,        &sp_counters[13]),
   DERIVED_COUNTER_PERFCNTR(SP_ANY_EU_WORKING_VS_STAGE,        &sp_counters[14]),
   DERIVED_COUNTER_PERFCNTR(SP_ANY_EU_WORKING_CS_STAGE,        &sp_counters[15]),
   DERIVED_COUNTER_PERFCNTR(SP_PIXELS,                         &sp_counters[16]),
   DERIVED_COUNTER_PERFCNTR(SP_RAY_QUERY_INSTRUCTIONS,         &sp_counters[17]),
   DERIVED_COUNTER_PERFCNTR(SP_RTU_BUSY_CYCLES,                &sp_counters[18]),
   DERIVED_COUNTER_PERFCNTR(SP_RTU_BVH_FETCH_LATENCY_CYCLES,   &sp_counters[19]),
   DERIVED_COUNTER_PERFCNTR(SP_RTU_BVH_FETCH_LATENCY_SAMPLES,  &sp_counters[20]),
   DERIVED_COUNTER_PERFCNTR(SP_RTU_RAY_BOX_INTERSECTIONS,      &sp_counters[21]),
   DERIVED_COUNTER_PERFCNTR(SP_RTU_RAY_TRIANGLE_INTERSECTIONS, &sp_counters[22]),
   DERIVED_COUNTER_PERFCNTR(SP_SCH_STALL_CYCLES_RTU,           &sp_counters[23]),

   /* CMP: 1/4 counters */
   DERIVED_COUNTER_PERFCNTR(CMPDECMP_VBIF_READ_DATA, &cmp_counters[0]),

   /* BV_PC: 3/8 counters */
   DERIVED_COUNTER_PERFCNTR_BV(PC_STALL_CYCLES_VFD, &bv_pc_counters[0]),
   DERIVED_COUNTER_PERFCNTR_BV(PC_VERTEX_HITS,      &bv_pc_counters[1]),
   DERIVED_COUNTER_PERFCNTR_BV(PC_VS_INVOCATIONS,   &bv_pc_counters[2]),

   /* BV_TP: 6/6 counters */
   DERIVED_COUNTER_PERFCNTR_BV(TP_L1_CACHELINE_REQUESTS,  &bv_tp_counters[0]),
   DERIVED_COUNTER_PERFCNTR_BV(TP_L1_CACHELINE_MISSES,    &bv_tp_counters[1]),
   DERIVED_COUNTER_PERFCNTR_BV(TP_OUTPUT_PIXELS,          &bv_tp_counters[2]),
   DERIVED_COUNTER_PERFCNTR_BV(TP_OUTPUT_PIXELS_POINT,    &bv_tp_counters[3]),
   DERIVED_COUNTER_PERFCNTR_BV(TP_OUTPUT_PIXELS_BILINEAR, &bv_tp_counters[4]),
   DERIVED_COUNTER_PERFCNTR_BV(TP_OUTPUT_PIXELS_ANISO,    &bv_tp_counters[5]),

   /* GP: 8/12 counters */
   DERIVED_COUNTER_PERFCNTR_BV(SP_STALL_CYCLES_TP,                &bv_sp_counters[0]),
   DERIVED_COUNTER_PERFCNTR_BV(SP_VS_STAGE_TEX_INSTRUCTIONS,      &bv_sp_counters[1]),
   DERIVED_COUNTER_PERFCNTR_BV(SP_VS_STAGE_EFU_INSTRUCTIONS,      &bv_sp_counters[2]),
   DERIVED_COUNTER_PERFCNTR_BV(SP_VS_STAGE_FULL_ALU_INSTRUCTIONS, &bv_sp_counters[3]),
   DERIVED_COUNTER_PERFCNTR_BV(SP_ICL1_REQUESTS,                  &bv_sp_counters[4]),
   DERIVED_COUNTER_PERFCNTR_BV(SP_ICL1_MISSES,                    &bv_sp_counters[5]),
   DERIVED_COUNTER_PERFCNTR_BV(SP_ANY_EU_WORKING_FS_STAGE,        &bv_sp_counters[6]),
   DERIVED_COUNTER_PERFCNTR_BV(SP_ANY_EU_WORKING_VS_STAGE,        &bv_sp_counters[7]),

   /* LRZ: 4/4 counters */
   DERIVED_COUNTER_PERFCNTR(LRZ_TOTAL_PIXEL, &lrz_counters[0]),
   DERIVED_COUNTER_PERFCNTR(LRZ_VISIBLE_PIXEL_AFTER_LRZ, &lrz_counters[1]),
   DERIVED_COUNTER_PERFCNTR(LRZ_TILE_KILLED, &lrz_counters[2]),
   DERIVED_COUNTER_PERFCNTR(LRZ_PRIM_KILLED_BY_LRZ, &lrz_counters[3]),
};

static uint64_t
safe_div(uint64_t a, uint64_t b)
{
   double value = 0.0;
   if (b)
      value = a / (double) b;

   union {
      double d;
      uint64_t u;
   } v;
   v.d = value;
   return v.u;
}

static uint64_t
percent(uint64_t a, uint64_t b)
{
   float value = 0;
   if (b)
      value = (a / (float) b) * 100.0f;

   union {
      float f;
      uint32_t u;
   } v;
   v.f = value;
   return (uint64_t )v.u & 0xffffffff;
}

#define DERIVED_COUNTER_CATEGORY_GPU_GENERAL "GPU General"
#define DERIVED_COUNTER_CATEGORY_GPU_MEMORY_STATS "GPU Memory Stats"
#define DERIVED_COUNTER_CATEGORY_GPU_PREEMPTION "GPU Preemption"
#define DERIVED_COUNTER_CATEGORY_GPU_PRIMITIVE_PROCESSING "GPU Primitive Processing"
#define DERIVED_COUNTER_CATEGORY_GPU_SHADER_PROCESSING "GPU Shader Processing"
#define DERIVED_COUNTER_CATEGORY_GPU_STALLS "GPU Stalls"
#define DERIVED_COUNTER_CATEGORY_GPU_LRZ "GPU LRZ"

#define DERIVED_COUNTER_PERFCNTRS_COUNT_IMPL(_0, _1, _2, _3, _4, _5, _6, _7, N, ...) N
#define DERIVED_COUNTER_PERFCNTRS_COUNT(...) DERIVED_COUNTER_PERFCNTRS_COUNT_IMPL(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define DERIVED_COUNTER_PERFCNTRS(...) __VA_ARGS__

#define PERFCNTR_VALUE_0(OP, _0, ...) OP(0, _0)
#define PERFCNTR_VALUE_1(OP, _0, _1, ...) PERFCNTR_VALUE_0(OP, _0) OP(1, _1)
#define PERFCNTR_VALUE_2(OP, _0, _1, _2, ...) PERFCNTR_VALUE_1(OP, _0, _1) OP(2, _2)
#define PERFCNTR_VALUE_3(OP, _0, _1, _2, _3, ...) PERFCNTR_VALUE_2(OP, _0, _1, _2) OP(3, _3)
#define PERFCNTR_VALUE_4(OP, _0, _1, _2, _3, _4, ...) PERFCNTR_VALUE_3(OP, _0, _1, _2, _3) OP(4, _4)
#define PERFCNTR_HANDLE_VALUES(OP, _0, _1, _2, _3, _4, N, ...) PERFCNTR_VALUE_##N(OP, _0, _1, _2, _3, _4)
#define PERFCNTR_OP_LIST(_index, _name) DERIVED_COUNTER_PERFCNTR_##_name,
#define PERFCNTR_OP_DECLARE(_index, _name) uint64_t _name = values[_index];

#define DERIVED_COUNTER_PERFCNTR_LIST_VALUES(...) PERFCNTR_HANDLE_VALUES(PERFCNTR_OP_LIST, __VA_ARGS__, 4, 3, 2, 1, 0)
#define DERIVED_COUNTER_PERFCNTR_DECLARE_VALUES(...) PERFCNTR_HANDLE_VALUES(PERFCNTR_OP_DECLARE, __VA_ARGS__, 4, 3, 2, 1, 0)

#define DERIVED_COUNTER(_impl_name, _name, _description, _category, _type, _perfcntrs, _derivation) \
   static uint64_t a7xx_derived_counter_##_impl_name##_derive(struct fd_derivation_context *context, uint64_t *values) {\
      DERIVED_COUNTER_PERFCNTR_DECLARE_VALUES(_perfcntrs) \
      _derivation \
   } \
   const struct fd_derived_counter a7xx_derived_counter_##_impl_name = { \
      .name = _name, .description = _description, \
      .category = DERIVED_COUNTER_CATEGORY_##_category, \
      .type = FD_PERFCNTR_TYPE_##_type, \
      .num_perfcntrs = DERIVED_COUNTER_PERFCNTRS_COUNT(_perfcntrs), \
      .perfcntrs = { DERIVED_COUNTER_PERFCNTR_LIST_VALUES(_perfcntrs) }, \
      .derive = a7xx_derived_counter_##_impl_name##_derive, \
   }
#define DERIVED_COUNTER_PTR(_impl_name) &a7xx_derived_counter_##_impl_name

DERIVED_COUNTER(clocks, "Clocks", "Number of GPU clocks", GPU_GENERAL, UINT64,
   DERIVED_COUNTER_PERFCNTRS(CP_ALWAYS_COUNT),
   {
      return CP_ALWAYS_COUNT;
   });

DERIVED_COUNTER(avg_bytes_per_fragment,
   "Avg Bytes / Fragment",
   "Average number of bytes transferred from main memory for each fragment",
   GPU_MEMORY_STATS, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(UCHE_VBIF_READ_BEATS_TP,
                             SP_PIXELS),
   {
      return safe_div(UCHE_VBIF_READ_BEATS_TP * 32, SP_PIXELS);
   });

DERIVED_COUNTER(avg_bytes_per_vertex,
   "Avg Bytes / Vertex",
   "Average number of bytes transferred from main memory for each vertex",
   GPU_MEMORY_STATS, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(UCHE_VBIF_READ_BEATS_VFD,
                             PC_VS_INVOCATIONS,
                             BV_PC_VS_INVOCATIONS),
   {
      return safe_div(UCHE_VBIF_READ_BEATS_VFD * 32,
                      PC_VS_INVOCATIONS + BV_PC_VS_INVOCATIONS);
   });

DERIVED_COUNTER(sp_memory_read,
   "SP Memory Read (Bytes)",
   "Bytes of data read from memory by the Shader Processors",
   GPU_MEMORY_STATS, UINT64,
   DERIVED_COUNTER_PERFCNTRS(UCHE_VBIF_READ_BEATS_SP),
   {
      return UCHE_VBIF_READ_BEATS_SP * 32;
   });

DERIVED_COUNTER(texture_memory_read,
   "Texture Memory Read BW (Bytes)",
   "Bytes of texture data read from memory",
   GPU_MEMORY_STATS, UINT64,
   DERIVED_COUNTER_PERFCNTRS(UCHE_VBIF_READ_BEATS_TP,
                             CMPDECMP_VBIF_READ_DATA),
   {
      return (UCHE_VBIF_READ_BEATS_TP + CMPDECMP_VBIF_READ_DATA) * 32;
   });

DERIVED_COUNTER(vertex_memory_read,
   "Vertex Memory Read (Bytes)",
   "Bytes of vertex data read from memory",
   GPU_MEMORY_STATS, UINT64,
   DERIVED_COUNTER_PERFCNTRS(UCHE_VBIF_READ_BEATS_VFD),
   {
      return UCHE_VBIF_READ_BEATS_VFD * 32;
   });

/* FIXME: disabled due to lack of support for VBIF perfcounters which
          have a more complex way of being enabled.
DERIVED_COUNTER(read_total,
   "Read Total (Bytes)",
   "Total number of bytes read by the GPU from memory",
   GPU_MEMORY_STATS, UINT64,
   DERIVED_COUNTER_PERFCNTRS(...),
   {
      return 0;
   });

DERIVED_COUNTER(write_total,
   "Write Total (Bytes)",
   "Total number of bytes written by the GPU to memory",
   GPU_MEMORY_STATS, UINT64,
   DERIVED_COUNTER_PERFCNTRS(...),
   {
      return 0;
   });
*/

DERIVED_COUNTER(avg_preemption_delay,
   "Avg Preemption Delay",
   "Average number of cycles from the preemption request to preemption start",
   GPU_PREEMPTION, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(CP_PREEMPTION_REACTION_DELAY,
                             CP_NUM_PREEMPTIONS,
                             CP_ALWAYS_COUNT),
   {
      if (!CP_ALWAYS_COUNT || !CP_NUM_PREEMPTIONS)
         return 0;

      union {
         double d;
         uint64_t u;
      } v;

      double delay = CP_PREEMPTION_REACTION_DELAY / (double) CP_ALWAYS_COUNT;
      v.d = delay / (CP_NUM_PREEMPTIONS / 2);
      return v.u;
   });

DERIVED_COUNTER(preemptions,
   "Preemptions",
   "The number of GPU preemptions that occurred",
   GPU_PREEMPTION, UINT64,
   DERIVED_COUNTER_PERFCNTRS(CP_NUM_PREEMPTIONS),
   {
      return CP_NUM_PREEMPTIONS / 2;
   });

DERIVED_COUNTER(average_polygon_area,
   "Average Polygon Area",
   "Average number of pixels per polygon",
   GPU_PRIMITIVE_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(TSE_OUTPUT_VISIBLE_PRIM,
                             SP_PIXELS),
   {
      return safe_div(SP_PIXELS, TSE_OUTPUT_VISIBLE_PRIM);
   });

DERIVED_COUNTER(average_vertices_per_polygon,
   "Average Vertices / Polygon",
   "Average number of vertices per polygon",
   GPU_PRIMITIVE_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(PC_VS_INVOCATIONS,
                             BV_PC_VS_INVOCATIONS,
                             TSE_INPUT_PRIM),
   {
      return safe_div(PC_VS_INVOCATIONS + BV_PC_VS_INVOCATIONS, TSE_INPUT_PRIM);
   });

DERIVED_COUNTER(preclipped_polygon,
   "Pre-clipped Polygon",
   "Number of polygons submitted to the GPU before any hardware clipping",
   GPU_PRIMITIVE_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(TSE_INPUT_PRIM),
   {
      return TSE_INPUT_PRIM;
   });

DERIVED_COUNTER(percent_prims_clipped,
   "% Prims Clipped",
   "Percentage of primitives clipped by the GPU (where new primitives are generated)",
   GPU_PRIMITIVE_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(TSE_CLIPPED_PRIM,
                             TSE_INPUT_PRIM),
   {
      return percent(TSE_CLIPPED_PRIM, TSE_INPUT_PRIM);
   });

DERIVED_COUNTER(percent_prims_trivially_rejected,
   "% Prims Trivially Rejected",
   "Percentage of primitives that are trivially rejected",
   GPU_PRIMITIVE_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(TSE_TRIVAL_REJ_PRIM,
                             TSE_INPUT_PRIM),
   {
      return percent(TSE_TRIVAL_REJ_PRIM, TSE_INPUT_PRIM);
   });

DERIVED_COUNTER(reused_vertices,
   "Reused Vertices",
   "Number of vertices used from the post-transform vertex buffer cache",
   GPU_PRIMITIVE_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(PC_VERTEX_HITS,
                             BV_PC_VERTEX_HITS),
   {
      return PC_VERTEX_HITS + BV_PC_VERTEX_HITS;
   });

DERIVED_COUNTER(alu_per_fragment,
   "ALU / Fragment",
   "Average number of scalar fragment shader ALU instructions issued per shaded fragment, expressed as full precision ALUs (2 mediump = 1 fullp)",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(SP_FS_STAGE_FULL_ALU_INSTRUCTIONS,
                             SP_FS_STAGE_HALF_ALU_INSTRUCTIONS,
                             SP_PIXELS),
   {
      return safe_div(SP_FS_STAGE_FULL_ALU_INSTRUCTIONS + SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2,
                      SP_PIXELS);
   });

DERIVED_COUNTER(alu_per_vertex,
   "ALU / Vertex",
   "Average number of vertex scalar shader ALU instructions issued per shaded vertex",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(PC_VS_INVOCATIONS,
                             BV_PC_VS_INVOCATIONS,
                             SP_VS_STAGE_FULL_ALU_INSTRUCTIONS,
                             BV_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS),
   {
      return safe_div(SP_VS_STAGE_FULL_ALU_INSTRUCTIONS + BV_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS,
                      PC_VS_INVOCATIONS + BV_PC_VS_INVOCATIONS);
   });

DERIVED_COUNTER(percent_anisotropic_filtered,
   "% Anisotropic Filtered",
   "Percent of texels filtered using the 'Anisotropic' sampling method",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(TP_OUTPUT_PIXELS,
                             BV_TP_OUTPUT_PIXELS,
                             TP_OUTPUT_PIXELS_ANISO,
                             BV_TP_OUTPUT_PIXELS_ANISO),
   {
      return percent(TP_OUTPUT_PIXELS_ANISO + BV_TP_OUTPUT_PIXELS_ANISO,
                     TP_OUTPUT_PIXELS + BV_TP_OUTPUT_PIXELS);
   });

DERIVED_COUNTER(average_bvh_fetch_latency_cycles,
   "Average BVH Fetch Latency Cycles",
   "The Average BVH Fetch Latency cycles is the latency counted from start of BVH query request till getting BVH Query result back",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(SP_RTU_BVH_FETCH_LATENCY_CYCLES,
                             SP_RTU_BVH_FETCH_LATENCY_SAMPLES),
   {
      return safe_div(SP_RTU_BVH_FETCH_LATENCY_CYCLES, SP_RTU_BVH_FETCH_LATENCY_SAMPLES);
   });

DERIVED_COUNTER(efu_per_fragment,
   "EFU / Fragment",
   "Average number of scalar fragment shader EFU instructions issued per shaded fragment",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(SP_FS_STAGE_EFU_INSTRUCTIONS,
                             SP_PIXELS),
   {
      return safe_div(SP_FS_STAGE_EFU_INSTRUCTIONS, SP_PIXELS);
   });

DERIVED_COUNTER(efu_per_vertex,
   "EFU / Vertex",
   "Average number of scalar vertex shader EFU instructions issued per shaded vertex",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(PC_VS_INVOCATIONS,
                             BV_PC_VS_INVOCATIONS,
                             SP_VS_STAGE_EFU_INSTRUCTIONS,
                             BV_SP_VS_STAGE_EFU_INSTRUCTIONS),
   {
      return safe_div(SP_VS_STAGE_EFU_INSTRUCTIONS + BV_SP_VS_STAGE_EFU_INSTRUCTIONS,
                      PC_VS_INVOCATIONS + BV_PC_VS_INVOCATIONS);
   });

DERIVED_COUNTER(fragment_alu_instructions_full,
   "Fragment ALU Instructions (Full)",
   "Total number of full precision fragment shader instructions issued",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(SP_FS_STAGE_FULL_ALU_INSTRUCTIONS),
   {
      return SP_FS_STAGE_FULL_ALU_INSTRUCTIONS * 4;
   });

DERIVED_COUNTER(fragment_alu_instructions_half,
   "Fragment ALU Instructions (Half)",
   "Total number of half precision Scalar fragment shader instructions issued",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(SP_FS_STAGE_HALF_ALU_INSTRUCTIONS),
   {
      return SP_FS_STAGE_HALF_ALU_INSTRUCTIONS * 4;
   });

DERIVED_COUNTER(fragment_efu_instructions,
   "Fragment EFU Instructions",
   "Total number of Scalar fragment shader Elementary Function Unit (EFU) instructions issued",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(SP_FS_STAGE_EFU_INSTRUCTIONS),
   {
      return SP_FS_STAGE_EFU_INSTRUCTIONS * 4;
   });

DERIVED_COUNTER(fragment_instructions,
   "Fragment Instructions",
   "Total number of fragment shader instructions issued",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(SP_FS_STAGE_EFU_INSTRUCTIONS,
                             SP_FS_STAGE_FULL_ALU_INSTRUCTIONS,
                             SP_FS_STAGE_HALF_ALU_INSTRUCTIONS),
   {
      return (SP_FS_STAGE_EFU_INSTRUCTIONS + SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
              SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2) * 4;
   });

DERIVED_COUNTER(fragments_shaded,
   "Fragments Shaded",
   "Number of fragments submitted to the shader engine",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(SP_PIXELS),
   {
      return SP_PIXELS;
   });

DERIVED_COUNTER(percent_linear_filtered,
   "% Linear Filtered",
   "Percent of texels filtered using the 'Linear' sampling method",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(TP_OUTPUT_PIXELS,
                             BV_TP_OUTPUT_PIXELS,
                             TP_OUTPUT_PIXELS_BILINEAR,
                             BV_TP_OUTPUT_PIXELS_BILINEAR),
   {
      return percent(TP_OUTPUT_PIXELS_BILINEAR + BV_TP_OUTPUT_PIXELS_BILINEAR,
                     TP_OUTPUT_PIXELS + BV_TP_OUTPUT_PIXELS);
   });

DERIVED_COUNTER(percent_nearest_filtered,
   "% Nearest Filtered",
   "Percent of texels filtered using the 'Nearest' sampling method",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(TP_OUTPUT_PIXELS,
                             BV_TP_OUTPUT_PIXELS,
                             TP_OUTPUT_PIXELS_POINT,
                             BV_TP_OUTPUT_PIXELS_POINT),
   {
      return percent(TP_OUTPUT_PIXELS_POINT + BV_TP_OUTPUT_PIXELS_POINT,
                     TP_OUTPUT_PIXELS + BV_TP_OUTPUT_PIXELS);
   });

DERIVED_COUNTER(percent_rtu_busy,
   "% RTU Busy",
   "Percentage of time that Ray Tracing Unit in SP is busy compared to whole SP",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_RTU_BUSY_CYCLES,
                             SP_BUSY_CYCLES),
   {
      return percent(SP_RTU_BUSY_CYCLES, SP_BUSY_CYCLES);
   });

DERIVED_COUNTER(rtu_ray_box_intersections_per_instruction,
   "RTU Ray Box Intersections Per Instruction",
   "Number of Ray Box intersections per instruction",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(SP_RTU_RAY_BOX_INTERSECTIONS,
                             SP_RAY_QUERY_INSTRUCTIONS),
   {
      return safe_div(SP_RTU_RAY_BOX_INTERSECTIONS, SP_RAY_QUERY_INSTRUCTIONS);
   });

DERIVED_COUNTER(rtu_ray_triangle_intersections_per_instruction,
   "RTU Ray Triangle Intersections Per Instruction",
   "Number of Ray Triangle intersections per instruction",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(SP_RTU_RAY_TRIANGLE_INTERSECTIONS,
                             SP_RAY_QUERY_INSTRUCTIONS),
   {
      return safe_div(SP_RTU_RAY_TRIANGLE_INTERSECTIONS, SP_RAY_QUERY_INSTRUCTIONS);
   });


/* FIXME: disabled due to lack of TP counter capacity
DERIVED_COUNTER(percent_non_base_level_textures,
   "% Non-Base Level Textures",
   "Percent of texels coming from a non-base MIP level",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(...),
   {
      return 0;
   });
*/

DERIVED_COUNTER(percent_shader_alu_capacity_utilized,
   "% Shader ALU Capacity Utilized",
   "Percent of maximum shader capacity (ALU operations) utilized",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_BUSY_CYCLES,
                             SP_VS_STAGE_FULL_ALU_INSTRUCTIONS,
                             BV_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS,
                             SP_FS_STAGE_FULL_ALU_INSTRUCTIONS,
                             SP_FS_STAGE_HALF_ALU_INSTRUCTIONS),
   {
      return percent(SP_VS_STAGE_FULL_ALU_INSTRUCTIONS + BV_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS +
                     SP_FS_STAGE_FULL_ALU_INSTRUCTIONS + SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2,
                     SP_BUSY_CYCLES * context->a7xx.number_of_alus_per_usptp);
   });

DERIVED_COUNTER(percent_shaders_busy,
   "% Shaders Busy",
   "Percentage of time that all Shader cores are busy",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_BUSY_CYCLES,
                             TP_BUSY_CYCLES,
                             RBBM_STATUS_MASKED),
   {
      uint64_t numerator = SP_BUSY_CYCLES;
      if (!numerator)
         numerator = TP_BUSY_CYCLES;
      return percent(numerator, context->a7xx.number_of_usptp * RBBM_STATUS_MASKED);
   });

DERIVED_COUNTER(percent_shaders_stalled,
   "% Shaders Stalled",
   "Percentage of time that all shader cores are idle with at least one active wave",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_NON_EXECUTION_CYCLES,
                             RBBM_STATUS_MASKED),
   {
      return percent(SP_NON_EXECUTION_CYCLES, context->a7xx.number_of_usptp * RBBM_STATUS_MASKED);
   });

DERIVED_COUNTER(percent_texture_pipes_busy,
   "% Texture Pipes Busy",
   "Percentage of time that any texture pipe is busy",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(TP_BUSY_CYCLES,
                             RBBM_STATUS_MASKED),
   {
      return percent(TP_BUSY_CYCLES, context->a7xx.number_of_usptp * RBBM_STATUS_MASKED);
   });

DERIVED_COUNTER(textures_per_fragment,
   "Textures / Fragment",
   "Average number of textures referenced per fragment",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(SP_VS_STAGE_TEX_INSTRUCTIONS,
                             TP_OUTPUT_PIXELS,
                             SP_PIXELS),
   {
      /* FIXME: SP_VS_STAGE_TEX_INSTRUCTIONS seems to be unused. */
      (void)SP_VS_STAGE_TEX_INSTRUCTIONS;

      return safe_div(TP_OUTPUT_PIXELS, SP_PIXELS);
   });

DERIVED_COUNTER(textures_per_vertex,
   "Textures / Vertex",
   "Average number of textures referenced per vertex",
   GPU_SHADER_PROCESSING, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(PC_VS_INVOCATIONS,
                             BV_PC_VS_INVOCATIONS,
                             SP_VS_STAGE_TEX_INSTRUCTIONS,
                             BV_SP_VS_STAGE_TEX_INSTRUCTIONS),
   {
      return safe_div(4 * (SP_VS_STAGE_TEX_INSTRUCTIONS + BV_SP_VS_STAGE_TEX_INSTRUCTIONS),
                      PC_VS_INVOCATIONS + BV_PC_VS_INVOCATIONS);
   });

DERIVED_COUNTER(percent_time_alus_working,
   "% Time ALUs Working",
   "Percentage of time the ALUs are working while the Shaders are busy",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_BUSY_CYCLES,
                             SP_ALU_WORKING_CYCLES),
   {
      return percent(SP_ALU_WORKING_CYCLES / 2, SP_BUSY_CYCLES);
   });

DERIVED_COUNTER(percent_time_compute,
   "% Time Compute",
   "Percentage of time spent in compute work compared to the total time spent shading everything",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_ANY_EU_WORKING_FS_STAGE,
                             SP_ANY_EU_WORKING_VS_STAGE,
                             BV_SP_ANY_EU_WORKING_VS_STAGE,
                             SP_ANY_EU_WORKING_CS_STAGE),
   {
      uint64_t total = SP_ANY_EU_WORKING_VS_STAGE + BV_SP_ANY_EU_WORKING_VS_STAGE +
                       SP_ANY_EU_WORKING_FS_STAGE;
      return percent(SP_ANY_EU_WORKING_CS_STAGE, total);
   });

DERIVED_COUNTER(percent_time_efus_working,
   "% Time EFUs Working",
   "Percentage of time the EFUs are working while the Shaders are busy",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_BUSY_CYCLES,
                             SP_EFU_WORKING_CYCLES),
   {
      return percent(SP_EFU_WORKING_CYCLES, SP_BUSY_CYCLES);
   });

DERIVED_COUNTER(percent_time_shading_fragments,
   "% Time Shading Fragments",
   "Percentage of time spent shading fragments compared to the total time spent shading everything",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_ANY_EU_WORKING_FS_STAGE,
                             SP_ANY_EU_WORKING_VS_STAGE,
                             BV_SP_ANY_EU_WORKING_VS_STAGE,
                             SP_ANY_EU_WORKING_CS_STAGE),
   {
      uint64_t total = SP_ANY_EU_WORKING_VS_STAGE + BV_SP_ANY_EU_WORKING_VS_STAGE +
                       SP_ANY_EU_WORKING_FS_STAGE;
      return percent(SP_ANY_EU_WORKING_FS_STAGE - SP_ANY_EU_WORKING_CS_STAGE, total);
   });

DERIVED_COUNTER(percent_time_shading_vertices,
   "% Time Shading Vertices",
   "Percentage of time spent shading vertices compared to the total time spent shading everything",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_ANY_EU_WORKING_FS_STAGE,
                             BV_SP_ANY_EU_WORKING_FS_STAGE,
                             SP_ANY_EU_WORKING_VS_STAGE,
                             BV_SP_ANY_EU_WORKING_VS_STAGE),
   {
      uint64_t total = SP_ANY_EU_WORKING_FS_STAGE + BV_SP_ANY_EU_WORKING_FS_STAGE +
                       SP_ANY_EU_WORKING_VS_STAGE + BV_SP_ANY_EU_WORKING_VS_STAGE;
      return percent(SP_ANY_EU_WORKING_VS_STAGE + BV_SP_ANY_EU_WORKING_VS_STAGE, total);
   });

DERIVED_COUNTER(vertex_instructions,
   "Vertex Instructions",
   "Total number of scalar vertex shader instructions issued",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(SP_VS_STAGE_EFU_INSTRUCTIONS,
                             SP_VS_STAGE_FULL_ALU_INSTRUCTIONS,
                             BV_SP_VS_STAGE_EFU_INSTRUCTIONS,
                             BV_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS),
   {
      return (SP_VS_STAGE_EFU_INSTRUCTIONS + BV_SP_VS_STAGE_EFU_INSTRUCTIONS +
              SP_VS_STAGE_FULL_ALU_INSTRUCTIONS + BV_SP_VS_STAGE_FULL_ALU_INSTRUCTIONS) * 4;
   });

DERIVED_COUNTER(vertices_shaded,
   "Vertices Shaded",
   "Number of vertices submitted to the shader engine",
   GPU_SHADER_PROCESSING, UINT64,
   DERIVED_COUNTER_PERFCNTRS(PC_VS_INVOCATIONS,
                             BV_PC_VS_INVOCATIONS),
   {
      return PC_VS_INVOCATIONS + BV_PC_VS_INVOCATIONS;
   });

/* FIXME: disabled due to lack of SP counter capacity
DERIVED_COUNTER(percent_wave_context_occupancy,
   "% Wave Context Occupancy",
   "Average percentage of wave context occupancy per cycle",
   GPU_SHADER_PROCESSING, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(...),
   {
   });
*/

DERIVED_COUNTER(percent_bvh_fetch_stall,
   "% BVH Fetch Stall",
   "Percentage of clock cycles where the RTU could not make any more requests for BVH fetch from scheduler",
   GPU_STALLS, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_SCH_STALL_CYCLES_RTU,
                             RBBM_STATUS_MASKED),
   {
      return percent(SP_SCH_STALL_CYCLES_RTU, RBBM_STATUS_MASKED);
   });

DERIVED_COUNTER(percent_instruction_cache_miss,
   "% Instruction Cache Miss",
   "Number of L1 instruction cache misses divided by L1 instruction cache requests",
   GPU_STALLS, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_ICL1_REQUESTS,
                             SP_ICL1_MISSES,
                             BV_SP_ICL1_REQUESTS,
                             BV_SP_ICL1_MISSES),
   {
      return percent(SP_ICL1_MISSES + BV_SP_ICL1_MISSES,
                     SP_ICL1_REQUESTS + BV_SP_ICL1_REQUESTS);
   });

DERIVED_COUNTER(l1_texture_cache_miss_per_pixel,
   "L1 Texture Cache Miss Per Pixel",
   "Average number of Texture L1 cache misses per pixel",
   GPU_STALLS, DOUBLE,
   DERIVED_COUNTER_PERFCNTRS(TP_L1_CACHELINE_MISSES,
                             BV_TP_L1_CACHELINE_MISSES,
                             SP_PIXELS),
   {
      return safe_div(TP_L1_CACHELINE_MISSES + BV_TP_L1_CACHELINE_MISSES, SP_PIXELS);
   });

DERIVED_COUNTER(percent_stalled_on_system_memory,
   "% Stalled on System Memory",
   "Percentage of cycles the L2 cache is stalled waiting for data from system memory",
   GPU_STALLS, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(UCHE_STALL_CYCLES_ARBITER,
                             RBBM_STATUS_MASKED),
   {
      return percent(UCHE_STALL_CYCLES_ARBITER, 4 * RBBM_STATUS_MASKED);
   });

DERIVED_COUNTER(percent_texture_fetch_stall,
   "% Texture Fetch Stall",
   "Percentage of clock cycles where the shader processors cannot make any more requests for texture data",
   GPU_STALLS, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(SP_STALL_CYCLES_TP,
                             BV_SP_STALL_CYCLES_TP,
                             RBBM_STATUS_MASKED),
   {
      return percent(SP_STALL_CYCLES_TP + BV_SP_STALL_CYCLES_TP,
                     context->a7xx.number_of_usptp * RBBM_STATUS_MASKED);
   });

DERIVED_COUNTER(percent_texture_l1_miss,
   "% Texture L1 Miss",
   "Number of L1 texture cache misses divided by L1 texture cache requests",
   GPU_STALLS, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(TP_L1_CACHELINE_REQUESTS,
                             TP_L1_CACHELINE_MISSES,
                             BV_TP_L1_CACHELINE_REQUESTS,
                             BV_TP_L1_CACHELINE_MISSES),
   {
      return percent(TP_L1_CACHELINE_MISSES + BV_TP_L1_CACHELINE_MISSES,
                     TP_L1_CACHELINE_REQUESTS + BV_TP_L1_CACHELINE_REQUESTS);
   });

DERIVED_COUNTER(percent_texture_l2_miss,
   "% Texture L2 Miss",
   "Number of L2 texture cache misses divided by L2 texture cache requests",
   GPU_STALLS, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(UCHE_VBIF_READ_BEATS_TP,
                             UCHE_READ_REQUESTS_TP),
   {
      return percent(2 * UCHE_VBIF_READ_BEATS_TP, UCHE_READ_REQUESTS_TP);
   });

DERIVED_COUNTER(percent_vertex_fetch_stall,
   "% Vertex Fetch Stall",
   "Percentage of clock cycles where the GPU cannot make any more requests for vertex data",
   GPU_STALLS, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(PC_STALL_CYCLES_VFD,
                             BV_PC_STALL_CYCLES_VFD,
                             RBBM_STATUS_MASKED),
   {
      return percent(PC_STALL_CYCLES_VFD + BV_PC_STALL_CYCLES_VFD,
                     RBBM_STATUS_MASKED);
   });

DERIVED_COUNTER(percent_lrz_pixel_killed,
   "% LRZ Pixel Killed",
   "Percentage of pixels killed by LRZ",
   GPU_LRZ, PERCENTAGE,
   DERIVED_COUNTER_PERFCNTRS(LRZ_TOTAL_PIXEL,
                             LRZ_VISIBLE_PIXEL_AFTER_LRZ),
   {
      return percent(LRZ_TOTAL_PIXEL - LRZ_VISIBLE_PIXEL_AFTER_LRZ, LRZ_TOTAL_PIXEL);
   });

DERIVED_COUNTER(lrz_primitives_killed,
   "LRZ Primitives Killed",
   "",
   GPU_LRZ, UINT64,
   DERIVED_COUNTER_PERFCNTRS(LRZ_PRIM_KILLED_BY_LRZ),
   {
      return LRZ_PRIM_KILLED_BY_LRZ;
   });

DERIVED_COUNTER(lrz_tiles_killed,
   "LRZ Tiles Killed",
   "",
   GPU_LRZ, UINT64,
   DERIVED_COUNTER_PERFCNTRS(LRZ_TILE_KILLED),
   {
      return LRZ_TILE_KILLED;
   });

const struct fd_derived_counter *a7xx_derived_counters[] = {
   /* Category: GPU General */
   DERIVED_COUNTER_PTR(clocks),

   /* Category: GPU Memory Stats */
   DERIVED_COUNTER_PTR(avg_bytes_per_fragment),
   DERIVED_COUNTER_PTR(avg_bytes_per_vertex),
   DERIVED_COUNTER_PTR(sp_memory_read),
   DERIVED_COUNTER_PTR(texture_memory_read),
   DERIVED_COUNTER_PTR(vertex_memory_read),

   /* Category: GPU Preemption */
   DERIVED_COUNTER_PTR(avg_preemption_delay),
   DERIVED_COUNTER_PTR(preemptions),

   /* Category: GPU Primitive Processing */
   DERIVED_COUNTER_PTR(average_polygon_area),
   DERIVED_COUNTER_PTR(average_vertices_per_polygon),
   DERIVED_COUNTER_PTR(preclipped_polygon),
   DERIVED_COUNTER_PTR(percent_prims_clipped),
   DERIVED_COUNTER_PTR(percent_prims_trivially_rejected),
   DERIVED_COUNTER_PTR(reused_vertices),

   /* Category: GPU Shader Processing */
   DERIVED_COUNTER_PTR(alu_per_fragment),
   DERIVED_COUNTER_PTR(alu_per_vertex),
   DERIVED_COUNTER_PTR(percent_anisotropic_filtered),
   DERIVED_COUNTER_PTR(average_bvh_fetch_latency_cycles),
   DERIVED_COUNTER_PTR(efu_per_fragment),
   DERIVED_COUNTER_PTR(efu_per_vertex),
   DERIVED_COUNTER_PTR(fragment_alu_instructions_full),
   DERIVED_COUNTER_PTR(fragment_alu_instructions_half),
   DERIVED_COUNTER_PTR(fragment_efu_instructions),
   DERIVED_COUNTER_PTR(fragment_instructions),
   DERIVED_COUNTER_PTR(fragments_shaded),
   DERIVED_COUNTER_PTR(percent_linear_filtered),
   DERIVED_COUNTER_PTR(percent_nearest_filtered),
   DERIVED_COUNTER_PTR(percent_rtu_busy),
   DERIVED_COUNTER_PTR(rtu_ray_box_intersections_per_instruction),
   DERIVED_COUNTER_PTR(rtu_ray_triangle_intersections_per_instruction),
   DERIVED_COUNTER_PTR(percent_shader_alu_capacity_utilized),
   DERIVED_COUNTER_PTR(percent_shaders_busy),
   DERIVED_COUNTER_PTR(percent_shaders_stalled),
   DERIVED_COUNTER_PTR(percent_texture_pipes_busy),
   DERIVED_COUNTER_PTR(textures_per_fragment),
   DERIVED_COUNTER_PTR(textures_per_vertex),
   DERIVED_COUNTER_PTR(percent_time_alus_working),
   DERIVED_COUNTER_PTR(percent_time_compute),
   DERIVED_COUNTER_PTR(percent_time_efus_working),
   DERIVED_COUNTER_PTR(percent_time_shading_fragments),
   DERIVED_COUNTER_PTR(percent_time_shading_vertices),
   DERIVED_COUNTER_PTR(vertex_instructions),
   DERIVED_COUNTER_PTR(vertices_shaded),

   /* Category: GPU Stalls */
   DERIVED_COUNTER_PTR(percent_bvh_fetch_stall),
   DERIVED_COUNTER_PTR(percent_instruction_cache_miss),
   DERIVED_COUNTER_PTR(l1_texture_cache_miss_per_pixel),
   DERIVED_COUNTER_PTR(percent_stalled_on_system_memory),
   DERIVED_COUNTER_PTR(percent_texture_fetch_stall),
   DERIVED_COUNTER_PTR(percent_texture_l1_miss),
   DERIVED_COUNTER_PTR(percent_texture_l2_miss),
   DERIVED_COUNTER_PTR(percent_vertex_fetch_stall),

   /* Category: GPU LRZ */
   DERIVED_COUNTER_PTR(percent_lrz_pixel_killed),
   DERIVED_COUNTER_PTR(lrz_primitives_killed),
   DERIVED_COUNTER_PTR(lrz_tiles_killed),
};

const unsigned a7xx_num_derived_counters = ARRAY_SIZE(a7xx_derived_counters);
static_assert(ARRAY_SIZE(a7xx_derived_counters) <= FD_DERIVED_COUNTER_COLLECTION_MAX_DERIVED_COUNTERS, "");

/* Prototype for linking purposes. */
void
a7xx_generate_derived_counter_collection(const struct fd_dev_id *id, struct fd_derived_counter_collection *collection);

void
a7xx_generate_derived_counter_collection(const struct fd_dev_id *id, struct fd_derived_counter_collection *collection)
{
   /* The provided collection should already specify the derived counters that will be measured.
    * This function will set up enabled_perfcntrs_map and enabled_perfcntrs array so that each
    * used DERIVED_COUNTER_PERFCNTR_* enum value will map to the corresponding index in the
    * array where the relevant fd_perfcntr_counter and fd_perfcntr_countable are stored.
    */

   collection->num_enabled_perfcntrs = 0;
   memset(collection->enabled_perfcntrs_map, 0xff, ARRAY_SIZE(collection->enabled_perfcntrs_map));

   for (unsigned i = 0; i < collection->num_counters; ++i) {
      const struct fd_derived_counter *counter = collection->counters[i];

      for (unsigned j = 0; j < counter->num_perfcntrs; ++j) {
         uint8_t perfcntr = counter->perfcntrs[j];
         collection->enabled_perfcntrs_map[perfcntr] = 0x00;
      }
   }

   /* Note if CP_ALWAYS_COUNT is enabled. This is the zero-index perfcntr. */
   collection->cp_always_count_enabled = !collection->enabled_perfcntrs_map[0];

   for (unsigned i = 0; i < ARRAY_SIZE(collection->enabled_perfcntrs_map); ++i) {
      if (collection->enabled_perfcntrs_map[i] == 0xff)
         continue;

      uint8_t enabled_perfcntr_index = collection->num_enabled_perfcntrs++;
      collection->enabled_perfcntrs_map[i] = enabled_perfcntr_index;

      collection->enabled_perfcntrs[enabled_perfcntr_index].counter =
         a7xx_derived_counter_perfcntrs[i].counter;
      collection->enabled_perfcntrs[enabled_perfcntr_index].countable =
         a7xx_derived_counter_perfcntrs[i].countable;
   }

   const struct fd_dev_info *info = fd_dev_info_raw(id);
   collection->derivation_context.a7xx.number_of_usptp = info->num_sp_cores * 2;
   collection->derivation_context.a7xx.number_of_alus_per_usptp = 128;
}

