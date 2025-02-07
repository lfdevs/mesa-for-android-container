# Copyright © 2022 Konstantin Seurer
# Copyright © 2024 Intel Corporation
# SPDX-License-Identifier: MIT

# source file, output name, defines
bvh_shaders = [
  [
    'encode.comp',
    'encode',
    [],
  ],
  [
    'header.comp',
    'header',
    [],
  ],
  [
    'copy.comp',
    'copy',
    []
  ],
]

anv_bvh_include_dir = dir_source_root + '/src/intel/vulkan/bvh'

anv_bvh_includes = files(
  'anv_build_helpers.h',
  'anv_build_interface.h',
  'anv_bvh.h',
)

foreach s : bvh_shaders
  command = [
    prog_glslang, '-V', '-I' + vk_bvh_include_dir, '-I' + anv_bvh_include_dir, '--target-env', 'spirv1.5', '-x', '-o', '@OUTPUT@', '@INPUT@'
  ]
  command += glslang_quiet

  foreach define : s[2]
    command += '-D' + define
  endforeach

  bvh_spv += custom_target(
    s[1] + '.spv.h',
    input : s[0],
    output : s[1] + '.spv.h',
    command : command,
    depend_files: [vk_bvh_includes, anv_bvh_includes],
  )
endforeach
