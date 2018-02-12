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

#include "radv_private.h"

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

#define RADV_HAS_SURFACE (VK_USE_PLATFORM_WAYLAND_KHR ||                          VK_USE_PLATFORM_XCB_KHR ||                          VK_USE_PLATFORM_XLIB_KHR)

bool
radv_instance_extension_supported(const char *name)
{
    if (strcmp(name, "VK_KHR_external_fence_capabilities") == 0)
        return true;
    if (strcmp(name, "VK_KHR_external_memory_capabilities") == 0)
        return true;
    if (strcmp(name, "VK_KHR_external_semaphore_capabilities") == 0)
        return true;
    if (strcmp(name, "VK_KHR_get_physical_device_properties2") == 0)
        return true;
    if (strcmp(name, "VK_KHR_get_surface_capabilities2") == 0)
        return true;
    if (strcmp(name, "VK_KHR_surface") == 0)
        return RADV_HAS_SURFACE;
    if (strcmp(name, "VK_KHR_wayland_surface") == 0)
        return VK_USE_PLATFORM_WAYLAND_KHR;
    if (strcmp(name, "VK_KHR_xcb_surface") == 0)
        return VK_USE_PLATFORM_XCB_KHR;
    if (strcmp(name, "VK_KHR_xlib_surface") == 0)
        return VK_USE_PLATFORM_XLIB_KHR;
    if (strcmp(name, "VK_EXT_debug_report") == 0)
        return true;
    return false;
}

VkResult radv_EnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
    VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_fence_capabilities",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_memory_capabilities",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_semaphore_capabilities",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_get_physical_device_properties2",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_get_surface_capabilities2",
                .specVersion = 1,
            };
        }
    }
    if (RADV_HAS_SURFACE) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_surface",
                .specVersion = 25,
            };
        }
    }
    if (VK_USE_PLATFORM_WAYLAND_KHR) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_wayland_surface",
                .specVersion = 6,
            };
        }
    }
    if (VK_USE_PLATFORM_XCB_KHR) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_xcb_surface",
                .specVersion = 6,
            };
        }
    }
    if (VK_USE_PLATFORM_XLIB_KHR) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_xlib_surface",
                .specVersion = 6,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_EXT_debug_report",
                .specVersion = 9,
            };
        }
    }

    return vk_outarray_status(&out);
}

uint32_t
radv_physical_device_api_version(struct radv_physical_device *dev)
{
    return VK_MAKE_VERSION(1, 0, 57);
}

bool
radv_physical_device_extension_supported(struct radv_physical_device *device,
                                        const char *name)
{
    if (strcmp(name, "VK_ANDROID_native_buffer") == 0)
        return ANDROID && device->rad_info.has_syncobj_wait_for_submit;
    if (strcmp(name, "VK_KHR_bind_memory2") == 0)
        return true;
    if (strcmp(name, "VK_KHR_dedicated_allocation") == 0)
        return true;
    if (strcmp(name, "VK_KHR_descriptor_update_template") == 0)
        return true;
    if (strcmp(name, "VK_KHR_external_fence") == 0)
        return device->rad_info.has_syncobj_wait_for_submit;
    if (strcmp(name, "VK_KHR_external_fence_fd") == 0)
        return device->rad_info.has_syncobj_wait_for_submit;
    if (strcmp(name, "VK_KHR_external_memory") == 0)
        return true;
    if (strcmp(name, "VK_KHR_external_memory_fd") == 0)
        return true;
    if (strcmp(name, "VK_KHR_external_semaphore") == 0)
        return device->rad_info.has_syncobj;
    if (strcmp(name, "VK_KHR_external_semaphore_fd") == 0)
        return device->rad_info.has_syncobj;
    if (strcmp(name, "VK_KHR_get_memory_requirements2") == 0)
        return true;
    if (strcmp(name, "VK_KHR_image_format_list") == 0)
        return true;
    if (strcmp(name, "VK_KHR_incremental_present") == 0)
        return true;
    if (strcmp(name, "VK_KHR_maintenance1") == 0)
        return true;
    if (strcmp(name, "VK_KHR_maintenance2") == 0)
        return true;
    if (strcmp(name, "VK_KHR_push_descriptor") == 0)
        return true;
    if (strcmp(name, "VK_KHR_relaxed_block_layout") == 0)
        return true;
    if (strcmp(name, "VK_KHR_sampler_mirror_clamp_to_edge") == 0)
        return true;
    if (strcmp(name, "VK_KHR_shader_draw_parameters") == 0)
        return true;
    if (strcmp(name, "VK_KHR_storage_buffer_storage_class") == 0)
        return true;
    if (strcmp(name, "VK_KHR_swapchain") == 0)
        return RADV_HAS_SURFACE;
    if (strcmp(name, "VK_KHR_variable_pointers") == 0)
        return true;
    if (strcmp(name, "VK_KHX_multiview") == 0)
        return false;
    if (strcmp(name, "VK_EXT_discard_rectangles") == 0)
        return true;
    if (strcmp(name, "VK_EXT_external_memory_dma_buf") == 0)
        return true;
    if (strcmp(name, "VK_EXT_global_priority") == 0)
        return device->rad_info.has_ctx_priority;
    if (strcmp(name, "VK_AMD_draw_indirect_count") == 0)
        return true;
    if (strcmp(name, "VK_AMD_rasterization_order") == 0)
        return device->rad_info.chip_class >= VI && device->rad_info.max_se >= 2;
    if (strcmp(name, "VK_AMD_shader_info") == 0)
        return true;
    return false;
}

VkResult radv_EnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
    RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
    VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);
    (void)device;

    if (ANDROID && device->rad_info.has_syncobj_wait_for_submit) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_ANDROID_native_buffer",
                .specVersion = 5,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_bind_memory2",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_dedicated_allocation",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_descriptor_update_template",
                .specVersion = 1,
            };
        }
    }
    if (device->rad_info.has_syncobj_wait_for_submit) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_fence",
                .specVersion = 1,
            };
        }
    }
    if (device->rad_info.has_syncobj_wait_for_submit) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_fence_fd",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_memory",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_memory_fd",
                .specVersion = 1,
            };
        }
    }
    if (device->rad_info.has_syncobj) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_semaphore",
                .specVersion = 1,
            };
        }
    }
    if (device->rad_info.has_syncobj) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_external_semaphore_fd",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_get_memory_requirements2",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_image_format_list",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_incremental_present",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_maintenance1",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_maintenance2",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_push_descriptor",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_relaxed_block_layout",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_sampler_mirror_clamp_to_edge",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_shader_draw_parameters",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_storage_buffer_storage_class",
                .specVersion = 1,
            };
        }
    }
    if (RADV_HAS_SURFACE) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_swapchain",
                .specVersion = 68,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHR_variable_pointers",
                .specVersion = 1,
            };
        }
    }
    if (false) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_KHX_multiview",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_EXT_discard_rectangles",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_EXT_external_memory_dma_buf",
                .specVersion = 1,
            };
        }
    }
    if (device->rad_info.has_ctx_priority) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_EXT_global_priority",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_AMD_draw_indirect_count",
                .specVersion = 1,
            };
        }
    }
    if (device->rad_info.chip_class >= VI && device->rad_info.max_se >= 2) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_AMD_rasterization_order",
                .specVersion = 1,
            };
        }
    }
    if (true) {
        vk_outarray_append(&out, prop) {
            *prop = (VkExtensionProperties) {
                .extensionName = "VK_AMD_shader_info",
                .specVersion = 1,
            };
        }
    }

    return vk_outarray_status(&out);
}
