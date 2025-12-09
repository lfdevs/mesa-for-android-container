/* Copyright (C) 2010 LunarG Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef _GLAPI_PRIV_H
#define _GLAPI_PRIV_H

#include "glapi/glapi/glapi.h"
#include "util/detect_arch.h"

/* REALLY_INITIAL_EXEC implies __GLIBC__ */
#if defined(REALLY_INITIAL_EXEC)
#if DETECT_ARCH_X86 && (MESA_SYSTEM_HAS_KMS_DRM || DETECT_OS_HURD)
#define _GLAPI_ENTRY_ARCH_TLS_H "glapi/entry_x86_tls.h"
#elif DETECT_ARCH_X86_64 && MESA_SYSTEM_HAS_KMS_DRM
#define _GLAPI_ENTRY_ARCH_TLS_H "glapi/entry_x86-64_tls.h"
#elif DETECT_ARCH_PPC_64 && UTIL_ARCH_LITTLE_ENDIAN && MESA_SYSTEM_HAS_KMS_DRM
#define _GLAPI_ENTRY_ARCH_TLS_H "glapi/entry_ppc64le_tls.h"
#endif
#endif /* defined(REALLY_INITIAL_EXEC) */

#endif /* _GLAPI_PRIV_H */
