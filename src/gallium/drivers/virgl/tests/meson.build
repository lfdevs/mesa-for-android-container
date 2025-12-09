# Copyright © 2019 Collabora Ltd.
# SPDX-License-Identifier: MIT

test(
  'virgl_staging_mgr',
  executable(
    'virgl_staging_mgr_test',
    files('virgl_staging_mgr_test.cpp'),
    dependencies : [dep_thread, idep_gtest, idep_mesautil, idep_nir_headers],
    include_directories : [inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_virtio, include_directories('..')],
    link_with : [libvirgl, libgallium],
  ),
  suite : ['virgl'],
  protocol : 'gtest',
)
