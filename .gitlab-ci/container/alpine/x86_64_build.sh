#!/usr/bin/env bash
# shellcheck disable=SC1091
set -e
set -o xtrace

EPHEMERAL=(
    autoconf
    automake
    bzip2
    libtool
    libepoxy-dev
    libtbb-dev
    make
    openssl-dev
    unzip
)


DEPS=(
    bash
    bison
    ccache
    cmake
    clang-dev
    coreutils
    curl
    flex
    gcc
    g++
    git
    gettext
    glslang
    linux-headers
    llvm16-dev
    meson
    expat-dev
    elfutils-dev
    libdrm-dev
    libselinux-dev
    libva-dev
    libpciaccess-dev
    zlib-dev
    python3-dev
    py3-mako
    py3-ply
    vulkan-headers
    spirv-tools-dev
    util-macros
    wayland-dev
    wayland-protocols
)

apk add "${DEPS[@]}" "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_pre_build.sh

pushd /usr/local
git clone https://gitlab.freedesktop.org/mesa/shader-db.git --depth 1
rm -rf shader-db/.git
cd shader-db
make
popd


############### Uninstall the build software

apk del "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh
