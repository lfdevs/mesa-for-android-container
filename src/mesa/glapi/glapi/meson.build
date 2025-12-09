# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

subdir('gen')

inc_glapi = include_directories('.')

bridge_glapi_files = files(
  'libgl_public.c',
  '../entry_x86-64_tls.h',
  '../entry_x86_tls.h',
  '../entry_ppc64le_tls.h',
)
bridge_glapi_files += glapi_mapi_tmp_h

if with_dri and ['apple', 'windows'].contains(with_dri_platform)
  bridge_glapi_files += [glapi_gentable_c, main_dispatch_h]
endif

bridge_glapi_args = [gcc_lto_quirk]
if with_platform_windows
  bridge_glapi_args += ['-D_GDI32_']
endif

libglapi_bridge = static_library(
  'glapi_bridge',
  bridge_glapi_files,
  include_directories : [inc_mesa, inc_include, inc_src, inc_gallium],
  c_args : [c_msvc_compat_args, bridge_glapi_args],
  dependencies : [dep_thread, idep_mesautil],
  build_by_default : false,
)
