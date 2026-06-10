/*
 * Copyright © 2017 Google.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DEBUG_HANG_H
#define RADV_DEBUG_HANG_H

#include "radv_debug.h"
#include "radv_device.h"
#include "radv_physical_device.h"

bool radv_init_trace(struct radv_device *device);
void radv_finish_trace(struct radv_device *device);

VkResult radv_check_gpu_hangs(struct radv_queue *queue, const struct radv_winsys_submit_info *submit_info);

void radv_dump_enabled_options(const struct radv_device *device, FILE *f);

bool radv_trap_handler_init(struct radv_device *device);
void radv_trap_handler_finish(struct radv_device *device);
void radv_check_trap_handler(struct radv_queue *queue);

bool radv_vm_fault_occurred(struct radv_device *device, struct radv_winsys_gpuvm_fault_info *fault_info);

ALWAYS_INLINE static bool
radv_device_fault_detection_enabled(const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   return instance->debug_flags & RADV_DEBUG_HANG;
}

struct radv_trace_data {
   uint32_t primary_id;
   uint32_t secondary_id;
   uint64_t gfx_ring_pipeline;
   uint64_t comp_ring_pipeline;
   uint64_t vertex_descriptors;
   uint64_t vertex_prolog;
   uint64_t ps_epilog;
   uint64_t descriptor_sets[MAX_SETS];
   VkDispatchIndirectCommand indirect_dispatch;
};

struct radv_address_binding_report {
   uint64_t timestamp; /* CPU timestamp */
   uint64_t va;
   uint64_t size;
   VkDeviceAddressBindingFlagsEXT flags;
   VkDeviceAddressBindingTypeEXT binding_type;
   uint64_t object_handle;
   VkObjectType object_type;
   char *object_name;
};

struct radv_address_binding_tracker {
   VkDebugUtilsMessengerEXT messenger;
   struct util_dynarray reports;
   simple_mtx_t mtx;
   struct radv_instance *instance;
};

#endif /* RADV_DEBUG_HANG_H */
