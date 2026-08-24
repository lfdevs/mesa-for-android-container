/*
 * Copyright © 2026 Intel Corporation
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

#ifndef INTEL_PERF_METRICS_LIBRARY_H
#define INTEL_PERF_METRICS_LIBRARY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool intel_perf_init_metrics_library(struct intel_perf_config *perf, int fd);
bool intel_perf_deinit_metrics_library(struct intel_perf_config *perf);
uint64_t intel_perf_metrics_library_create_configuration(struct intel_perf_config *perf);
bool intel_perf_metrics_library_destroy_configuration(struct intel_perf_config *perf, uint64_t config_id);
void* intel_perf_metrics_library_create_query_pool(struct intel_perf_config *perf, uint32_t query_count);
bool intel_perf_metrics_library_destroy_query_pool(struct intel_perf_config *perf, void* query_pool);
bool intel_perf_metrics_library_get_query_results(struct intel_perf_config *perf, void* query_pool, void* data, uint32_t query_index, bool* write_results);
bool intel_perf_metrics_library_activate_configuration(struct intel_perf_config *perf, uint64_t config_id);
bool intel_perf_metrics_library_get_stream_marker_cmds(struct intel_perf_config *perf, uint32_t marker_value, void* cmds, uint32_t* cmds_size);
bool intel_perf_metrics_library_get_perf_query_cmds(struct intel_perf_config *perf, void* metrics_library_query_pool, uint64_t gpu_memory_offset, void* cpu_memory_offset, uint32_t query_index, uint64_t perf_marker, bool begin, void* cmds, uint32_t* cmds_size);

#ifdef __cplusplus
}
#endif

#endif /* INTEL_PERF_METRICS_LIBRARY_H */
