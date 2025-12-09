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

executor_hw_libs = []
foreach v: ['90', '110', '120', '125', '200', '300']
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
  ],
  dependencies: [
    dep_libdrm,
    dep_lua,
    dep_valgrind,
    idep_brw_asm,
    idep_genxml,
    idep_intel_decoder_brw,
    idep_intel_dev,
    idep_libintel_common,
  ],
  include_directories: [executor_includes],
  link_with: [executor_hw_libs],
  c_args: [executor_flags],
  gnu_symbol_visibility: 'hidden',
  install: true
)
