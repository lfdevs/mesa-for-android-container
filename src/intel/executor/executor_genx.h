/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef EXECUTOR_H
#error This file must be included via executor.h
#endif

void genX(emit_execute)(const executor_run *run);
void genX(emit_perf_stall)(executor_context *ec);
void genX(emit_mi_report_perf_count)(executor_context *ec, executor_bo *bo,
                                      uint32_t offset_in_bytes,
                                      uint32_t report_id);
void genX(store_register_mem)(executor_context *ec, executor_bo *bo,
                              uint32_t reg, uint32_t reg_size,
                              uint32_t offset_in_bytes);
