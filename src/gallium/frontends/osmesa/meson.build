# Copyright © 2017-2018 Intel Corporation
# SPDX-License-Identifier: MIT

osmesa_st_c_args = []
if with_platform_windows
  if not with_shared_glapi
    osmesa_st_c_args += ['-D_GLAPI_NO_EXPORTS']
  endif
endif

libosmesa_st = static_library(
  'osmesa_st',
  'osmesa.c',
  c_args : osmesa_st_c_args,
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_mapi, inc_mesa,
  ],
  dependencies : [idep_mesautil],
)
