# Copyright © 2017 Jon Turney
# SPDX-License-Identifier: MIT

# protocol defines for the Windows-DRI server extension

files_windowsdriproto = files(
 'windowsdriconst.h',
 'windowsdristr.h',
 )

install_headers(
  files_windowsdriproto,
  subdir: 'X11/extensions',
)

pkg.generate(
  name : 'windowsdriproto',
  description : 'Windows-DRI extension headers',
  version : '1.0.0',
)

# library for using the Windows-DRI server extension
files_libwindowsdri = files(
  'xwindowsdri.c',
  'xwindowsdri.h',
)

libwindowsdri = static_library(
  'driwindows',
  [files_libwindowsdri, files_windowsdriproto],
  dependencies: dep_xext,
  build_by_default: false,
)

# library for native GL on windows
files_libwindowsglx = files(
  'windowsgl.c',
  'windowsgl.h',
  'windowsgl_internal.h',
  'windows_drawable.c',
  'wgl.c',
  'wgl.h',
)

libwindowsglx = static_library(
  'glxwindows',
  [files_libwindowsglx, files_windowsdriproto],
  include_directories: [inc_include, inc_src, inc_glapi],
  gnu_symbol_visibility : 'hidden',
  build_by_default: false,
)
