# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_libblorp = files(
  'blorp.c',
  'blorp.h',
  'blorp_blit.c',
  'blorp_clear.c',
  'blorp_nir_builder.h',
  'blorp_priv.h',
)

files_blorp_brw = files(
  'blorp_genX_exec_brw.h',
  'blorp_brw.c',
)

files_blorp_elk = files(
  'blorp_genX_exec_elk.h',
  'blorp_elk.c',
)

libblorp = static_library(
  'blorp',
  [files_libblorp, files_blorp_brw],
  include_directories : [inc_include, inc_src, inc_intel, inc_intel_compiler],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_nir_headers, idep_genxml, idep_mesautil, idep_intel_dev],
  build_by_default: false,
)

idep_intel_blorp = declare_dependency(
  link_with: libblorp,
)

if with_intel_elk
  libblorp_elk = static_library(
    'blorp_elk',
    [files_libblorp, files_blorp_elk],
    include_directories : [inc_include, inc_src, inc_intel],
    c_args : [no_override_init_args],
    gnu_symbol_visibility : 'hidden',
    dependencies : [idep_nir_headers, idep_genxml, idep_mesautil, idep_intel_dev],
    build_by_default: true, # FIXME XXX
  )

  idep_intel_blorp_elk = declare_dependency(
    dependencies: [idep_intel_dev],
    link_with: libblorp_elk,
  )
else
  idep_intel_blorp_elk = null_dep
endif
