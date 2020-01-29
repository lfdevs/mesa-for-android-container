/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#ifndef __PAN_BLEND_H
#define __PAN_BLEND_H

#include "util/hash_table.h"

/* An internal blend shader descriptor, from the compiler */

struct panfrost_blend_shader {
        struct panfrost_context *ctx;

        /* The compiled shader */
        void *buffer;

        /* Byte count of the shader */
        unsigned size;

        /* Number of 128-bit work registers required by the shader */
        unsigned work_count;

        /* Offset into the shader to patch constants. Zero to disable patching
         * (it is illogical to have constants at offset 0). */
        unsigned patch_index;

        /* First instruction tag (for tagging the pointer) */
        unsigned first_tag;
};

/* A blend shader descriptor ready for actual use */

struct panfrost_blend_shader_final {
        /* GPU address where we're compiled to */
        uint64_t gpu;

        /* First instruction tag (for tagging the pointer) */
        unsigned first_tag;

        /* Same meaning as panfrost_blend_shader */
        unsigned work_count;
};

struct panfrost_blend_equation_final {
        struct mali_blend_equation *equation;
        float constant;
};

struct panfrost_blend_rt {
        /* If has_fixed_function is set, equation is the
         * fixed-function configuration for this blend state */

        bool has_fixed_function;
        struct mali_blend_equation equation;

        /* Mask of blend color components read */
        unsigned constant_mask;

        /* Regardless of fixed-function blending, this is a map of pipe_format
         * to panfrost_blend_shader */

        struct hash_table_u64 *shaders;
};

struct panfrost_blend_state {
        struct pipe_blend_state base;

        struct panfrost_blend_rt rt[PIPE_MAX_COLOR_BUFS];
};

/* Container for a final blend state, specialized to constants and a
 * framebuffer formats. */

struct panfrost_blend_final {
        /* Set for a shader, clear for an equation */
        bool is_shader;

        /* Set if the destination needs to be loaded from the tilebuffer,
         * basically (for an equation) or if a shader is present */
        bool no_blending;

        union {
                struct panfrost_blend_shader_final shader;
                struct panfrost_blend_equation_final equation;
        };
};

void
panfrost_blend_context_init(struct pipe_context *pipe);

struct panfrost_blend_final
panfrost_get_blend_for_context(struct panfrost_context *ctx, unsigned rt, struct panfrost_bo **bo, unsigned *shader_offset);

#endif
