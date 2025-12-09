# Copyright © 2024 Google
# SPDX-License-Identifier: MIT

project(
  'thiserror',
  'rust',
  version : '2.0.11',
  license : 'MIT OR Apache-2.0',
)

syn = subproject('syn-2-rs').get_variable('lib')
quote = subproject('quote-1-rs').get_variable('lib')
proc_macro2 = subproject('proc-macro2-1-rs').get_variable('lib')
thiserror_impl = subproject('thiserror-impl-2-rs').get_variable('lib')

lib = static_library(
  'thiserror',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  rust_abi : 'rust',
  link_with : [thiserror_impl]
)

dep_thiserror = declare_dependency(
  link_with : [lib, thiserror_impl]
)
