# Copyright 2019 Google, Inc
# SPDX-License-Identifier: MIT

rocket_registers = custom_target(
  'rkt_registers.h',
  input : ['gen_header.py', 'registers.xml'],
  output : 'rkt_registers.h',
  command : [prog_python, '@INPUT0@', '--rnn', '.', '--xml', '@INPUT1@', 'c-defines'],
  capture : true,
)

files_rocket = files(
  'rkt_coefs.c',
  'rkt_device.c',
  'rkt_ml.c',
  'rkt_regcmd.c',
  'rkt_task.c',
)

librocket = static_library(
  'rocket',
  [files_rocket, rocket_registers],
  include_directories : [inc_gallium_aux, inc_gallium, inc_include, inc_src],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil, dep_libdrm],
)

driver_rocket = declare_dependency(
  compile_args : '-DGALLIUM_ROCKET',
  link_with : [librocketwinsys, librocket]
)

shared_library('intercept',
   [files('intercept.c'), rocket_registers],
   include_directories : [inc_include],
   dependencies : [dep_libdrm],
   c_args: ['-Wno-error=missing-prototypes', '-g', '-O0']
)
