# Copyright © 2022 Collabora Ltd
# SPDX-License-Identifier: MIT
prog_cbindgen = find_program(
  'cbindgen',
  required : true,
  native : true,
  version : '>= 0.25'
)

dep_paste = dependency('paste',
  version : '>= 1.0.14',
  fallback : ['paste-1-rs', 'dep_paste'],
  required : true,
)

_nil_format_table = custom_target(
  'nil_format_table',
  input : files('nil_format_table_gen.py', 'nil_formats.csv'),
  output : ['nil_format_table.h', 'nil_format_table.c'],
  depends: u_format_gen_h,
  command : [
    prog_python, '@INPUT0@', '--csv', '@INPUT1@',
    '--out-h', '@OUTPUT0@', '--out-c', '@OUTPUT1@',
  ],
)

_libnil_format_table = static_library(
  'libnil_format_table',
  _nil_format_table,
  include_directories : [inc_include, inc_src],
  dependencies : [idep_nvidia_headers, idep_mesautil],
  gnu_symbol_visibility: 'hidden',
)

_libnil_deps = [
  dep_paste,
  idep_bitview_rs,
  idep_nvidia_headers_rs,
]

_nil_bindings_rs = rust.bindgen(
  input: ['nil_bindings.h', _nil_format_table],
  output: 'nil_bindings.rs',
  c_args: [
    pre_args,
  ],
  include_directories : [inc_include, inc_src, include_directories('.')],
  args: [
    bindgen_output_args,
    '--allowlist-function', 'util_format_description',
    '--allowlist-function', 'util_format_get_blocksize',
    '--allowlist-function', 'util_format_is_compressed',
    '--allowlist-function', 'util_format_is_pure_integer',
    '--allowlist-function', 'util_format_is_srgb',
    '--allowlist-function', 'drm_format_mod_block_linear_2D',
    '--allowlist-function', 'drm_mod_is_nvidia',
    '--allowlist-type', 'nil_format_support_flags',
    '--allowlist-type', 'nv_device_info',
    '--allowlist-type', 'nv_device_type',
    '--allowlist-type', 'nv_zcull_device_info',
    '--allowlist-type', 'pipe_format',
    '--allowlist-type', 'pipe_swizzle',
    '--allowlist-var', 'nil_format_table',
    '--allowlist-var', 'drm_format_mod_invalid',
    '--allowlist-var', 'drm_format_mod_linear',
    '--no-prepend-enum-name',
  ],
  dependencies: _libnil_deps,
)

_libnil_rs_bindings = static_library(
  'nil_rs_bindings',
  _nil_bindings_rs,
  gnu_symbol_visibility: 'hidden',
  rust_abi: 'rust',
)

_libnil = static_library(
  'nil',
  files('lib.rs'),
  gnu_symbol_visibility: 'hidden',
  rust_abi: 'c',
  rust_args: nouveau_rust_args,
  link_with: [_libnil_format_table, _libnil_rs_bindings],
  dependencies: _libnil_deps,
)

# TODO: Linking Rust executables (such as unit tests) doesn't play nicely
# with the sanitizers because meson doesn't know to pass -fsanitize to the
# Rust linker.  See also https://github.com/mesonbuild/meson/issues/11741
if with_tests and get_option('b_sanitize') == 'none'
  rust.test(
    'nil',
    _libnil,
    suite : ['nouveau'],
    # This is needed to ensure we link against glibc.
    # The --no-as-needed/-lm/--as-needed workaround forces the linker to
    # keep libm for sqrtf in libmesa_util.a. This is needed because rustc
    # places default libraries (-lm) before static archives in the link
    # command, and Coverity's cov-build strips rustc's -fuse-ld=lld,
    # falling back to ld.bfd which has strict left-to-right link order.
    # Remove this workaround once rustc resolves link ordering of
    # native libraries (https://github.com/rust-lang/rust/issues/154975).
    rust_args: [
      '-C', 'default-linker-libraries',
      '-C', 'link-arg=-Wl,--no-as-needed',
      '-C', 'link-arg=-lm',
      '-C', 'link-arg=-Wl,--as-needed',
    ],
  )
endif

_nil_h = custom_target(
  'nil_h',
  input : [files('cbindgen.toml'), 'lib.rs'],
  output : ['nil.h'],
  command : [
    prog_cbindgen, '-q', '--config', '@INPUT0@', '--lang', 'c',
    '--output', '@OUTPUT0@', '--depfile', '@DEPFILE@',
    '--', '@INPUT1@',
  ],
  depfile : 'nil.h.d',
)

idep_nil_h = declare_dependency(
  include_directories : include_directories('.'),
  sources : [_nil_h],
)

idep_nil = declare_dependency(
  include_directories : include_directories('.'),
  link_with : _libnil,
  sources : [_nil_h],
)
