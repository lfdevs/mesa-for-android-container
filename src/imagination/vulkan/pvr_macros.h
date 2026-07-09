/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_MACROS_H
#define PVR_MACROS_H

#include "util/u_debug.h"

#ifdef HAVE_VALGRIND
#   include <valgrind/valgrind.h>
#   include <valgrind/memcheck.h>
#   define VG(x) x
#else
#   define VG(x) ((void)0)
#endif

/**
 * Print a FINISHME message, including its source location.
 *
 * Suppressed when PVR_IGNORE_FINISHME_WARNINGS environment variable is set.
 */
#define pvr_finishme(format, ...)              \
   do {                                        \
      static bool reported = false;            \
      if (!reported) {                         \
         if (!debug_get_bool_option("PVR_IGNORE_FINISHME_WARNINGS", false)) { \
            mesa_logw("%s:%d: FINISHME: " format, \
                      __FILE__,                \
                      __LINE__,                \
                      ##__VA_ARGS__);          \
         }                                     \
         reported = true;                      \
      }                                        \
   } while (false)

#define PVR_WRITE(_buffer, _value, _offset, _max)                \
   do {                                                          \
      __typeof__(_value) __value = _value;                       \
      uint64_t __offset = _offset;                               \
      uint32_t __nr_dwords = sizeof(__value) / sizeof(uint32_t); \
      static_assert(__same_type(*_buffer, __value),              \
                    "Buffer and value type mismatch");           \
      assert((__offset + __nr_dwords) <= (_max));                \
      assert((__offset % __nr_dwords) == 0U);                    \
      _buffer[__offset / __nr_dwords] = __value;                 \
   } while (0)

#define PVR_ARCH_NAME(name, arch) pvr_##arch##_##name

#define PVR_ARCH_DISPATCH(name, arch, ...)        \
   do {                                           \
      switch (arch) {                             \
      case PVR_DEVICE_ARCH_ROGUE:                 \
         PVR_ARCH_NAME(name, rogue)(__VA_ARGS__); \
         break;                                   \
      default:                                    \
         UNREACHABLE("Unsupported architecture"); \
      }                                           \
   } while (0)

#define PVR_ARCH_DISPATCH_RET(name, arch, ret, ...)     \
   do {                                                 \
      switch (arch) {                                   \
      case PVR_DEVICE_ARCH_ROGUE:                       \
         ret = PVR_ARCH_NAME(name, rogue)(__VA_ARGS__); \
         break;                                         \
      default:                                          \
         UNREACHABLE("Unsupported architecture");       \
      }                                                 \
   } while (0)

#if defined(PVR_BUILD_ARCH_ROGUE)
#   define PVR_PER_ARCH(name) PVR_ARCH_NAME(name, rogue)
#endif

#endif /* PVR_MACROS_H */
