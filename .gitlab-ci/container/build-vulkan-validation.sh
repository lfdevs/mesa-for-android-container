#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_GL_TAG

set -uex

uncollapsed_section_start vulkan-validation "Building Vulkan validation layers"

VALIDATION_TAG="f7a6d134b827a17a1c75a5e04fbf04e97d5d9388"

mkdir Vulkan-ValidationLayers
pushd Vulkan-ValidationLayers
git init
git remote add origin https://github.com/KhronosGroup/Vulkan-ValidationLayers.git
git fetch --depth 1 origin "$VALIDATION_TAG"
git checkout FETCH_HEAD

# we don't need to build SPIRV-Tools tools
sed -i scripts/known_good.json -e 's/SPIRV_SKIP_EXECUTABLES=OFF/SPIRV_SKIP_EXECUTABLES=ON/'
python3 scripts/update_deps.py --dir external --config release --generator Ninja --optional tests
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF -C external/helper.cmake -S . -B build
ninja -C build -j"${FDO_CI_CONCURRENT:-4}"
cmake --install build --strip
popd
rm -rf Vulkan-ValidationLayers

section_end vulkan-validation
