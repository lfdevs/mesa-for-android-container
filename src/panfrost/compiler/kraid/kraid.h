/* Copyright © 2026 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef KRAID_H
#define KRAID_H

#include "panfrost/compiler/pan_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

void kraid_compile_nir(nir_shader *nir,
                       const struct pan_compile_inputs *inputs,
                       struct util_dynarray *binary,
                       struct pan_shader_info *info);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* KRAID_H */
