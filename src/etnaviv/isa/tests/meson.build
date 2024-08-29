# Copyright © 2023 Igalia S.L.
# SPDX-License-Identifier: MIT

test(
  'etnaviv_isa_disasm',
  executable(
    'etnaviv_disasm', 'disasm.cpp',
    cpp_args : [
      pre_args,
    ],
    include_directories: [inc_etnaviv, inc_src],
    dependencies : [idep_gtest, idep_etnaviv_enums_h, idep_libetnaviv_decode],
    link_with : [libetnaviv_isa_rs, libetnaviv_encode],
  )
)
