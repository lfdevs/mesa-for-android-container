# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

if with_shared_glapi
  files_glx_test = files(
    'clientinfo_unittest.cpp',
    'create_context_unittest.cpp',
    'enum_sizes.cpp',
    'fake_glx_screen.cpp',
    'fake_glx_screen.h',
    'indirect_api.cpp',
    'mock_xdisplay.h',
    'query_renderer_unittest.cpp',
  )
  if with_dri2
    files_glx_test += files('query_renderer_implementation_unittest.cpp')
  endif

  test(
    'dispatch-index-check',
    files('dispatch-index-check'),
    suite : ['glx'],
  )
  test(
    'glx-test',
    executable(
      'glx-test',
      [files_glx_test, glx_indirect_size_h, main_dispatch_h],
      link_with : [libglx, libglapi, glx_gallium_link, libloader],
      include_directories : [
        inc_src, inc_include, inc_mesa, inc_mapi, inc_glx, inc_loader, inc_gallium
      ],
      dependencies : [dep_libdrm, dep_glproto, dep_thread, idep_gtest]
    ),
    suite : ['glx'],
    protocol : 'gtest',
  )
endif
