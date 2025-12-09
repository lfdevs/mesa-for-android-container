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

#ifdef HAVE_VALGRIND
#   include <valgrind/valgrind.h>
#   include <valgrind/memcheck.h>
#   define VG(x) x
#else
#   define VG(x) ((void)0)
#endif

/**
 * Print a FINISHME message, including its source location.
 */
#define pvr_finishme(format, ...)              \
   do {                                        \
      static bool reported = false;            \
      if (!reported) {                         \
         mesa_logw("%s:%d: FINISHME: " format, \
                   __FILE__,                   \
                   __LINE__,                   \
                   ##__VA_ARGS__);             \
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

/* A non-fatal assert. Useful for debugging. */
#if MESA_DEBUG
#   define pvr_assert(x)                                           \
      ({                                                           \
         if (unlikely(!(x)))                                       \
            mesa_loge("%s:%d ASSERT: %s", __FILE__, __LINE__, #x); \
      })
#else
#   define pvr_assert(x)
#endif

#endif /* PVR_MACROS_H */
