/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include "gen_enums.h"

struct intel_device_info;

#ifdef __cplusplus
extern "C" {
#endif

const char *gen_opcode_to_string(gen_opcode op);
const char *gen_reg_type_to_string(enum gen_reg_type type);
const char *gen_arf_to_string(unsigned arf);
const char *gen_condition_to_string(enum gen_condition cmod);
const char *gen_predicate_to_string(const struct intel_device_info *devinfo,
                                    bool align16, gen_predicate pred);
const char *gen_math_function_to_string(gen_math func);
const char *gen_sync_function_to_string(gen_sync_func func);
const char *gen_pipe_to_string(gen_pipe pipe);
const char *gen_sfid_to_string(const struct intel_device_info *devinfo,
                               gen_sfid sfid);

const char *gen_lsc_opcode_to_string(enum lsc_opcode opcode);
const char *gen_lsc_addr_size_to_string(enum lsc_addr_size addr_size);
const char *gen_lsc_data_size_to_string(enum lsc_data_size data_size);
const char *gen_lsc_cmask_to_string(enum lsc_cmask cmask);
const char *gen_lsc_fence_scope_to_string(enum lsc_fence_scope scope);
const char *gen_lsc_flush_type_to_string(enum lsc_flush_type flush_type);
const char *gen_lsc_cache_ctrl_to_string(const struct intel_device_info *devinfo,
                                         enum lsc_opcode op,
                                         unsigned cache_ctrl);

const char *gen_sampler_msg_type_to_string(const struct intel_device_info *devinfo,
                                           unsigned msg_type);
const char *gen_sampler_params_to_string(const struct intel_device_info *devinfo,
                                         unsigned msg_type);
const char *gen_urb_opcode_to_string(unsigned opcode);
const char *gen_hdc1_surface_simd_mode_to_string(unsigned field);
const char *gen_hdc1_aop_to_string(unsigned aop);
const char *gen_hdc1_float_aop_to_string(unsigned aop);
const char *gen_hdc1_owords_to_string(unsigned v);
const char *gen_rt_write_subtype_to_string(const struct intel_device_info *devinfo,
                                           unsigned subtype);

gen_opcode gen_opcode_from_string(const char *str, int size, bool *valid);
enum gen_reg_type gen_reg_type_from_string(const char *str, int size, bool *valid);
enum gen_condition gen_condition_from_string(const char *str, int size, bool *valid);
gen_predicate gen_predicate_from_string(const struct intel_device_info *devinfo,
                                        bool align16,
                                        const char *str, int size,
                                        bool *valid);
gen_math gen_math_function_from_string(const char *str, int size, bool *valid);
gen_sync_func gen_sync_function_from_string(const char *str, int size, bool *valid);
gen_pipe gen_pipe_from_string(const char *str, int size, bool *valid);
gen_sfid gen_sfid_from_string(const struct intel_device_info *devinfo,
                              const char *str, int size, bool *valid);

enum lsc_opcode gen_lsc_opcode_from_string(const char *str, int size, bool *valid);
enum lsc_addr_size gen_lsc_addr_size_from_string(const char *str, int size,
                                                 bool *valid);
enum lsc_data_size gen_lsc_data_size_from_string(const char *str, int size,
                                                 bool *valid);
enum lsc_cmask gen_lsc_cmask_from_string(const char *str, int size, bool *valid);
enum lsc_fence_scope gen_lsc_fence_scope_from_string(const char *str, int size,
                                                     bool *valid);
enum lsc_flush_type gen_lsc_flush_type_from_string(const char *str, int size,
                                                   bool *valid);
unsigned gen_lsc_cache_ctrl_from_string(const struct intel_device_info *devinfo,
                                        enum lsc_opcode op,
                                        const char *str, int size, bool *valid);

#ifdef __cplusplus
} /* extern "C" */
#endif
