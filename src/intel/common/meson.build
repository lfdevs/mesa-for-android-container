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

# TODO: android?

files_libintel_common = files(
  'gen_aux_map.c',
  'gen_aux_map.h',
  'gen_buffer_alloc.h',
  'gen_clflush.h',
  'gen_batch_decoder.c',
  'gen_decoder.c',
  'gen_decoder.h',
  'gen_disasm.c',
  'gen_disasm.h',
  'gen_gem.c',
  'gen_gem.h',
  'gen_guardband.h',
  'gen_l3_config.c',
  'gen_l3_config.h',
  'gen_urb_config.c',
  'gen_sample_positions.h',
  'gen_uuid.c',
  'gen_uuid.h',
)

libintel_common = static_library(
  ['intel_common', genX_xml_h],
  files_libintel_common,
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_intel],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  link_with : [libisl],
  dependencies : [dep_expat, dep_libdrm, dep_thread, idep_genxml],
)

libintel_extra_dependencies = []
if with_platform_android
  libintel_extra_dependencies += dep_android
endif

idep_libintel_common = declare_dependency(
  link_with: [libintel_common],
  dependencies: [libintel_extra_dependencies],
)

install_intel_gpu_tests = get_option('install-intel-gpu-tests')

if install_intel_gpu_tests
  foreach g : [['70', 'gen7'], ['75', 'hsw'], ['80', 'gen8'],
               ['90', 'gen9'], ['110', 'gen11'], ['120', 'gen12']]
    executable(
      'intel_@0@_mi_builder_test'.format(g[1]),
      files('tests/gen_mi_builder_test.cpp'),
      cpp_args : [
        cpp_msvc_compat_args,
        '-DGEN_VERSIONx10=@0@'.format(g[0])
      ],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_intel],
      link_with : [libintel_dev],
      dependencies : [dep_libdrm, idep_gtest, idep_genxml, idep_mesautil],
      install : install_intel_gpu_tests,
    )
  endforeach
endif

if with_tests
  gentest_xml = files('tests/gentest.xml')
  _name = 'gentest_pack.h'
  gentest_pack = custom_target(
    _name,
    input : [gen_pack_header_py, gentest_xml],
    output : _name,
    command : [prog_python, '@INPUT@'],
    capture : true,
  )

  genxml_path = join_paths(meson.source_root(),
                           '@0@'.format(gentest_xml[0]))

  test(
    'genxml_test',
    executable(
      'genxml_test',
      ['tests/genxml_test.c', gentest_pack],
      include_directories : [inc_include, inc_src, inc_intel],
      dependencies : idep_mesautil,
      link_with : libintel_common,
      c_args : [
        '-DGENXML_PATH="@0@"'.format(genxml_path)
      ],
    ),
    args : ['-quiet'],
    suite : ['intel'],
  )
endif
