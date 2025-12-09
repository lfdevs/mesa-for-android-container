# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

general_ir_test_files = files(
  'builtin_variable_test.cpp',
  'general_ir_test.cpp',
)
general_ir_test_files += ir_expression_operation_h

if with_gles2
  general_ir_test_files += files('test_gl_lower_mediump.cpp')
endif

test(
  'general_ir_test',
  executable(
    'general_ir_test',
    general_ir_test_files,
    cpp_args : [cpp_msvc_compat_args],
    gnu_symbol_visibility : 'hidden',
    include_directories : [inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux, inc_glsl],
    link_with : [libglsl, libglsl_standalone, libglsl_util],
    dependencies : [dep_clock, dep_thread, idep_gtest, idep_mesautil, idep_nir],
  ),
  suite : ['compiler', 'glsl'],
  protocol : 'gtest',
)

test(
  'sampler_types_test',
  executable(
    'sampler_types_test',
    ['sampler_types_test.cpp', ir_expression_operation_h],
    cpp_args : [cpp_msvc_compat_args],
    gnu_symbol_visibility : 'hidden',
    include_directories : [inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux, inc_glsl],
    link_with : [libglsl, libglsl_util],
    dependencies : [dep_thread, idep_gtest, idep_mesautil, idep_compiler],
  ),
  suite : ['compiler', 'glsl'],
  protocol : 'gtest',
)

test(
  'list_iterators',
  executable(
    'list_iterators',
    ['list_iterators.cpp'],
    cpp_args : [cpp_msvc_compat_args],
    gnu_symbol_visibility : 'hidden',
    include_directories : [inc_include, inc_src, inc_glsl],
    link_with : [libglsl, libglsl_util],
    dependencies : [dep_thread, idep_gtest],
  ),
  suite : ['compiler', 'glsl'],
  protocol : 'gtest',
)

# Meson can't auto-skip these on cross builds because of the python wrapper
if meson.can_run_host_binaries()
  test(
    'glsl compiler warnings',
    prog_python,
    args : [
      files('warnings_test.py'),
      '--glsl-compiler', glsl_compiler,
      '--test-directory', join_paths(
        dir_source_root, 'src', 'compiler', 'glsl', 'tests', 'warnings'
      ),
    ],
    suite : ['compiler', 'glsl'],
    timeout: 60,
  )
endif

if with_tools.contains('glsl')
  if with_gles2
    test(
      'glsl lower-precision test',
      prog_python,
      args : [files('lower_precision_test.py'),
              glsl_compiler
            ],
      suite : ['compiler', 'glsl'],
      timeout: 60,
    )
  endif
endif
