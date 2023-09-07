/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_COMPUTE_H
#define SI_COMPUTE_H

#include "si_shader.h"
#include "util/u_inlines.h"

struct si_compute {
   struct si_shader_selector sel;
   struct si_shader shader;

   unsigned ir_type;
   unsigned input_size;

   int max_global_buffers;
   struct pipe_resource **global_buffers;
};

void si_destroy_compute(struct si_compute *program);

static inline void si_compute_reference(struct si_compute **dst, struct si_compute *src)
{
   if (pipe_reference(&(*dst)->sel.base.reference, &src->sel.base.reference))
      si_destroy_compute(*dst);

   *dst = src;
}

#endif /* SI_COMPUTE_H */
