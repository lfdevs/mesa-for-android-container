# Copyright © 2019 Intel Corporation
# SPDX-License-Identifier: MIT

vklayer_intel_nullhw_files = files(
  'intel_nullhw.c',
)

vklayer_intel_nullhw = shared_library(
  'VkLayer_INTEL_nullhw',
  vklayer_intel_nullhw_files,
  c_args : [no_override_init_args],
  dependencies : [idep_vulkan_util, idep_mesautil, vulkan_wsi_deps, dep_dl],
  include_directories : [inc_include, inc_src],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install : true
)

install_data(
  files('VkLayer_INTEL_nullhw.json'),
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'explicit_layer.d'),
  install_tag : 'runtime',
)
