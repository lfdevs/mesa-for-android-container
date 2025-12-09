# Copyright © 2025 Google
# SPDX-License-Identifier: MIT

project(
  'windows-sys',
  'rust',
  version : '0.61.1',
  license : 'MIT OR Apache-2.0',
)

windows_link = subproject('windows-link-0.2-rs').get_variable('lib')
windows_sys_args = [
  '--cfg', 'feature="Win32"',
  '--cfg', 'feature="Win32_Networking"',
  '--cfg', 'feature="Win32_Networking_WinSock"',
  '--cfg', 'feature="Win32_Foundation"',
  '--cfg', 'feature="Win32_Foundation_WinSock"',
  '--cfg', 'feature="Win32_System"',
  '--cfg', 'feature="Win32_System_Diagnostics"',
  '--cfg', 'feature="Win32_System_Diagnostics_Debug"',
]

lib = static_library(
  'windows_sys',
  'src/lib.rs',
  override_options : ['rust_std=2021', 'build.rust_std=2021'],
  link_with: [windows_link],
  rust_abi: 'rust',
  rust_args: windows_sys_args,
)

dep_windows_sys = declare_dependency(
  link_with: [lib],
)
