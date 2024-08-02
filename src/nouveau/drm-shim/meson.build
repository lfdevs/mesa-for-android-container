# Copyright © 2021 Ilia Mirkin
# SPDX-License-Identifier: MIT

libnouveau_noop_drm_shim = shared_library(
  'nouveau_noop_drm_shim',
  'nouveau_noop.c',
  include_directories: [inc_include, inc_src],
  c_args: [ cc.get_supported_arguments('-Wno-gnu-variable-sized-type-not-at-end') ],
  dependencies: dep_drm_shim,
  gnu_symbol_visibility : 'hidden',
  install : true,
)
