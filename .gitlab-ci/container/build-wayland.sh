#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -uex

uncollapsed_section_start wayland "Building Wayland"

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# ALPINE_X86_64_BUILD_TAG
# DEBIAN_BUILD_TAG
# DEBIAN_TEST_BASE_TAG
# FEDORA_X86_64_BUILD_TAG

export LIBWAYLAND_VERSION="1.24.0"
export WAYLAND_PROTOCOLS_VERSION="1.41"

git clone https://gitlab.freedesktop.org/wayland/wayland
cd wayland
git checkout "$LIBWAYLAND_VERSION"

# Build the native library and scanner first in case we're a cross build.
# Note the lack of EXTRA_MESON_ARGS here.
meson setup -Dtests=false -Ddocumentation=false -Ddtd_validation=false -Dlibraries=true -Dscanner=true _build
meson install -C _build

# Now build cross libwayland using the native scanner
meson setup -Dtests=false -Ddocumentation=false -Ddtd_validation=false -Dlibraries=true -Dscanner=false _cross ${EXTRA_MESON_ARGS:-}
meson install -C _cross
cd ..
rm -rf wayland

git clone https://gitlab.freedesktop.org/wayland/wayland-protocols
cd wayland-protocols
git checkout "$WAYLAND_PROTOCOLS_VERSION"
meson setup -Dtests=false _build ${EXTRA_MESON_ARGS:-}
meson install -C _build
cd ..
rm -rf wayland-protocols

section_end wayland
