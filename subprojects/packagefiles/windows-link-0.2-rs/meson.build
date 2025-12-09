# Copyright © 2025 Google
# SPDX-License-Identifier: MIT

project(
  'windows-link-rs',
  'rust',
  version : '0.2.0',
  license : 'MIT OR Apache-2.0',
)

lib = static_library(
  'windows_link',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi: 'rust',
)

dep_windows_link = declare_dependency(
  link_with: [lib],
)
