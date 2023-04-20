#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace


EPHEMERAL="
        autoconf
        automake
        bzip2
        cmake
        git
        libtool
        libepoxy-dev
        libtbb-dev
        make
        openssl-dev
        unzip
        xz
	zstd-dev
        "

apk add \
    bash \
    bison \
    ccache \
    clang-dev \
    coreutils \
    curl \
    flex \
    gcc \
    g++ \
    gettext \
    glslang \
    linux-headers \
    llvm15-dev \
    meson \
    expat-dev \
    elfutils-dev \
    libselinux-dev \
    libva-dev \
    libpciaccess-dev \
    zlib-dev \
    python3-dev \
    py3-mako \
    py3-ply \
    vulkan-headers \
    spirv-tools-dev \
    util-macros \
    $EPHEMERAL


. .gitlab-ci/container/container_pre_build.sh

. .gitlab-ci/container/build-libdrm.sh

. .gitlab-ci/container/build-wayland.sh

pushd /usr/local
git clone https://gitlab.freedesktop.org/mesa/shader-db.git --depth 1
rm -rf shader-db/.git
cd shader-db
make
popd


############### Uninstall the build software

apk del $EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
