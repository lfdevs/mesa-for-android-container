# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libgtest = static_library(
  'gtest',
  files('src/gtest-all.cc', 'src/gtest_main.cc'),
  include_directories : include_directories('include'),
  override_options: 'warning_level=1',
  build_by_default : false,
)

idep_gtest = declare_dependency(
  link_with : libgtest,
  include_directories : include_directories('include', is_system : true),
  dependencies : [dep_thread],
)
