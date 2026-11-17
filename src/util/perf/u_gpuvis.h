/*
 * Copyright 2023 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 */

#ifndef U_GPUVIS_H
#define U_GPUVIS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_GPUVIS

/* Not strictly necessary, but can be used to avoid initialization overhead on first begin call. */
void util_gpuvis_init(void);

/* All begin/end pairs must be balanced. */
void util_gpuvis_begin(const char *name);
void util_gpuvis_end(void);

#else

static inline void
util_gpuvis_init(void)
{
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* U_GPUVIS_H */