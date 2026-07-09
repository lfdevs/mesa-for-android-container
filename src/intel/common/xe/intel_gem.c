/*
 * Copyright © 2023 Intel Corporation
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
#include "xe/intel_gem.h"

#include "drm-uapi/xe_drm.h"

#include "common/intel_gem.h"
#include "common/xe/intel_engine.h"

#include "util/os_time.h"
#include "util/timespec.h"
#include "util/compiler.h"
#include "util/macros.h"
#include "util/stack_array.h"

bool
xe_gem_read_render_timestamp(int fd, uint64_t *value)
{
   UNUSED uint64_t cpu;

   return xe_gem_read_correlate_cpu_gpu_timestamp(fd, INTEL_ENGINE_CLASS_RENDER,
                                                  0, CLOCK_MONOTONIC, &cpu,
                                                  value, NULL);
}

bool
xe_gem_can_render_on_fd(int fd)
{
   struct drm_xe_device_query query = {
      .query = DRM_XE_DEVICE_QUERY_ENGINES,
   };
   return intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query) == 0;
}

bool
xe_gem_read_correlate_cpu_gpu_timestamp(int fd,
                                        enum intel_engine_class engine_class,
                                        uint16_t engine_instance,
                                        clockid_t cpu_clock_id,
                                        uint64_t *cpu_timestamp,
                                        uint64_t *gpu_timestamp,
                                        uint64_t *cpu_delta)
{
   struct drm_xe_query_engine_cycles engine_cycles = {};
   struct drm_xe_device_query query = {
      .query = DRM_XE_DEVICE_QUERY_ENGINE_CYCLES,
      .size = sizeof(engine_cycles),
      .data = (uintptr_t)&engine_cycles,
   };

   switch (cpu_clock_id) {
   case CLOCK_MONOTONIC:
#ifdef CLOCK_MONOTONIC_RAW
   case CLOCK_MONOTONIC_RAW:
#endif
   case CLOCK_REALTIME:
#ifdef CLOCK_BOOTTIME
   case CLOCK_BOOTTIME:
#endif
#ifdef CLOCK_TAI
   case CLOCK_TAI:
#endif
      break;
   default:
      return false;
   }

   engine_cycles.eci.engine_class = intel_engine_class_to_xe(engine_class);
   engine_cycles.eci.engine_instance = engine_instance;
   engine_cycles.eci.gt_id = 0;
   engine_cycles.clockid = cpu_clock_id;

   if (intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query))
         return false;

   *cpu_timestamp = engine_cycles.cpu_timestamp;
   *gpu_timestamp = engine_cycles.engine_cycles;
   if (cpu_delta)
      *cpu_delta = engine_cycles.cpu_delta;

   return true;
}

void
intel_xe_gem_add_ext(uint64_t *ptr, uint32_t ext_name, void *data)
{
   struct drm_xe_user_extension *ext = data;
   ext->next_extension = *ptr;
   ext->name = ext_name;
   *ptr = (uintptr_t)ext;
}

bool
xe_gem_supports_protected_exec_queue(int fd)
{
   struct drm_xe_query_pxp_status pxp_status = {};
   struct drm_xe_device_query query = {
      .query = DRM_XE_DEVICE_QUERY_PXP_STATUS,
      .size = sizeof(pxp_status),
      .data = (uintptr_t)&pxp_status,
   };

   /* returning as supported even when PXP is still in progress.
    * exec_queue_create with PXP set will return EBUSY until PXP
    * initialization is completed
    */
   return intel_ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query) == 0;
}

bool xe_gem_supports_get_vm_faults(int fd)
{
   struct drm_xe_vm_get_property prop = {
      .vm_id = -1,
      .property = DRM_XE_VM_GET_PROPERTY_FAULTS,
   };
   /* FIXME: Get a proper kernel api to detect this */
   int ret = intel_ioctl(fd, DRM_IOCTL_XE_VM_GET_PROPERTY, &prop);
   return ret == -1 && errno == ENOENT;
}

static inline enum intel_pagefault_access
xe_vm_fault_get_access(const struct xe_vm_fault *vm_fault)
{
   switch (vm_fault->access_type) {
   default: UNREACHABLE("not handled");
   case FAULT_ACCESS_TYPE_READ: return INTEL_PAGEFAULT_ACCESS_READ;
   case FAULT_ACCESS_TYPE_WRITE: return INTEL_PAGEFAULT_ACCESS_WRITE;
   case FAULT_ACCESS_TYPE_ATOMIC: return INTEL_PAGEFAULT_ACCESS_ATOMIC;
   }
}

static inline enum intel_pagefault_type
xe_vm_fault_get_type(const struct xe_vm_fault *vm_fault)
{
   switch (vm_fault->fault_type) {
   default: UNREACHABLE("not handled");
   case FAULT_TYPE_NOT_PRESENT: return INTEL_PAGEFAULT_TYPE_NOT_PRESENT;
   case FAULT_TYPE_WRITE_ACCESS: return INTEL_PAGEFAULT_TYPE_WRITE_ACCESS;
   case FAULT_TYPE_ATOMIC_ACCESS: return INTEL_PAGEFAULT_TYPE_ATOMIC_ACCESS;
   }
}

static inline enum intel_pagefault_level
xe_vm_fault_get_level(const struct xe_vm_fault *vm_fault)
{
   switch (vm_fault->fault_level) {
   default: UNREACHABLE("not handled");
   case FAULT_LEVEL_PTE: return INTEL_PAGEFAULT_LEVEL_PTE;
   case FAULT_LEVEL_PDE: return INTEL_PAGEFAULT_LEVEL_PDE;
   case FAULT_LEVEL_PDP: return INTEL_PAGEFAULT_LEVEL_PDP;
   case FAULT_LEVEL_PML4: return INTEL_PAGEFAULT_LEVEL_PML4;
   case FAULT_LEVEL_PML5: return INTEL_PAGEFAULT_LEVEL_PML5;
   }
}

struct intel_pagefault_buffer *
xe_gem_alloc_get_vm_faults(int fd, int vm_id)
{
   struct drm_xe_vm_get_property prop = {
      .vm_id = vm_id,
      .property = DRM_XE_VM_GET_PROPERTY_FAULTS,
   };

   int ret = intel_ioctl(fd, DRM_IOCTL_XE_VM_GET_PROPERTY, &prop);
   if (ret)
      return NULL;

   unsigned size = (unsigned) (prop.size / sizeof(struct xe_vm_fault));

   STACK_ARRAY(struct xe_vm_fault, kmd_faults, size);
   prop.size = size * sizeof(struct xe_vm_fault);
   prop.data = (uintptr_t) kmd_faults;

   struct intel_pagefault_buffer *result = NULL;

   if (size != 0)
      ret = intel_ioctl(fd, DRM_IOCTL_XE_VM_GET_PROPERTY, &prop);

   if (ret == 0) {
      size = MIN2(size, (unsigned) (prop.size / sizeof(struct xe_vm_fault)));
      result = malloc(sizeof(*result) + sizeof(result->items[0]) * size);
      if (result) {
         result->size = size;
         for (unsigned i = 0; i < size; ++i) {
            result->items[i].address = kmd_faults[i].address;
            result->items[i].precision = kmd_faults[i].address_precision;
            result->items[i].access = xe_vm_fault_get_access(&kmd_faults[i]);
            result->items[i].type = xe_vm_fault_get_type(&kmd_faults[i]);
            result->items[i].level = xe_vm_fault_get_level(&kmd_faults[i]);
         }
      }
   }

   STACK_ARRAY_FINISH(kmd_faults);

   return result;
}
