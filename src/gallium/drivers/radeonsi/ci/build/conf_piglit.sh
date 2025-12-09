#!/bin/bash
#
# Copyright 2025 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

set -e

cmake . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DPIGLIT_BUILD_DMA_BUF_TESTS=ON \
	-DPIGLIT_BUILD_GLES1_TESTS=ON \
	-DPIGLIT_BUILD_GLES2_TESTS=ON \
	-DPIGLIT_BUILD_GLES3_TESTS=ON \
	-DPIGLIT_BUILD_GLX_TESTS=ON \
	-DPIGLIT_BUILD_GL_TESTS=ON \
	-DPIGLIT_USE_WAFFLE=ON

echo
echo !!! Piglit is not supposed to be installed !!!
