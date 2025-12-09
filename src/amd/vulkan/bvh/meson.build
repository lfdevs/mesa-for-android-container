# Copyright © 2022 Konstantin Seurer
# SPDX-License-Identifier: MIT

# source file, output name, defines
bvh_shaders = [
  [
    'copy_blas_addrs_gfx12.comp',
    'copy_blas_addrs_gfx12',
  ],
  [
    'copy.comp',
    'copy',
  ],
  [
    'encode_gfx12.comp',
    'encode_gfx12',
  ],[
    'encode_triangles_gfx12.comp',
    'encode_triangles_gfx12',
  ],
  [
    'encode.comp',
    'encode',
  ],
  [
    'header.comp',
    'header',
  ],
  [
    'update.comp',
    'update',
  ],
  [
    'update_gfx12.comp',
    'update_gfx12',
  ],
  [
    'leaf.comp',
    'radv_leaf',
  ],
]

bvh_include_dir = dir_source_root + '/src/amd/vulkan/bvh'
vk_bvh_include_dir = dir_source_root + '/src/vulkan/runtime/bvh'

bvh_includes = files(
  'build_helpers.h',
  'build_interface.h',
  'bvh.h',
  'encode.h',
  'update.h',
  vk_bvh_include_dir + '/vk_build_helpers.h',
  vk_bvh_include_dir + '/vk_bvh.h',
)

bvh_spv = []
foreach s : bvh_shaders
  command = [
    prog_glslang, '-V', '-I' + bvh_include_dir, '-I' + vk_bvh_include_dir, '--target-env', 'spirv1.5',
    '-x', '-o', '@OUTPUT@', '@INPUT@', glslang_depfile, glslang_quiet,
  ]
  command += vk_glsl_shader_preamble

  _bvh_name = '@0@.spv.h'.format(s[1])
  bvh_spv += custom_target(
    _bvh_name,
    input : s[0],
    output : _bvh_name,
    command : command,
    depfile : f'@_bvh_name@.d',
    depend_files: bvh_includes
  )
endforeach
