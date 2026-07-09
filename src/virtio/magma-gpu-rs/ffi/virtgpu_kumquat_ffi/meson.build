# Copyright © 2026 Google
# SPDX-License-Identifier: MIT

inc_virtgpu_kumquat_ffi = include_directories('include')

dep_rust_log = dependency('rust-log',
  version: '>= 0.4.22',
  fallback: ['log-0.4-rs', 'dep_log'],
  required: true,
)

dep_rust_libc = dependency('rust-libc',
  version: '>= 0.2.168',
  fallback: ['libc-0.2-rs', 'dep_libc'],
  required: true,
)

virtgpu_kumquat_ffi_args = [
  # we want unsafe blocks inside unsafe functions
  '-Dunsafe_op_in_unsafe_fn',
]

libvirtgpu_kumquat_ffi = static_library(
  'virtgpu_kumquat_ffi',
  'lib.rs',
  gnu_symbol_visibility : 'hidden',
  rust_abi : 'c',
  rust_args : [
    rust_global_args,
    virtgpu_kumquat_ffi_args,
  ],
  link_with: [libmagma_gpu_rs],
  dependencies: [dep_magma_gpu_rs, dep_rust_log, dep_rust_libc]
)

dep_virtgpu_kumquat_ffi = declare_dependency(
  include_directories : [inc_virtgpu_kumquat_ffi],
  link_with : [libvirtgpu_kumquat_ffi]
)
