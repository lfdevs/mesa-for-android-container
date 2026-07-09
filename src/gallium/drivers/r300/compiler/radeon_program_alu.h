/*
 * Copyright 2008 Nicolai Haehnle.
 * SPDX-License-Identifier: MIT
 */

#ifndef __RADEON_PROGRAM_ALU_H_
#define __RADEON_PROGRAM_ALU_H_

#include "radeon_program.h"

int radeonTransformALU(struct radeon_compiler *c, struct rc_instruction *inst, void *);

int r300_transform_vertex_alu(struct radeon_compiler *c, struct rc_instruction *inst, void *);

#endif /* __RADEON_PROGRAM_ALU_H_ */
