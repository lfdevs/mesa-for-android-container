# Copyright 2024 Google LLC
# SPDX-License-Identifier: MIT

inc_aemu = include_directories('include')

files_libaemu = files('Stream.cpp', 'ring_buffer.cpp')

libaemu = static_library('aemu', files_libaemu, include_directories: [inc_aemu])
