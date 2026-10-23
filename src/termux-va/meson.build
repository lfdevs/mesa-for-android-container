# SPDX-License-Identifier: MIT

shim_sources = files('tva_drm_shim_wayland.c')
shim_dependencies = [dep_libdrm, dep_dl]
if with_platform_x11
  shim_sources += files('tva_dri3_shim.c')
  shim_dependencies += [dep_xcb, dep_thread]
endif

libtva_drm_shim_wayland = shared_library(
  'tva_drm_shim_wayland',
  shim_sources,
  dependencies : shim_dependencies,
  gnu_symbol_visibility : 'default',
  install : true,
  install_tag : 'runtime',
)
