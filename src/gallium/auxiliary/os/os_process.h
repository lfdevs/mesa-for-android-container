/**************************************************************************
 *
 * Copyright 2013 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#ifndef OS_PROCESS_H
#define OS_PROCESS_H

#include <stdbool.h>
#include <string.h>

#include "util/u_debug.h"
#include "util/u_process.h"


static inline bool
os_get_process_name(char *str, size_t size)
{
   const char *process_name = debug_get_option("GALLIUM_PROCESS_NAME",
                                               util_get_process_name());
   if (!process_name)
      return false;

   assert(str);
   assert(size);

   size_t len = strnlen(process_name, size - 1);
   memcpy(str, process_name, len);
   str[len] = '\0';
   return true;
}


#endif /* OS_PROCESS_H */
