/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef MDA_DEBUG_ARCHIVE_H
#define MDA_DEBUG_ARCHIVE_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct debug_archiver debug_archiver;

debug_archiver *debug_archiver_open(void *mem_ctx, const char *name, const char *info);

void debug_archiver_set_prefix(debug_archiver *da, const char *prefix);

void debug_archiver_write_file(debug_archiver *da, const char *filename, const char *data, unsigned size);

FILE *debug_archiver_start_file(debug_archiver *da, const char *filename);
void debug_archiver_finish_file(debug_archiver *da);

void debug_archiver_close(debug_archiver *da);

#ifdef __cplusplus
}
#endif

#endif /* MDA_DEBUG_ARCHIVE_H */
