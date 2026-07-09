# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

gen_opcodes = custom_target(
  'gen_opcodes',
  input : ['gen_opcodes_gen.py'],
  output : ['gen_opcodes.h', 'gen_opcodes_private.h', 'gen_opcodes.cpp'],
  command : [prog_python, '@INPUT@',
             '--public-header', '@OUTPUT0@',
             '--private-header', '@OUTPUT1@',
             '--code', '@OUTPUT2@'],
  depend_files : files('gen_opcodes.py'),
)

idep_gen_opcodes_h = declare_dependency(
  sources : [gen_opcodes[0], gen_opcodes[1]],
  include_directories : include_directories('.'),
)

libintel_compiler_gen_files = files(
  'gen.h',
  'gen_enums.h',
  'gen_types.h',
  'gen_helpers.h',
  'gen_names.h',
  'gen_info_util.h',

  'gen_private.h',

  'gen_names.c',
  'gen_compact.cpp',
  'gen_control_flow.cpp',
  'gen_parse.cpp',
  'gen_print.cpp',
  'gen_encoding.cpp',
  'gen_encoding_pre_xe.cpp',
  'gen_validate.cpp',
)

gen_gen_srcs = custom_target('gen_info_*.h',
                             input : ['gen_inst_info.py',
                                      'pre_xe.json', 'xe.json'],
                             output : ['gen_info_pre_xe.h', 'gen_info_xe.h',
                                       'gen_info_xe2.h', 'gen_info_xe3p.h'],
                             command : [prog_python,
                                       '@INPUT0@',
                                       '--json=@INPUT1@', '--json=@INPUT2@',
                                        '--output-dir=@OUTDIR@', '--cpp'],
                             depend_files : files('gen_opcodes.py'))

libintel_compiler_gen = static_library(
  'intel_compiler_gen',
  [libintel_compiler_gen_files, gen_gen_srcs, gen_opcodes[2]],
  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
  c_args : [no_override_init_args],
  cpp_args : ['-Werror=vla'],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_gen_opcodes_h, idep_mesautil, idep_intel_dev],
  build_by_default : false,
)

idep_intel_compiler_gen = declare_dependency(
  link_with : [libintel_compiler_gen],
  dependencies : [
    idep_gen_opcodes_h,
    idep_mesautil,
  ],
)

gen_roundtrip_cases = files(
  'tests/basic.txt',
  'tests/basic_verbose.txt',
  'tests/basic_translated.txt',
  'tests/send.txt',
)

if with_tests
  test(
    'intel_compiler_gen_tests',
    executable(
      'intel_compiler_gen_tests',
      files(
        'gen_control_flow_test.cpp',
        'gen_encoding_test.cpp',
        'gen_parse_test.cpp',
        'gen_print_test.cpp',
        'gen_validate_test.cpp',
      ),
      include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
      cpp_args : ['-Werror=vla'],
      dependencies : [idep_gtest, idep_mesautil, idep_intel_dev,
                      idep_intel_compiler_gen],
    ),
    suite : ['intel'],
    protocol : 'gtest',
  )
endif

if with_intel_tools
  gentool = executable(
    'gentool',
    files('gentool.cpp'),
    include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
    cpp_args : ['-Werror=vla'],
    gnu_symbol_visibility : 'hidden',
    dependencies : [dep_thread, idep_mesautil, idep_intel_dev,
                    idep_intel_compiler_gen],
    install : true,
  )

  test(
    'intel_compiler_gen_gentool_roundtrip',
    gentool,
    args : ['check-roundtrip', gen_roundtrip_cases],
    suite : ['intel'],
  )
endif

