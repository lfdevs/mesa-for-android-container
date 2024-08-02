# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

test(
  'st_format_test',
  executable(
    'st_format_test',
    ['st_format.c'],
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
    link_with : [
      libmesa, libglapi, libgallium,
    ],
    dependencies : [idep_gtest, idep_mesautil],
  ),
  suite : ['st_mesa'],
)
