#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_BUILD_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

: "${LLVM_VERSION:?llvm version not set}"

apt-get -y install ca-certificates curl gnupg2
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list.d/*
echo "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" | tee /etc/apt/sources.list.d/gfx-ci_.list

. .gitlab-ci/container/debian/maybe-add-llvm-repo.sh

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
    libssl-dev
)

DEPS=(
    apt-utils
    android-libext4-utils
    autoconf
    automake
    bc
    bison
    ccache
    cmake
    curl
    "clang-${LLVM_VERSION}"
    dpkg-dev
    fastboot
    file
    flatbuffers-compiler
    flex
    g++
    git
    glslang-tools
    kmod
    "libclang-${LLVM_VERSION}-dev"
    "libclang-cpp${LLVM_VERSION}-dev"
    "libclang-common-${LLVM_VERSION}-dev"
    libasan8
    libdrm-dev
    libelf-dev
    libexpat1-dev
    libflatbuffers-dev
    "libllvm${LLVM_VERSION}"
    libvulkan-dev
    libx11-dev
    libx11-xcb-dev
    libxcb-dri2-0-dev
    libxcb-dri3-dev
    libxcb-glx0-dev
    libxcb-present-dev
    libxcb-randr0-dev
    libxcb-shm0-dev
    libxcb-xfixes0-dev
    libxdamage-dev
    libxext-dev
    libxrandr-dev
    libxshmfence-dev
    libxtensor-dev
    libxxf86vm-dev
    libwayland-dev
    libwayland-egl-backend-dev
    "llvm-${LLVM_VERSION}-dev"
    ninja-build
    openssh-server
    pkgconf
    python3-mako
    python3-pil
    python3-pip
    python3-pycparser
    python3-requests
    python3-setuptools
    python3-venv
    shellcheck
    u-boot-tools
    xz-utils
    yamllint
    zlib1g-dev
    zstd
)

apt-get update

apt-get -y install "${DEPS[@]}" "${EPHEMERAL[@]}"

# Needed for ci-fairy s3cp
pip3 install --break-system-packages "ci-fairy[s3] @ git+https://gitlab.freedesktop.org/freedesktop/ci-templates@$MESA_TEMPLATES_COMMIT"

pip3 install --break-system-packages -r bin/ci/test/requirements.txt

. .gitlab-ci/container/install-meson.sh

arch=armhf

. .gitlab-ci/container/cross_build.sh

. .gitlab-ci/container/container_pre_build.sh

. .gitlab-ci/container/build-mold.sh

. .gitlab-ci/container/build-wayland.sh

. .gitlab-ci/container/build-llvm-spirv.sh

. .gitlab-ci/container/build-libclc.sh

. .gitlab-ci/container/build-rust.sh build

. .gitlab-ci/container/build-bindgen.sh

apt-get purge -y "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh
