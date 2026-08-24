/*
 * Copyright © 2026 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef V3DV_COMMON_H
#define V3DV_COMMON_H

#include "vk_object.h"
#include "util/log.h"
#include "common/v3d_debug.h"
#include "vk_debug_utils.h"

#ifdef HAVE_VALGRIND
#include <valgrind.h>
#include <memcheck.h>
#define VG(x) x
#else
#define VG(x) ((void)0)
#endif

/* A non-fatal assert.  Useful for debugging. */
#if MESA_DEBUG
#define v3dv_assert(x) ({ \
   if (unlikely(!(x))) \
      mesa_loge("%s:%d ASSERT: %s", __FILE__, __LINE__, #x); \
})
#else
#define v3dv_assert(x)
#endif

#define perf_debug(...) do {                       \
   if (V3D_DBG(PERF))                            \
      mesa_logi(__VA_ARGS__);                \
} while (0)

#define V3DV_FROM_HANDLE(__v3dv_type, __name, __handle)			\
   VK_FROM_HANDLE(__v3dv_type, __name, __handle)

static inline void
v3dv_emit_device_memory_report(struct vk_device *device,
                               VkResult result,
                               bool is_alloc,
                               bool is_import,
                               uint64_t mem_obj_id,
                               VkDeviceSize size,
                               VkObjectType obj_type,
                               uint64_t obj_handle)
{
   if (likely(!device->memory_reports))
      return;

   VkDeviceMemoryReportEventTypeEXT type;
   if (result != VK_SUCCESS) {
      type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATION_FAILED_EXT;
   } else if (is_alloc) {
      type = is_import ? VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT
                       : VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
   } else {
      type = is_import ? VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT
                       : VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
   }

   /* heap_index is always zero since broadcom is an iGPU */
   vk_emit_device_memory_report(device, type, mem_obj_id, size,
                                obj_type, obj_handle, 0);
}

#endif /* V3DV_COMMON_H */
