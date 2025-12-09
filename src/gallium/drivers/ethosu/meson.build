# Copyright 2019 Google, Inc
# SPDX-License-Identifier: MIT

ethosu_registers = custom_target(
  'ethosu_registers.h',
  input : ['gen_parser.py', 'gen_header.py', 'registers.xml'],
  output : 'ethosu_registers.h',
  command : [prog_python, '@INPUT1@', '--rnn', '.', '--xml', '@INPUT2@', 'c-defines'],
  capture : true,
)

files_ethosu = files(
  'ethosu_cmd.c',
  'ethosu_coefs.c',
  'ethosu_device.c',
  'ethosu_lower.c',
  'ethosu_ml.c',
  'ethosu_sched.c',
  'mlw_codec/mlw_encode.c',
)

libethosu = static_library(
  'ethosu',
  [files_ethosu, ethosu_registers],
  include_directories : [inc_gallium_aux, inc_gallium, inc_include, inc_src],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil, dep_libdrm],
)

driver_ethosu = declare_dependency(
  compile_args : '-DGALLIUM_ETHOSU',
  link_with : [libethosuwinsys, libethosu]
)
