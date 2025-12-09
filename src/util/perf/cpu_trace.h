/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef CPU_TRACE_H
#define CPU_TRACE_H

#include "u_perfetto.h"
#include "u_gpuvis.h"
#include "u_sysprof.h"

#include "util/detect_os.h"
#include "util/macros.h"
#include "util/os_time.h"

struct mesa_trace_flow {
   uint64_t id;
   int64_t start_time;
};

#if defined(HAVE_PERFETTO)

/* note that util_perfetto_is_tracing_enabled always returns false util
 * util_perfetto_init is called
 */
#define _MESA_TRACE_BEGIN(name)                                              \
   do {                                                                      \
      if (unlikely(util_perfetto_is_tracing_enabled()))                      \
         util_perfetto_trace_begin(name);                                    \
   } while (0)

#define _MESA_TRACE_FLOW_BEGIN(name, id)                                     \
   do {                                                                      \
      if (unlikely(util_perfetto_is_tracing_enabled()))                      \
         util_perfetto_trace_begin_flow(name, id);                           \
   } while (0)

#define _MESA_TRACE_END()                                                    \
   do {                                                                      \
      if (unlikely(util_perfetto_is_tracing_enabled()))                      \
         util_perfetto_trace_end();                                          \
   } while (0)

#define _MESA_TRACE_SET_COUNTER(name, value)                                 \
   do {                                                                      \
      if (unlikely(util_perfetto_is_tracing_enabled()))                      \
         util_perfetto_counter_set(name, value);                             \
   } while (0)

#define _MESA_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)      \
   do {                                                                             \
      if (unlikely(util_perfetto_is_tracing_enabled()))                             \
         util_perfetto_trace_full_begin(name, track_id, flow_id, clock, timestamp); \
   } while (0)

#define _MESA_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)                 \
   do {                                                                             \
      if (unlikely(util_perfetto_is_tracing_enabled()))                             \
         util_perfetto_trace_full_end(name, track_id, clock, timestamp);            \
   } while (0)

/* NOTE: for now disable atrace for C++ to workaround a ndk bug with ordering
 * between stdatomic.h and atomic.h.  See:
 *
 *   https://github.com/android/ndk/issues/1178
 */
#elif DETECT_OS_ANDROID && !defined(__cplusplus)

#include <cutils/trace.h>

#define _MESA_TRACE_BEGIN(name)                                              \
   atrace_begin(ATRACE_TAG_GRAPHICS, name)
#define _MESA_TRACE_END() atrace_end(ATRACE_TAG_GRAPHICS)
#define _MESA_TRACE_FLOW_BEGIN(name, id)                                     \
   atrace_begin(ATRACE_TAG_GRAPHICS, name)
#define _MESA_TRACE_SET_COUNTER(name, value)
#define _MESA_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)
#define _MESA_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)
#else

#define _MESA_TRACE_BEGIN(name)
#define _MESA_TRACE_END()
#define _MESA_TRACE_FLOW_BEGIN(name, id)
#define _MESA_TRACE_SET_COUNTER(name, value)
#define _MESA_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)
#define _MESA_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)

#endif /* HAVE_PERFETTO */

#if defined(HAVE_GPUVIS)

#define _MESA_GPUVIS_TRACE_BEGIN(name) util_gpuvis_begin(name)
#define _MESA_GPUVIS_TRACE_END() util_gpuvis_end()

#else

#define _MESA_GPUVIS_TRACE_BEGIN(name)
#define _MESA_GPUVIS_TRACE_END()

#endif /* HAVE_GPUVIS */

#if defined(HAVE_SYSPROF)

#define _MESA_SYSPROF_TRACE_BEGIN(name) util_sysprof_begin(name)
#define _MESA_SYSPROF_TRACE_END(scope) util_sysprof_end(scope)

#else

#define _MESA_SYSPROF_TRACE_BEGIN(name) NULL
#define _MESA_SYSPROF_TRACE_END(scope)

#endif /* HAVE_SYSPROF */

#if __has_attribute(cleanup) && __has_attribute(unused)

#include <stdarg.h>
#include <stdio.h>

#define _MESA_TRACE_SCOPE_MAX_NAME_LENGTH 4096

#define _MESA_TRACE_SCOPE_VAR_CONCAT(name, suffix) name##suffix
#define _MESA_TRACE_SCOPE_VAR(suffix)                                        \
   _MESA_TRACE_SCOPE_VAR_CONCAT(_mesa_trace_scope_, suffix)

/* This must expand to a single non-scoped statement for
 *
 *    if (cond)
 *       _MESA_TRACE_SCOPE(format, ...)
 *
 * to work.
 */
#define _MESA_TRACE_SCOPE_NAME(name)                                         \
   void *_MESA_TRACE_SCOPE_VAR(__LINE__)                                     \
      __attribute__((cleanup(_mesa_trace_scope_end), unused)) =              \
         _mesa_trace_scope_begin_name(name)

#define _MESA_TRACE_SCOPE(format, ...)                                       \
   void *_MESA_TRACE_SCOPE_VAR(__LINE__)                                     \
      __attribute__((cleanup(_mesa_trace_scope_end), unused)) =              \
         _mesa_trace_scope_begin(format, ##__VA_ARGS__)

#define _MESA_TRACE_SCOPE_FLOW(name, id)                                     \
   void *_MESA_TRACE_SCOPE_VAR(__LINE__)                                     \
      __attribute__((cleanup(_mesa_trace_scope_end), unused)) =              \
         _mesa_trace_scope_flow_begin(name, id)


static inline void *
_mesa_trace_scope_begin_name(const char *name)
{
   void *scope = NULL;
   _MESA_TRACE_BEGIN(name);
   _MESA_GPUVIS_TRACE_BEGIN(name);
   scope = _MESA_SYSPROF_TRACE_BEGIN(name);
   return scope;
}

__attribute__((format(printf, 1, 2)))
static inline void *
_mesa_trace_scope_begin(const char *format, ...)
{
   char name[_MESA_TRACE_SCOPE_MAX_NAME_LENGTH];
   va_list args;

   va_start(args, format);
   ASSERTED size_t len = vsnprintf(name, _MESA_TRACE_SCOPE_MAX_NAME_LENGTH,
                                   format, args);
   va_end(args);
   assert(len < _MESA_TRACE_SCOPE_MAX_NAME_LENGTH);

   return _mesa_trace_scope_begin_name(name);
}

static inline void *
_mesa_trace_scope_flow_begin(const char *name,
			     struct mesa_trace_flow *flow)
{
   void *scope = NULL;

   if (flow->id == 0) {
      flow->id = util_perfetto_next_id();
      flow->start_time = os_time_get_nano();
   }

   _MESA_TRACE_FLOW_BEGIN(name, flow->id);
   _MESA_GPUVIS_TRACE_BEGIN(name);
   scope = _MESA_SYSPROF_TRACE_BEGIN(name);
   return scope;
}

static inline void
_mesa_trace_scope_end(UNUSED void **scope)
{
   _MESA_GPUVIS_TRACE_END();
   _MESA_TRACE_END();
   _MESA_SYSPROF_TRACE_END(scope);
}

#else

#define _MESA_TRACE_SCOPE(format, ...)
#define _MESA_TRACE_SCOPE_FLOW(func, id)
#define _MESA_TRACE_SCOPE_NAME(name)

#endif /* __has_attribute(cleanup) && __has_attribute(unused) */

#define MESA_TRACE_SCOPE(format, ...) _MESA_TRACE_SCOPE(format, ##__VA_ARGS__)
#define MESA_TRACE_SCOPE_FLOW(name, id) _MESA_TRACE_SCOPE_FLOW(name, id)
#define MESA_TRACE_FUNC() _MESA_TRACE_SCOPE_NAME(__func__)
#define MESA_TRACE_FUNC_FLOW(id) _MESA_TRACE_SCOPE_FLOW(__func__, id)
#define MESA_TRACE_SET_COUNTER(name, value) _MESA_TRACE_SET_COUNTER(name, value)
#define MESA_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp) \
   _MESA_TRACE_TIMESTAMP_BEGIN(name, track_id, flow_id, clock, timestamp)
#define MESA_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp) \
   _MESA_TRACE_TIMESTAMP_END(name, track_id, clock, timestamp)

static inline void
util_cpu_trace_init()
{
#if defined(HAVE_PERFETTO)
   util_perfetto_init();
#elif DETECT_OS_ANDROID && !defined(__cplusplus)
   atrace_init();
#endif /* HAVE_PERFETTO */

   util_gpuvis_init();
}

#endif /* CPU_TRACE_H */
