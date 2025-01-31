/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>

#include "intel/decoder/intel_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

struct intel_device_info;

/* Helpers to abstract some BRW/ELK differences. */

void intel_disassemble(const struct intel_device_info *devinfo,
                       const void *assembly, int start, FILE *out);

void intel_decoder_init(struct intel_batch_decode_ctx *ctx,
                        const struct intel_device_info *devinfo,
                        FILE *fp, enum intel_batch_decode_flags flags,
                        const char *xml_path,
                        struct intel_batch_decode_bo (*get_bo)(void *, bool, uint64_t),
                        unsigned (*get_state_size)(void *, uint64_t, uint64_t),
                        void *user_data);

#ifdef __cplusplus
}
#endif
