# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_wayland_drm = include_directories('.')

_wl_drm = mod_wl.scan_xml('wayland-drm.xml', server : true, include_core_only : false)
wayland_drm_protocol_c = _wl_drm[0]
wayland_drm_client_protocol_h = _wl_drm[1]
wayland_drm_server_protocol_h = _wl_drm[2]

libwayland_drm = static_library(
  'wayland_drm',
  ['wayland-drm.c', wayland_drm_protocol_c, wayland_drm_server_protocol_h],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_wayland_server],
  include_directories : inc_include,
  build_by_default : false,
)
