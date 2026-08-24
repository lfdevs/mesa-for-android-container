/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#include "util/glheader.h"

#include "main/api_trace_helpers.h"

void
_mesa_trace_format_array(char *buf, size_t buflen,
                         const void *arr, size_t n,
                         enum mesa_trace_elem_kind kind)
{
   static const size_t MAX_TRACE_ARRAY = 16;

   if (!arr) {
      snprintf(buf, buflen, "(null)");
      return;
   }

   size_t shown = (n < MAX_TRACE_ARRAY) ? n : MAX_TRACE_ARRAY;
   size_t pos = 0;
   int w;

   w = snprintf(buf + pos, buflen - pos, "[");
   if (w < 0 || (size_t)w >= buflen - pos)
      return;
   pos += w;

   for (size_t i = 0; i < shown; i++) {
      if (i > 0) {
         w = snprintf(buf + pos, buflen - pos, ", ");
         if (w < 0 || (size_t)w >= buflen - pos)
            return;
         pos += w;
      }

      switch (kind) {
      case MESA_TRACE_ELEM_FLOAT:
         w = snprintf(buf + pos, buflen - pos, "%f",
                      ((const GLfloat *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_DOUBLE:
         w = snprintf(buf + pos, buflen - pos, "%f",
                      ((const GLdouble *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_BYTE:
         w = snprintf(buf + pos, buflen - pos, "%d",
                      ((const GLbyte *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_SHORT:
         w = snprintf(buf + pos, buflen - pos, "%d",
                      ((const GLshort *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_INT:
         w = snprintf(buf + pos, buflen - pos, "%d",
                      ((const GLint *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_UBYTE:
         w = snprintf(buf + pos, buflen - pos, "%u",
                      ((const GLubyte *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_USHORT:
         w = snprintf(buf + pos, buflen - pos, "%u",
                      ((const GLushort *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_UINT:
         w = snprintf(buf + pos, buflen - pos, "%u",
                      ((const GLuint *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_HALF:
         w = snprintf(buf + pos, buflen - pos, "0x%x",
                      ((const GLhalfNV *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_INT64:
         w = snprintf(buf + pos, buflen - pos, "%" PRId64,
                      (int64_t)((const GLint64 *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_UINT64:
         w = snprintf(buf + pos, buflen - pos, "%" PRIu64,
                      (uint64_t)((const GLuint64 *)arr)[i]);
         break;
      case MESA_TRACE_ELEM_INTPTR:
         w = snprintf(buf + pos, buflen - pos, "%" PRIdPTR,
                      (intptr_t)((const GLintptr *)arr)[i]);
         break;
      }
      if (w < 0 || (size_t)w >= buflen - pos)
         return;
      pos += w;
   }

   if (n > MAX_TRACE_ARRAY)
      snprintf(buf + pos, buflen - pos, ", ... %zu of %zu]", shown, n);
   else
      snprintf(buf + pos, buflen - pos, "]");
}

void
_mesa_trace_format_bitfield_group(char *buf, size_t buflen,
                                  const char *enum_names[],
                                  const unsigned enum_values[],
                                  size_t num_enums,
                                  unsigned mask)
{
   if (mask == 0) {
      snprintf(buf, buflen, "0");
      return;
   }

   size_t pos = 0;
   int w;

   for (size_t i = 0; i < num_enums; ++i) {
      unsigned val = enum_values[i];
      assert(val != 0);
      if ((mask & val) != val)
         continue;

      w = snprintf(buf + pos, buflen - pos, "%sGL_%s",
                   pos > 0 ? " | " : "", enum_names[i]);
      if (w < 0 || (size_t)w >= buflen - pos)
         return;
      pos += w;

      mask &= ~val;
   }

   if (mask != 0) {
      /* report remaining, unknown bits */
      snprintf(buf + pos, buflen - pos, "%s0x%x",
               pos > 0 ? " | " : "", mask);
   }
}
