# Copyright © 2022 Konstantin Seurer
# Copyright © 2024 Intel Corporation
# SPDX-License-Identifier: MIT

# source file, output name, defines
bvh_shaders = [
  'encode.comp',
  'header.comp',
  'copy.comp',
]

# A mapping: [filename version, GFX_VERx10 define version]
gfx_versions = [
  ['125', '125'],
  ['20', '200'],
  ['30', '300']
]

anv_bvh_include_dir = dir_source_root + '/src/intel/vulkan/bvh'

anv_bvh_includes = files(
  'anv_build_helpers.h',
  'anv_build_interface.h',
  'anv_bvh.h',
)

foreach shader : bvh_shaders
  foreach gfx: gfx_versions
    file_name_ver = gfx[0]
    define_ver = gfx[1]
    command = [
      prog_glslang, '-V', '-I' + vk_bvh_include_dir, '-I' + anv_bvh_include_dir,
      '--target-env', 'spirv1.5', '-x', '-o', '@OUTPUT@', '@INPUT@',
      '-DGFX_VERx10=' + define_ver # so that we can use this macro inside shaders
    ]
    command += glslang_quiet
    command += vk_glsl_shader_preamble

    shader_name = shader.split('.')[0]
    output_name = 'gfx' + file_name_ver + '_' + shader_name + '.spv.h'
    # By doing this, encode.comp with DGFX_VERx10=300 will be compiled to gfx30_encode.spv.h
    # So the genX(encode).spv.h in genX_acceleration_structure.c can find the right file

    bvh_spv += custom_target(
      output_name,
      input : shader,
      output : output_name,
      command : command,
      depend_files: [vk_bvh_includes, anv_bvh_includes],
    )
   endforeach
endforeach
