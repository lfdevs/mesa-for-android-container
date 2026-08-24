# Copyright © 2024 Intel Corporation
# SPDX-License-Identifier: MIT

executor_flags = [
  no_override_init_args,
  sse2_args,
]

executor_includes = [
  inc_include,
  inc_src,
  inc_intel,
]

executor_manual_h = custom_target(
  'executor_manual.h',
  input: 'executor.1',
  output: 'executor_manual.h',
  command: [prog_python, files_xxd, '@INPUT@', '@OUTPUT@', '--name', 'executor_manual'],
)

executor_hw_libs = []
foreach v: ['90', '110', '120', '125', '200', '300', '350']
  executor_hw_libs += static_library(
    'executor_hw_ver@0@'.format(v),
    ['executor_genx.c', gen_xml_pack],
    include_directories: [executor_includes],
    c_args: [
      executor_flags,
      '-DGFX_VERx10=@0@'.format(v),
    ],
    gnu_symbol_visibility: 'hidden',
    dependencies: [
      dep_valgrind,
      idep_genxml,
      idep_intel_dev,
    ],
    build_by_default : false,
  )
endforeach

executor = executable(
  'executor',
  [
    'executor_main.c',
    'executor_macros.c',
    executor_manual_h,
    # TODO: Move slice.h into a proper common place.
    '../mda/slice.c',
  ],
  dependencies: [
    dep_libdrm,
    dep_lua,
    dep_valgrind,
    idep_genxml,
    idep_intel_compiler_gen,
    idep_intel_decoder_gen,
    idep_intel_dev,
    idep_intel_perf,
    idep_libintel_common,
  ],
  include_directories: [executor_includes],
  link_with: [executor_hw_libs],
  c_args: [executor_flags],
  gnu_symbol_visibility: 'hidden',
  install: true
)
