# Copyright © 2025 Collabora, Ltd.
# SPDX-License-Identifier: MIT

libpanfrost_compiler_files = files(
  'pan_compiler.c',
  'pan_compiler.h',
  'pan_nir_collect_varyings.c',
  'pan_nir_fuse_io_cvt.c',
  'pan_nir_lower_bool_to_bitsize.c',
  'pan_nir_lower_var_special.c',
  'pan_nir_lower_framebuffer.c',
  'pan_nir_lower_fs_inputs.c',
  'pan_nir_lower_fs_outputs.c',
  'pan_nir_lower_helper_invocation.c',
  'pan_nir_lower_image.c',
  'pan_nir_lower_image_ms.c',
  'pan_nir_lower_mem_to_global.c',
  'pan_nir_lower_noperspective.c',
  'pan_nir_lower_sample_position.c',
  'pan_nir_lower_tex.c',
  'pan_nir_lower_vertex_id.c',
  'pan_nir_lower_vs_outputs.c',
  'pan_nir_lower_xfb.c',
  'pan_nir_resize_varying_io.c',
)

libpanfrost_compiler_c_args = [no_override_init_args, soong_compat_c_args]
if with_panfrost_rust
  libpanfrost_compiler_c_args += ['-DWITH_PANFROST_RUST']
endif

if with_panfrost_rust
  subdir('kraid')
endif
subdir('bifrost')
subdir('midgard')

libpanfrost_compiler_link_with = [
  libpanfrost_midgard,
  libpanfrost_bifrost,
]
if with_panfrost_rust
  libpanfrost_compiler_link_with += [libpanfrost_kraid]
endif

libpanfrost_compiler = static_library(
  'panfrost_compiler',
  [libpanfrost_compiler_files],
  include_directories : [inc_include, inc_src, inc_valhall],
  dependencies: [idep_nir, idep_mesautil, idep_valhall_enums_h, idep_bi_opcodes_h],
  link_with: libpanfrost_compiler_link_with,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

if with_tests
  subdir('bifrost/test')
  subdir('bifrost/valhall/test')
endif
