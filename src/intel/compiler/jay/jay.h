/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/brw/brw_compiler.h"
#include "util/shader_stats.h"
#include "nir.h"

struct intel_device_info;
struct nir_shader_compiler_options;

struct jay_shader_bin {
   const uint32_t *kernel;
   uint32_t size;
   struct genisa_stats stats;
};

struct jay_shader_bin *jay_compile(const struct intel_device_info *devinfo,
                                   void *mem_ctx,
                                   nir_shader *nir,
                                   union brw_any_prog_data *prog_data,
                                   union brw_any_prog_key *key);
