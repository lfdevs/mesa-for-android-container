/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>

enum mesa_trace_elem_kind {
   MESA_TRACE_ELEM_FLOAT,
   MESA_TRACE_ELEM_DOUBLE,
   MESA_TRACE_ELEM_BYTE,
   MESA_TRACE_ELEM_SHORT,
   MESA_TRACE_ELEM_INT,
   MESA_TRACE_ELEM_UBYTE,
   MESA_TRACE_ELEM_USHORT,
   MESA_TRACE_ELEM_UINT,
   MESA_TRACE_ELEM_HALF,
   MESA_TRACE_ELEM_INT64,
   MESA_TRACE_ELEM_UINT64,
   MESA_TRACE_ELEM_INTPTR,
};

void
_mesa_trace_format_array(char *buf, size_t buflen,
                         const void *arr, size_t n,
                         enum mesa_trace_elem_kind kind);

void
_mesa_trace_format_bitfield_group(char *buf, size_t buflen,
                                  const char *enum_names[],
                                  const unsigned enum_values[],
                                  size_t num_enums,
                                  unsigned mask);
