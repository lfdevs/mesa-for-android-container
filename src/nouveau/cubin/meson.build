# Copyright © 2025 Autumn Ashton
# SPDX-License-Identifier: MIT

libnouveau_cubin_files = files(
  'nv_cubin.c',
  'nv_fatbin.c',
)

_libnouveau_cubin = static_library(
  'nouveau_cubin',
  libnouveau_cubin_files,
  include_directories : [inc_include, inc_src],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_mesautil,
    dep_elf,
  ],
)

idep_nouveau_cubin = declare_dependency(
  include_directories : include_directories('.'),
  link_with : _libnouveau_cubin,
)
