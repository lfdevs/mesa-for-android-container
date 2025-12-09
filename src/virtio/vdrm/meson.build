# Copyright 2023 Google LLC
# SPDX-License-Identifier: MIT

inc_virtio_vdrm = include_directories('.')

libvdrm_c_args = [no_override_init_args]
if with_amdgpu_virtio
  libvdrm_c_args += ['-DENABLE_DRM_AMDGPU']
endif
libvdrm = static_library(
  'libvdrm',
  [
    'vdrm.c',
    'vdrm.h',
    'vdrm_virtgpu.c',
    'vdrm_vpipe.c',
  ],
  include_directories : [
    inc_include,
    inc_src,
    inc_virtio,
    inc_virtio_gpu,
  ],
  c_args : libvdrm_c_args,
  gnu_symbol_visibility: 'hidden',
  dependencies: [
    dep_libdrm,
    dep_valgrind,
    idep_mesautil,
  ],
  link_with: [
    _libmesa_util,
  ],
  build_by_default: false,
)
