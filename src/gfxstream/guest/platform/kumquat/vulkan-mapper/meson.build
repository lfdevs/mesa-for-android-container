# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_gfxstream_vulkan_mapper = include_directories('.')

files_gfxstream_vulkan_mapper = files('GfxStreamVulkanMapper.cpp')

libgfxstream_vulkan_mapper = static_library(
    'gfxstream_vulkan_mapper',
    files_gfxstream_vulkan_mapper,
    cpp_args: gfxstream_guest_args,
    include_directories: [
        inc_src,
        inc_vulkan_util,
        inc_gfxstream_vulkan_mapper,
        inc_include,
    ],
    dependencies: [
        idep_vulkan_util_headers,
        idep_vulkan_util,
        idep_mesautil,
        dep_virtgpu_kumquat_ffi,
    ],
)

dep_gfxstream_vulkan_mapper = declare_dependency(
    include_directories: [inc_gfxstream_vulkan_mapper],
    link_with: [libgfxstream_vulkan_mapper],
)
