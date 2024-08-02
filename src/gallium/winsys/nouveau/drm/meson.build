# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

files_libnouveauwinsys = files(
  'nouveau.h',
  'nouveau.c',
  'nouveau_drm_public.h',
  'nouveau_drm_winsys.c',
)

libnouveauwinsys = static_library(
  'nouveauwinsys',
  files_libnouveauwinsys,
  include_directories : [
    inc_src,
    inc_include,
    inc_nouveau_drm,
    inc_gallium,
    inc_gallium_aux,
    inc_gallium_drivers,
  ],
  c_args: [
    cc.get_supported_arguments('-Wno-gnu-variable-sized-type-not-at-end')
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_libdrm, idep_mesautil, idep_nvidia_headers],
)

idep_libnouveauwinsys = declare_dependency(
  include_directories : [
    include_directories('.'),
    inc_nouveau_drm,
  ],
  link_with : [libnouveauwinsys],
)
