/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */

#ifndef NV_CUBIN_DEFS_H
#define NV_CUBIN_DEFS_H 1

#include <stdint.h>

/* common */

#define NV_ELF_OSABI_41 0x41

#define NVINFO_EIFMT_BVAL 2 /* byte value */
#define NVINFO_EIFMT_HVAL 3 /* half (short) value */
#define NVINFO_EIFMT_SVAL 4 /* sized value */

struct nvinfo_attribute_header_t {
   uint8_t format;
   uint8_t attribute;
   union {
      struct { uint8_t value; } bval;
      struct { uint16_t value; } hval;

      struct { uint16_t size; } sval;
   };
};

/* .nv.info */

#define NVINFO_EIATTR_MIN_STACK_SIZE 18
struct nvinfo_attr_min_stack_size_t {
   uint32_t symbol_index;
   uint32_t size;
};

#define NVINFO_EIATTR_REGCOUNT 47
struct nvinfo_attr_regcount_t {
   uint32_t symbol_index;
   uint32_t count;
};

/* .nv.info.function_name */

#define NVINFO_EIATTR_PARAM_CBANK 10
struct nvinfo_param_cbank_t {
   uint32_t unknown1;
   uint16_t params_offset;
   uint16_t params_size;
};

#define NVINFO_EIATTR_KPARAM_INFO 23
struct nvinfo_kparam_info_t {
   uint32_t index;
   uint16_t ordinal;
   uint16_t offset;
   uint32_t packed;
};

#define NVINFO_EIATTR_CRS_STACK_SIZE 30
struct nvinfo_crs_stack_size_t {
   uint32_t size;
};

#define NVINFO_EIATTR_NUM_BARRIERS 76
/* No structure, this is a BVAL. */

#endif