/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "u_dynarray.h"

unsigned util_dynarray_is_data_stack_allocated;

void *
util_dynarray_grow_bytes_slow(struct util_dynarray *buf, unsigned growbytes)
{
   unsigned newsize = buf->size + growbytes;
   void *p = util_dynarray_ensure_cap(buf, newsize);
   if (!p)
      return NULL;

   buf->size = newsize;
   return p;
}
