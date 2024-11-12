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

libglapi = shared_library(
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
  soversion : host_machine.system() == 'windows' ? '' : '0',
  version : '0.0.0',
  name_prefix : host_machine.system() == 'windows' ? 'lib' : [],  # always use lib, but avoid warnings on !windows
  install : true,
)
libglapi_build_dir = meson.current_build_dir()

if with_any_opengl and with_tests
  test(
    'shared-glapi-test',
    executable(
      ['shared-glapi-test', glapitable_h],
      'tests/check_table.cpp',
      cpp_args : [cpp_msvc_compat_args],
      include_directories : [inc_src, inc_include, inc_mapi],
      link_with : [libglapi],
      dependencies : [dep_thread, idep_gtest, idep_mesautilc11],
    ),
    suite : ['mapi'],
    protocol : 'gtest',
  )
  if with_symbols_check
    test(
      'shared-glapi symbols check',
      symbols_check,
      args : [
        '--lib', libglapi,
        '--symbols-file', files('glapi-symbols.txt'),
        symbols_check_args,
      ],
      suite : ['mapi'],
    )
  endif
endif
