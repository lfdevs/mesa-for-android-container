# Copyright © 2025 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

pco_usclib_shader_files = files(
  'common.cl',
  'sync.cl',
  'tex.cl',
)

pco_usclib_spv = custom_target(
  'pco_usclib.spv',
  input : pco_usclib_shader_files,
  output : 'pco_usclib.spv',
  command : [
    prog_mesa_clc,
    '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    pco_usclib_shader_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'include'),
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    '-I' + join_paths(meson.current_source_dir(), '../..'),
    '-I' + join_paths(meson.current_source_dir(), '../../include'),
    '-I' + join_paths(meson.current_source_dir(), '../../..'),
    '-I' + join_paths(meson.current_build_dir(), '../..'),
    cl_args,
  ],
  depends : [pco_pygen_sources, pvr_xml_pack],
  depfile : 'pco_usclib.h.d',
)

pco_usclib = custom_target(
  'pco_usclib.h',
  input : pco_usclib_spv,
  output : ['pco_usclib.cpp', 'pco_usclib.h'],
  command : [prog_vtn_bindgen2, pco_usclib_spv, '@OUTPUT0@', '@OUTPUT1@'],
)

idep_pco_usclib = declare_dependency(
  sources : [pco_usclib],
  include_directories : include_directories('.'),
)
