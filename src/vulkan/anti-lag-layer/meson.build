# Copyright © 2025 Valve Corporation
# SPDX-License-Identifier: MIT

vklayer_files = files(
  'anti_lag_layer.c',
  'anti_lag_layer_interface.c',
)

vklayer_mesa_anti_lag = shared_library(
  'VkLayer_MESA_anti_lag',
  vklayer_files,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_vulkan_util, idep_mesautil,
  ],
  include_directories : [inc_include, inc_util, inc_src],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install : true
)

configure_file(
  configuration : {'library_path' : 'libVkLayer_MESA_anti_lag.so'},
  input : 'VkLayer_MESA_anti_lag.json.in',
  output : '@BASENAME@',
  install : true,
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
  install_tag : 'runtime',
)

configure_file(
  configuration : {'library_path' : vklayer_mesa_anti_lag.full_path()},
  input : 'VkLayer_MESA_anti_lag.json.in',
  output : '@PLAINNAME@_dev.json',
)
devenv.append('VK_ADD_IMPLICIT_LAYER_PATH', meson.current_build_dir())
