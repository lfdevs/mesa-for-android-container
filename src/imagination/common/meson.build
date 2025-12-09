# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

pvr_device_info_dir = join_paths(meson.current_source_dir(), 'device_info')

libpowervr_common = static_library(
  'powervr_common',
  [
    'pvr_debug.c',
    'pvr_device_info.c',
    'pvr_dump.c',
    'pvr_dump_info.c',
    'pvr_util.c',
    sha1_h,
  ],
  include_directories : [
    inc_include,
    inc_src,
    inc_imagination,
  ],
  c_args : [imagination_c_args, no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)
