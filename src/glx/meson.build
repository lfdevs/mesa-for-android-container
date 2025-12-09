# Copyright © 2017-2019 Intel Corporation
# SPDX-License-Identifier: MIT

inc_glx = include_directories('.')

subdir('apple')
if with_dri_platform == 'windows'
  subdir('windows')
endif

files_libglx = files(
  'clientattrib.c',
  'clientinfo.c',
  'compsize.c',
  'create_context.c',
  'dri_common.c',
  'dri_common.h',
  'dri_common_query_renderer.c',
  'drisw_glx.c',
  'drisw_priv.h',
  'eval.c',
  'glxclient.h',
  'glxcmds.c',
  'glxconfig.c',
  'glxconfig.h',
  'glxcurrent.c',
  'glx_error.c',
  'glx_error.h',
  'glxext.c',
  'glxextensions.c',
  'glxextensions.h',
  'glxhash.c',
  'glxhash.h',
  'glx_pbuffer.c',
  'glx_query.c',
  'indirect_glx.c',
  'indirect_init.h',
  'indirect_texture_compression.c',
  'indirect_transpose_matrix.c',
  'indirect_vertex_array.c',
  'indirect_vertex_array.h',
  'indirect_vertex_array_priv.h',
  'indirect_vertex_program.c',
  'indirect_window_pos.c',
  'packrender.h',
  'packsingle.h',
  'pixel.c',
  'pixelstore.c',
  'query_renderer.c',
  'render2.c',
  'renderpix.c',
  'single2.c',
  'vertarr.c',
  'xfont.c',
)

extra_libs_libglx = []
extra_deps_libgl = []
extra_ld_args_libgl = []

if with_dri_platform == 'drm'
  files_libglx += files('dri3_glx.c', 'dri3_priv.h')
endif

if with_dri_platform == 'apple'
  files_libglx += files('applegl_glx.c')
  extra_libs_libglx += libappleglx
elif with_dri_platform == 'windows'
  files_libglx += files('driwindows_glx.c')
  extra_libs_libglx += [
    libwindowsdri,
    libwindowsglx,
  ]
  extra_deps_libgl = [
    meson.get_compiler('c').find_library('gdi32'),
    meson.get_compiler('c').find_library('opengl32')
  ]
  extra_ld_args_libgl = '-Wl,--disable-stdcall-fixup'
endif

if not with_glvnd
  gl_lib_name = 'GL'
  gl_lib_version = '1.2.0'
else
  gl_lib_name = 'GLX_@0@'.format(glvnd_vendor_name)
  gl_lib_version = '0.0.0'
  files_libglx += files(
    'g_glxglvnddispatchfuncs.c',
    'g_glxglvnddispatchindices.h',
    'glxglvnd.c',
    'glxglvnd.h',
    'glxglvnddispatchfuncs.h',
  )
endif

glx_gallium_link = []
if with_dri
  glx_gallium_link += libgallium_dri
endif
if with_platform_windows
  glx_gallium_link += libgallium_wgl
endif
libglx = static_library(
  'glx',
  [files_libglx, glx_generated, main_dispatch_h],
  include_directories : [inc_include, inc_src, inc_glapi, inc_loader, inc_loader_x11,
                         inc_gallium, inc_mesa, inc_st_dri, inc_gallium_aux],
  gnu_symbol_visibility : 'hidden',
  link_with : [
    libloader, libloader_x11,
    extra_libs_libglx, glx_gallium_link
  ],
  dependencies : [
    idep_mesautil, idep_xmlconfig,
    dep_libdrm, dep_glproto, dep_x11, dep_xext, dep_glvnd, dep_xxf86vm, dep_xshmfence,
  ],
)

# libglapi_bridge contains GL functions that we need to export from libGL.so.
# Other than that, we don't need it.
libgl_link = with_glvnd ? [] : [libglapi_bridge]

libgl = shared_library(
  gl_lib_name,
  [],
  link_whole : [libglx, libgl_link],
  link_args : [ld_args_bsymbolic, ld_args_gc_sections, extra_ld_args_libgl],
  dependencies : [
    dep_libdrm, dep_dl, dep_m, dep_thread, dep_x11, dep_xcb_glx, dep_xcb,
    dep_x11_xcb, dep_xext, dep_xxf86vm, dep_xcb_shm, extra_deps_libgl,
  ],
  version : gl_lib_version,
  darwin_versions : '4.0.0',
  install : true,
)

if not with_glvnd
  pkg.generate(
    name : 'glx',
    description : 'Mesa GLX Library',
    version : meson.project_version(),
    libraries : libgl,
    libraries_private : gl_priv_libs,
    requires_private : gl_priv_reqs,
    variables : ['glx_tls=yes'],
  )
endif

if with_symbols_check
  libgl_symbols_file = with_glvnd ? 'glvnd-symbols.txt' : 'libgl-symbols.txt'

  test(
    'libGL-ABI-check',
    symbols_check,
    args : [
      '--lib', libgl,
      '--symbols-file', files(libgl_symbols_file),
      symbols_check_args,
    ],
    suite : ['glx'],
  )
endif
