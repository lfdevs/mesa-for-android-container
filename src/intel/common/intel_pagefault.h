/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

enum intel_pagefault_access {
   INTEL_PAGEFAULT_ACCESS_READ,
   INTEL_PAGEFAULT_ACCESS_WRITE,
   INTEL_PAGEFAULT_ACCESS_ATOMIC,
};

enum intel_pagefault_type {
   INTEL_PAGEFAULT_TYPE_NOT_PRESENT,
   INTEL_PAGEFAULT_TYPE_WRITE_ACCESS,
   INTEL_PAGEFAULT_TYPE_ATOMIC_ACCESS,
};

enum intel_pagefault_level {
   INTEL_PAGEFAULT_LEVEL_PTE,
   INTEL_PAGEFAULT_LEVEL_PDE,
   INTEL_PAGEFAULT_LEVEL_PDP,
   INTEL_PAGEFAULT_LEVEL_PML4,
   INTEL_PAGEFAULT_LEVEL_PML5,
};

struct intel_pagefault_info {
    uint64_t address;
    uint32_t precision;
    enum intel_pagefault_access access;
    enum intel_pagefault_type type;
    enum intel_pagefault_level level;
};

struct intel_pagefault_buffer {
   unsigned size;
   struct intel_pagefault_info items[];
};

const char *
intel_pagefault_access_to_string(enum intel_pagefault_access access);
const char *
intel_pagefault_type_to_string(enum intel_pagefault_type type);
const char *
intel_pagefault_level_to_string(enum intel_pagefault_level level);
