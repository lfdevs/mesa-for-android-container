# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

foreach t : ['tri', 'quad-tex']
  executable(
    t,
    '@0@.c'.format(t),
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
    link_with : [libgallium, libpipe_loader_dynamic],
    dependencies : idep_mesautil,
    install : false,
  )
endforeach
