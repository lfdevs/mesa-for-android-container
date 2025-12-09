# Copyright © 2024 Google
# SPDX-License-Identifier: MIT

project(
  'cfg-if',
  'rust',
  version : '1.0.0',
  license : 'MIT OR Apache-2.0',
)

lib = static_library(
  'cfg_if',
  'src/lib.rs',
  override_options : ['rust_std=2018', 'build.rust_std=2018'],
  rust_abi : 'rust',
)

dep_cfg_if = declare_dependency(
  link_with : [lib]
)
