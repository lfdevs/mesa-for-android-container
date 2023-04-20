/*
 * Copyright © 2022 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common/intel_gem.h"

#include "drm-uapi/i915_drm.h"

bool i915_gem_create_context(int fd, uint32_t *context_id);
bool i915_gem_destroy_context(int fd, uint32_t context_id);
bool i915_gem_create_context_engines(int fd,
                                     const struct intel_query_engine_info *info,
                                     int num_engines, enum intel_engine_class *engine_classes,
                                     uint32_t *context_id);
bool i915_gem_set_context_param(int fd, uint32_t context, uint32_t param,
                                uint64_t value);
bool i915_gem_get_context_param(int fd, uint32_t context, uint32_t param,
                                uint64_t *value);
bool i915_gem_read_render_timestamp(int fd, uint64_t *value);
bool
i915_gem_create_context_ext(int fd,
                            enum intel_gem_create_context_flags flags,
                            uint32_t *ctx_id);
bool i915_gem_supports_protected_context(int fd);
bool i915_gem_get_param(int fd, uint32_t param, int *value);
bool i915_gem_can_render_on_fd(int fd);
