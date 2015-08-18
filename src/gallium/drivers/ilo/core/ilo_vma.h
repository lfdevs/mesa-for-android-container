/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2015 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef ILO_VMA_H
#define ILO_VMA_H

#include "ilo_core.h"
#include "ilo_debug.h"
#include "ilo_dev.h"

struct intel_bo;

/**
 * A virtual memory area.
 */
struct ilo_vma {
   /* address space */
   uint32_t vm_size;
   uint32_t vm_alignment;

   /* backing storage */
   struct intel_bo *bo;
   uint32_t bo_offset;
};

static inline bool
ilo_vma_init(struct ilo_vma *vma, const struct ilo_dev *dev,
             uint32_t size, uint32_t alignment)
{
   assert(ilo_is_zeroed(vma, sizeof(*vma)));
   assert(size && alignment);

   vma->vm_alignment = alignment;
   vma->vm_size = size;

   return true;
}

static inline void
ilo_vma_set_bo(struct ilo_vma *vma, const struct ilo_dev *dev,
               struct intel_bo *bo, uint32_t offset)
{
   assert(offset % vma->vm_alignment == 0);

   vma->bo = bo;
   vma->bo_offset = offset;
}

#endif /* ILO_VMA_H */
