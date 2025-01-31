/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) ||     \
    defined(VK_USE_PLATFORM_XCB_KHR) || \
    defined(VK_USE_PLATFORM_XLIB_KHR) || \
    defined(VK_USE_PLATFORM_DISPLAY_KHR)
#define ANV_USE_WSI_PLATFORM
#endif

#ifdef ANDROID_STRICT
#if ANDROID_API_LEVEL >= 33
#define ANV_API_VERSION VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION)
#else
#define ANV_API_VERSION VK_MAKE_VERSION(1, 1, VK_HEADER_VERSION)
#endif
#else
#define ANV_API_VERSION VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION)
#endif
