/*
 * Copyright 2017 Intel Corporation
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#ifndef ANV_EXTENSIONS_H
#define ANV_EXTENSIONS_H

#include "stdbool.h"

#define ANV_INSTANCE_EXTENSION_COUNT 10

extern const VkExtensionProperties anv_instance_extensions[];

struct anv_instance_extension_table {
   union {
      bool extensions[ANV_INSTANCE_EXTENSION_COUNT];
      struct {
         bool KHR_external_fence_capabilities;
         bool KHR_external_memory_capabilities;
         bool KHR_external_semaphore_capabilities;
         bool KHR_get_physical_device_properties2;
         bool KHR_get_surface_capabilities2;
         bool KHR_surface;
         bool KHR_wayland_surface;
         bool KHR_xcb_surface;
         bool KHR_xlib_surface;
         bool EXT_debug_report;
      };
   };
};

extern const struct anv_instance_extension_table anv_instance_extensions_supported;


#define ANV_DEVICE_EXTENSION_COUNT 26

extern const VkExtensionProperties anv_device_extensions[];

struct anv_device_extension_table {
   union {
      bool extensions[ANV_DEVICE_EXTENSION_COUNT];
      struct {
        bool ANDROID_native_buffer;
        bool KHR_16bit_storage;
        bool KHR_bind_memory2;
        bool KHR_dedicated_allocation;
        bool KHR_descriptor_update_template;
        bool KHR_external_fence;
        bool KHR_external_fence_fd;
        bool KHR_external_memory;
        bool KHR_external_memory_fd;
        bool KHR_external_semaphore;
        bool KHR_external_semaphore_fd;
        bool KHR_get_memory_requirements2;
        bool KHR_image_format_list;
        bool KHR_incremental_present;
        bool KHR_maintenance1;
        bool KHR_maintenance2;
        bool KHR_push_descriptor;
        bool KHR_relaxed_block_layout;
        bool KHR_sampler_mirror_clamp_to_edge;
        bool KHR_sampler_ycbcr_conversion;
        bool KHR_shader_draw_parameters;
        bool KHR_storage_buffer_storage_class;
        bool KHR_swapchain;
        bool KHR_variable_pointers;
        bool KHX_multiview;
        bool EXT_external_memory_dma_buf;
      };
   };
};

struct anv_physical_device;

void
anv_physical_device_get_supported_extensions(const struct anv_physical_device *device,
                                             struct anv_device_extension_table *extensions);

#endif /* ANV_EXTENSIONS_H */
