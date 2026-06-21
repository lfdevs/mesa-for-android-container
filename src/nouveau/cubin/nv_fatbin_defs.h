/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */

#ifndef NV_FATBIN_DEFS_H
#define NV_FATBIN_DEFS_H 1

#include <stdint.h>

#define NV_FATBIN_HEADER_MAGIC 0xBA55ED50

struct nv_fatbin_header_t {
   uint32_t magic;
   uint16_t version;
   uint16_t header_size;
   uint64_t size;
};

#define NV_FATBIN_OBJECT_KIND_PTX      1
#define NV_FATBIN_OBJECT_KIND_BYTECODE 2

struct nv_fatbin_object_header_t {
   uint16_t kind;
   uint16_t unknown1;
   uint32_t header_size;
   uint64_t size;
   uint32_t unknown2;
   uint32_t unknown3;
   uint32_t unknown4;
   uint32_t sm;
};

#endif