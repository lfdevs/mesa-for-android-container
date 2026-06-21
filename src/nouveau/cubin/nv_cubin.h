/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */

#ifndef NV_CUBIN_PARSER_H
#define NV_CUBIN_PARSER_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Elf;

struct nv_cubin_function_param_info {
   uint32_t index;
   uint16_t ordinal;
   uint16_t offset;
   uint16_t size;
};

struct nv_cubin_function {
   /* from symbol table */
   uint32_t symbol_index;
   const char *name;

   uint8_t *code_ptr;
   size_t code_size;
   size_t func_offset; /* offset of function in the code section */

   uint32_t static_smem_size;

   /* from .nv.info */
   uint32_t slm_size;
   uint32_t gpr_count;

   /* from .nv.info.funcname */
   uint32_t crs_size;
   uint8_t num_control_barriers; /* shf_barriers on old version */

   uint16_t params_offset; /* offset of the param buffer in cb0 (root) */
   uint16_t params_size;   /* size of the param buffer in cb0 (root) */

   uint32_t param_info_count;
   struct nv_cubin_function_param_info *param_infos;
};

struct nv_cubin_module {
   struct Elf *elf;
   size_t shstrndx; /* section header string table index */

   uint32_t abi;
   uint32_t sm; /* arch */

   uint32_t function_count;
   struct nv_cubin_function *functions;
};

bool nv_cubin_module_init(struct nv_cubin_module *module, const void *data,
                          size_t size);
void nv_cubin_module_fini(struct nv_cubin_module *module);

const struct nv_cubin_function *
nv_cubin_module_find_function(const struct nv_cubin_module *module,
                              const char *name);

const struct nv_cubin_function_param_info *
nv_cubin_function_get_param_info(const struct nv_cubin_function *function,
                                 uint32_t index);

#endif