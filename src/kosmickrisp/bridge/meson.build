# Copyright 2025 LunarG, Inc.
# Copyright 2025 Google LLC
# SPDX-License-Identifier: MIT

mtl_bridge_files = files(
  'vk_to_mtl_map.h',
  'vk_to_mtl_map.c',
  'mtl_format.h',
)

if host_machine.system() == 'darwin'
  mtl_bridge_files += files(
    'mtl_bridge.m',
    'mtl_buffer.m',
    'mtl_command_buffer.m',
    'mtl_command_queue.m',
    'mtl_compute_state.m',
    'mtl_device.m',
    'mtl_encoder.m',
    'mtl_heap.m',
    'mtl_library.m',
    'mtl_render_state.m',
    'mtl_residency_set.m',
    'mtl_sampler.m',
    'mtl_sync.m',
    'mtl_texture.m',
  )
else
  mtl_bridge_files += files(
    'stubs/mtl_bridge.c',
    'stubs/mtl_buffer.c',
    'stubs/mtl_command_buffer.c',
    'stubs/mtl_command_queue.c',
    'stubs/mtl_compute_state.c',
    'stubs/mtl_device.c',
    'stubs/mtl_encoder.c',
    'stubs/mtl_heap.c',
    'stubs/mtl_library.c',
    'stubs/mtl_render_state.c',
    'stubs/mtl_residency_set.c',
    'stubs/mtl_sampler.c',
    'stubs/mtl_sync.c',
    'stubs/mtl_texture.c',
  )
endif

mtl_bridge_dependencies = [
  idep_vulkan_lite_runtime_headers,
  idep_vulkan_util
]

libmtl_bridge = static_library(
  'mtl_bridge',
  [mtl_bridge_files],
  include_directories : [include_directories('../vulkan/'), inc_include, inc_src],
  dependencies : mtl_bridge_dependencies,
  gnu_symbol_visibility: 'hidden',
  build_by_default: false,
)

idep_mtl_bridge = declare_dependency(
  link_with : libmtl_bridge,
)
