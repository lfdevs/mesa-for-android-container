# Copyright 2018 Rob Clark
# Copyright 2019 Collabora, Ltd.
# SPDX-License-Identifier: MIT

inc_asahi = include_directories([
   '.', 'layout', 'lib', 'compiler'
])

subdir('layout')
subdir('lib')
subdir('compiler')

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif
