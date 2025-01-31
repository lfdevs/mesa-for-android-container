# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

if with_any_opengl and with_tests
  test(
    'shared-glapi-test',
    executable(
      ['shared-glapi-test', glapitable_h],
      'check_table.cpp',
      cpp_args : [cpp_msvc_compat_args],
      include_directories : [inc_src, inc_include, inc_mapi],
      link_with : [shared_glapi_lib],
      dependencies : [dep_thread, idep_gtest, idep_mesautilc11],
    ),
    suite : ['mapi'],
    protocol : 'gtest',
  )
endif
