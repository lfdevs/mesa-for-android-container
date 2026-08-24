# Copyright 2019 Google, Inc
# SPDX-License-Identifier: MIT

subdir('mlw_codec')

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
  'ethosu_encode.cpp',
  'ethosu_lower.c',
  'ethosu_ml.c',
  'ethosu_sched.c',
  'ethosu_sched_u85.c',
  'ethosu_weight_cache.c',
)

libethosu = static_library(
  'ethosu',
  [files_ethosu, ethosu_registers],
  include_directories : [inc_gallium_aux, inc_gallium, inc_include, inc_src, inc_libmlw_codec],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil, dep_libdrm],
  link_with : [libmlw_codec],
)

driver_ethosu = declare_dependency(
  compile_args : '-DGALLIUM_ETHOSU',
  link_with : [libethosuwinsys, libethosu]
)
