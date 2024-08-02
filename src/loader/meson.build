# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_loader = include_directories('.')

if with_platform_x11 and with_dri3
  libloader_dri3_helper = static_library(
    'loader_dri3_helper',
    'loader_dri3_helper.c',
    gnu_symbol_visibility : 'hidden',
    include_directories : [inc_include, inc_src, inc_gallium],
    dependencies : [
      idep_mesautil,
      dep_libdrm, dep_xcb_dri3, dep_xcb_present, dep_xcb_sync, dep_xshmfence,
      dep_xcb_xfixes,
    ],
    build_by_default : false,
  )
else
  libloader_dri3_helper = []
endif

if with_platform_wayland
  libloader_wayland_helper = static_library(
    'loader_wayland_helper',
    'loader_wayland_helper.c',
    gnu_symbol_visibility : 'hidden',
    include_directories : [inc_include, inc_src, inc_gallium],
    dependencies : [
      idep_mesautil,
      dep_wayland_client,
    ],
    build_by_default : false,
  )
else
  libloader_wayland_helper = []
endif

loader_c_args = [
  '-DUSE_DRICONF',
  '-DDEFAULT_DRIVER_DIR="@0@"'.format(dri_search_path),
]
libloader = static_library(
  'loader',
  ['loader_dri_helper.c', 'loader.c', sha1_h],
  c_args : loader_c_args,
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_util, inc_gallium],
  dependencies : [idep_mesautil, dep_libdrm, dep_thread, dep_xcb, dep_xcb_xrandr],
  build_by_default : false,
)
