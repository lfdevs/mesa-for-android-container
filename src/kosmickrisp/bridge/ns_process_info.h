/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef NS_PROCESS_INFO_H
#define NS_PROCESS_INFO_H 1

#include <inttypes.h>
#include <stdbool.h>

bool ns_is_os_version_at_least(uint32_t major, uint32_t minor, uint32_t patch);

#endif /* NS_PROCESS_INFO_H */
