/*
 * Copyright 2026 LunarG, Inc.
 * Copyright 2026 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "ns_process_info.h"

#include <Foundation/Foundation.h>

bool
ns_is_os_version_at_least(uint32_t major, uint32_t minor, uint32_t patch)
{
   @autoreleasepool {
      NSOperatingSystemVersion version = (NSOperatingSystemVersion){major, minor, patch};
      return [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:version];
   }
}
