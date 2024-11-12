# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

osmesa_link_with = []
osmesa_link_args = []
osmesa_link_deps = []

if with_shared_glapi
  osmesa_link_with += libglapi
endif
if with_ld_version_script
  osmesa_link_args += [
    '-Wl,--version-script', join_paths(meson.current_source_dir(), 'osmesa.sym')
  ]
  osmesa_link_deps += files('osmesa.sym')
endif

osmesa_def = custom_target(
  'osmesa.def',
  input: 'osmesa.def.in',
  output : 'osmesa.def',
  command : gen_vs_module_defs_normal_command,
)

libosmesa = shared_library(
  osmesa_lib_name,
  'osmesa_target.c',
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_gc_sections, osmesa_link_args],
  vs_module_defs : osmesa_def,
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_winsys,
    inc_gallium_drivers,
  ],
  link_depends : osmesa_link_deps,
  link_whole : [libosmesa_st, libglapi_static],
  link_with : [
    libmesa, libgallium, libws_null, osmesa_link_with,
  ],
  dependencies : [
    dep_ws2_32, dep_thread, dep_clock, dep_unwind, driver_swrast, idep_mesautil,
  ],
  name_prefix : host_machine.system() == 'windows' ? '' : [],  # otherwise mingw will create libosmesa.dll
  soversion : host_machine.system() == 'windows' ? '' : '8',
  version : '8.0.0',
  darwin_versions : '9.0.0',
  install : true,
)

if with_symbols_check
  test('osmesa-symbols-check',
    symbols_check,
    args : [
      '--lib', libosmesa,
      '--symbols-file', files('osmesa-symbols.txt'),
      symbols_check_args,
    ],
    suite : ['gallium'],
  )
endif

pkg.generate(
  name : 'osmesa',
  description : 'Mesa Off-screen Rendering Library',
  version : '8.0.0',
  libraries : libosmesa,
  libraries_private : gl_priv_libs,
)

if with_tests
  test('osmesa-render',
    executable(
      'osmesa-render',
      'test-render.cpp',
      include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
      link_with: libosmesa,
      dependencies : [idep_gtest, idep_mesautil],
    ),
    suite: 'gallium',
    protocol : 'gtest',
  )
endif
