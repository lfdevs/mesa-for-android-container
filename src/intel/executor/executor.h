/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "intel/dev/intel_device_info.h"
#include "intel/isl/isl.h"
#include "perf/intel_perf.h"
#include "perf/intel_perf_query.h"
#include "util/u_dynarray.h"
/* TODO: Move slice.h into a proper common place. */
#include "../mda/slice.h"

typedef struct {
   uint32_t size;
   uint32_t handle;
   void *map;
   void *cursor;
   uint64_t addr;
} executor_bo;

typedef struct {
   executor_bo *bo;
   uint32_t offset;
   uint32_t size;
   void *map;
   const char *name;
} executor_mem_region;

typedef enum {
   EXECUTOR_SURFACE_BUFFER,
   EXECUTOR_SURFACE_2D,
} executor_surface_type;

typedef struct {
   executor_mem_region region;
   executor_surface_type type;
   enum isl_format format;
   uint32_t stride;
   uint32_t width;
   uint32_t height;
   uint32_t bti;
} executor_surface_binding;

typedef enum {
   EXECUTOR_SAMPLER_FILTER_NEAREST,
   EXECUTOR_SAMPLER_FILTER_LINEAR,
} executor_sampler_filter;

typedef enum {
   EXECUTOR_SAMPLER_ADDRESS_CLAMP,
   EXECUTOR_SAMPLER_ADDRESS_REPEAT,
   EXECUTOR_SAMPLER_ADDRESS_MIRROR,
} executor_sampler_address;

typedef struct {
   uint32_t index;
   executor_sampler_filter min_filter;
   executor_sampler_filter mag_filter;
   executor_sampler_address address_mode;
   bool nonnormalized_coordinates;
   float min_lod;
   float max_lod;
} executor_sampler_binding;

typedef struct {
   void *mem_ctx;

   struct intel_device_info *devinfo;
   struct isl_device *isl_dev;
   int fd;

   struct {
      uint32_t ctx_id;
   } i915;

   struct {
      uint32_t vm_id;
      uint32_t queue_id;
   } xe;

   struct {
      executor_bo batch;
      executor_bo extra;
      executor_bo data;
      executor_bo perf;
   } bo;

   bool perf_enabled;

   struct {
      struct intel_perf_context *ctx;
      struct intel_perf_query_object *obj;
   } perf_query;

   uint64_t batch_start;

   struct util_dynarray mem_regions;
   uint32_t next_mem_name_id;

   executor_surface_binding *surface_bindings;
   uint32_t num_surface_bindings;
   uint32_t surface_binding_capacity;

   executor_sampler_binding *sampler_bindings;
   uint32_t num_sampler_bindings;
   uint32_t sampler_binding_capacity;
} executor_context;

typedef struct {
   executor_context *ec;
   void *tmp_ctx;

   slice original_src;
   uint32_t hw_regs;
   uint32_t hw_threads;
   uint32_t thread_groups;
   uint32_t simd;
   uint32_t slm_size;
   bool autoswsb;

   void *kernel_bin;
   uint32_t kernel_size;
} executor_run;

typedef struct {
   uint64_t offset;
} executor_address;

__attribute__((unused)) static uint64_t
executor_combine_address(void *data, void *location,
                         executor_address address, uint32_t delta)
{
   return address.offset + delta;
}

executor_address executor_address_of_ptr(executor_bo *bo, void *ptr);

void *executor_alloc_bytes(executor_bo *bo, uint32_t size);
void *executor_alloc_bytes_aligned(executor_bo *bo, uint32_t size, uint32_t alignment);
executor_mem_region *executor_find_mem_region(executor_context *ec,
                                              const char *key);

void failf(const char *fmt, ...) PRINTFLIKE(1, 2);

slice strip_spaces(slice s);
slice trim_comments(slice s);
bool parse_int64(slice s, int64_t *value);

const char *executor_apply_macros(executor_run *run);

#ifdef genX
#  include "executor_genx.h"
#else
#  define genX(x) gfx9_##x
#  include "executor_genx.h"
#  undef genX
#  define genX(x) gfx11_##x
#  include "executor_genx.h"
#  undef genX
#  define genX(x) gfx12_##x
#  include "executor_genx.h"
#  undef genX
#  define genX(x) gfx125_##x
#  include "executor_genx.h"
#  undef genX
#  define genX(x) gfx20_##x
#  include "executor_genx.h"
#  undef genX
#  define genX(x) gfx30_##x
#  include "executor_genx.h"
#  undef genX
#  define genX(x) gfx35_##x
#  include "executor_genx.h"
#  undef genX
#endif

#endif /* EXECUTOR_H */
