/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdio.h>

#include "intel/decoder/intel_decoder.h"

#include "compiler/brw_isa_info.h"
#ifdef INTEL_USE_ELK
#include "compiler/elk/elk_isa_info.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct intel_device_info;

struct intel_isa_info {
   union {
      struct brw_isa_info brw_isa;
#ifdef INTEL_USE_ELK
      struct elk_isa_info elk_isa;
#endif
   };
};

/* Helpers to abstract some BRW/ELK differences. */

void intel_disassemble(const struct intel_device_info *devinfo,
                       const void *assembly, int start, FILE *out);

void intel_decoder_init(struct intel_batch_decode_ctx *ctx,
                        struct intel_isa_info *isa_info,
                        const struct intel_device_info *devinfo,
                        FILE *fp, enum intel_batch_decode_flags flags,
                        const char *xml_path,
                        struct intel_batch_decode_bo (*get_bo)(void *, bool, uint64_t),
                        unsigned (*get_state_size)(void *, uint64_t, uint64_t),
                        void *user_data);

#ifdef __cplusplus
}
#endif
