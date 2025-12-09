#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -uex

section_start weston "Building Weston"

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_BASE_TAG

export WESTON_VERSION="14.0.1"

git clone https://gitlab.freedesktop.org/wayland/weston
cd weston
git checkout "$WESTON_VERSION"
patch -p1 < "$OLDPWD/.gitlab-ci/container/patches/weston-no-xwm.patch"
meson setup \
    -Dbackend-drm=false \
    -Dbackend-drm-screencast-vaapi=false \
    -Dbackend-headless=true \
    -Dbackend-pipewire=false \
    -Dbackend-rdp=false \
    -Dscreenshare=false \
    -Dbackend-vnc=false \
    -Dbackend-wayland=false \
    -Dbackend-x11=false \
    -Dbackend-default=headless \
    -Drenderer-gl=true \
    -Dxwayland=true \
    -Dsystemd=false \
    -Dremoting=false \
    -Dpipewire=false \
    -Dshell-desktop=true \
    -Dshell-fullscreen=false \
    -Dshell-ivi=false \
    -Dshell-kiosk=false \
    -Dcolor-management-lcms=false \
    -Dimage-jpeg=false \
    -Dimage-webp=false \
    -Dtools= \
    -Ddemo-clients=false \
    -Dsimple-clients= \
    -Dresize-pool=false \
    -Dwcap-decode=false \
    -Dtests=false \
    -Ddoc=false \
    -Dno-xwm-decorations=true \
    _build ${EXTRA_MESON_ARGS:-}
meson install -C _build
cd ..
rm -rf weston

section_end weston
