# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

libgetopt = static_library(
  'getopt',
  'getopt_long.c',
)

idep_getopt = declare_dependency(
  link_with : libgetopt,
  include_directories : include_directories('.', is_system : true),
)
