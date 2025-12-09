# Copyright © 2025 Collabora Ltd.
# SPDX-License-Identifier: MIT

libpoly_nir_files = files(
  'poly_nir_lower_gs.c',
  'poly_nir_lower_sysvals.c',
  'poly_nir_lower_tess.c',
  'poly_nir_lower_vs.c',
)

libpoly_nir = static_library(
  'libpoly_nir',
  [libpoly_nir_files],
  include_directories : [inc_poly],
  c_args : [no_override_init_args, '-Wno-c2x-extensions'],
  gnu_symbol_visibility : 'hidden',
  dependencies: [idep_nir, idep_mesautil, idep_libpoly],
  build_by_default : false,
)
