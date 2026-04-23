# Copyright © 2017-2019 Intel Corporation
# SPDX-License-Identifier: MIT

libva_c_args = [
    '-DVA_DRIVER_INIT_FUNC=__vaDriverInit_@0@_@1@'.format(
        libva_version[0], libva_version[1]
    )
]

libva_files = files(
    'buffer.c', 'config.c', 'context.c', 'display.c',
    'picture.c', 'surface.c', 'decode.c', 'image.c'
)

if with_gfx_compute
  libva_files += files('postproc.c', 'subpicture.c')
endif

if _codecs.contains('jpegdec')
  libva_files += files('picture_mjpeg.c')
endif
if _codecs.contains('mpeg12dec')
  libva_files += files('picture_mpeg12.c')
endif
if _codecs.contains('mpeg4dec')
  libva_files += files('picture_mpeg4.c')
endif
if _codecs.contains('vc1dec')
  libva_files += files('picture_vc1.c')
endif
if _codecs.contains('vp9dec')
  libva_files += files('picture_vp9.c')
endif
if _codecs.contains('h264dec')
  libva_files += files('picture_h264.c')
endif
if _codecs.contains('h265dec')
  libva_files += files('picture_hevc.c')
endif
if _codecs.contains('av1dec')
  libva_files += files('picture_av1.c')
endif

if _codecs.contains('h264enc')
  libva_files += files('picture_h264_enc.c')
endif
if _codecs.contains('h265enc')
  libva_files += files('picture_hevc_enc.c')
endif
if _codecs.contains('av1enc')
  libva_files += files('picture_av1_enc.c')
endif
if with_video_encode
  libva_files += files('encode.c')
endif

libva_st = static_library(
  'va_st',
  libva_files,
  c_args : libva_c_args,
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_gallium, inc_gallium_aux],
  dependencies : [
    dep_va_headers, dep_x11_xcb, dep_xcb, dep_xcb_dri3,
    dep_libdrm, idep_mesautil,
  ],
)
