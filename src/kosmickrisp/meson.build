# Copyright 2025 LunarG, Inc.
# Copyright 2025 Google LLC
# SPDX-License-Identifier: MIT

subdir('bridge')
subdir('compiler')
subdir('util')
subdir('vulkan')

executable(
    'kosmicomp',
    files('kosmicomp.c'),
    dependencies : [idep_nir, idep_vtn, idep_vulkan_runtime_headers, idep_vulkan_util_headers],
    link_with: [libkk],
    link_args: ['-Wl,-undefined,dynamic_lookup'],
)
