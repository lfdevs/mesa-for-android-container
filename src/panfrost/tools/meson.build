# Copyright © 2021 Collabora
# SPDX-License-Identifier: MIT

coredumpdec = executable(
  'panfrostdump',
  files('panfrostdump.c'),
  c_args : [c_msvc_compat_args, no_override_init_args, compile_args_panfrost],
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src],
  dependencies: [libpanfrost_dep],
  build_by_default : true,
  install: true
)

panfrost_texfeatures = executable(
  'panfrost_texfeatures',
  files('panfrost_texfeatures.c'),
  c_args : [c_msvc_compat_args, no_override_init_args, compile_args_panfrost],
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src],
  dependencies: [libpanfrost_dep],
  build_by_default : true,
  install: true
)
