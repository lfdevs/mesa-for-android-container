# Copyright © 2017 Jon Turney
# SPDX-License-Identifier: MIT

# library for native GL on macos
files_libappleglx = files(
  'apple_cgl.c',
  'apple_cgl.h',
  'appledri.c',
  'appledri.h',
  'appledristr.h',
  'apple_glapi.c',
  'apple_glx.c',
  'apple_glx_context.c',
  'apple_glx_context.h',
  'apple_glx_drawable.c',
  'apple_glx_drawable.h',
  'apple_glx.h',
  'apple_glx_log.c',
  'apple_glx_log.h',
  'apple_glx_pbuffer.c',
  'apple_glx_pixmap.c',
  'apple_glx_surface.c',
  'apple_visual.c',
  'apple_visual.h',
  'apple_xgl_api.h',
  'apple_xgl_api_read.c',
  'apple_xgl_api_stereo.c',
  'apple_xgl_api_viewport.c',
  'glx_empty.c',
)

dep_xplugin = null_dep
if with_dri_platform == 'apple'
  dep_xplugin = meson.get_compiler('c').find_library('Xplugin')
endif

libappleglx = static_library(
  'glxapple',
  [files_libappleglx, glapitable_h],
  include_directories: [inc_mesa, inc_glx, inc_src, inc_include, inc_glapi, inc_loader],
  dependencies: [dep_xext, dep_xplugin],
  gnu_symbol_visibility : 'hidden',
  build_by_default: false,
)
