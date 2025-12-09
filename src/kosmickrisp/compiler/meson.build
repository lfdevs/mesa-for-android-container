# Copyright 2025 LunarG, Inc.
# Copyright 2025 Google LLC
# SPDX-License-Identifier: MIT

libmsl_compiler_files = files(
    'nir_to_msl.c',
    'msl_type_inference.c',
    'msl_iomap.c',
    'msl_nir_lower_common.c',
    'msl_nir_lower_subgroups.c',
)

msl_nir_algebraic_c = custom_target(
  input : 'msl_nir_algebraic.py',
  output : 'msl_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@', '-p', dir_compiler_nir,
  ],
  capture : true,
  depend_files : nir_algebraic_depends,
)


libmsl_compiler = static_library(
    'msl_compiler',
    [libmsl_compiler_files, msl_nir_algebraic_c],
    dependencies : [idep_nir, idep_mesautil],
    gnu_symbol_visibility: 'hidden',
    build_by_default: false,
)

idep_msl_to_nir = declare_dependency(
  link_with : libmsl_compiler,
)

