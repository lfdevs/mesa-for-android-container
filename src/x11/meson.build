# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_loader_x11 = include_directories('.')

if with_platform_x11 and with_dri_platform == 'drm'
  libloader_x11 = static_library(
    'loader_x11',
    'loader_x11.c',
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
  libloader_x11 = []
endif
