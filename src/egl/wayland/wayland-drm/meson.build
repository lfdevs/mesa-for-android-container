# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_wayland_drm = include_directories('.')

wayland_drm_protocol_c = custom_target(
  'wayland-drm-protocol.c',
  input : 'wayland-drm.xml',
  output : 'wayland-drm-protocol.c',
  command : [prog_wl_scanner, wl_scanner_arg, '@INPUT@', '@OUTPUT@'],
)

wayland_drm_client_protocol_h = custom_target(
  'wayland-drm-client-protocol.h',
  input : 'wayland-drm.xml',
  output : 'wayland-drm-client-protocol.h',
  command : [prog_wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

wayland_drm_server_protocol_h = custom_target(
  'wayland-drm-server-protocol.h',
  input : 'wayland-drm.xml',
  output : 'wayland-drm-server-protocol.h',
  command : [prog_wl_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

libwayland_drm = static_library(
  'wayland_drm',
  ['wayland-drm.c', wayland_drm_protocol_c, wayland_drm_server_protocol_h],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_wayland_server],
  build_by_default : false,
)

# linux-dmabuf isn't part of wayland-drm, but this happens to be the only
# place which is a) guaranteed to be built when building either or both
# of EGL and Vulkan WSI, and b) guaranteed to be included before both,
# so the targets are actually defined by the time the files come to be
# used.
#
# Moving these to a src/wsi/ was suggested but shot down, so put these
# here for now as the maybe-least-bad solution.
wp_dir = dep_wl_protocols.get_variable(pkgconfig : 'pkgdatadir', internal : 'pkgdatadir')
wp_protos = {
  'fifo-v1': 'staging/fifo/fifo-v1.xml',
  'commit-timing-v1': 'staging/commit-timing/commit-timing-v1.xml',
  'linux-dmabuf-unstable-v1': 'unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml',
  'presentation-time': 'stable/presentation-time/presentation-time.xml',
  'tearing-control-v1': 'staging/tearing-control/tearing-control-v1.xml',
  'linux-drm-syncobj-v1': 'staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml',
}
wp_files = {}
foreach name, xml : wp_protos
  code = custom_target(
    name + '-protocol.c',
    input : files(join_paths(wp_dir, xml)),
    output : name + '-protocol.c',
    command : [prog_wl_scanner, wl_scanner_arg, '@INPUT@', '@OUTPUT@'],
  )
  header = custom_target(
    name + '-client-protocol.h',
    input : files(join_paths(wp_dir, xml)),
    output : name + '-client-protocol.h',
    command : [prog_wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
  wp_files += { name: [code, header] }
endforeach
