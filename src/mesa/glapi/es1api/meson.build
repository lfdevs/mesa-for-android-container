# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

es1_glapi_mapi_tmp_h = custom_target(
  'es1_glapi_mapi_tmp.h',
  input : ['../new/gen_gldispatch_mapi.py', '../glapi/registry/gl.xml'],
  output : 'es1_glapi_mapi_tmp.h',
  command : [prog_python, '@INPUT0@', 'glesv1', '@INPUT1@'],
  depend_files : glapi_xml_py_deps,
  capture : true,
)

_es1_c_args = []
if with_platform_windows
  _es1_c_args += ['-D_GDI32_']
endif

gles1_def = custom_target(
  'gles1.def',
  input: 'gles1.def.in',
  output : 'gles1.def',
  command : gen_vs_module_defs_normal_command,
)

libglesv1_cm = shared_library(
  'GLESv1_CM' + get_option('gles-lib-suffix'),
  ['libgles1_public.c', es1_glapi_mapi_tmp_h],
  vs_module_defs : gles1_def,
  c_args : [
    c_msvc_compat_args,
    _es1_c_args,
    gcc_lto_quirk,
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_gc_sections],
  include_directories : [inc_mesa, inc_src, inc_include],
  link_with : shared_glapi_lib,
  dependencies : [dep_thread, dep_libdrm, dep_m, dep_dl, idep_mesautilc11],
  soversion : host_machine.system() == 'windows' ? '' : '1',
  version : '1.1.0',
  darwin_versions  : '3.0.0',
  name_prefix : host_machine.system() == 'windows' ? 'lib' : [],  # always use lib, but avoid warnings on !windows
  install : true,
)

pkg.generate(
  name : 'glesv1_cm',
  filebase : 'glesv1_cm',
  description : 'Mesa OpenGL ES 1.1 CM library',
  version : meson.project_version(),
  libraries : libglesv1_cm,
  libraries_private : gl_priv_libs,
)

if with_symbols_check
  test(
    'es1-ABI-check',
    symbols_check,
    args : [
      '--lib', libglesv1_cm,
      '--symbols-file', files('gles1-symbols.txt'),
      symbols_check_args,
    ],
    suite : ['glapi'],
  )
endif
