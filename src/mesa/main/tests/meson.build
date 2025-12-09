# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_main_test = files(
  'enum_strings.cpp',
  'disable_windows_include.c',
  'mesa_formats.cpp',
  'mesa_extensions.cpp',
  'program_state_string.cpp',
)
# disable_windows_include.c includes this generated header.
files_main_test += main_marshal_generated_h

test(
  'main-test',
  executable(
    'main_test',
    [files_main_test, main_dispatch_h],
    include_directories : [inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux],
    dependencies : [idep_gtest, dep_clock, dep_dl, dep_thread, idep_nir_headers, idep_mesautil],
    link_with : [libmesa, libgallium, libglapi],
  ),
  suite : ['mesa'],
  protocol : 'gtest',
)
