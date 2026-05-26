# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# SPDX-License-Identifier: MIT

pan_packers = []
foreach packer : ['common', 'v4', 'v5', 'v6', 'v7', 'v9', 'v10', 'v12', 'v13', 'v14']
  pan_packers += custom_target(
    packer + '_pack.h',
    input : ['gen_pack.py', packer + '.xml'],
    output : packer + '_pack.h',
    command : [prog_python, '@INPUT@'],
    capture : true,
  )
endforeach

idep_pan_packers = declare_dependency(
  sources : [pan_packers],
  include_directories : include_directories('.'),
)

libpanfrost_decode_per_arch = []

foreach ver : ['4', '5', '6', '7', '9', '10', '12', '13', '14']
  libpanfrost_decode_per_arch += static_library(
    'pandecode-arch-v' + ver,
    ['decode.c', 'decode_jm.c', 'decode_csf.c', pan_packers],
    include_directories : [inc_include, inc_src, inc_panfrost],
    dependencies : [dep_libdrm, idep_pan_packers, idep_nir, idep_mesautil],
    c_args : [no_override_init_args, soong_compat_c_args, '-DPAN_ARCH=' + ver],
    gnu_symbol_visibility : 'hidden',
    build_by_default : false,
  )
endforeach

libpanfrost_decode = static_library(
  'panfrost_decode',
  [
    'decode_common.c',
    pan_packers
  ],
  include_directories : [inc_include, inc_src, inc_panfrost],
  dependencies : idep_mesautil,
  c_args : [no_override_init_args, soong_compat_c_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
  link_with: [libpanfrost_decode_per_arch],
)

if with_tests
  test('cs_builder_tests',
    executable(
      'cs_builder_tests',
      ['test/cs_builder.cpp'],
      # Nothing arch-specific is tested currently, only compile v10.
      cpp_args : [cpp_msvc_compat_args, '-DPAN_ARCH=10'],
      gnu_symbol_visibility : 'hidden',
      include_directories: [inc_include, inc_src, inc_panfrost],
      dependencies: [idep_gtest, idep_pan_packers, idep_mesautil],
    ),
    suite: ['panfrost'],
    protocol: 'gtest',
  )
endif
