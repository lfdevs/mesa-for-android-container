/*
 * Copyright 2024 Valve Corporation
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * This header adds definitions that are common between the CPU and the GPU for
 * shared headers. It also fills in basic standard library holes for internal
 * OpenCL.
 */

#include <stdint.h>
#include "util/macros.h"

#ifndef __OPENCL_VERSION__

/* Structures defined in common host/device headers that include device pointers
 * need to resolve to a real pointer in OpenCL but an opaque 64-bit address on
 * the host. The DEVICE macro facilitates that.
 */
#define DEVICE(type_) uint64_t

/* However, inline functions defined in common host/device headers that take
 * pointers need to resolve to pointers on either host or device. (Host pointers
 * on the host, device pointers on the device.) This would be automatic with
 * OpenCL generic pointers, but those can cause headaches and lose constantness,
 * so these defines allow GLOBAL/CONST keywords to be used even in CPU code.
 * Annoyingly, we can't use global/constant here because it conflicts with C++
 * standard library headers.
 */
#define GLOBAL
#define CONST const

#else

/* GenXML likes to use fp16. Since fp16 is supported by all grown up drivers, we
 * just enable the extension everywhere.
 */
#pragma OPENCL EXTENSION cl_khr_fp16 : enable

/* The OpenCL side of DEVICE must resolve to real pointer types, unlike
 * the host version.
 */
#define DEVICE(type_)   global type_ *

/* Passthrough */
#define GLOBAL global
#define CONST constant

/* OpenCL C defines work-item functions to return a scalar for a particular
 * dimension. This is a really annoying papercut, and is not what you want for
 * either 1D or 3D dispatches.  In both cases, it's nicer to get vectors. For
 * syntax, we opt to define uint3 "magic globals" for each work-item vector.
 * This matches the GLSL convention, although retaining OpenCL names. For
 * example, `gl_GlobalInvocationID.xy` is expressed here as `cl_global_id.xy`.
 * That is much nicer than standard OpenCL C's syntax `(uint2)(get_global_id(0),
 * get_global_id(1))`.
 *
 * We define the obvious mappings for each relevant function in "Work-Item
 * Functions" in the OpenCL C specification.
 */
#define _CL_WORKITEM3(name) ((uint3)(name(0), name(1), name(2)))

#define cl_global_size         _CL_WORKITEM3(get_global_size)
#define cl_global_id           _CL_WORKITEM3(get_global_id)
#define cl_local_size          _CL_WORKITEM3(get_local_size)
#define cl_enqueued_local_size _CL_WORKITEM3(get_enqueued_local_size)
#define cl_local_id            _CL_WORKITEM3(get_local_id)
#define cl_num_groups          _CL_WORKITEM3(get_num_groups)
#define cl_group_id            _CL_WORKITEM3(get_group_id)
#define cl_global_offset       _CL_WORKITEM3(get_global_offset)

/* NIR's precompilation infrastructure requires specifying a workgroup size with
 * the kernel, via reqd_work_group_size. Unfortunately, reqd_work_group_size has
 * terrible ergonomics, so we provide these aliases instead.
 */
#define KERNEL3D(x, y, z)                                                      \
   __attribute__((reqd_work_group_size(x, y, z))) kernel void

#define KERNEL2D(x, y)   KERNEL3D(x, y, 1)
#define KERNEL(x)        KERNEL2D(x, 1)

/* stddef.h usually defines this. We don't have that on the OpenCL side but we
 * can use the builtin.
 */
#define offsetof(x, y) __builtin_offsetof(x, y)

/* This is not an exact match for the util/macros.h version but without the
 * aligned(4) we get garbage code gen and in practice this is what you want.
 */
#ifdef PACKED
#undef PACKED
#endif
#define PACKED __attribute__((packed, aligned(4)))

/* This is the unreachable macro from macros.h that uses __builtin_unreachable,
 * which is a clang builtin available in OpenCL C.
 */
#define UNREACHABLE(str)                                                       \
   do {                                                                        \
      (void)"" str; /* str must be a string literal */                         \
      assert(!str);                                                            \
      __builtin_unreachable();                                                 \
   } while (0)

static inline uint16_t
_mesa_float_to_half(float f)
{
   return as_ushort(convert_half(f));
}

static inline float
_mesa_half_to_float(uint16_t w)
{
   return convert_float(as_half(w));
}

#endif
