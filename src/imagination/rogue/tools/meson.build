# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

# FIXME: Remove idep_vulkan_runtime once
# the compiler/driver interface is finalised.
rogue_compiler = executable(
  'rogue_vk_compiler',
  'vk_compiler.c',
  link_with : [libpowervr_rogue],
  dependencies : [idep_mesautil, idep_nir, idep_vulkan_runtime, dep_csbgen],
  include_directories : [
    inc_imagination,
    inc_include,
    inc_src,
    inc_rogue,
  ],
  c_args : [imagination_c_args],
  build_by_default : with_imagination_tools,
  install : false,
)
