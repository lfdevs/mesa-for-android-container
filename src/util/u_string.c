/*
 * Copyright 2022 Yonggang Luo
 * SPDX-License-Identifier: MIT
 *
 * String utils
 */

#include "util/u_string.h"
#if DETECT_OS_WINDOWS
#include <windows.h>
#endif

#if DETECT_OS_WINDOWS

static char *
strdup_wstr_codepage(unsigned codepage, const wchar_t *utf16_str)
{
   if (!utf16_str)
      return NULL;
   const int multi_byte_length = WideCharToMultiByte(codepage, 0, utf16_str, -1, NULL,
                                                      0, NULL, NULL);
   char* multi_byte = malloc(multi_byte_length + 1);
   WideCharToMultiByte(codepage, 0, utf16_str, -1, multi_byte, multi_byte_length, NULL,
                        NULL);
   multi_byte[multi_byte_length] = 0;
   return multi_byte;
}

char *
strdup_wstr_utf8(const wchar_t *wstr)
{
   return strdup_wstr_codepage(CP_UTF8, wstr);
}

#endif
