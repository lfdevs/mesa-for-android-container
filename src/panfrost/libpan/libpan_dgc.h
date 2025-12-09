/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef LIBPAN_DGC_H
#define LIBPAN_DGC_H
#include "libpan.h"

enum panlib_barrier {
   PANLIB_BARRIER_NONE = 0,
   PANLIB_BARRIER_JM_BARRIER = (1 << 0),
   PANLIB_BARRIER_JM_SUPPRESS_PREFETCH = (1 << 1),
   /* On panvk, increment the syncobj on the compute subqueue */
   PANLIB_BARRIER_CSF_SYNC = (1 << 2),
   /* On panvk, insert a WAIT on the compute dispatch */
   PANLIB_BARRIER_CSF_WAIT = (1 << 3),
};

struct panlib_precomp_grid {
   uint32_t count[3];

   union {
      struct {
         uint16_t local_dep;
         uint16_t global_dep;
      } jm;
   };
};

static struct panlib_precomp_grid
panlib_3d(uint32_t x, uint32_t y, uint32_t z)
{
   return (struct panlib_precomp_grid){.count = {x, y, z}};
}

static struct panlib_precomp_grid
panlib_3d_with_jm_deps(uint32_t x, uint32_t y, uint32_t z, uint16_t local_dep,
                       uint16_t global_dep)
{
   return (struct panlib_precomp_grid){
      .count = {x, y, z},
      .jm = {local_dep, global_dep},
   };
}

static struct panlib_precomp_grid
panlib_1d(uint32_t x)
{
   return panlib_3d(x, 1, 1);
}

static struct panlib_precomp_grid
panlib_1d_with_jm_deps(uint32_t x, uint16_t local_dep, uint16_t global_dep)
{
   return panlib_3d_with_jm_deps(x, 1, 1, local_dep, global_dep);
}

#endif
