# Copyright © 2024 Google
# SPDX-License-Identifier: MIT

project(
  'bitflags',
  'rust',
  version : '2.9.1',
  license : 'MIT OR Apache-2.0',
)

lib = static_library(
  'bitflags',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi : 'rust',
)

dep_bitflags = declare_dependency(
  link_with : [lib]
)
