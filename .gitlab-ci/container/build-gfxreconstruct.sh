#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VK_TAG

set -ex

section_start gfxreconstruct "Building gfxreconstruct"

GFXRECONSTRUCT_VERSION=6670c53aa2013d1e839e57410f8b332dc58e38ce

git clone https://github.com/LunarG/gfxreconstruct.git \
    --single-branch \
    -b master \
    --no-checkout \
    /gfxreconstruct
pushd /gfxreconstruct
git checkout "$GFXRECONSTRUCT_VERSION"
git submodule update --init
git submodule update
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX:PATH=/gfxreconstruct/build -DBUILD_WERROR=OFF
cmake --build _build --parallel --target tools/{replay,info}/install/strip
find . -not -path './build' -not -path './build/*' -delete
popd

section_end gfxreconstruct
