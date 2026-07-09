# Copyright © 2026 Collabora, Ltd.
# SPDX-License-Identifier: MIT


_hw_runner_bindings_rs = rust.bindgen(
  input : 'hw_runner_bindings.h',
  output : 'kraid_hw_runner_bindings.rs',
  output_inline_wrapper : 'hw_runner_bindings.c',
  include_directories : [inc_include, inc_src, inc_panfrost],
  c_args : [pre_args],
  args : [
    bindgen_output_args,
    compiler_rs_bindgen_blocklist,
    '--raw-line', 'use compiler::bindings::*;',
    '--allowlist-type', 'pan_kmod_.*',
    '--allowlist-type', 'pandecode_.*',
    '--allowlist-type', 'drm.*',
    '--allowlist-type', '.*',
    '--allowlist-function', 'pan_arch',
    '--allowlist-function', 'munmap',
    '--allowlist-function', 'pan_kmod_.*',
    '--allowlist-function', 'pandecode_.*',
    '--allowlist-function', 'hw_runner_.*',
    '--allowlist-function', 'drm.*',
    '--allowlist-var', 'DRM_.*',
    '--allowlist-var', 'ETIME',
    '--allowlist-var', 'PROT_.*',
    '--allowlist-var', 'MAP_SHARED',
    '--no-prepend-enum-name',
    '--with-derive-default',
    '--wrap-static-fns-suffix', '_kraid_hw_runner_extern'
  ],
  dependencies : [
    dep_libdrm,
    idep_mesautil,
  ],
)

_libkraid_hw_runner_bindings_rs = static_library(
  'kraid_hw_runner_bindings',
  _hw_runner_bindings_rs[0],
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  dependencies : [idep_compiler_rs],
)

_hw_runner_per_arch_libs = []
foreach ver : ['10', '12', '13']
  _hw_runner_per_arch_libs += static_library(
    'kraid_hw_runner_c_v' + ver,
    files('hw_runner_vX.c'),
    include_directories : [inc_include, inc_src, inc_panfrost],
    c_args : [
      no_override_init_args,
      cc.get_supported_arguments('-Wno-missing-prototypes'),
      '-DPAN_ARCH=' + ver,
    ],
    gnu_symbol_visibility : 'hidden',
    dependencies : [dep_libdrm, idep_mesautil, idep_pan_packers],
    build_by_default : false,
  )
endforeach

_libkraid_hw_runner_c = static_library(
  'kraid_hw_runner_c',
  files('hw_runner.c') + _hw_runner_bindings_rs[1],
  include_directories : [
    inc_include, inc_src, inc_panfrost,
    fs.relative_to(meson.project_build_root(), meson.current_source_dir()),
  ],
  c_args : [no_override_init_args, cc.get_supported_arguments('-Wno-missing-prototypes'),],
  dependencies : [dep_libdrm, idep_mesautil, idep_pan_packers],
  link_with : [
    libpankmod_lib,
    libpanfrost_lib,
    libpanfrost_decode,
    _hw_runner_per_arch_libs,
  ],
  build_by_default : false,
)

libkraid_hw_runner_rs = static_library(
  'kraid_hw_runner',
  files('lib.rs'),
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'rust',
  rust_args : [
    panfrost_rust_args,
    # Otherwise, rustc trips up on -pthread
    '-Clink-arg=-Wno-unused-command-line-argument',
  ],
  dependencies : [
    idep_compiler_rs,
  ],
  link_with : [
    _libkraid_hw_runner_bindings_rs,
    _libkraid_hw_runner_c,
  ],
  build_by_default : false,
)

idep_kraid_hw_runner_rs = declare_dependency(
  link_with : [libkraid_hw_runner_rs],
)
