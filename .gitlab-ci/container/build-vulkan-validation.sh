#!/usr/bin/env bash

set -ex

VALIDATION_TAG="v1.3.238"

git clone -b "$VALIDATION_TAG" --single-branch --depth 1 https://github.com/KhronosGroup/Vulkan-ValidationLayers.git
pushd Vulkan-ValidationLayers
mkdir build
pushd build
python3 ../scripts/update_deps.py --dir ../external --arch x64 --config debug
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF -C ../external/helper.cmake  ..
ninja install
popd
rm -rf Vulkan-ValidationLayers
