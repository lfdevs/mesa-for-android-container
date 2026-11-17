# Copyright © 2023 Collabora
# SPDX-License-Identifier: MIT

libpankmod_lib_args = [no_override_init_args]
libpankmod_lib_link_with = []
libpankmod_lib_inc = []
libpankmod_lib_code = [
  'pan_kmod.c',
  'panfrost_kmod.c',
  'panthor_kmod.c',
]

if with_panfrost_vdrm
  libpankmod_lib_inc += [inc_virtio_gpu, inc_virtio_vdrm, include_directories('virtio')]
  libpankmod_lib_args += '-DHAVE_PANFROST_VDRM'
  libpankmod_lib_link_with += libvdrm
endif

libpankmod_lib_files = files(libpankmod_lib_code)

if panfrost_kmds.contains('kbase')
  libpankmod_lib_files += files('kbase_kmod.c')
endif

libpankmod_lib = static_library(
  'pankmod_lib',
  [libpankmod_lib_files],
  include_directories : [inc_include, inc_src, inc_panfrost, libpankmod_lib_inc],
  c_args : libpankmod_lib_args,
  gnu_symbol_visibility : 'hidden',
  dependencies: [dep_libdrm, idep_mesautil, idep_pan_packers, libpanfrost_model_dep],
  build_by_default : false,
  link_with: libpankmod_lib_link_with
)

libpankmod_dep = declare_dependency(
  include_directories: [inc_include, libpankmod_lib_inc],
  dependencies: [dep_libdrm],
)
