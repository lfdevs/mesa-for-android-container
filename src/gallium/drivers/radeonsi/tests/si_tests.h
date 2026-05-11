/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_TESTS_H
#define SI_TESTS_H

#include "util/u_stub_gfx_compute.h"

enum
{
   /* Tests: */
   DBG_TEST_CLEAR_BUFFER,
   DBG_TEST_COPY_BUFFER,
   DBG_TEST_IMAGE_COPY,
   DBG_TEST_COMPUTE_BLIT,
   DBG_TEST_VMFAULT_CP,
   DBG_TEST_VMFAULT_SHADER,
   DBG_TEST_DMA_PERF,
   DBG_TEST_MEM_PERF,
};

struct si_screen;

MESAPROC void si_run_tests(struct si_screen *sscreen) TAILV;

#endif /* SI_TESTS_H */
