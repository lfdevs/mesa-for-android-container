/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct intel_perf_config;
struct intel_perf_registers;
struct drm_i915_perf_oa_config;

uint64_t i915_perf_get_oa_format(struct intel_perf_config *perf);

int i915_perf_stream_open(struct intel_perf_config *perf_config, int drm_fd,
                          uint32_t ctx_id, uint64_t metrics_set_id,
                          uint64_t report_format, uint64_t period_exponent,
                          bool hold_preemption, bool enable);
int i915_perf_stream_read_samples(struct intel_perf_config *perf_config, int perf_stream_fd, uint8_t *buffer, size_t buffer_len);
int i915_perf_stream_set_state(int perf_stream_fd, bool enable);
int i915_perf_stream_set_metrics_id(int perf_stream_fd, uint64_t metrics_set_id);

struct intel_perf_registers *i915_perf_load_configurations(struct intel_perf_config *perf_cfg, int fd, const char *guid);

bool i915_oa_metrics_available(struct intel_perf_config *perf, int fd, bool use_register_snapshots);

bool i915_has_dynamic_config_support(struct intel_perf_config *perf, int fd);
uint64_t i915_add_config(struct intel_perf_config *perf, int fd,
                         const struct intel_perf_registers *config,
                         const char *guid);
int i915_remove_config(struct intel_perf_config *perf, int fd, uint64_t config_id);
