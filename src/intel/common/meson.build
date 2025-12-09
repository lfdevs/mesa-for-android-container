# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_libintel_common = files(
  'i915/intel_engine.c',
  'i915/intel_engine.h',
  'i915/intel_gem.c',
  'i915/intel_gem.h',
  'xe/intel_device_query.c',
  'xe/intel_device_query.h',
  'xe/intel_engine.c',
  'xe/intel_engine.h',
  'xe/intel_gem.c',
  'xe/intel_gem.h',
  'xe/intel_queue.c',
  'xe/intel_queue.h',
  'intel_aux_map.c',
  'intel_aux_map.h',
  'intel_bind_timeline.c',
  'intel_bind_timeline.h',
  'intel_buffer_alloc.h',
  'intel_common.c',
  'intel_common.h',
  'intel_compute_slm.c',
  'intel_compute_slm.h',
  'intel_debug_identifier.h',
  'intel_debug_identifier.c',
  'intel_engine.c',
  'intel_engine.h',
  'intel_gem.c',
  'intel_gem.h',
  'intel_genX_state_brw.h',
  'intel_genX_state_elk.h',
  'intel_guardband.h',
  'intel_l3_config.c',
  'intel_l3_config.h',
  'intel_tiled_render.h',
  'intel_urb_config.c',
  'intel_sample_positions.c',
  'intel_sample_positions.h',
  'intel_uuid.c',
  'intel_uuid.h',
  'intel_measure.c',
  'intel_measure.h',
  'intel_pixel_hash.h'
)

libintel_common_links = [libisl]

libintel_common = static_library(
  'intel_common',
  [files_libintel_common, genX_xml_h, sha1_h],
  include_directories : [inc_include, inc_src, inc_intel],
  c_args : [no_override_init_args, sse2_args],
  gnu_symbol_visibility : 'hidden',
  link_with : libintel_common_links,
  dependencies : [dep_libdrm, dep_thread, idep_genxml, idep_mesautil, idep_intel_dev],
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
if install_intel_gpu_tests and not with_tests
  error('`install-intel-gpu-tests` requires `build-tests`')
endif

if with_tests and not with_platform_android
  foreach g : [['70', 'gfx7'], ['75', 'hsw'], ['80', 'gfx8'],
               ['90', 'gfx9'], ['110', 'gfx11'], ['120', 'gfx12'],
               ['125', 'gfx125'], ['200', 'gfx20'], ['300', 'gfx30']]
    executable(
      'intel_@0@_mi_builder_test'.format(g[1]),
      files('tests/mi_builder_test.cpp'),
      cpp_args : [
        cpp_msvc_compat_args,
        '-DGFX_VERx10=@0@'.format(g[0])
      ],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_intel],
      link_with : [libintel_common],
      dependencies : [dep_libdrm, idep_gtest, idep_genxml, idep_mesautil, idep_intel_dev],
      install : install_intel_gpu_tests,
    )
  endforeach
endif
