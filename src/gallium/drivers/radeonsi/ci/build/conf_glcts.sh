#!/bin/bash
#
# Copyright 2025 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

set -e
python3 external/fetch_sources.py

rm -rf build
mkdir build
cmake -B build . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDEQP_TARGET=x11_egl

echo
echo !!! GLCTS is not supposed to be installed !!!
echo Type:
echo '   ninja -Cbuild'
