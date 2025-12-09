# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# Copyright © 2025 Arm Ltd.
# SPDX-License-Identifier: MIT


libpanfrost_model_files = files(
  'pan_model.h',
  'pan_model.c',
)

libpanfrost_model_lib = static_library(
  'panfrost_model_lib',
  [libpanfrost_model_files],
  include_directories : [inc_src],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libpanfrost_model_dep = declare_dependency(
  link_with: [libpanfrost_model_lib],
  include_directories: [inc_include, inc_src, inc_panfrost],
)
