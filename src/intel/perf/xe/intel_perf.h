/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct intel_bind_timeline;
struct intel_perf_config;
struct intel_perf_registers;

uint64_t xe_perf_get_oa_format(struct intel_perf_config *perf);

bool xe_oa_metrics_available(struct intel_perf_config *perf, int fd, bool use_register_snapshots);

uint64_t xe_add_config(struct intel_perf_config *perf, int fd, const struct intel_perf_registers *config, const char *guid);
void xe_remove_config(struct intel_perf_config *perf, int fd, uint64_t config_id);

int xe_perf_stream_open(struct intel_perf_config *perf_config, int drm_fd,
                        uint32_t exec_id, uint64_t metrics_set_id,
                        uint64_t report_format, uint64_t period_exponent,
                        bool hold_preemption, bool enable,
                        struct intel_bind_timeline *timeline);
int xe_perf_stream_set_state(int perf_stream_fd, bool enable);
int xe_perf_stream_set_metrics_id(int perf_stream_fd, int drm_fd,
                                  uint32_t exec_queue, uint64_t metrics_set_id,
                                  struct intel_bind_timeline *timeline);
int xe_perf_stream_read_samples(struct intel_perf_config *perf_config, int perf_stream_fd,
                                uint8_t *buffer, size_t buffer_len);
