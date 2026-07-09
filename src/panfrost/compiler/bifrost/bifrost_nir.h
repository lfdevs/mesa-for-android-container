/*
 * Copyright (C) 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include "nir.h"
#include "nir_builder.h"

bool bifrost_nir_lower_algebraic_late(nir_shader *shader, unsigned gpu_arch,
                                      bool is_kraid);
bool bifrost_nir_opt_boolean_bitwise(nir_shader *shader);
