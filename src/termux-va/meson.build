# SPDX-License-Identifier: MIT

libtva_drm_shim_wayland = shared_library(
  'tva_drm_shim_wayland',
  'tva_drm_shim_wayland.c',
  dependencies : [dep_libdrm, dep_dl],
  gnu_symbol_visibility : 'default',
  install : true,
  install_tag : 'runtime',
)
