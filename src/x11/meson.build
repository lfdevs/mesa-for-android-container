# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_loader_x11 = include_directories('.')

loader_x11_files = files(
  'x11_display.c',
)

if with_dri_platform == 'drm'
  loader_x11_files += files('x11_dri3.c')
endif

libloader_x11 = static_library(
  'loader_x11',
  loader_x11_files,
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src],
  dependencies : [
    idep_mesautil,
    dep_libdrm, dep_xcb_dri3, dep_xcb_present, dep_xcb_sync, dep_xshmfence,
    dep_xcb_xfixes, dep_x11
  ],
  build_by_default : false,
)
