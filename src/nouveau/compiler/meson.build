# Copyright © 2022 Collabora, Ltd.
# SPDX-License-Identifier: MIT
nak_rust_args = [
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

dep_paste = dependency('paste',
  version : '>= 1.0.14',
  fallback : ['paste', 'dep_paste'],
  required : true,
)

libnak_c_files = files(
  'nak.h',
  'nak_nir.c',
  'nak_nir_lower_cf.c',
  'nak_nir_lower_fs_inputs.c',
  'nak_nir_lower_gs_intrinsics.c',
  'nak_nir_lower_non_uniform_ldcx.c',
  'nak_nir_lower_scan_reduce.c',
  'nak_nir_lower_tex.c',
  'nak_nir_lower_vtg_io.c',
  'nak_nir_split_64bit_conversions.c',
)

_libacorn_rs = static_library(
  'acorn',
  files('acorn/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : nak_rust_args,
)

idep_acorn_rs = declare_dependency(
  link_with : _libacorn_rs,
)

_libbitview_rs = static_library(
  'bitview',
  files('bitview/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : nak_rust_args,
)

idep_bitview_rs = declare_dependency(
  link_with : _libbitview_rs,
)

libnak_deps = [
  idep_mesautil,
  idep_nir_headers,
  idep_nvidia_headers,
]

_nak_bindings_rs = rust.bindgen(
  input : ['nak_bindings.h'],
  output : 'nak_bindings.rs',
  c_args : [
    pre_args,
  ],
  args : [
    compiler_rs_bindgen_blocklist,
    '--raw-line', '#![allow(non_camel_case_types)]',
    '--raw-line', '#![allow(non_snake_case)]',
    '--raw-line', '#![allow(non_upper_case_globals)]',
    '--raw-line', 'use compiler::bindings::*;',
    '--allowlist-type', 'drm.*',
    '--allowlist-type', 'nak_.*',
    '--allowlist-type', 'nouveau_ws_.*',
    '--allowlist-var', 'DRM_.*',
    '--allowlist-var', 'NVIDIA_VENDOR_ID',
    '--allowlist-function', 'drm.*',
    '--allowlist-function', 'nak_.*',
    '--allowlist-function', 'nouveau_ws_.*',
    '--no-prepend-enum-name',
  ],
  dependencies : [
    dep_libdrm,
    idep_nouveau_ws,
    libnak_deps,
  ],
)

_libnak_bindings_rs = static_library(
  'nak_bindings',
  _nak_bindings_rs,
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_compiler_rs,
  ],
  rust_abi : 'rust',
)

_libnak_ir_proc_rs = rust.proc_macro(
  'nak_ir_proc',
  files('nak/ir_proc.rs'),
  dependencies : [idep_compiler_proc_rs],
)

_libnak_rs = static_library(
  'nak_rs',
  files('nak/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'c',
  rust_args : [
    nak_rust_args,
    # Otherwise, rustc trips up on -pthread
    '-Clink-arg=-Wno-unused-command-line-argument',
  ],
  dependencies : [
    dep_paste,
    idep_compiler_rs,
    idep_nvidia_headers_rs,
  ],
  link_with : [
    _libbitview_rs,
    _libnak_bindings_rs,
    _libnak_ir_proc_rs,
  ],
)

# TODO: Linking Rust executables (such as unit tests) doesn't play nicely
# with the sanitizers because meson doesn't know to pass -fsanitize to the
# Rust linker.  See also https://github.com/mesonbuild/meson/issues/11741
if with_tests and get_option('b_sanitize') == 'none'
  rust.test(
    'nak',
    _libnak_rs,
    args : [
      # Don't run HW tests by default
      '--skip', 'hw_tests::',
    ],
    suite : ['nouveau'],
    dependencies : [
      dep_libdrm,
      idep_nouveau_ws,
      idep_compiler.partial_dependency(link_args : true, links : true),
      idep_mesautil.partial_dependency(link_args : true, links : true),
      idep_nv_push_rs,
    ],
    # This is needed to ensure we link against glibc
    # See also https://gitlab.freedesktop.org/mesa/mesa/-/issues/11632
    rust_args: ['-C', 'default-linker-libraries'],
    link_with: [
      _libacorn_rs,
    ],
  )
endif

nak_nir_algebraic_c = custom_target(
  'nak_nir_algebraic.c',
  input : 'nak_nir_algebraic.py',
  output : 'nak_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@',
    '-p', dir_compiler_nir,
    '--out', '@OUTPUT@',
  ],
  depend_files : nir_algebraic_depends,
)

_libnak = static_library(
  'nak',
  [libnak_c_files, nak_nir_algebraic_c],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium],
  dependencies : libnak_deps,
  link_with : [_libnak_rs],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

idep_nak = declare_dependency(
  include_directories : include_directories('.'),
  link_with : _libnak,
)
