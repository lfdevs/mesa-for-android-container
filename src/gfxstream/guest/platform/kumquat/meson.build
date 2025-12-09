# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

if with_virtgpu_kumquat
    subdir('vulkan-mapper')
    files_libplatform_virtgpu_kumquat = files(
        'VirtGpuKumquatBlob.cpp',
        'VirtGpuKumquatBlobMapping.cpp',
        'VirtGpuKumquatDevice.cpp',
        'VirtGpuKumquatSync.cpp',
    )
else
    dep_gfxstream_vulkan_mapper = null_dep
    files_libplatform_virtgpu_kumquat = files('VirtGpuKumquatStub.cpp')
endif

libplatform_virtgpu_kumquat = static_library(
    'platform_virtgpu_kumquat',
    files_libplatform_virtgpu_kumquat,
    cpp_args: gfxstream_guest_args,
    include_directories: [
        inc_platform_virtgpu,
        inc_src,
        inc_vulkan_util,
        inc_include,
    ],
    dependencies: [
        idep_vulkan_util_headers,
        dep_virtgpu_kumquat_ffi,
        dep_gfxstream_vulkan_mapper,
    ],
)
