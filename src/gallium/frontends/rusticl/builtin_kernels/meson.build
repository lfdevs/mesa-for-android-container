# Copyright 2026 Red Hat, Inc
# SPDX-License-Identifier: MIT

rusticl_kernels_files = files(
  'copy.cl',
)

rusticl_kernels_files_ulong = files(
  'copy_ulong.cl',
)

rusticl_kernels_spv64_rs = custom_target(
  input : rusticl_kernels_files,
  output : 'rusticl_kernels.spv64',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    rusticl_kernels_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    cl_args,
    ],
  depfile : 'rusticl_shaders_64.h.d',
)

rusticl_kernels_spv32_rs = custom_target(
  input : rusticl_kernels_files,
  output : 'rusticl_kernels.spv32',
  command : [
    prog_mesa_clc, '--32bit', '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    rusticl_kernels_files, '--',
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    cl_args,
    ],
  depfile : 'rusticl_shaders_32.h.d',
)

rusticl_kernels_spv64_ulong_rs = custom_target(
  input : rusticl_kernels_files_ulong,
  output : 'rusticl_kernels_ulong.spv64',
  command : [
    prog_mesa_clc, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    rusticl_kernels_files_ulong, '--',
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    cl_args,
    ],
  depfile : 'rusticl_shaders_64_ulong.h.d',
)

rusticl_kernels_spv32_ulong_rs = custom_target(
  input : rusticl_kernels_files_ulong,
  output : 'rusticl_kernels_ulong.spv32',
  command : [
    prog_mesa_clc, '--32bit', '-o', '@OUTPUT@', '--depfile', '@DEPFILE@',
    rusticl_kernels_files_ulong, '--',
    '-I' + join_paths(meson.project_source_root(), 'src/compiler/libcl'),
    '-I' + join_paths(meson.current_source_dir(), '.'),
    cl_args,
    ],
  depfile : 'rusticl_shaders_32_ulong.h.d',
)

rusticl_kernels_spv = static_library(
  'rusticl_kernels',
  structured_sources(
    'lib.rs',
    {
      'gen/' : [
        rusticl_kernels_spv64_rs,
        rusticl_kernels_spv64_ulong_rs,
        rusticl_kernels_spv32_rs,
        rusticl_kernels_spv32_ulong_rs
      ],
    },
  ),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
)
