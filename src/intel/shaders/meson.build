# Copyright © 2023 Intel Corporation
# SPDX-License-Identifier: MIT

if with_intel_vk
  intel_float64_spv_h = custom_target(
    'float64_spv.h',
    input : [glsl2spirv, float64_glsl_file],
    output : 'float64_spv.h',
    command : [
      prog_python, '@INPUT@', '@OUTPUT@',
      prog_glslang,
      '--create-entry', 'main',
      '--vn', 'float64_spv_source',
      '--glsl-version', '450',
      '-Olib',
      glslang_depfile,
    ],
    depfile : 'float64_spv.h.d',
  )
endif

intel_shader_files = files(
  'libintel_shaders.h',
  'generate.cl',
  'generate_draws.cl',
  'generate_draws_iris.cl',
  'query_copy.cl',
  'util.cl',
)

intel_shaders_gens = [ [ 80,   8],
                       [ 90,   9],
                       [110,  11],
                       [120,  12],
                       [125, 125],
                       [200,  20],
                       [300,  30] ]
intel_shaders = []
foreach gen : intel_shaders_gens
  intel_shaders_spv = custom_target(
    input : intel_shader_files,
    output : 'intel_gfx@0@_shaders.spv'.format(gen[0]),
    command : [
      prog_mesa_clc,
      intel_shader_files, '-o', '@OUTPUT@', '--',
      '-DNDEBUG=1',
      '-DGFX_VERx10=@0@'.format(gen[0]),
      '-I' + join_paths(meson.current_source_dir(), '.'),
      '-I' + join_paths(dir_source_root, 'src/compiler/libcl'),
      '-I' + join_paths(dir_source_root, 'src'),
      '-I' + join_paths(dir_source_root, 'src/intel'),
      '-I' + join_paths(meson.project_build_root(), 'src/intel'),
      '-I' + join_paths(dir_source_root, 'src/intel/genxml'),
    ],
    depends : [gen_cl_xml_pack, intel_dev_wa_src],
  )

  intel_shaders += custom_target(
    input : ['spv2hex.py', intel_shaders_spv],
    output : 'intel_gfx@0@_shaders_spv.h'.format(gen[0]),
    command : [
      prog_python, '@INPUT@', '--output', '@OUTPUT@',
      '--prefix', 'gfx@0@_shaders_spv'.format(gen[1]),
    ],
  )

  intel_shaders += custom_target(
    input : intel_shaders_spv,
    output : 'intel_gfx@0@_shaders_binding.h'.format(gen[0]),
    command : [prog_vtn_bindgen, intel_shaders_spv, '@OUTPUT@'],
  )
endforeach

idep_intel_shaders = declare_dependency(
  sources : intel_shaders,
  include_directories : include_directories('.'),
)
