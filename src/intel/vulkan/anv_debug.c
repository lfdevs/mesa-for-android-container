/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include "anv_private.h"

/* Avoid redefining NSEC_PER_SEC in timespec.h */
#undef NSEC_PER_SEC
#include "util/timespec.h"

/** Vendor fault code definitions for the device fault extension */
#define ANV_FAULT_CODE_DEFS                                                \
DEF(IOCTL_ERROR_LOST,    "Hardware Lost - IOCTL Error",        0x00000001) \
DEF(IOCTL_ERROR_DIED,    "Hardware Failure - IOCTL Error",     0x00000002) \
DEF(IOCTL_ERROR_SUBMIT,  "Submit Failure - IOCTL Error",       0x00000003) \
DEF(IOCTL_ERROR_OTHER,   "Other Failure - IOCTL Error",        0x00000004) \
DEF(QUEUE_INDEX_BANNED,  "Banned Queue - Family/Index/Flags",  0x00000005) \

/** Bits 63:32 of all vendor fault codes */
#define ANV_VENDOR_PREFIX 0x80860000

/** Fault code types, Bits 31:0 of all vendor fault codes */
enum anv_fault_code {
#define DEF(name, desc, code) \
   ANV_FAULT_CODE_##name = (code),
ANV_FAULT_CODE_DEFS
#undef DEF
};

/** Get the description of a fault code type */
static const char *
anv_fault_code_desc(enum anv_fault_code code)
{
   switch (code) {
#define DEF(name, desc, code) \
   case ANV_FAULT_CODE_##name: return desc;
ANV_FAULT_CODE_DEFS
#undef DEF
   default: return "Unknown";
   }
}

#undef ANV_FAULT_CODE_DEFS

struct anv_fault_code_info {
   struct anv_device *device;
   void (*_push_fault_addr)(void*, struct intel_pagefault_info*);
   void (*_push_fault_code)(void*, enum anv_fault_code, int64_t);
   PRINTFLIKE(2, 3)
   void (*_apply_fault_reason)(void*, const char*, ...);
};

#define push_fault_addr(pagefault) \
   info->_push_fault_addr(info, &(pagefault))
#define push_fault_code(code, value) \
   info->_push_fault_code(info, ANV_FAULT_CODE_##code, (value))
#define apply_fault_reason(...) \
   info->_apply_fault_reason(info, __VA_ARGS__)

/** Where we actually generate all the fault codes */
static void
anv_fill_device_fault_info(struct anv_fault_code_info *info)
{
   struct anv_device *device = info->device;
   struct anv_device_fault_state *state = &device->fault.state;
   bool has_pagefaults = false;

   struct intel_pagefault_buffer *faults =
      anv_device_alloc_get_vm_faults(device);
   if (faults) {
      has_pagefaults = faults->size != 0;
      for (unsigned i = 0; i < faults->size; ++i)
         push_fault_addr(faults->items[i]);
      free(faults);
   }

   if (state->device_status == ENODEV) {
      /* ENODEV is the standard DRM error code for when the device got
       * disconnected (such as an eGPU connected via thunderbolt).
       */
      push_fault_code(IOCTL_ERROR_LOST, -ENODEV);
      apply_fault_reason("Hardware Lost");
   } else if (state->device_status == EIO) {
      /* EIO is the standard DRM error code for when the device hangs
       * and could not be ressurected through a reset.
       */
      push_fault_code(IOCTL_ERROR_DIED, -EIO);
      apply_fault_reason("Hardware Failure");
   } else if (state->queue_status) {
      assert(!(state->queue.family & ~0xFFFF));
      assert(!(state->queue.index & ~0xFFFF));
      assert(!(state->queue.flags & ~0x7FFFFFFF));

      /* Pack the queue family/index/flags into the fault code value */
      int64_t queue_index =
         (int64_t) state->queue.family |
         ((int64_t) state->queue.index << 16) |
         ((int64_t) state->queue.flags << 32);

      enum intel_engine_class engine_class =
         device->physical->queue.families[state->queue.family].engine_class;

      /* Find the queue's debug label to include in the description */
      const char *debug_label = "NULL";
      vk_foreach_queue(iter, &device->vk) {
         if (iter->queue_family_index == state->queue.family &&
             iter->index_in_family == state->queue.index &&
             iter->flags == state->queue.flags) {
            debug_label = vk_object_base_name(&iter->base);
            break;
         }
      }

      if (state->device_status)
         push_fault_code(IOCTL_ERROR_OTHER, -state->device_status);

      if (state->queue_status != ECANCELED)
         push_fault_code(IOCTL_ERROR_SUBMIT, -state->queue_status);

      push_fault_code(QUEUE_INDEX_BANNED, queue_index);
      apply_fault_reason("Banned Queue: %s #%u - \"%s\" (%s)",
                         intel_engines_class_to_string(engine_class),
                         state->queue.index, debug_label,
                         state->queue_status != ECANCELED ? "Submit Failure" :
                         !has_pagefaults ? "Engine Reset" : "w/ Page Faults");
   } else if (state->device_status) {
      /* Any other failure not associated with a specific queue. */
      push_fault_code(IOCTL_ERROR_OTHER, -state->device_status);
      apply_fault_reason("Unknown IOCTL Failure");
   } else {
      /* No IOCTL error codes were captured. */
      apply_fault_reason("No IOCTL error codes were captured");
   }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"

struct anv_fault_code_info_ext {
   struct anv_fault_code_info base;
   char (*description)[VK_MAX_DESCRIPTION_SIZE];
   vk_outarray(VkDeviceFaultAddressInfoEXT) addr_buffer;
   vk_outarray(VkDeviceFaultVendorInfoEXT) vendor_buffer;
};

#pragma clang diagnostic pop

static inline void
anv_translate_address_info(struct intel_pagefault_info *src,
                           VkDeviceFaultAddressInfoEXT *dst)
{
   dst->reportedAddress = src->address;
   dst->addressPrecision = src->precision;
   dst->addressType =
      src->access == INTEL_PAGEFAULT_ACCESS_READ ?
         VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT :
         VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT;
}

static void
anv_push_fault_addr_ext(void *_info, struct intel_pagefault_info *pagefault)
{
   struct anv_fault_code_info_ext *info = _info;
   VkDeviceFaultAddressInfoEXT *el = vk_outarray_next(&info->addr_buffer);
   if (el)
      anv_translate_address_info(pagefault, el);
}

static inline void
anv_translate_vendor_info(enum anv_fault_code code, int64_t value,
                          VkDeviceFaultVendorInfoEXT *dst)
{
   dst->vendorFaultCode = (((uint64_t) ANV_VENDOR_PREFIX) << 32) | code;
   dst->vendorFaultData = (uint64_t) value;
   snprintf(dst->description, sizeof(dst->description),
            "%s", anv_fault_code_desc(code));
}

static void
anv_push_fault_code_ext(void *_info, enum anv_fault_code code, int64_t value)
{
   struct anv_fault_code_info_ext *info = _info;
   VkDeviceFaultVendorInfoEXT *el = vk_outarray_next(&info->vendor_buffer);
   if (el)
      anv_translate_vendor_info(code, value, el);
}

static void
anv_apply_fault_reason_ext(void *_info, const char *fmt, ...)
{
   struct anv_fault_code_info_ext *info = _info;
   if (info->description) {
      va_list args;
      va_start(args, fmt);
      vsnprintf(*info->description, sizeof(*info->description), fmt, args);
      va_end(args);
   }
}

VkResult
anv_GetDeviceFaultInfoEXT(VkDevice _device,
                          VkDeviceFaultCountsEXT *pFaultCounts,
                          VkDeviceFaultInfoEXT *pFaultInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   struct anv_fault_code_info_ext info = {
      .base = {
         .device = device,
         ._push_fault_addr = anv_push_fault_addr_ext,
         ._push_fault_code = anv_push_fault_code_ext,
         ._apply_fault_reason = anv_apply_fault_reason_ext,
      },
      .description = pFaultInfo ? &pFaultInfo->description : NULL,
   };

   vk_outarray_init(&info.addr_buffer,
                    pFaultInfo ? pFaultInfo->pAddressInfos : NULL,
                    &pFaultCounts->addressInfoCount);
   vk_outarray_init(&info.vendor_buffer,
                    pFaultInfo ? pFaultInfo->pVendorInfos : NULL,
                    &pFaultCounts->vendorInfoCount);

   mtx_lock(&device->fault.mutex);
   anv_fill_device_fault_info(&info.base);
   mtx_unlock(&device->fault.mutex);

   pFaultCounts->vendorBinarySize = 0;

   return (vk_outarray_status(&info.addr_buffer) == VK_INCOMPLETE ||
           vk_outarray_status(&info.vendor_buffer) == VK_INCOMPLETE) ?
             VK_INCOMPLETE : VK_SUCCESS;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-variable-sized-type-not-at-end"

struct anv_fault_code_info_khr {
   struct anv_fault_code_info base;
   vk_outarray(VkDeviceFaultInfoKHR) buffer;
   uint32_t begin_offset;
   bool finalized;
};

#pragma clang diagnostic pop

static void
anv_push_fault_addr_khr(void *_info, struct intel_pagefault_info *pagefault)
{
   struct anv_fault_code_info_khr *info = _info;
   assert(!info->finalized);

   if (info->begin_offset > 0) {
      --info->begin_offset;
      return;
   }

   VkDeviceFaultInfoKHR *el = vk_outarray_next(&info->buffer);
   if (el) {
      el->sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;
      el->flags = VK_DEVICE_FAULT_FLAG_MEMORY_ADDRESS_KHR;
      el->groupId = 0;
      anv_translate_address_info(pagefault, &el->faultAddressInfo);
      snprintf(el->description, sizeof(el->description),
               "GPU VM Fault - Page Address: 0x%016"PRIx64", "
               "Page Size: 0x%04x, Access: %s, Type: %s, Level: %s",
               pagefault->address, pagefault->precision,
               intel_pagefault_access_to_string(pagefault->access),
               intel_pagefault_type_to_string(pagefault->type),
               intel_pagefault_level_to_string(pagefault->level));
   }
}

static void
anv_push_fault_code_khr(void *_info, enum anv_fault_code code, int64_t value)
{
   struct anv_fault_code_info_khr *info = _info;
   assert(!info->finalized);

   if (info->begin_offset > 0) {
      --info->begin_offset;
      return;
   }

   VkDeviceFaultInfoKHR *el = vk_outarray_next(&info->buffer);
   if (el) {
      el->sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;
      el->flags = VK_DEVICE_FAULT_FLAG_VENDOR_KHR;
      el->groupId = 0;
      anv_translate_vendor_info(code, value, &el->vendorInfo);
      if (value < 0) {
         assert(-value <= (int64_t) INT32_MAX);
         const char *errname = strerror((int) -value);
         snprintf(el->description, sizeof(el->description),
                  "%s: %s",
                  anv_fault_code_desc(code),
                  errname != NULL ? errname : "<Unknown>");
      } else {
         snprintf(el->description, sizeof(el->description),
                  "%s: 0x%016"PRIx64,
                  anv_fault_code_desc(code),
                  (uint64_t) value);
      }
   }
}

static void
anv_apply_fault_reason_khr(void *_info, const char *fmt, ...)
{
   struct anv_fault_code_info_khr *info = _info;
   assert(!info->finalized);

   if (info->begin_offset > 0) {
      --info->begin_offset;
      return;
   }

   VkDeviceFaultInfoKHR *el = vk_outarray_next(&info->buffer);
   if (el) {
      info->finalized = true;
      el->sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_KHR;
      el->flags = VK_DEVICE_FAULT_FLAG_DEVICE_LOST_KHR;
      el->groupId = 0;
      va_list args;
      va_start(args, fmt);
      vsnprintf(el->description, sizeof(el->description), fmt, args);
      va_end(args);
   }
}

VkResult
anv_GetDeviceFaultReportsKHR(VkDevice _device,
                             uint64_t timeout,
                             uint32_t *pFaultCounts,
                             VkDeviceFaultInfoKHR *pFaultInfo)
{
   ANV_FROM_HANDLE(anv_device, device, _device);

   int64_t timeout_at = timeout ? os_time_get_absolute_timeout(timeout) : 0;

   struct anv_fault_code_info_khr info = {
      .base = {
         .device = device,
         ._push_fault_addr = anv_push_fault_addr_khr,
         ._push_fault_code = anv_push_fault_code_khr,
         ._apply_fault_reason = anv_apply_fault_reason_khr,
      },
   };

   vk_outarray_init(&info.buffer, pFaultInfo, pFaultCounts);

   VkResult res = VK_SUCCESS;

   mtx_lock(&device->fault.mutex);

   /* The KHR variant requires us to sleep until a fault actually happens */
   if (device->fault.state.device_status == 0 &&
       device->fault.state.queue_status == 0) {
      if (timeout_at == 0) {
         res = VK_TIMEOUT;
      } else {
         struct timespec abs_timeout_ts;
         timespec_from_nsec(&abs_timeout_ts, timeout_at);
         int wait_result =
            u_cnd_monotonic_timedwait(&device->fault.lost_cnd,
                                      &device->fault.mutex,
                                      &abs_timeout_ts);
         if (wait_result == thrd_timedout)
            res = VK_TIMEOUT;
         else if (wait_result != thrd_success)
            res = VK_ERROR_UNKNOWN;
      }
   }

   if (res != VK_SUCCESS)
      goto unlock;

   uint32_t begin = device->fault.num_reported;
   if (begin == UINT32_MAX)
      goto unlock;

   info.begin_offset = begin;
   anv_fill_device_fault_info(&info.base);

   if (pFaultInfo) {
      /* Once the device is lost and all faults have been returned, store
       * UINT32_MAX into num_reported so further invocations just sleep and
       * return nothing.
       */
      if (vk_outarray_status(&info.buffer) != VK_INCOMPLETE)
         device->fault.num_reported = UINT32_MAX;
      else
         device->fault.num_reported += *pFaultCounts;
   }

unlock:
   mtx_unlock(&device->fault.mutex);

   if (res != VK_SUCCESS)
      return res;

   if (vk_outarray_status(&info.buffer) == VK_INCOMPLETE)
      return VK_INCOMPLETE;

   if (*pFaultCounts != 0) {
      assert(!pFaultInfo || info.finalized);
      return VK_SUCCESS;
   }

   if (timeout_at != 0)
      os_time_nanosleep_until(timeout_at);

   return VK_TIMEOUT;
}

VkResult
anv_GetDeviceFaultDebugInfoKHR(VkDevice _device,
                               VkDeviceFaultDebugInfoKHR *pDebugInfo)
{
   pDebugInfo->vendorBinarySize = 0;
   return VK_SUCCESS;
}

static inline void
anv_device_print_vm_faults(struct anv_device *device)
{
   struct intel_pagefault_buffer *faults =
      anv_device_alloc_get_vm_faults(device);

   if (!faults)
      return;

   for (unsigned i = 0; i < faults->size; ++i) {
      mesa_loge("[GPU-VM-FAULT] Page Address: 0x%016"PRIx64", "
                "Page Size: 0x%04x, Access: %s, Type: %s, Level: %s",
                 faults->items[i].address, faults->items[i].precision,
                 intel_pagefault_access_to_string(faults->items[i].access),
                 intel_pagefault_type_to_string(faults->items[i].type),
                 intel_pagefault_level_to_string(faults->items[i].level));
   }

   free(faults);
}

void
anv_device_update_fault_state(struct anv_device *device,
                              int device_errno)
{
   assert(device_errno != 0);

   mtx_lock(&device->fault.mutex);

   struct anv_device_fault_state *state = &device->fault.state;
   if (device_errno == ENODEV || device_errno == EIO || !state->device_status)
      state->device_status = device_errno;

   u_cnd_monotonic_broadcast(&device->fault.lost_cnd);

   mtx_unlock(&device->fault.mutex);
}

void
anv_queue_update_fault_state(struct anv_queue *queue,
                             int queue_errno)
{
   assert(queue != NULL);
   assert(queue_errno != 0);

   struct anv_device *device = queue->device;
   bool print_vm_faults = false;

   mtx_lock(&device->fault.mutex);

   struct anv_device_fault_state *state = &device->fault.state;
   if (queue_errno == ENODEV || queue_errno == EIO) {
      state->device_status = queue_errno;
   } else if (!state->queue_status) {
      state->queue_status = queue_errno;
      state->queue.family = queue->vk.queue_family_index;
      state->queue.index = queue->vk.index_in_family;
      state->queue.flags = queue->vk.flags;
      print_vm_faults = queue_errno == ECANCELED;
   }

   u_cnd_monotonic_broadcast(&device->fault.lost_cnd);

   mtx_unlock(&device->fault.mutex);

   if (print_vm_faults)
      anv_device_print_vm_faults(device);
}
