# Copyright © 2019 Purism SPC
# SPDX-License-Identifier: MIT

etnaviv_cmd_stream_test = executable(
  'etnaviv_cmd_stream_test',
  'etnaviv_cmd_stream_test.c',
  include_directories : [
    inc_etnaviv,
    inc_include,
    inc_src,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    dep_valgrind,
    idep_mesautil,
  ],
  link_with : libetnaviv_drm,
  install: false,
)

etnaviv_bo_cache_test = executable(
  'etnaviv_bo_cache_test',
  'etnaviv_bo_cache_test.c',
  include_directories : [
    inc_etnaviv,
    inc_include,
    inc_src,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    dep_valgrind,
    idep_mesautil,
  ],
  link_with : libetnaviv_drm,
  install: false,
)
