/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 */

#ifndef __MIDGARD_H_
#define __MIDGARD_H_

#include "compiler/nir/nir.h"
#include "util/u_dynarray.h"

/* Define the general compiler entry point */

#define MAX_SYSVAL_COUNT 32

/* Allow 2D of sysval IDs, while allowing nonparametric sysvals to equal
 * their class for equal comparison */

#define PAN_SYSVAL(type, no) ((no << 16) | PAN_SYSVAL_##type)

/* Define some common types. We start at one for easy indexing of hash
 * tables internal to the compiler */

enum {
        PAN_SYSVAL_VIEWPORT_SCALE = 1,
        PAN_SYSVAL_VIEWPORT_OFFSET = 2,
} pan_sysval;

typedef struct {
        int work_register_count;
        int uniform_count;
        int uniform_cutoff;

        int attribute_count;
        int varying_count;

        /* Prepended before uniforms, mapping to SYSVAL_ names for the
         * sysval */

        unsigned sysval_count;
        unsigned sysvals[MAX_SYSVAL_COUNT];

        unsigned varyings[32];

        /* Boolean properties of the program */
        bool can_discard;
        bool writes_point_size;

        int first_tag;

        struct util_dynarray compiled;

        /* For a blend shader using a constant color -- patch point. If
         * negative, there's no constant. */

        int blend_patch_offset;

        /* IN: For a fragment shader with a lowered alpha test, the ref value */
        float alpha_ref;
} midgard_program;

int
midgard_compile_shader_nir(nir_shader *nir, midgard_program *program, bool is_blend);

/* NIR options are shared between the standalone compiler and the online
 * compiler. Defining it here is the simplest, though maybe not the Right
 * solution. */

static const nir_shader_compiler_options midgard_nir_options = {
        .lower_ffma = true,
        .lower_sub = true,
        .lower_scmp = true,
        .lower_flrp32 = true,
        .lower_flrp64 = true,
        .lower_ffract = true,
        .lower_fmod32 = true,
        .lower_fmod64 = true,
        .lower_fdiv = true,
        .lower_idiv = true,
        .lower_isign = true,
        .lower_fpow = true,
        .lower_find_lsb = true,

        /* TODO: We have native ops to help here, which we'll want to look into
         * eventually */
        .lower_fsign = true,

        .vertex_id_zero_based = true,
        .lower_extract_byte = true,
        .lower_extract_word = true,

        .native_integers = true
};

#endif
