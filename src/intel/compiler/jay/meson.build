# Copyright 2017 Intel Corporation
# SPDX-License-Identifier: MIT

jay_opcodes = custom_target(
  input : ['jay_opcodes_gen.py'],
  output : ['jay_opcodes.c', 'jay_opcodes.h'],
  command : [prog_python, '@INPUT@', '--code', '@OUTPUT0@', '--header', '@OUTPUT1@'],
  depend_files : files('jay_opcodes.py'),
)

idep_jay_opcodes_h = declare_dependency(
  sources : [jay_opcodes[1]],
  include_directories : include_directories('.'),
)

jay_extra_info_h = custom_target(
  input : ['jay_extra_info.h.py'],
  output : 'jay_extra_info.h',
  command : [prog_python, '@INPUT@', '@OUTPUT@'],
  depend_files : files('jay_opcodes.py'),
)

idep_jay_extra_info_h = declare_dependency(
  sources : [jay_extra_info_h],
  include_directories : include_directories('.'),
)

jay_builder_opcodes_h = custom_target(
  input : 'jay_builder_opcodes.h.py',
  output : 'jay_builder_opcodes.h',
  command : [prog_python, '@INPUT@', '@OUTPUT@'],
  depend_files : files('jay_opcodes.py'),
)

idep_jay_builder_opcodes_h = declare_dependency(
  sources : [jay_builder_opcodes_h],
  include_directories : include_directories('.'),
)

jay_nir_algebraic = custom_target(
  'jay_nir_algebraic.c',
  input : ['jay_nir_algebraic.py'],
  output : 'jay_nir_algebraic.c',
  command : [prog_python, '@INPUT@', '@OUTPUT@', '-p', dir_compiler_nir] ,
  depend_files : nir_algebraic_depends,
)

libintel_compiler_jay_files = files(
  'jay.h',
  'jay_assign_accumulators.c',
  'jay_assign_flags.c',
  'jay_from_nir.c',
  'jay_ir.h',
  'jay_insert_fp_mode.c',
  'jay_liveness.c',
  'jay_lower_post_ra.c',
  'jay_lower_pre_ra.c',
  'jay_lower_scoreboard.c',
  'jay_lower_spill.c',
  'jay_nir.c',
  'jay_opt_dead_code.c',
  'jay_opt_predicate.c',
  'jay_opt_propagate.c',
  'jay_print.c',
  'jay_private.h',
  'jay_prog_data.c',
  'jay_repair_ssa.c',
  'jay_register_allocate.c',
  'jay_simd_width.c',
  'jay_spill.c',
  'jay_to_binary.c',
  'jay_validate.c',
  'jay_validate_ra.c',
)

libintel_compiler_jay = static_library(
  'intel_compiler_jay',
  [libintel_compiler_jay_files, jay_nir_algebraic, jay_opcodes[0]],
  include_directories : [inc_include, inc_src, inc_intel],
  c_args : [no_override_init_args, '-Wno-c23-extensions', '-Wno-array-bounds'],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_nir_headers, idep_jay_opcodes_h, idep_jay_builder_opcodes_h, idep_jay_extra_info_h, idep_mesautil, idep_intel_dev],
  build_by_default : false,
)

idep_intel_compiler_jay = declare_dependency(
  link_with : [libintel_compiler_jay],
  dependencies : [
    idep_nir,
    idep_vtn,
  ],
)

if with_tests
  test(
    'jay_tests',
    executable(
      'jay_tests',
      files(
         'test/test-lower-post-ra.cpp',
         'test/test-optimizer.cpp',
         'test/test-repair-ssa.cpp',
      ),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_intel],
      dependencies: [idep_gtest, idep_nir, idep_jay_opcodes_h, idep_jay_builder_opcodes_h, idep_jay_extra_info_h, idep_mesautil, idep_intel_dev],
      link_with : [libintel_compiler_jay],
    ),
    suite : ['intel'],
    protocol : 'gtest',
  )
endif
