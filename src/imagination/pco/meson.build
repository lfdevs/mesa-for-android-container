# Copyright © 2024 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

inc_powervr_compiler = include_directories(['.'])

libpowervr_compiler_files = files(
  'pco.c',
  'pco_binary.c',
  'pco_bool.c',
  'pco_cf.c',
  'pco_const_imms.c',
  'pco_debug.c',
  'pco_end.c',
  'pco_group_instrs.c',
  'pco_index.c',
  'pco_ir.c',
  'pco_legalize.c',
  'pco_nir.c',
  'pco_nir_compute.c',
  'pco_nir_io.c',
  'pco_nir_lower_null_descriptors.c',
  'pco_nir_pvfio.c',
  'pco_nir_sync.c',
  'pco_nir_tex.c',
  'pco_nir_vk.c',
  'pco_opt.c',
  'pco_print.c',
  'pco_ra.c',
  'pco_schedule.c',
  'pco_trans_nir.c',
  'pco_validate.c',
)

pco_pygen_dep_files = files('pco_pygen_common.py', 'pco_isa.py', 'pco_ops.py', 'pco_map.py')

pco_builder_ops_h = custom_target(
  'pco_builder_ops.h',
  input : ['pco_builder_ops.h.py'],
  output : 'pco_builder_ops.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : pco_pygen_dep_files,
)

pco_common_h = custom_target(
  'pco_common.h',
  input : ['pco_common.h.py'],
  output : 'pco_common.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : pco_pygen_dep_files,
)

pco_info_c = custom_target(
  'pco_info.c',
  input : ['pco_info.c.py'],
  output : 'pco_info.c',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : pco_pygen_dep_files,
)

pco_isa_h = custom_target(
  'pco_isa.h',
  input : ['pco_isa.h.py'],
  output : 'pco_isa.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : pco_pygen_dep_files,
)

pco_map_h = custom_target(
  'pco_map.h',
  input : ['pco_map.h.py'],
  output : 'pco_map.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : pco_pygen_dep_files,
)

pco_ops_h = custom_target(
  'pco_ops.h',
  input : ['pco_ops.h.py'],
  output : 'pco_ops.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
  depend_files : pco_pygen_dep_files,
)

pco_pygen_sources = [
  pco_builder_ops_h,
  pco_common_h,
  pco_info_c,
  pco_isa_h,
  pco_map_h,
  pco_ops_h,
]

idep_pco_pygen = declare_dependency(
  sources : pco_pygen_sources,
  include_directories : inc_powervr_compiler,
)

pco_pygen_c_files = [pco_info_c]

subdir('usclib')

pco_nir_algebraic_c = custom_target(
  'pco_nir_algebraic.c',
  input : 'pco_nir_algebraic.py',
  output : 'pco_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@', '-p', dir_compiler_nir,
  ],
  capture : true,
  depend_files : nir_algebraic_depends,
)

libpowervr_compiler = static_library(
  'powervr_compiler',
  [libpowervr_compiler_files, pco_pygen_c_files, pco_nir_algebraic_c],
  include_directories : [
    inc_imagination,
    inc_powervr_compiler,
    inc_include,
    inc_compiler,
    inc_src,
  ],
  # Suppress 'parameter passing for argument of type ... changed in GCC ...' warnings.
  c_args : [imagination_c_args, no_override_init_args, '-Wno-psabi'],
  dependencies : [dep_csbgen, idep_mesautil, idep_nir, idep_pco_pygen, idep_pco_usclib],
  gnu_symbol_visibility : 'hidden',
  install : false,
)

subdir('uscgen')
