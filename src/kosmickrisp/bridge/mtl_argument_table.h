/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_ARGUMENT_TABLE_H
#define KK_ARGUMENT_TABLE_H 1

#include "mtl_types.h"

#include <stdint.h>

/* MTLArgumentTableDescriptor */
mtl_argument_table_descriptor *mtl_new_argument_table_descriptor(void);
void mtl_set_max_buffer_binding_count(mtl_argument_table_descriptor *descriptor,
                                      uint32_t count);

/* MTLArgumentTable */
mtl_argument_table *
mtl_new_argument_table(mtl_device *dev,
                       mtl_argument_table_descriptor *descriptor);
void mtl_set_address(mtl_argument_table *table, uint64_t address,
                     uint32_t binding);

#endif /* KK_ARGUMENT_TABLE_H */