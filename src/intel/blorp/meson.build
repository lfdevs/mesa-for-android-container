# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

blorp_shader_files = files(
  'blorp_shaders.cl',
)

blorp_shaders_spv = custom_target(
  input : blorp_shader_files,
  output : 'blorp_shaders.spv',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    blorp_shader_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'include'),
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.project_source_root(), 'src'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    cl_args,
    ],
  depends : [gen_cl_xml_pack],
  depfile : 'blorp_shaders.h.d',
)

blorp_shaders = custom_target(
  input : blorp_shaders_spv,
  output : ['blorp_shaders.cpp', 'blorp_shaders.h'],
  command : [prog_vtn_bindgen2, blorp_shaders_spv, '@OUTPUT0@', '@OUTPUT1@'],
)

idep_blorp_shaders = declare_dependency(
  sources : [blorp_shaders],
  include_directories : include_directories('.'),
)

files_libblorp = files(
  'blorp.c',
  'blorp.h',
  'blorp_blit.c',
  'blorp_clear.c',
  'blorp_indirect_copy.c',
  'blorp_nir_builder.h',
  'blorp_priv.h',
)

files_blorp_brw = files(
  'blorp_genX_exec_brw.h',
  'blorp_brw.c',
)

files_blorp_elk = files(
  'blorp_genX_exec_elk.h',
  'blorp_elk.c',
)

libblorp = static_library(
  'blorp',
  [files_libblorp, files_blorp_brw],
  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
  c_args : [no_override_init_args, soong_compat_c_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_nir_headers, idep_genxml, idep_mesautil, idep_intel_dev,
    idep_intel_compiler_gen, idep_blorp_shaders
  ],
  build_by_default: false,
)

idep_intel_blorp = declare_dependency(
  link_with: libblorp,
  dependencies: [idep_intel_compiler_gen],
)

if with_intel_elk
  libblorp_elk = static_library(
    'blorp_elk',
    [files_libblorp, files_blorp_elk],
    include_directories : [inc_include, inc_src, inc_intel],
    c_args : [no_override_init_args, soong_compat_c_args],
    gnu_symbol_visibility : 'hidden',
    dependencies : [
      idep_nir_headers, idep_genxml, idep_mesautil, idep_intel_dev,
      idep_blorp_shaders
    ],
    build_by_default: false,
  )

  idep_intel_blorp_elk = declare_dependency(
    dependencies: [idep_intel_dev],
    link_with: libblorp_elk,
  )
else
  idep_intel_blorp_elk = null_dep
endif
