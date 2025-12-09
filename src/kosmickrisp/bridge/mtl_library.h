/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef MTL_LIBRARY_H
#define MTL_LIBRARY_H 1

#include "mtl_types.h"

mtl_library *mtl_new_library(mtl_device *device, const char *src);
mtl_function *mtl_new_function_with_name(mtl_library *lib,
                                         const char *entry_point);

#endif /* MTL_LIBRARY_H */
