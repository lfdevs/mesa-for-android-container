/*
 * Copyright 2023 Pavel Ondračka <pavel.ondracka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#ifndef R300_NIR_H
#define R300_NIR_H

#include "pipe/p_screen.h"
#include "compiler/nir/nir.h"

char *r300_finalize_nir(struct pipe_screen *pscreen, void *nir);

extern bool r300_transform_vs_trig_input(struct nir_shader *shader);

extern bool r300_transform_fs_trig_input(struct nir_shader *shader);

extern bool r300_nir_fuse_fround_d3d9(struct nir_shader *shader);

extern bool r300_nir_lower_bool_to_float(struct nir_shader *shader);

#endif /* R300_NIR_H */
