# Copyright 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
# SPDX-License-Identifier: MIT

files_mlw_codec = files(
   'source/mlw_encode.cpp',
   'source/mlw_encode_fwd.cpp',
   'source/ml_ethosu_encode.cpp',
   'source/mlw_decode.cpp',
)

# mlw_codec is vendored verbatim from Arm's Regor compiler (Ethos-U Vela).
# Keep the imported sources pristine for clean re-vendoring and silence their
# known warnings at build-config level rather than patching vendored code.
mlw_codec_cpp_args = cpp.get_supported_arguments(
  '-Wno-unused-but-set-variable',
  '-Wno-sign-compare',
  '-Wno-unused-const-variable',
)

libmlw_codec = static_library(
  'mlw_codec',
  [files_mlw_codec],
  gnu_symbol_visibility : 'hidden',
  cpp_args : mlw_codec_cpp_args,
)

inc_libmlw_codec = [include_directories('include')]