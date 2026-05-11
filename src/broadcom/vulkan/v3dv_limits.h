/*
 * Copyright © 2020 Raspberry Pi Ltd
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
#ifndef V3DV_LIMITS_H
#define V3DV_LIMITS_H

#include "drm-uapi/v3d_drm.h"
#include "util/detect_os.h"

#if DETECT_OS_ANDROID
#define V3DV_MAX_QUEUES 4
#else
#define V3DV_MAX_QUEUES 1
#endif


/* From vulkan spec "If the multiple viewports feature is not enabled,
 * scissorCount must be 1", ditto for viewportCount. For now we don't support
 * that feature.
 */
#define MAX_VIEWPORTS 1
#define MAX_SCISSORS  1

#define MAX_VBS 16
#define MAX_VERTEX_ATTRIBS 16

#define MAX_SETS 16

#define MAX_PUSH_CONSTANTS_SIZE 128

#define MAX_SAMPLED_IMAGES 16
#define MAX_STORAGE_IMAGES 4
#define MAX_INPUT_ATTACHMENTS 4

#define MAX_UNIFORM_BUFFERS 16
#define MAX_INLINE_UNIFORM_BUFFERS 4
#define MAX_INLINE_UNIFORM_BLOCK_SIZE 4096
#define MAX_STORAGE_BUFFERS 8

#define MAX_DYNAMIC_UNIFORM_BUFFERS 8
#define MAX_DYNAMIC_STORAGE_BUFFERS 4
#define MAX_DYNAMIC_BUFFERS (MAX_DYNAMIC_UNIFORM_BUFFERS + \
                             MAX_DYNAMIC_STORAGE_BUFFERS)

#define MAX_MULTIVIEW_VIEW_COUNT 16

#define V3DV_SUPPORTED_SHADER_STAGES 4

#define V3DV_MAX_PLANE_COUNT 3

/* Minimum required by the Vulkan 1.1 spec */
#define MAX_MEMORY_ALLOCATION_SIZE (1ull << 30)

/* Maximum performance counters number */
#define V3D_MAX_PERFCNT 93

/* Number of perfmons required to handle all supported performance counters */
#define V3DV_MAX_PERFMONS DIV_ROUND_UP(V3D_MAX_PERFCNT, \
                                       DRM_V3D_MAX_PERF_COUNTERS)

#define MAX_STAGES 3

#define MAX_TOTAL_TEXTURE_SAMPLERS (V3D_MAX_TEXTURE_SAMPLERS * MAX_STAGES)

/* This tracks state BOs for both textures and samplers, so we
 * multiply by 2.
 */
#define MAX_TOTAL_STATES (2 * V3D_MAX_TEXTURE_SAMPLERS * MAX_STAGES)

#define MAX_TOTAL_UNIFORM_BUFFERS ((MAX_UNIFORM_BUFFERS + \
                                    MAX_INLINE_UNIFORM_BUFFERS) * MAX_STAGES)
#define MAX_TOTAL_STORAGE_BUFFERS (MAX_STORAGE_BUFFERS * MAX_STAGES)

/* These are tunable parameters in the HW design, but all the V3D
 * implementations agree.
 */
#define V3D_UIFCFG_BANKS 8
#define V3D_UIFCFG_PAGE_SIZE 4096
#define V3D_UIFCFG_XOR_VALUE (1 << 4)
#define V3D_PAGE_CACHE_SIZE (V3D_UIFCFG_PAGE_SIZE * V3D_UIFCFG_BANKS)
#define V3D_UBLOCK_SIZE 64
#define V3D_UIFBLOCK_SIZE (4 * V3D_UBLOCK_SIZE)
#define V3D_UIFBLOCK_ROW_SIZE (4 * V3D_UIFBLOCK_SIZE)

#define PAGE_UB_ROWS (V3D_UIFCFG_PAGE_SIZE / V3D_UIFBLOCK_ROW_SIZE)
#define PAGE_UB_ROWS_TIMES_1_5 ((PAGE_UB_ROWS * 3) >> 1)
#define PAGE_CACHE_UB_ROWS (V3D_PAGE_CACHE_SIZE / V3D_UIFBLOCK_ROW_SIZE)
#define PAGE_CACHE_MINUS_1_5_UB_ROWS (PAGE_CACHE_UB_ROWS - PAGE_UB_ROWS_TIMES_1_5)

/* Pre-generating packets needs to consider changes in packet sizes across hw
 * versions. Keep things simple and allocate enough space for any supported
 * version. We ensure the size is large enough through static asserts.
 */
#define V3DV_TEXTURE_SHADER_STATE_LENGTH 32
#define V3DV_SAMPLER_STATE_LENGTH 24
#define V3DV_BLEND_CFG_LENGTH 5
#define V3DV_CFG_BITS_LENGTH 4
#define V3DV_GL_SHADER_STATE_RECORD_LENGTH 36
#define V3DV_VCM_CACHE_SIZE_LENGTH 2
#define V3DV_GL_SHADER_STATE_ATTRIBUTE_RECORD_LENGTH 16
#define V3DV_STENCIL_CFG_LENGTH 6

#endif /* V3DV_LIMITS_H */
