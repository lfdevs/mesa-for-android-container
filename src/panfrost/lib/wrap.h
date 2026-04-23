/*
 * Copyright (C) 2017-2019 Lyude Paul
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_DECODE_PUBLIC_H__
#define __PAN_DECODE_PUBLIC_H__

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Public entrypoints for the tracing infrastructure. This API should be kept
 * more or less stable. Don't feel bad if you have to change it; just feel
 * slightly guilty about creating more work for me later. -Alyssa <3
 *
 * I'm joking. Mostly. panwrap (out-of-tree) includes this, so update that if
 * you need to change something here. panwrap is open-source but cannot be
 * included in-tree.
 */

// TODO: update panwrap

struct pandecode_context;

struct pandecode_context *pandecode_create_context(bool to_stderr);

void pandecode_next_frame(struct pandecode_context *ctx);

void pandecode_destroy_context(struct pandecode_context *ctx);

void pandecode_inject_mmap(struct pandecode_context *ctx, uint64_t gpu_va,
                           void *cpu, unsigned sz, const char *name);

void pandecode_inject_free(struct pandecode_context *ctx, uint64_t gpu_va,
                           unsigned sz);

void pandecode_jc(struct pandecode_context *ctx, uint64_t jc_gpu_va,
                  uint64_t gpu_id);

void pandecode_interpret_cs(struct pandecode_context *ctx,
                            uint64_t queue_gpu_va, uint32_t size,
                            uint64_t gpu_id, uint32_t *regs);

void pandecode_cs_binary(struct pandecode_context *ctx, uint64_t binary_gpu_va,
                         uint32_t size, uint64_t gpu_id);

void pandecode_cs_trace(struct pandecode_context *ctx, uint64_t trace_gpu_va,
                        uint32_t size, uint64_t gpu_id);

void pandecode_abort_on_fault(struct pandecode_context *ctx, uint64_t jc_gpu_va,
                              uint64_t gpu_id);

#endif /* __MMAP_TRACE_H__ */
