# Copyright © 2025 Collabora
# SPDX-License-Identifier: MIT

test(
  'bifrost_tests',
  executable(
    'bifrost_tests',
    files(
      'test-constant-fold.cpp',
      'test-dual-texture.cpp',
      'test-lower-swizzle.cpp',
      'test-message-preload.cpp',
      'test-optimizer.cpp',
      'test-pack-formats.cpp',
      'test-packing.cpp',
      'test-scheduler-predicates.cpp',
    ),
    cpp_args : [cpp_msvc_compat_args],
    gnu_symbol_visibility : 'hidden',
    include_directories : [inc_include, inc_src, inc_valhall],
    dependencies: [idep_gtest, idep_nir, idep_bi_opcodes_h, idep_bi_builder_h, idep_valhall_enums_h],
    link_with : [libpanfrost_compiler],
  ),
  suite : ['panfrost'],
  protocol : 'gtest',
)
