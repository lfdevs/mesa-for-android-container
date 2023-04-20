#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -ex

export LIBDRM_VERSION=libdrm-2.4.110

curl -L -O --retry 4 -f --retry-all-errors --retry-delay 60 \
    https://dri.freedesktop.org/libdrm/"$LIBDRM_VERSION".tar.xz
tar -xvf "$LIBDRM_VERSION".tar.xz && rm "$LIBDRM_VERSION".tar.xz
cd "$LIBDRM_VERSION"
meson build -D vc4=false -D freedreno=false -D etnaviv=false $EXTRA_MESON_ARGS
ninja -C build install
cd ..
rm -rf "$LIBDRM_VERSION"
