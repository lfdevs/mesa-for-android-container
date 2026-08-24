/*
 * Copyright © 2026 Google LLC.
 * Copyright © 2025 Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_NIR_H
#define PANVK_NIR_H

#include "nir.h"

bool panvk_nir_lower_cooperative_matrix(nir_shader *nir,
                                        unsigned subgroup_size);

bool panvk_nir_lower_tile_image(nir_shader *nir, uint32_t *color_read_out,
                                bool *z_read_out, bool *s_read_out);

#endif
