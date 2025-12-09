/*
 * Copyright 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef MDA_TAR_H
#define MDA_TAR_H

/* Subset of the tar archive format.  The writer produces a fully valid tar file,
 * and the reader is capable to read files procuded by that writer.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "slice.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
   RECORD_SIZE = 512,
};

typedef long archive_pos;

typedef struct tar_writer {
   FILE *file;
   archive_pos header_pos;
   char header[RECORD_SIZE];
   bool error;
   const char *prefix;
   time_t timestamp;
} tar_writer;

void tar_writer_init(tar_writer *tw, FILE *f);
void tar_writer_start_file(tar_writer *tw, const char *filename);
void tar_writer_finish_file(tar_writer *tw);
void tar_writer_file_from_bytes(tar_writer *tw, const char *filename,
                                const char *contents, unsigned contents_size);

typedef struct {
   slice contents;

   bool error;

   archive_pos pos;
} tar_reader;

void tar_reader_init_from_bytes(tar_reader *tr, const char *contents, unsigned contents_size);

typedef struct {
   slice prefix;
   slice name;
   slice contents;

   time_t mtime;
} tar_reader_entry;

bool tar_reader_next(tar_reader *tr, tar_reader_entry *entry);

#ifdef __cplusplus
}
#endif

#endif /* MDA_TAR_H */
