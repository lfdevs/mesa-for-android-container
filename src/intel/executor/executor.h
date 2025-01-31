/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdint.h>

#include "intel/dev/intel_device_info.h"
#include "intel/isl/isl.h"

typedef struct {
   uint32_t size;
   uint32_t handle;
   void *map;
   void *cursor;
   uint64_t addr;
} executor_bo;

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
   } bo;

   uint64_t batch_start;
} executor_context;

typedef struct {
   const char *original_src;

   void *kernel_bin;
   uint32_t kernel_size;
} executor_params;

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

void failf(const char *fmt, ...) PRINTFLIKE(1, 2);

const char *executor_apply_macros(executor_context *ec, const char *original_src);

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
#endif

#endif /* EXECUTOR_H */
