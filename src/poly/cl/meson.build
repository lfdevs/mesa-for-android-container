# Copyright 2024 Valve Corporation
# Copyright © 2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

libpoly_shader_files = files(
  'geometry.cl',
  'tessellation.cl',
)

libpoly_shaders_spv = custom_target(
  input : libpoly_shader_files,
  output : 'libpoly.spv',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    libpoly_shader_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'include'),
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    '-I' + join_paths(meson.current_source_dir(), '../../'),
    cl_args,
    ],
  depends : [],
  depfile : 'libpoly_shaders.h.d',
)

libpoly_shaders = custom_target(
  input : libpoly_shaders_spv,
  output : ['libpoly.cpp', 'libpoly.h'],
  command : [prog_vtn_bindgen2, libpoly_shaders_spv, '@OUTPUT0@', '@OUTPUT1@'],
)

idep_libpoly = declare_dependency(
  sources : [libpoly_shaders],
  include_directories : include_directories('.'),
)
