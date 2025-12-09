/*
 * Copyright 2022 Alyssa Rosenzweig
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "util/format/u_formats.h"
#include "nir.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KK_MAX_ATTRIBS (32)
#define KK_MAX_VBUFS   (32)

/* See pipe_vertex_element for justification on the sizes. This structure should
 * be small so it can be embedded into a shader key.
 */
struct kk_attribute {
   /* If instanced, Zero means all get the same value (Vulkan semantics). */
   uint32_t divisor;
   /* Buffer binding to load stride from root_table */
   uint32_t binding;

   /* pipe_format, all vertex formats should be <= 255 */
   uint8_t format;

   unsigned buf   : 7;
   bool instanced : 1;
};

bool kk_nir_lower_vbo(nir_shader *shader, struct kk_attribute *attribs);

bool kk_vbo_supports_format(enum pipe_format format);

#ifdef __cplusplus
} /* extern C */
#endif