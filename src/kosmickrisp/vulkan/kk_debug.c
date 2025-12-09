/*
 * Copyright 2025 LunarG, Inc.
 * SPDX-License-Identifier: MIT
 */
#include "kk_debug.h"
#include "util/u_debug.h"

enum kk_debug kk_mesa_debug_flags = 0;

const struct debug_named_value flags[] = {
   {"nir", KK_DEBUG_NIR},
   {"msl", KK_DEBUG_MSL},
   {NULL, 0},
};

DEBUG_GET_ONCE_FLAGS_OPTION(mesa_kk_debug, "MESA_KK_DEBUG", flags, 0);

void
kk_process_debug_variable(void)
{
   kk_mesa_debug_flags = debug_get_option_mesa_kk_debug();
}
