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
intel_shaders_headers = []
foreach gen : intel_shaders_gens
  intel_shaders_spv = custom_target(
    input : intel_shader_files,
    output : 'intel_gfx@0@_shaders.spv'.format(gen[0]),
    command : [
      prog_mesa_clc,
      intel_shader_files, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@', '--',
      '-DGFX_VERx10=@0@'.format(gen[0]),
      '-I' + join_paths(meson.current_source_dir(), '.'),
      '-I' + join_paths(dir_source_root, 'src/compiler/libcl'),
      '-I' + join_paths(dir_source_root, 'src'),
      '-I' + join_paths(dir_source_root, 'src/intel'),
      '-I' + join_paths(meson.project_build_root(), 'src/intel'),
      '-I' + join_paths(dir_source_root, 'src/intel/genxml'),
      cl_args,
    ],
    depends : [gen_cl_xml_pack, intel_dev_wa_src],
    depfile : 'intel_gfx@0@_shaders.spv.d'.format(gen[0]),
  )

  _intel_shaders = custom_target(
    input : intel_shaders_spv,
    output : ['intel_gfx@0@_shaders_binding.cpp'.format(gen[0]),
              'intel_gfx@0@_shaders_binding.h'.format(gen[0])],
    command : [prog_vtn_bindgen2, intel_shaders_spv, '@OUTPUT0@', '@OUTPUT1@'],
  )

  intel_shaders += _intel_shaders
  intel_shaders_headers += _intel_shaders[1]
endforeach

libintel_shaders = static_library(
  'intel_shaders',
  intel_shaders,
  dependencies : idep_nir,
)

idep_intel_shaders = declare_dependency(
  link_with : libintel_shaders,
  sources : intel_shaders_headers,
  include_directories : include_directories('.'),
)
