# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

libradeonwinsys_deps = [idep_mesautil, dep_libdrm]
libradeonwinsys_c_args = []

if with_gallium_radeonsi
  libradeonwinsys_deps += [idep_amdgfxregs_h]
  libradeonwinsys_c_args = ['-DHAVE_GALLIUM_RADEONSI']
endif

libradeonwinsys = static_library(
  'radeonwinsys',
  files('radeon_drm_bo.c',
        'radeon_drm_bo.h',
        'radeon_drm_cs.c',
        'radeon_drm_cs.h',
        'radeon_drm_surface.c',
        'radeon_drm_winsys.c',
        'radeon_drm_winsys.h',
        'radeon_surface.c',
        'radeon_surface.h'),
  include_directories : [inc_src, inc_include, inc_gallium, inc_gallium_aux],
  gnu_symbol_visibility : 'hidden',
  c_args : libradeonwinsys_c_args,
  dependencies : libradeonwinsys_deps,
)
