/* -*- c++ -*- */
/*
 * Copyright © 2021 Intel Corporation
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

#ifndef BRW_PRIVATE_H
#define BRW_PRIVATE_H

#include "brw_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

unsigned brw_required_dispatch_width(const struct shader_info *info);

bool brw_simd_should_compile(void *mem_ctx,
                             unsigned simd,
                             const struct intel_device_info *devinfo,
                             struct brw_cs_prog_data *prog_data,
                             unsigned required_dispatch_width,
                             const char **error);

void brw_simd_mark_compiled(unsigned simd,
                            struct brw_cs_prog_data *prog_data,
                            bool spilled);

int brw_simd_select(const struct brw_cs_prog_data *prog_data);

int brw_simd_select_for_workgroup_size(const struct intel_device_info *devinfo,
                                       const struct brw_cs_prog_data *prog_data,
                                       const unsigned *sizes);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // BRW_PRIVATE_H
