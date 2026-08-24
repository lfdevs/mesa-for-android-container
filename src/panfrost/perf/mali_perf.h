/*
 * Copyright (c) 2026 Arm Ltd.
 * Copyright (c) 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef MALI_PERF_H
#define MALI_PERF_H

#include <util/macros.h>

#define MALI_PERF_MAX_COUNTERS_PER_BLOCK 128

enum mali_perf_block_type {
   MALI_PERF_BLOCK_NONE = -1,
   MALI_PERF_BLOCK_GPU_FRONT_END = 0,
   MALI_PERF_BLOCK_MEMSYS,
   MALI_PERF_BLOCK_SHADER_CORE,
   MALI_PERF_BLOCK_TILER,
   MALI_PERF_BLOCK_TYPE_COUNT,
};

enum mali_perf_counter_units {
   MALI_PERF_COUNTER_UNITS_CYCLES,
   MALI_PERF_COUNTER_UNITS_JOBS,
   MALI_PERF_COUNTER_UNITS_TASKS,
   MALI_PERF_COUNTER_UNITS_PRIMITIVES,
   MALI_PERF_COUNTER_UNITS_BEATS,
   MALI_PERF_COUNTER_UNITS_REQUESTS,
   MALI_PERF_COUNTER_UNITS_WARPS,
   MALI_PERF_COUNTER_UNITS_QUADS,
   MALI_PERF_COUNTER_UNITS_TILES,
   MALI_PERF_COUNTER_UNITS_INSTRUCTIONS,
   MALI_PERF_COUNTER_UNITS_TRANSACTIONS,
   MALI_PERF_COUNTER_UNITS_THREADS,
   MALI_PERF_COUNTER_UNITS_BITS,
   MALI_PERF_COUNTER_UNITS_BYTES,
   MALI_PERF_COUNTER_UNITS_PIXELS,
   MALI_PERF_COUNTER_UNITS_ISSUES,
   MALI_PERF_COUNTER_UNITS_INTERRUPTS,
   MALI_PERF_COUNTER_UNITS_PERCENT,
   MALI_PERF_COUNTER_UNITS_TESTS,
   MALI_PERF_COUNTER_UNITS_RAYS,
   MALI_PERF_COUNTER_UNITS_NODES,
   MALI_PERF_COUNTER_UNITS_BOXES,
   MALI_PERF_COUNTER_UNITS_BYTES_PER_SECOND,
   MALI_PERF_COUNTER_UNITS_INSTANCES,
};

struct mali_perf_constants {
   uint32_t ext_bus_byte_size;
   uint32_t l2_cache_count;
   uint32_t shader_core_count;
};

struct mali_perf_dump_info {
   struct {
      uint64_t start_ns;
      uint64_t end_ns;
   } time_span;
};

struct mali_perf_counter_source {
   int8_t block;
   uint16_t counter;
};

struct mali_perf_hw_counter_id {
   struct {
      int8_t type;
      uint8_t index;
   } block;

   uint16_t index;
};

struct mali_perf_backend {
   int64_t (*get_hw_counter_value)(struct mali_perf_backend *backend,
                                   struct mali_perf_hw_counter_id id);
};

enum mali_perf_class {
   /* Anything after that must match the perfetto
    * GpuCounterDescriptor.GpuCounterGroup definition.
    */
   MALI_PERF_UNCLASSIFIED = 0,
   MALI_PERF_SYSTEM = 1,
   MALI_PERF_VERTICES = 2,
   MALI_PERF_FRAGMENTS = 3,
   MALI_PERF_PRIMITIVES = 4,
   MALI_PERF_MEMORY = 5,
   MALI_PERF_COMPUTE = 6,
   MALI_PERF_RAY_TRACING = 7,
};

struct mali_perf_counter {
   const char *name;
   const char *desc;
   enum mali_perf_counter_units units;
   enum mali_perf_block_type block;
   const struct mali_perf_counter_source *sources;
   uint64_t classes;

   double (*get_value)(struct mali_perf_backend *backend, uint8_t blk_idx,
                       const struct mali_perf_constants *constants,
                       const struct mali_perf_dump_info *dump_info);
};

struct mali_perf_gpu_info {
   const struct mali_perf_counter *counters;
};

static inline const char *
mali_perf_block_type_str(enum mali_perf_block_type block_type)
{
   switch (block_type) {
   case MALI_PERF_BLOCK_NONE:
      return "None";
   case MALI_PERF_BLOCK_GPU_FRONT_END:
      return "GPU Front-end";
   case MALI_PERF_BLOCK_MEMSYS:
      return "Memory System";
   case MALI_PERF_BLOCK_SHADER_CORE:
      return "Shader Core";
   case MALI_PERF_BLOCK_TILER:
      return "Tiler";
   default:
      return "Unknown";
   }
}

static inline uint8_t
mali_perf_block_count(enum mali_perf_block_type block_type,
                      const struct mali_perf_constants *constants)
{
   switch (block_type) {
   case MALI_PERF_BLOCK_MEMSYS:
      return constants->shader_core_count;
   case MALI_PERF_BLOCK_SHADER_CORE:
      return constants->l2_cache_count;
   case MALI_PERF_BLOCK_GPU_FRONT_END:
   case MALI_PERF_BLOCK_TILER:
   case MALI_PERF_BLOCK_NONE:
      return 1;
   default:
      return 0;
   }
}

extern const struct mali_perf_gpu_info mali_perf_g1;
extern const struct mali_perf_gpu_info mali_perf_g31;
extern const struct mali_perf_gpu_info mali_perf_g51;
extern const struct mali_perf_gpu_info mali_perf_g52;
extern const struct mali_perf_gpu_info mali_perf_g71;
extern const struct mali_perf_gpu_info mali_perf_g710;
extern const struct mali_perf_gpu_info mali_perf_g715;
extern const struct mali_perf_gpu_info mali_perf_g72;
extern const struct mali_perf_gpu_info mali_perf_g720;
extern const struct mali_perf_gpu_info mali_perf_g725;
extern const struct mali_perf_gpu_info mali_perf_g76;
extern const struct mali_perf_gpu_info mali_perf_g77;
extern const struct mali_perf_gpu_info mali_perf_g78;
extern const struct mali_perf_gpu_info mali_perf_t720;
extern const struct mali_perf_gpu_info mali_perf_t760;
extern const struct mali_perf_gpu_info mali_perf_t830;
extern const struct mali_perf_gpu_info mali_perf_t880;

static inline const struct mali_perf_gpu_info *
mali_perf_get_info_for_gpu(const char *gpu_name)
{
   static const struct {
      const char *name;
      const struct mali_perf_gpu_info *info;
   } name_to_info[] = {
      {"G1", &mali_perf_g1},
      {"G31", &mali_perf_g31},
      {"G51", &mali_perf_g51},
      {"G52", &mali_perf_g52},
      {"G71", &mali_perf_g71},
      {"G710", &mali_perf_g710},
      {"G715", &mali_perf_g715},
      {"G72", &mali_perf_g72},
      {"G720", &mali_perf_g720},
      {"G725", &mali_perf_g725},
      {"G76", &mali_perf_g76},
      {"G77", &mali_perf_g77},
      {"G78", &mali_perf_g78},
      {"T720", &mali_perf_t720},
      {"T760", &mali_perf_t760},
      {"T830", &mali_perf_t830},
      {"T880", &mali_perf_t880},
   };

   for (unsigned i = 0; i < ARRAY_SIZE(name_to_info); i++) {
      if (!strcmp(name_to_info[i].name, gpu_name) &&
          strlen(gpu_name) == strlen(name_to_info[i].name))
         return name_to_info[i].info;
   }

   return NULL;
}

#endif /* MALI_PERF_H */
