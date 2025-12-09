# Copyright 2022 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

c_args_lanczos = cc.get_supported_arguments([
  '-Wall',
  '-Wextra',
  '-Wno-unused',
  '-Wno-unused-parameter',
  '-Wno-unused-command-line-argument',
  '-Wno-ignored-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-self-assign',
  '-Wno-implicit-fallthrough',
  '-Werror=comment',
  '-Werror=missing-braces',
  '-Werror=override-init',
  '-Werror=enum-conversion',
  '-Werror=enum-compare',
])

c_args_lanczos += [
#  '-DGM_SIM',
]

lanczos_files = files(
  'lanczos_adaptor.h',
  'lanczos_adaptor.cpp',
  'lanczosFilter/src/lanczosFilterGenerator.h',
  'lanczosFilter/src/lanczosFilterGenerator.cpp',
)

inc_amd_lanczos = include_directories(
  'lanczosFilter/src',
)

liblanczos = static_library(
  'liblanczos.a',
  lanczos_files,
  install : false,
  c_args : c_args_lanczos,
  include_directories : inc_amd_lanczos
)
