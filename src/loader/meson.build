# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_loader = include_directories('.')

if with_platform_wayland
  wp_protos = {
    'fifo-v1': mod_wl.find_protocol('fifo', state : 'staging', version : 1),
    'commit-timing-v1': mod_wl.find_protocol('commit-timing', state : 'staging', version : 1),
    'linux-dmabuf-unstable-v1': mod_wl.find_protocol('linux-dmabuf', state : 'unstable', version : 1),
    'presentation-time': mod_wl.find_protocol('presentation-time'),
    'tearing-control-v1': mod_wl.find_protocol('tearing-control', state : 'staging', version : 1),
    'linux-drm-syncobj-v1': mod_wl.find_protocol('linux-drm-syncobj', state : 'staging', version : 1),
    'color-management-v1': mod_wl.find_protocol('color-management', state : 'staging', version : 1)
  }
  wp_files = {}
  foreach name, xml : wp_protos
    wp_files += {name : mod_wl.scan_xml(xml, include_core_only : false)}
  endforeach

  libloader_wayland_helper = static_library(
    'loader_wayland_helper',
    'loader_wayland_helper.c',
    wp_files['presentation-time'],
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
