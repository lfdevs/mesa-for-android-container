/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_RESIDENCY_SET_H
#define MTL_RESIDENCY_SET_H 1

#include "mtl_types.h"

mtl_residency_set *mtl_new_residency_set(mtl_device *device);
void mtl_residency_set_add_allocation(mtl_residency_set *residency_set,
                                      mtl_allocation *allocation);
void mtl_residency_set_remove_allocation(mtl_residency_set *residency_set,
                                         mtl_allocation *allocation);
void mtl_residency_set_commit(mtl_residency_set *residency_set);
void mtl_residency_set_request_residency(mtl_residency_set *residency_set);
void mtl_residency_set_end_residency(mtl_residency_set *residency_set);

#endif /* MTL_RESIDENCY_SET_H */