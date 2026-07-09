# Copyright © 2026 Valve Corporation
#
# SPDX-License-Identifier: MIT

gamma_files = files(
  'radv/gamma_file_view_radv_bvh4.cpp',
  'radv/gamma_file_view_radv_bvh8.cpp',
  'radv/gamma_file_view_radv.cpp',
  'main.cpp',
  'gamma_app.cpp',
  'gamma_file_view.cpp',
  'gamma_util.cpp',
)

subdir('shaders')

dep_sdl3 = dependency('sdl3')

dep_vulkan = dependency('vulkan')

executable(
  'gamma',
  [gamma_files, spv_shaders],
  cpp_args: [cpp_msvc_compat_args],
  gnu_symbol_visibility: 'hidden',
  link_with: [],
  include_directories: [
    inc_include,
    inc_src,
    inc_util,
    inc_radv_bvh,
    inc_vulkan_bvh,
    inc_radv_tools,
  ],
  dependencies: [idep_mesautil, libimgui_sdl3_vulkan_dep, dep_sdl3, dep_vulkan],
)