#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -uex

uncollapsed_section_start xwayland "Building XWayland"

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_BASE_TAG
#
export XORGPROTO_VERSION="xorgproto-2024.1"
export XWAYLAND_VERSION="xwayland-24.1.8"

git clone https://gitlab.freedesktop.org/xorg/proto/xorgproto
cd xorgproto
git checkout "$XORGPROTO_VERSION"
meson setup _build ${EXTRA_MESON_ARGS:-}
meson install -C _build
cd ..
rm -rf xorgproto

git clone https://gitlab.freedesktop.org/xorg/xserver
cd xserver
git checkout "$XWAYLAND_VERSION"
meson setup _build ${EXTRA_MESON_ARGS:-}
meson install -C _build
cd ..
rm -rf xserver

section_end xwayland
