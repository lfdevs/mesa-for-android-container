/*
 * Copyright © 2025 Autumn Ashton
 * SPDX-License-Identifier: MIT
 */

#ifndef NV_FATBIN_H
#define NV_FATBIN_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct nv_fatbin_header_t;

struct nv_fatbin {
   const struct nv_fatbin_header_t *header;
   const uint8_t *data;
   size_t size;
};

bool nv_fatbin_init(struct nv_fatbin *fatbin, const void *data, size_t size);
bool nv_fatbin_get_bytecode(struct nv_fatbin *fatbin, uint32_t sm, bool backwards_compat,
                            const void **out_data, size_t *out_size);

bool nv_is_sm_compatible(uint32_t device_sm, uint32_t requested_sm, bool backwards_compat);

#endif