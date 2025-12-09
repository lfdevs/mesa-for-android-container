# Copyright © 2025 Imagination Technologies Ltd.
# based in part on intel driver which is:
# Copyright 2017 Intel Corporation
# SPDX-License-Identifier: MIT

if get_option('precomp-compiler') == 'system'
  prog_pco_clc = find_program('pco_clc', native : true)
else
  prog_pco_clc = executable(
    'pco_clc',
    ['pco_clc.c'],
    link_with : [libpowervr_compiler, libpowervr_common],
    include_directories : [inc_imagination, inc_include, inc_src],
    c_args : [pre_args, no_override_init_args],
    link_args : [ld_args_build_id],
    dependencies : [idep_vtn, idep_nir, idep_mesautil],
    # If we can run host binaries directly, just build pco_clc for the host.
    # Most commonly this happens when doing a cross compile from an x86_64 build
    # machine to an x86 host
    native : not meson.can_run_host_binaries(),
    install : get_option('install-precomp-compiler'),
  )
endif

###

uscgen_devices = get_option('imagination-uscgen-devices')
foreach uscgen_device : uscgen_devices
  device_info_path = join_paths(pvr_device_info_dir, uscgen_device + '.h')
  if not fs.is_file(device_info_path)
     error('Missing device info for ' + uscgen_device)
  endif
endforeach

pco_uscgen_programs = custom_target(
  'pco_uscgen_programs',
  input : pco_usclib_spv,
  output : ['pco_uscgen_programs.h', 'pco_uscgen_programs.c'],
  command : [prog_pco_clc, pco_usclib_spv, '@OUTPUT@', uscgen_devices],
  env: ['PCO_DEBUG=no_pred_cf'],
)

idep_pco_uscgen_programs_headers = declare_dependency(
  sources : [pco_uscgen_programs[0]],
  include_directories : include_directories('.'),
)

idep_pco_uscgen_programs = declare_dependency(
  sources : [pco_uscgen_programs],
  dependencies : idep_pco_uscgen_programs_headers,
)

###

libpowervr_uscgen = static_library(
  'powervr_uscgen',
  [pco_uscgen_programs],
  include_directories : [inc_imagination],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies: [idep_nir, idep_mesautil],
)
