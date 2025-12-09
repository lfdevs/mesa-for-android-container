# Copyright © 2025 Collabora, Ltd.
# SPDX-License-Identifier: MIT

nouveau_rust_args = [
  rust_2024_lint_args,
  '-Aclippy::identity_op',
  '-Aclippy::len_zero',
  '-Aclippy::manual_range_contains',
  # normally this is a good one, but we use it where the "better" code is worse
  '-Aclippy::needless_range_loop',
  '-Aclippy::redundant_field_names',
  '-Aclippy::upper_case_acronyms',
  '-Aclippy::vec_box',
  '-Aclippy::write_with_newline',
  # warns about public function might dereference a raw pointer, but nothing is
  # actually public here
  '-Aclippy::not_unsafe_ptr_arg_deref',
  '-Anon_snake_case',
]

_libbitview_rs = static_library(
  'bitview',
  files('bitview/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : nouveau_rust_args,
)

idep_bitview_rs = declare_dependency(
  link_with : _libbitview_rs,
)
