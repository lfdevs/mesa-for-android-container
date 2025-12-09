/* Copyright (C) 2010 LunarG Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#include "glapi/glapi_priv.h"

#if defined(_GLAPI_ENTRY_ARCH_TLS_H)
#include _GLAPI_ENTRY_ARCH_TLS_H
#define MAPI_TMP_STUB_ASM_GCC_NO_HIDDEN
#else
/* C version of the public entries */
#define MAPI_TMP_DEFINES
#define MAPI_TMP_PUBLIC_ENTRIES_NO_HIDDEN
#endif /* defined(_GLAPI_ENTRY_ARCH_TLS_H) */

#include "es2_glapi_mapi_tmp.h"
