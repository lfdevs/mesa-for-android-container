# Copyright 2026 LunarG, Inc.
# Copyright 2026 Google LLC
# SPDX-License-Identifier: MIT

if get_option('precomp-compiler') == 'system'
  prog_kk_clc = find_program('kk_clc', native : true)
else
  prog_kk_clc = executable(
    'kk_clc',
    ['kk_clc.c'],
    link_with : [libmsl_compiler],
    include_directories : [inc_include, inc_src],
    c_args : [pre_args, no_override_init_args],
    link_args : [ld_args_build_id],
    dependencies : [idep_vtn, idep_nir, idep_mesautil],
    # If we can run host binaries directly, just build kk_clc for the host.
    native : not meson.can_run_host_binaries(),
    install : get_option('install-precomp-compiler'),
  )
endif