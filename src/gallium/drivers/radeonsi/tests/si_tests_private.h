/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_TESTS_PRIVATE_H
#define SI_TESTS_PRIVATE_H

#include <stdint.h>

 /* si_test_image_copy_region.c */
void si_test_image_copy_region(struct si_screen *sscreen);
void si_test_blit(struct si_screen *sscreen, unsigned test_flags);

/* si_test_dma_perf.c */
void si_test_dma_perf(struct si_screen *sscreen);
void si_test_mem_perf(struct si_screen *sscreen);
void si_test_clear_buffer(struct si_screen *sscreen);
void si_test_copy_buffer(struct si_screen *sscreen);

/* si_test_vm_fault.c */
void si_test_vmfault(struct si_screen *sscreen, uint64_t test_flags);

#endif
