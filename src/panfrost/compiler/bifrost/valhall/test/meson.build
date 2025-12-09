# Copyright © 2025 Collabora
# SPDX-License-Identifier: MIT

test(
  'valhall_tests',
  executable(
    'valhall_tests',
    files(
      'test-add-imm.cpp',
      'test-validate-fau.cpp',
      'test-insert-flow.cpp',
      'test-lower-isel.cpp',
      'test-lower-constants.cpp',
      'test-mark-last.cpp',
      'test-merge-flow.cpp',
      'test-packing.cpp',
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
