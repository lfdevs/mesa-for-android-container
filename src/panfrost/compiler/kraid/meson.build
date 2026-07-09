# Copyright © 2026 Collabora, Ltd.
# SPDX-License-Identifier: MIT

dep_bitflags = dependency('bitflags',
  version : '>= 2.9.1',
  fallback : ['bitflags-2-rs', 'dep_bitflags'],
  required : true,
)

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

dep_xml_rs = dependency('xml',
  version: '>= 1.2.1',
  fallback: ['xml-rs', 'dep_xml_rs'],
  required: true,
  native: true,
)

_kraid_bindings_deps = [
  idep_mesautil,
  idep_nir_headers,
]

_kraid_bindings_rs = rust.bindgen(
  input : ['bindings.h'],
  output : 'kraid_bindings.rs',
  output_inline_wrapper : 'kraid_bindings.c',
  c_args : [
    pre_args,
  ],
  args : [
    bindgen_output_args,
    compiler_rs_bindgen_blocklist,
    '--raw-line', 'use compiler::bindings::*;',
    '--allowlist-type', 'bi_.*',
    '--allowlist-type', 'pan_.*',
    '--allowlist-function', 'pan_.*',
    '--allowlist-function', 'kraid_.*',
    '--allowlist-var', 'BI_VA_LOD_MODE_.*',
    '--no-prepend-enum-name',
    '--with-derive-default',
  ],
  dependencies : _kraid_bindings_deps,
)

_libkraid_binding_wrappers = static_library(
  'kraid_binding_wrappers',
  ['bindings.h', _kraid_bindings_rs[1]],
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    fs.relative_to(meson.project_build_root(), meson.current_source_dir()),
  ],
  c_args : [
    pre_args,
    cc.get_supported_arguments('-Wno-missing-prototypes'),
  ],
  dependencies : _kraid_bindings_deps,
)

_libkraid_bindings_rs = static_library(
  'kraid_bindings',
  _kraid_bindings_rs[0],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_compiler_rs,
  ],
  link_with : [_libkraid_binding_wrappers],
  rust_abi : 'rust',
)

_libkraid_proc_rs = static_library(
  'kraid_proc',
  files('proc/lib.rs'),
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_syn,
    dep_paste,
    dep_xml_rs,
  ],
  rust_args : rust_global_args,
  rust_abi : 'rust',
  native : true,
)

_libkraid_proc_macros_rs = rust.proc_macro(
  'kraid_proc_macros',
  files('proc/macros.rs'),
  dependencies : [
    idep_compiler_proc_rs,
    dep_syn,
  ],
  link_with : [
    _libkraid_proc_rs
  ],
  rust_args : rust_global_args,
)

libpanfrost_kraid = static_library(
  'kraid',
  files('lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'c',
  rust_args : [
    panfrost_rust_args,
    '--cfg', 'kraid_isa_xml_path="' + meson.current_source_dir() + '"',
    # Disable dead code warnings for now
    '-A', 'dead_code',
    # Otherwise, rustc trips up on -pthread
    '-Clink-arg=-Wno-unused-command-line-argument',
  ],
  dependencies : [
    dep_bitflags,
    dep_paste,
    dep_rustc_hash,
    idep_compiler_rs,
  ],
  link_with : [
    _libkraid_bindings_rs,
    _libkraid_proc_macros_rs,
  ],
)

if with_tools.contains('panfrost')
  executable(
    'isa-gen',
    files('tools/isa-gen.rs'),
    link_with : [
      _libkraid_proc_rs,
    ],
    native : true,
  )
endif

# TODO: Linking Rust executables (such as unit tests) doesn't play nicely
# with the sanitizers because meson doesn't know to pass -fsanitize to the
# Rust linker.  See also https://github.com/mesonbuild/meson/issues/11741
if with_tests and get_option('b_sanitize') == 'none'
  subdir('hw_runner')

  rust.test(
    'kraid_test',
    libpanfrost_kraid,
    args : [
      # Don't run HW tests by default
      '--skip', 'hw_tests::',
    ],
    suite : ['panfrost'],
    rust_args: [
      # This is needed to ensure we link against glibc
      # See also https://gitlab.freedesktop.org/mesa/mesa/-/issues/11632
      '-C', 'default-linker-libraries',
      # Meson adds --test but no --cfg test, do it manually to help rust-analyzer
      '--cfg', 'test',
    ],
    dependencies : [idep_acorn_rs, idep_kraid_hw_runner_rs],
  )
endif
