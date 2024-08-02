# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

libradeonwinsys = static_library(
  'radeonwinsys',
  files('radeon_drm_bo.c', 'radeon_drm_bo.h', 'radeon_drm_cs.c',
        'radeon_drm_cs.h', 'radeon_drm_surface.c',
        'radeon_drm_winsys.c', 'radeon_drm_winsys.h'),
  include_directories : [inc_src, inc_include, inc_gallium, inc_gallium_aux],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_libdrm_radeon, idep_mesautil],
)
