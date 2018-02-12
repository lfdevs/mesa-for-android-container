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

#include "anv_private.h"

#include "vk_util.h"

/* Convert the VK_USE_PLATFORM_* defines to booleans */
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#   undef VK_USE_PLATFORM_ANDROID_KHR
#   define VK_USE_PLATFORM_ANDROID_KHR true
#else
#   define VK_USE_PLATFORM_ANDROID_KHR false
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
#   undef VK_USE_PLATFORM_WAYLAND_KHR
#   define VK_USE_PLATFORM_WAYLAND_KHR true
#else
#   define VK_USE_PLATFORM_WAYLAND_KHR false
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
#   undef VK_USE_PLATFORM_XCB_KHR
#   define VK_USE_PLATFORM_XCB_KHR true
#else
#   define VK_USE_PLATFORM_XCB_KHR false
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
#   undef VK_USE_PLATFORM_XLIB_KHR
#   define VK_USE_PLATFORM_XLIB_KHR true
#else
#   define VK_USE_PLATFORM_XLIB_KHR false
#endif

/* And ANDROID too */
#ifdef ANDROID
#   undef ANDROID
#   define ANDROID true
#else
#   define ANDROID false
#endif

#define ANV_HAS_SURFACE (VK_USE_PLATFORM_WAYLAND_KHR ||                          VK_USE_PLATFORM_XCB_KHR ||                          VK_USE_PLATFORM_XLIB_KHR)

const VkExtensionProperties anv_instance_extensions[ANV_INSTANCE_EXTENSION_COUNT] = {
   {"VK_KHR_external_fence_capabilities", 1},
   {"VK_KHR_external_memory_capabilities", 1},
   {"VK_KHR_external_semaphore_capabilities", 1},
   {"VK_KHR_get_physical_device_properties2", 1},
   {"VK_KHR_get_surface_capabilities2", 1},
   {"VK_KHR_surface", 25},
   {"VK_KHR_wayland_surface", 6},
   {"VK_KHR_xcb_surface", 6},
   {"VK_KHR_xlib_surface", 6},
   {"VK_EXT_debug_report", 8},
};

const struct anv_instance_extension_table anv_instance_extensions_supported = {
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
   .KHR_get_surface_capabilities2 = ANV_HAS_SURFACE,
   .KHR_surface = ANV_HAS_SURFACE,
   .KHR_wayland_surface = VK_USE_PLATFORM_WAYLAND_KHR,
   .KHR_xcb_surface = VK_USE_PLATFORM_XCB_KHR,
   .KHR_xlib_surface = VK_USE_PLATFORM_XLIB_KHR,
   .EXT_debug_report = true,
};

uint32_t
anv_physical_device_api_version(struct anv_physical_device *dev)
{
    return VK_MAKE_VERSION(1, 0, 57);
}

const VkExtensionProperties anv_device_extensions[ANV_DEVICE_EXTENSION_COUNT] = {
   {"VK_ANDROID_native_buffer", 5},
   {"VK_KHR_16bit_storage", 1},
   {"VK_KHR_bind_memory2", 1},
   {"VK_KHR_dedicated_allocation", 1},
   {"VK_KHR_descriptor_update_template", 1},
   {"VK_KHR_external_fence", 1},
   {"VK_KHR_external_fence_fd", 1},
   {"VK_KHR_external_memory", 1},
   {"VK_KHR_external_memory_fd", 1},
   {"VK_KHR_external_semaphore", 1},
   {"VK_KHR_external_semaphore_fd", 1},
   {"VK_KHR_get_memory_requirements2", 1},
   {"VK_KHR_image_format_list", 1},
   {"VK_KHR_incremental_present", 1},
   {"VK_KHR_maintenance1", 1},
   {"VK_KHR_maintenance2", 1},
   {"VK_KHR_push_descriptor", 1},
   {"VK_KHR_relaxed_block_layout", 1},
   {"VK_KHR_sampler_mirror_clamp_to_edge", 1},
   {"VK_KHR_sampler_ycbcr_conversion", 1},
   {"VK_KHR_shader_draw_parameters", 1},
   {"VK_KHR_storage_buffer_storage_class", 1},
   {"VK_KHR_swapchain", 68},
   {"VK_KHR_variable_pointers", 1},
   {"VK_KHX_multiview", 1},
   {"VK_EXT_external_memory_dma_buf", 1},
};

void
anv_physical_device_get_supported_extensions(const struct anv_physical_device *device,
                                             struct anv_device_extension_table *extensions)
{
   *extensions = (struct anv_device_extension_table) {
      .ANDROID_native_buffer = ANDROID,
      .KHR_16bit_storage = false,
      .KHR_bind_memory2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_descriptor_update_template = true,
      .KHR_external_fence = device->has_syncobj_wait,
      .KHR_external_fence_fd = device->has_syncobj_wait,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = true,
      .KHR_external_semaphore_fd = true,
      .KHR_get_memory_requirements2 = true,
      .KHR_image_format_list = true,
      .KHR_incremental_present = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_push_descriptor = true,
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = true,
      .KHR_shader_draw_parameters = true,
      .KHR_storage_buffer_storage_class = true,
      .KHR_swapchain = ANV_HAS_SURFACE,
      .KHR_variable_pointers = true,
      .KHX_multiview = false,
      .EXT_external_memory_dma_buf = true,
   };
}
