# Copyright 2026 LunarG, Inc.
# Copyright 2026 Google LLC
# SPDX-License-Identifier: MIT

libkk_shader_files = files(
  'kk_draws.cl',
  'kk_triangle_fan.cl',
  'kk_query.cl',
)

libkk_spv = custom_target(
  input : libkk_shader_files,
  output : 'libkk.spv',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    libkk_shader_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'include'),
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.project_source_root(), 'src/poly/cl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    '-I' + join_paths(meson.current_source_dir(), '../../'),
    '-I' + join_paths(meson.current_source_dir(), 'shaders'),
    cl_args,
    ],
  depfile : 'libkk_shaders.h.d',
)

libkk = custom_target(
  input : libkk_spv,
  output : ['libkk.cpp', 'libkk.h'],
  command : [prog_vtn_bindgen2, libkk_spv, '@OUTPUT0@', '@OUTPUT1@'],
)

idep_libkk = declare_dependency(
  sources : [libkk],
  include_directories : include_directories('.'),
)

libkk_shaders = custom_target(
  input : libkk_spv,
  output : ['libkk_shaders.h', 'libkk_shaders.c'],
  command : [prog_kk_clc, libkk_spv, '@OUTPUT0@', '@OUTPUT1@']
)

# TODO_KOSMICKRISP We can get rid of this library once we remove the generated entrypoints for the trampoline
libkk_shaders_lib = static_library(
  'libkk_shaders',
  sources : [libkk_shaders],
  link_with : [libpoly_nir],
  dependencies : [idep_nir, idep_mesautil, idep_libpoly],
)

idep_libkk_shaders_h = declare_dependency(
  sources : [libkk_shaders[0]],
  include_directories : include_directories('.'),
  link_with : libkk_shaders_lib,
)
