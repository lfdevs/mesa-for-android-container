# Copyright 2022 Google
# SPDX-License-Identifier: MIT

files_libplatform_virtgpu_drm = files(
    'DrmSync.cpp',
    'DrmVirtGpuBlob.cpp',
    'DrmVirtGpuBlobMapping.cpp',
    'DrmVirtGpuDevice.cpp',
)

libplatform_virtgpu_drm = static_library(
    'platform_virtgpu_drm',
    files_libplatform_virtgpu_drm,
    cpp_args: gfxstream_guest_args,
    include_directories: [inc_platform_virtgpu, inc_src, inc_include],
    dependencies: dep_libdrm,
)
