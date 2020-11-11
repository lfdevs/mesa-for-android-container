# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

isl_gen_files = files(
  'isl_emit_depth_stencil.c',
  'isl_surface_state.c',
)

isl_gen4_files = files(
  'isl_gen4.c',
  'isl_gen4.h',
)

isl_gen6_files = files(
  'isl_gen6.c',
  'isl_gen6.h',
)

isl_gen7_files = files(
  'isl_gen7.c',
  'isl_gen7.h',
)

isl_gen8_files = files(
  'isl_gen8.c',
  'isl_gen8.h',
)

isl_gen9_files = files(
  'isl_gen9.c',
  'isl_gen9.h',
)

isl_gen12_files = files(
  'isl_gen12.c',
  'isl_gen12.h',
)

isl_gen_libs = []
foreach g : [['40', isl_gen4_files], ['50', []], ['60', isl_gen6_files],
             ['70', isl_gen7_files], ['75', []], ['80', isl_gen8_files],
             ['90', isl_gen9_files], ['110', []], ['120', isl_gen12_files]]
  _gen = g[0]
  isl_gen_libs += static_library(
    'isl_gen@0@'.format(_gen),
    [g[1], isl_gen_files, gen_xml_pack],
    include_directories : [inc_include, inc_src, inc_gallium, inc_intel],
    c_args : [
      no_override_init_args,
      '-DGEN_VERSIONx10=@0@'.format(_gen)
    ],
    gnu_symbol_visibility : 'hidden',
  )
endforeach

isl_format_layout_c = custom_target(
  'isl_format_layout.c',
  input : ['gen_format_layout.py', 'isl_format_layout.csv'],
  output : 'isl_format_layout.c',
  command : [prog_python, '@INPUT0@', '--csv', '@INPUT1@', '--out', '@OUTPUT@'],
)

files_isl_tiled_memcpy = files(
  'isl_tiled_memcpy_normal.c'
)

files_isl_tiled_memcpy_sse41 = files(
  'isl_tiled_memcpy_sse41.c',
)

isl_tiled_memcpy = static_library(
  'isl_tiled_memcpy',
  [files_isl_tiled_memcpy],
  include_directories : [
    inc_include, inc_src, inc_mesa, inc_gallium, inc_intel,
  ],
  c_args : [no_override_init_args, '-msse2'],
  gnu_symbol_visibility : 'hidden',
  extra_files : ['isl_tiled_memcpy.c']
)

if with_sse41
  isl_tiled_memcpy_sse41 = static_library(
    'isl_tiled_memcpy_sse41',
    [files_isl_tiled_memcpy_sse41],
    include_directories : [
      inc_include, inc_src, inc_mesa, inc_gallium, inc_intel,
    ],
    link_args : ['-Wl,--exclude-libs=ALL'],
    c_args : [no_override_init_args, '-msse2', sse41_args],
    gnu_symbol_visibility : 'hidden',
    extra_files : ['isl_tiled_memcpy.c']
  )
else
  isl_tiled_memcpy_sse41 = []
endif

libisl_files = files(
  'isl.c',
  'isl.h',
  'isl_aux_info.c',
  'isl_drm.c',
  'isl_genX_priv.h',
  'isl_format.c',
  'isl_priv.h',
  'isl_storage_image.c',
)

libisl = static_library(
  'isl',
  [libisl_files, isl_format_layout_c, genX_bits_h],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel],
  link_with : [isl_gen_libs, isl_tiled_memcpy, isl_tiled_memcpy_sse41],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
)

if with_tests
  test(
    'isl_surf_get_image_offset',
    executable(
      'isl_surf_get_image_offset_test',
      'tests/isl_surf_get_image_offset_test.c',
      dependencies : [dep_m, idep_mesautil],
      include_directories : [inc_include, inc_src, inc_gallium, inc_intel],
      link_with : [libisl, libintel_dev],
    ),
    suite : ['intel'],
  )
  test(
    'isl_aux_info',
    executable(
      'isl_aux_info_test',
      [
        'tests/isl_aux_info_test.cpp',
        'isl_aux_info.c',
      ],
      dependencies : [dep_m, idep_gtest, idep_mesautil],
      include_directories : [inc_include, inc_src, inc_gallium, inc_intel],
      c_args : '-DIN_UNIT_TEST',
      cpp_args : '-DIN_UNIT_TEST',
    ),
    suite : ['intel'],
  )
endif
