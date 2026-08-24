/*
 * Copyright (C) 2020 Collabora Ltd.
 * Copyright (C) 2022 Alyssa Rosenzweig
 * Copyright (C) 2025 Arm Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/u_debug.h"
#include "bi_debug.h"
#include "bifrost_compile.h"


/* clang-format off */
static const struct debug_named_value bifrost_debug_options[] = {
   {"shaders",    BIFROST_DBG_SHADERS,	   "Dump shaders in NIR and MIR"},
   {"shaderdb",   BIFROST_DBG_SHADERDB,	"Print statistics"},
   {"verbose",    BIFROST_DBG_VERBOSE,	   "Disassemble verbosely"},
   {"internal",   BIFROST_DBG_INTERNAL,	"Dump even internal shaders"},
   {"nosched",    BIFROST_DBG_NOSCHED, 	"Force trivial bundling"},
   {"nopsched",   BIFROST_DBG_NOPSCHED,   "Disable scheduling for pressure"},
   {"inorder",    BIFROST_DBG_INORDER, 	"Force in-order bundling"},
   {"novalidate", BIFROST_DBG_NOVALIDATE, "Skip IR validation"},
   {"noopt",      BIFROST_DBG_NOOPT,      "Skip optimization passes"},
   {"noidvs",     BIFROST_DBG_NOIDVS,     "Disable IDVS"},
   {"nosb",       BIFROST_DBG_NOSB,       "Disable scoreboarding"},
   {"nopreload",  BIFROST_DBG_NOPRELOAD,  "Disable message preloading"},
   {"spill",      BIFROST_DBG_SPILL,      "Test register spilling"},
   {"nossara",    BIFROST_DBG_NOSSARA,    "Disable SSA in register allocation"},
   {"statsabs",   BIFROST_DBG_STATSABS,   "Don't normalize statistics"},
   {"statsfull",  BIFROST_DBG_STATSFULL,  "Print verbose statistics"},
   {"debuginfo",  BIFROST_DBG_DEBUGINFO,  "Print debug information"},
   DEBUG_NAMED_VALUE_END
};
/* clang-format on */

DEBUG_GET_ONCE_FLAGS_OPTION(bifrost_debug, "BIFROST_MESA_DEBUG",
                            bifrost_debug_options, 0)

unsigned bifrost_debug = 0;

void
bifrost_init_debug_options() {
   bifrost_debug = debug_get_option_bifrost_debug();
}

uint32_t
bifrost_get_compiler_flags()
{
   bifrost_init_debug_options();
   return bifrost_debug;
}

bool
bifrost_will_dump_shaders(void)
{
   bifrost_init_debug_options();
   return bifrost_debug & BIFROST_DBG_SHADERS;
}

bool
bifrost_want_debug_info(void)
{
   bifrost_init_debug_options();
   return bifrost_debug & BIFROST_DBG_DEBUGINFO;
}

