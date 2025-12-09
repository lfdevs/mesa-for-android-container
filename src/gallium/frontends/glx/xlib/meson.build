# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libxlib = static_library(
  'xlib',
  files('glx_api.c', 'glx_getproc.c', 'glx_usefont.c', 'xm_api.c', 'xm_st.c'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_mesa],
  dependencies : [dep_x11, dep_xext, dep_xcb, dep_glproto, idep_mesautil, idep_dispatch_h],
)
