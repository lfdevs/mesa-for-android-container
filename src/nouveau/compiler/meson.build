# Copyright © 2022 Collabora, Ltd.
# SPDX-License-Identifier: MIT

subdir('latencies')

dep_paste = dependency('paste',
  version : '>= 1.0.14',
  fallback : ['paste-1-rs', 'dep_paste'],
  required : true,
)

dep_rustc_hash = dependency('rustc-hash',
  version: '>= 2.0',
  fallback: ['rustc-hash-2-rs', 'dep_rustc_hash'],
  required: true,
)

libnak_c_files = files(
  'nak.h',
  'nak_nir.c',
  'nak_nir_lower_cf.c',
  'nak_nir_lower_cmat.c',
  'nak_nir_lower_fs_inputs.c',
  'nak_nir_lower_gs_intrinsics.c',
  'nak_nir_lower_image_addrs.c',
  'nak_nir_lower_non_uniform_ldcx.c',
  'nak_nir_lower_scan_reduce.c',
  'nak_nir_lower_kepler_shared_atomics.c',
  'nak_nir_lower_tex.c',
  'nak_nir_lower_vtg_io.c',
  'nak_nir_mark_lcssa_invariants.c',
  'nak_nir_rematerialize_load_const.c',
)

_libacorn_rs = static_library(
  'acorn',
  files('acorn/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : nouveau_rust_args,
)

idep_acorn_rs = declare_dependency(
  link_with : _libacorn_rs,
)

libnak_deps = [
  idep_mesautil,
  idep_nir_headers,
  idep_nvidia_headers,
  idep_nil_h,
]

_nak_bindings_rs = rust.bindgen(
  input : ['nak_bindings.h'],
  output : 'nak_bindings.rs',
  c_args : [
    pre_args,
  ],
  args : [
    bindgen_output_args,
    compiler_rs_bindgen_blocklist,
    '--raw-line', 'use compiler::bindings::*;',
    '--allowlist-type', 'drm.*',
    '--allowlist-type', 'nak_.*',
    '--allowlist-type', 'nouveau_ws_.*',
    '--allowlist-var', 'DRM_.*',
    '--allowlist-var', 'NAK_.*',
    '--allowlist-var', 'NVIDIA_VENDOR_ID',
    '--allowlist-function', 'drm.*',
    '--allowlist-function', 'nak_.*',
    '--allowlist-function', 'nouveau_ws_.*',
    # provided through compiler::bindings::*
    '--blocklist-type', 'glsl_.*',
    '--no-prepend-enum-name',
    '--with-derive-default',
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
    nouveau_rust_args,
    # Otherwise, rustc trips up on -pthread
    '-Clink-arg=-Wno-unused-command-line-argument',
  ],
  dependencies : [
    dep_paste,
    dep_rustc_hash,
    idep_bitview_rs,
    idep_compiler_rs,
    idep_nvidia_headers_rs,
  ],
  link_with : [
    _libnak_bindings_rs,
    _libnak_ir_proc_rs,
    libnak_latencies_rs,
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
      # Don't run HW or nvdisasm tests by default
      '--skip', 'hw_tests::',
      '--skip', 'nvdisasm_tests::',
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
  include_directories : [inc_include, inc_src],
  dependencies : libnak_deps,
  link_with : [_libnak_rs],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

idep_nak = declare_dependency(
  include_directories : include_directories('.'),
  link_with : _libnak,
)
