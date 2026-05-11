/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SI_MM_H
#define SI_MM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct si_screen;
struct si_context;

/* si_mm_context.c */
bool si_init_mm_context(struct si_screen *sscreen, struct si_context *sctx, unsigned flags);
void si_fini_mm_context(struct si_context *sctx);

/* si_mm_screen.c */
bool si_init_mm_screen(struct si_screen *sscreen);
void si_fini_mm_screen(struct si_screen *sscreen);

#ifdef __cplusplus
}
#endif

#endif /* SI_MM_H */
