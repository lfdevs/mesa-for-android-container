# Copyright © 2026 Valve Corporation
#
# SPDX-License-Identifier: MIT

shaders = [
  [
    'renderer.vert',
    'renderer_vert',
  ],
  [
    'renderer.frag',
    'renderer_frag',
  ],
]

shader_includes = files(
)

spv_shaders = []
foreach s : shaders
  command = [
    prog_glslang, '-V', '--target-env', 'spirv1.5',
    '-x', '-o', '@OUTPUT@', '@INPUT@', glslang_depfile, glslang_quiet,
  ]

  _spv_name = '@0@.spv.h'.format(s[1])
  spv_shaders += custom_target(
    _spv_name,
    input : s[0],
    output : _spv_name,
    command : command,
    depfile : f'@_spv_name@.d',
    depend_files: shader_includes
  )
endforeach
