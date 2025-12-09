#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# ALPINE_X86_64_BUILD_TAG
# DEBIAN_BUILD_TAG
# DEBIAN_TEST_BASE_TAG
# FEDORA_X86_64_BUILD_TAG

set -ex

section_start mold "Building mold"

MOLD_VERSION="2.40.1"

git clone -b v"$MOLD_VERSION" --single-branch --depth 1 https://github.com/rui314/mold.git
pushd mold

cmake -DCMAKE_BUILD_TYPE=Release -D BUILD_TESTING=OFF -D MOLD_LTO=ON
cmake --build . --parallel "${FDO_CI_CONCURRENT:-4}"
cmake --install . --strip

# Always use mold from now on
find /usr/bin \( -name '*-ld' -o -name 'ld' \) \
  -exec ln -sf /usr/local/bin/ld.mold {} \; \
  -exec ls -l {} +

popd
rm -rf mold

section_end mold
