#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VIDEO_TAG

set -uex

section_start va-tools "Building va-tools"

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"

git clone \
    https://github.com/intel/libva-utils.git \
    -b 2.22.0 \
    --depth 1 \
    /va-utils

pushd /va-utils

meson setup build -D tests=true -Dprefix=/va ${EXTRA_MESON_ARGS:-}
meson install -C build
popd
rm -rf /va-utils

section_end va-tools
