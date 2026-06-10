/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_PRINTF_H
#define RADV_PRINTF_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "radv_debug.h"

#include <vulkan/vulkan_core.h>

#include "util/bitset.h"
#include "util/u_dynarray.h"

struct radv_device;
typedef struct nir_builder nir_builder;
typedef struct nir_shader nir_shader;
typedef struct nir_def nir_def;

struct radv_printf_data {
   uint32_t buffer_size;
   struct radv_backed_buffer buffer;
   VkDeviceAddress buffer_addr;
   struct util_dynarray formats;
};

struct radv_printf_format {
   char *string;
   uint32_t divergence_mask;
   uint8_t element_sizes[32];
};

struct radv_printf_buffer_header {
   uint32_t offset;
   uint32_t size;
};

struct radv_valid_va_data {
   struct radv_backed_buffer buffer;
   VkDeviceAddress buffer_addr;
   BITSET_WORD *vas;
};

struct radv_debug_nir {
   struct radv_printf_data printf;
   struct radv_valid_va_data valid_va;
};

/* shader printf */

VkResult radv_printf_data_init(struct radv_device *device);

void radv_printf_data_finish(struct radv_device *device);

void radv_build_printf_args(struct radv_debug_nir *debug_nir, nir_builder *b, const char *format, uint32_t argc,
                            nir_def **args);

void radv_build_printf(nir_builder *b, nir_def *cond, const char *format, ...);

void radv_dump_printf_data(struct radv_device *device, FILE *out, bool wait_idle);

/* shader va validation */

VkResult radv_init_va_validation(struct radv_device *device);

void radv_finish_va_validation(struct radv_device *device);

void radv_va_validation_update_page(struct radv_device *device, uint64_t va, uint64_t size, bool valid);

nir_def *radv_build_is_valid_va(struct radv_debug_nir *debug_nir, nir_builder *b, nir_def *addr);

#endif /* RADV_PRINTF_H */
