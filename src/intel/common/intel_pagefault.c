/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_pagefault.h"

const char *
intel_pagefault_access_to_string(enum intel_pagefault_access access)
{
   static const char *const lookup[] = {
      [INTEL_PAGEFAULT_ACCESS_READ] = "Read",
      [INTEL_PAGEFAULT_ACCESS_WRITE] = "Write",
      [INTEL_PAGEFAULT_ACCESS_ATOMIC] = "Atomic",
   };
   return lookup[access];
}

const char *
intel_pagefault_type_to_string(enum intel_pagefault_type type)
{
   static const char *const lookup[] = {
      [INTEL_PAGEFAULT_TYPE_NOT_PRESENT] = "Not Present",
      [INTEL_PAGEFAULT_TYPE_WRITE_ACCESS] = "Not Writable",
      [INTEL_PAGEFAULT_TYPE_ATOMIC_ACCESS] = "Atomic",
   };
   return lookup[type];
}

const char *
intel_pagefault_level_to_string(enum intel_pagefault_level level)
{
   static const char *const lookup[] = {
      [INTEL_PAGEFAULT_LEVEL_PTE] = "PTE",
      [INTEL_PAGEFAULT_LEVEL_PDE] = "PDE",
      [INTEL_PAGEFAULT_LEVEL_PDP] = "PDP",
      [INTEL_PAGEFAULT_LEVEL_PML4] = "PML4",
      [INTEL_PAGEFAULT_LEVEL_PML5] = "PML5",
   };
   return lookup[level];
}
