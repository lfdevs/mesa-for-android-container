# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libintel_compiler_brw_files = files(
  '../brw_list.h',
  '../intel_prim.h',
  'brw_analysis.h',
  'brw_analysis.cpp',
  'brw_analysis_def.cpp',
  'brw_analysis_liveness.cpp',
  'brw_analysis_performance.cpp',
  'brw_builder.cpp',
  'brw_builder.h',
  'brw_cfg.cpp',
  'brw_cfg.h',
  'brw_compile_bs.cpp',
  'brw_compile_cs.cpp',
  'brw_compile_fs.cpp',
  'brw_compile_gs.cpp',
  'brw_compile_mesh.cpp',
  'brw_compile_tcs.cpp',
  'brw_compile_tes.cpp',
  'brw_compile_vs.cpp',
  'brw_compiler.c',
  'brw_compiler.h',
  'brw_debug_recompile.c',
  'brw_disasm.c',
  'brw_disasm_info.cpp',
  'brw_disasm_info.h',
  'brw_eu.c',
  'brw_eu_compact.c',
  'brw_eu_defines.h',
  'brw_eu_emit.c',
  'brw_eu_inst.h',
  'brw_eu.h',
  'brw_eu_validate.c',
  'brw_from_nir.cpp',
  'brw_generator.cpp',
  'brw_generator.h',
  'brw_inst.cpp',
  'brw_inst.h',
  'brw_isa_info.h',
  'brw_load_reg.cpp',
  'brw_lower.cpp',
  'brw_lower_dpas.cpp',
  'brw_lower_fill_spill.cpp',
  'brw_lower_integer_multiplication.cpp',
  'brw_lower_logical_sends.cpp',
  'brw_lower_pack.cpp',
  'brw_lower_regioning.cpp',
  'brw_lower_scoreboard.cpp',
  'brw_lower_simd_width.cpp',
  'brw_lower_subgroup_ops.cpp',
  'brw_nir.h',
  'brw_nir.c',
  'brw_nir_analyze_ubo_ranges.c',
  'brw_nir_lower_cooperative_matrix.c',
  'brw_nir_lower_cs_intrinsics.c',
  'brw_nir_lower_alpha_to_coverage.c',
  'brw_nir_lower_fs_barycentrics.c',
  'brw_nir_lower_fs_load_output.c',
  'brw_nir_lower_immediate_offsets.c',
  'brw_nir_lower_intersection_shader.c',
  'brw_nir_lower_ray_queries.c',
  'brw_nir_lower_rt_intrinsics.c',
  'brw_nir_lower_rt_intrinsics_pre_trace.c',
  'brw_nir_lower_sample_index_in_coord.c',
  'brw_nir_lower_shader_calls.c',
  'brw_nir_lower_storage_image.c',
  'brw_nir_lower_texel_address.c',
  'brw_nir_lower_texture.c',
  'brw_nir_wa_18019110168.c',
  'brw_nir_opt_fsat.c',
  'brw_nir_rt.h',
  'brw_nir_rt.c',
  'brw_nir_rt_builder.h',
  'brw_opt.cpp',
  'brw_opt_address_reg_load.cpp',
  'brw_opt_algebraic.cpp',
  'brw_opt_bank_conflicts.cpp',
  'brw_opt_cmod_propagation.cpp',
  'brw_opt_combine_constants.cpp',
  'brw_opt_copy_propagation.cpp',
  'brw_opt_cse.cpp',
  'brw_opt_dead_code_eliminate.cpp',
  'brw_opt_fill_spill.cpp',
  'brw_opt_register_coalesce.cpp',
  'brw_opt_saturate_propagation.cpp',
  'brw_opt_txf_combiner.cpp',
  'brw_opt_virtual_grfs.cpp',
  'brw_packed_float.c',
  'brw_print.cpp',
  'brw_private.h',
  'brw_reg.cpp',
  'brw_reg.h',
  'brw_reg_allocate.cpp',
  'brw_reg_type.c',
  'brw_reg_type.h',
  'brw_rt.h',
  'brw_sampler.c',
  'brw_sampler.h',
  'brw_schedule_instructions.cpp',
  'brw_shader.cpp',
  'brw_shader.h',
  'brw_simd_selection.cpp',
  'brw_thread_payload.cpp',
  'brw_thread_payload.h',
  'brw_validate.cpp',
  'brw_vue_map.c',
  'brw_workaround.cpp',
)

brw_nir_lower_fsign = custom_target(
  'brw_nir_lower_fsign.c',
  input : 'brw_nir_lower_fsign.py',
  output : 'brw_nir_lower_fsign.c',
  command : [
    prog_python, '@INPUT@', '-p', dir_compiler_nir,
  ],
  depend_files : nir_algebraic_depends,
  capture : true,
)

brw_nir_workarounds = custom_target(
  'brw_nir_workarounds.c',
  input : 'brw_nir_workarounds.py',
  output : 'brw_nir_workarounds.c',
  command : [
    prog_python, '@INPUT@', '-p', dir_compiler_nir,
  ],
  depend_files : nir_algebraic_depends,
  capture : true,
)

libintel_compiler_brw = static_library(
  'intel_compiler',
  [libintel_compiler_brw_files, brw_nir_lower_fsign, brw_nir_workarounds, [brw_device_sha1_gen_src]],
  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
  c_args : [no_override_init_args],
  cpp_args : ['-Werror=vla'],
  gnu_symbol_visibility : 'hidden',
  link_with : libintel_compiler_nir,
  dependencies : [idep_nir_headers, idep_mesautil, idep_intel_dev, idep_vtn, idep_mda],
  build_by_default : false,
)

idep_intel_compiler_brw = declare_dependency(
  link_with : [libintel_compiler_brw, libisl],
  dependencies : [
    idep_nir,
    idep_mesautil,
    idep_vtn,
  ],
)

if with_tests
  test(
    'intel_compiler_brw_tests',
    executable(
      'intel_compiler_brw_tests',
      files(
        'test_eu_compact.cpp',
        'test_eu_validate.cpp',
        'test_helpers.cpp',
        'test_helpers.h',
        'test_insert_load_reg.cpp',
        'test_lower_scoreboard.cpp',
        'test_opt_algebraic.cpp',
        'test_opt_cmod_propagation.cpp',
        'test_opt_combine_constants.cpp',
        'test_opt_copy_propagation.cpp',
        'test_opt_cse.cpp',
        'test_opt_register_coalesce.cpp',
        'test_opt_saturate_propagation.cpp',
        'test_simd_selection.cpp',
        'test_vf_float_conversions.cpp',
      ),
      include_directories : [inc_include, inc_src, inc_intel],
      cpp_args : ['-Werror=vla'],
      link_with : libisl,
      dependencies : [idep_gtest, idep_nir, idep_mesautil, idep_intel_dev,
                      idep_intel_compiler_brw],
    ),
    suite : ['intel'],
    protocol : 'gtest',
  )
endif

if with_intel_tools

bison_command = []
if yacc_is_bison
  bison_command = [
    prog_bison, '@INPUT@', '--defines=@OUTPUT1@',
    '--output=@OUTPUT0@'
  ]
else
  bison_command = [
    prog_bison, '-H', '@OUTPUT1@',
    '-o', '@OUTPUT0@', '@INPUT@'
  ]
endif

brw_gram_tab = custom_target(
  'brw_gram.tab.[ch]',
  input : 'brw_gram.y',
  output : ['brw_gram.tab.c', 'brw_gram.tab.h'],
  command : bison_command
)

brw_lex_yy_c = custom_target(
  'brw_lex.yy.c',
  input : 'brw_lex.l',
  output : 'brw_lex.yy.c',
  command : [prog_flex, '-o', '@OUTPUT@', '@INPUT@']
)

brw_asm_deps = [
  dep_thread,
  idep_intel_compiler_brw,
  idep_intel_dev,
  idep_mesautil,
]

brw_asm = static_library(
  'brw_asm',
  ['brw_asm.c', brw_gram_tab[0], brw_gram_tab[1], brw_lex_yy_c],
  dependencies : brw_asm_deps,
  include_directories : [inc_include, inc_src, inc_intel],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

idep_brw_asm = declare_dependency(
  link_with : brw_asm,
  dependencies : brw_asm_deps,
)

brw_asm_tool = executable(
  'brw_asm',
  ['brw_asm_tool.c'],
  dependencies : idep_brw_asm,
  include_directories : [inc_include, inc_src, inc_intel],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  install : true
)

asm_testcases = [
  ['skl', 'gfx9'],
  ['icl', 'gfx11'],
  ['tgl', 'gfx12'],
  ['dg2', 'gfx12.5'],
]

test_runner = find_program('tests/run-test.py')
foreach testcase : asm_testcases
  _gen_name = testcase[0]
  _gen_num = testcase[1]
  _gen_folder = join_paths(meson.current_source_dir(), 'tests',
                           _gen_num.replace('gfx', 'gen'))
  test(
    'brw_asm_' + _gen_num, test_runner,
    args : [
      '--brw_asm', brw_asm_tool,
      '--gen_name', _gen_name,
      '--gen_folder', _gen_folder,
    ],
    suite : 'intel',
  )
endforeach

brw_disasm_tool = executable(
  'brw_disasm',
  files('brw_disasm_tool.c'),
  dependencies : [idep_mesautil, dep_thread, idep_intel_dev,
                  idep_intel_compiler_brw],
  include_directories : [inc_include, inc_src, inc_intel],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  install : true
)

endif
