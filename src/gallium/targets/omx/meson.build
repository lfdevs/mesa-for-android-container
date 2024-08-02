# Copyright © 2017-2018 Intel Corporation
# SPDX-License-Identifier: MIT

# TODO: support non-static targets
# Static targets are always enabled in autotools (unless you modify
# configure.ac)

omx_link_args = []
omx_link_depends = []

omx_sym = configure_file(input : 'omx.sym.in', output : 'omx.sym', configuration : sym_config)

if with_ld_version_script
  omx_link_args += ['-Wl,--version-script', join_paths(meson.current_build_dir(), 'omx.sym')]
  omx_link_depends += omx_sym
endif

libomx_gallium = shared_library(
  'omx_mesa',
  'omx_target.c',
  gnu_symbol_visibility : 'hidden',
  link_args : [omx_link_args, ld_args_gc_sections],
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_util, inc_gallium_winsys, inc_gallium_drivers,
  ],
  link_whole : [libomx_st],
  link_with : [
    libgalliumvlwinsys, libgalliumvl, libgallium,
    libpipe_loader_static, libws_null, libwsw, libswdri, libswkmsdri,
  ],
  link_depends : omx_link_depends,
  dependencies : [idep_mesautil, driver_r600, driver_radeonsi, driver_nouveau],
  install : true,
  install_dir : omx_drivers_path,
)
