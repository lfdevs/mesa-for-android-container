# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_shared_glapi = files(
  '../entry.c',
  '../u_current.c',
  '../u_current.h',
  'glapi.c',
  'stub.c',
  'stub.h',
  'table.c',
  'table.h',
)

shared_glapi_mapi_tmp_h = custom_target(
  'shared_glapi_mapi_tmp.h',
  input : [mapi_abi_py, gl_and_es_api_files],
  output : 'glapi_mapi_tmp.h',
  command : [prog_python, '@INPUT0@', '--printer', 'shared-glapi', '@INPUT1@'],
  depend_files : api_xml_files,
  capture : true,
)

_glapi_c_args = []
if with_platform_windows
  _glapi_c_args += ['-D_GLAPI_DLL_EXPORTS']
endif

libglapi = static_library(
  'glapi',
  [files_shared_glapi, shared_glapi_mapi_tmp_h],
  c_args : [
    _glapi_c_args,
    c_msvc_compat_args,
    '-DMAPI_MODE_GLAPI',
    '-DMAPI_ABI_HEADER="@0@"'.format(shared_glapi_mapi_tmp_h.full_path()),
    gcc_lto_quirk,
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_gc_sections],
  include_directories : [inc_src, inc_include, inc_mapi],
  dependencies : [dep_thread, idep_mesautil],
  install : false,
)
libglapi_build_dir = meson.current_build_dir()
