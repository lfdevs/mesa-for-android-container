# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libintel_compiler_nir_files = files(
  'intel_nir.h',
  'intel_nir.c',

  'intel_nir_blockify_uniform_loads.c',
  'intel_nir_clamp_image_1d_2d_array_sizes.c',
  'intel_nir_clamp_per_vertex_loads.c',
  'intel_nir_lower_non_uniform_barycentric_at_sample.c',
  'intel_nir_lower_non_uniform_resource_intel.c',
  'intel_nir_lower_printf.c',
  'intel_nir_lower_shading_rate_output.c',
  'intel_nir_lower_sparse.c',
  'intel_nir_opt_peephole_ffma.c',
  'intel_nir_opt_peephole_imul32x16.c',
  'intel_nir_tcs_workarounds.c',
)

libintel_compiler_nir = static_library(
  'intel_compiler_nir',
  [libintel_compiler_nir_files],
  include_directories : [inc_include, inc_src, inc_intel],
  c_args : [no_override_init_args],
  cpp_args : ['-Werror=vla'],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_nir_headers, idep_mesautil, idep_intel_dev, idep_vtn, idep_mda],
  build_by_default : false,
)

brw_device_sha1_gen_src = custom_target('brw_device_sha1_gen.c',
                                        input : ['brw_device_sha1_gen_c.py', '../dev/intel_device_info.py'],
                                        output : ['brw_device_sha1_gen.c'],
                                        command : [prog_python, '@INPUT0@', '--out', '@OUTPUT@'])

subdir('brw')

if with_intel_elk
  subdir('elk')
else
  idep_intel_compiler_elk = null_dep
endif
