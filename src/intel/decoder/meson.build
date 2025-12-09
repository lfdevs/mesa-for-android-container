# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libintel_decoder_common_args = {
  'include_directories' : [inc_include, inc_src, inc_intel],
  'c_args' : [no_override_init_args, sse2_args],
  'gnu_symbol_visibility' : 'hidden',
  'build_by_default' : false,
}

libintel_decoder_stub_files = files(
  'intel_decoder.h',
  'intel_batch_decoder_stub.c',
)

libintel_decoder_impl_files = files (
  'intel_decoder.h',
  'intel_decoder.c',
  'intel_batch_decoder.c',
)

libintel_decoder_stub_deps = [
  idep_intel_dev
]

libintel_decoder_impl_deps = [
  idep_intel_dev,
  dep_expat
]

if not dep_expat.found()
  libintel_decoder_deps = libintel_decoder_stub_deps
  libintel_decoder_files = libintel_decoder_stub_files
  libintel_decoder_brw_files = files()
  libintel_decoder_elk_files = files()
else
  libintel_decoder_deps = libintel_decoder_impl_deps
  libintel_decoder_files = libintel_decoder_impl_files
  libintel_decoder_brw_files = files('intel_batch_decoder_brw.c')
  libintel_decoder_elk_files = files('intel_batch_decoder_elk.c')
endif

libintel_decoder = static_library(
  'intel_decoder',
  [libintel_decoder_files, genX_xml_h, sha1_h],
  dependencies : libintel_decoder_deps,
  kwargs : libintel_decoder_common_args
)

idep_intel_decoder = declare_dependency(
  link_with : [libintel_decoder],
  dependencies : libintel_decoder_deps,
)

libintel_decoder_brw = static_library(
  'intel_decoder_brw',
  [libintel_decoder_brw_files, genX_xml_h, sha1_h],
  dependencies : idep_intel_decoder,
  kwargs : libintel_decoder_common_args
)

idep_intel_decoder_brw = declare_dependency(
  link_with : [libintel_decoder_brw],
  dependencies : idep_intel_decoder,
)

if with_intel_elk
  libintel_decoder_elk = static_library(
    'intel_decoder_elk',
    [libintel_decoder_elk_files, genX_xml_h, sha1_h],
    dependencies : idep_intel_decoder,
    kwargs : libintel_decoder_common_args
  )

  idep_intel_decoder_elk = declare_dependency(
    link_with : [libintel_decoder_elk],
    dependencies : idep_intel_decoder,
  )
else
  idep_intel_decoder_elk = null_dep
endif

# Stub decoder.
libintel_decoder_stub = static_library(
  'intel_decoder_stub',
  [libintel_decoder_stub_files, genX_xml_h, sha1_h],
  dependencies : libintel_decoder_stub_deps,
  kwargs : libintel_decoder_common_args
)

idep_intel_decoder_stub = declare_dependency(
  link_with : [libintel_decoder_stub],
  dependencies : libintel_decoder_stub_deps,
)

libintel_decoder_stub_brw = static_library(
  'intel_decoder_stub_brw',
  [genX_xml_h, sha1_h],
  dependencies : idep_intel_decoder_stub,
  kwargs : libintel_decoder_common_args
)

idep_intel_decoder_stub_brw = declare_dependency(
  link_with : [libintel_decoder_stub_brw],
  dependencies : idep_intel_decoder_stub,
)

# Test.
if with_tests and not with_platform_android
  gentest_xml = 'tests/gentest.xml'
  _name = 'gentest_pack.h'
  gentest_pack = custom_target(
    _name,
    input : [gen_pack_header_py, gentest_xml],
    output : _name,
    command : [prog_python, '@INPUT@'],
    capture : true,
    depend_files: gen_pack_header_deps
  )

  genxml_path = join_paths(meson.current_source_dir(),
                           '@0@'.format(gentest_xml))

  test(
    'genxml_test',
    executable(
      'genxml_test',
      ['tests/genxml_test.c', gentest_pack],
      include_directories : [
        inc_include,
        inc_src,
        inc_intel
      ],
      dependencies : [
        idep_libintel_common,
        idep_intel_decoder_brw,
        idep_intel_decoder_elk,
        idep_mesautil,
        idep_intel_dev,
        idep_genxml,
      ],
      c_args : [
        '-DGENXML_DIR="@0@"'.format(fs.parent(genxml_path)),
        '-DGENXML_FILE="@0@"'.format(fs.name(genxml_path)),
      ],
    ),
    args : ['-quiet'],
    suite : ['intel'],
  )
endif
