# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

shared_glapi_mapi_tmp_h = custom_target(
  'shared_glapi_mapi_tmp.h',
  input : ['../mapi_abi.py', '../glapi/gen/gl_and_es_API.xml', '../../../glx/libgl-symbols.txt'],
  output : 'shared_glapi_mapi_tmp.h',
  command : [prog_python, '@INPUT0@', '--printer', 'shared-glapi', '--gl_symbols', '@INPUT2@', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

libglapi = static_library(
  'glapi',
  ['core.c', shared_glapi_mapi_tmp_h],
  c_args : [
    c_msvc_compat_args,
    '-DMAPI_MODE_SHARED_GLAPI',
    gcc_lto_quirk,
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_gc_sections],
  include_directories : [inc_mesa, inc_src, inc_include],
  dependencies : [dep_thread, idep_mesautil],
  install : false,
)
libglapi_build_dir = meson.current_build_dir()
