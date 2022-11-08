#!/bin/bash

set -ex

wget https://github.com/KhronosGroup/SPIRV-LLVM-Translator/archive/refs/tags/v13.0.0.tar.gz
tar -xvf v13.0.0.tar.gz && rm v13.0.0.tar.gz

mkdir SPIRV-LLVM-Translator-13.0.0/build
pushd SPIRV-LLVM-Translator-13.0.0/build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
ninja
ninja install
# For some reason llvm-spirv is not installed by default
ninja llvm-spirv
cp tools/llvm-spirv/llvm-spirv /usr/bin/
popd

du -sh SPIRV-LLVM-Translator-13.0.0
rm -rf SPIRV-LLVM-Translator-13.0.0
