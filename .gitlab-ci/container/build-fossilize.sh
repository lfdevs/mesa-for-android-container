#!/bin/bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VK_TAG

set -ex

section_start fossilize "Building fossilize"

git clone https://github.com/ValveSoftware/Fossilize.git
cd Fossilize
git checkout c774839d5412dd675dc15b41412041c316393069
git submodule update --init
mkdir build
cd build
cmake -S .. -B . -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C . install
cd ../..
rm -rf Fossilize

section_end fossilize
