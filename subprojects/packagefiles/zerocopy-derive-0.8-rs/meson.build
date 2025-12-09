# Copyright © 2024 Google
# SPDX-License-Identifier: MIT

project(
  'zerocopy-derive',
  'rust',
  version : '0.8.13',
  license : 'MIT OR Apache-2.0 OR BSD 2-Clause',
)

syn = subproject('syn-2-rs').get_variable('lib')
quote = subproject('quote-1-rs').get_variable('lib')
proc_macro2 = subproject('proc-macro2-1-rs').get_variable('lib')

rust = import('rust')
lib = rust.proc_macro(
  'zerocopy_derive',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  link_with : [syn, quote, proc_macro2]
)

dep_zerocopy_derive = declare_dependency(
  link_with : [lib]
)
