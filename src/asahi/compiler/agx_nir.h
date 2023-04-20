/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */
#ifndef AGX_NIR_H
#define AGX_NIR_H

#include <stdbool.h>

struct nir_shader;

bool agx_nir_lower_algebraic_late(struct nir_shader *shader);

#endif
