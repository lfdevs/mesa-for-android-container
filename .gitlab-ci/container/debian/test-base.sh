#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_BASE_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

uncollapsed_section_start debian_setup "Base Debian system setup"

export DEBIAN_FRONTEND=noninteractive

apt-get install -y curl ca-certificates gnupg2

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list.d/*

echo "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" | tee /etc/apt/sources.list.d/gfx-ci_.list

: "${LLVM_VERSION:?llvm version not set!}"

. .gitlab-ci/container/debian/maybe-add-llvm-repo.sh

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
    autoconf
    automake
    bc
    bison
    bzip2
    ccache
    cmake
    "clang-${LLVM_VERSION}"
    dpkg-dev
    flex
    glslang-tools
    g++
    libasound2-dev
    libcairo2-dev
    libcap-dev
    "libclang-cpp${LLVM_VERSION}-dev"
    "libclang-rt-${LLVM_VERSION}-dev"
    libdrm-dev
    libegl-dev
    libelf-dev
    libepoxy-dev
    libexpat1-dev
    libgbm-dev
    libinput-dev
    libgles2-mesa-dev
    liblz4-dev
    libpciaccess-dev
    libpixman-1-dev
    libssl-dev
    libtirpc-dev
    libvulkan-dev
    libudev-dev
    libwaffle-dev
    libx11-xcb-dev
    libxcb-composite0-dev
    libxcb-dri2-0-dev
    libxcb-dri3-dev
    libxcb-present-dev
    libxfixes-dev
    libxcb-ewmh-dev
    libxcursor-dev
    libxcvt-dev
    libxext-dev
    libxfont-dev
    libxkbcommon-dev
    libxkbfile-dev
    libxrandr-dev
    libxrender-dev
    libxshmfence-dev
    libzstd-dev
    "llvm-${LLVM_VERSION}-dev"
    make
    meson
    mesa-common-dev
    patch
    pkgconf
    protobuf-compiler
    python3-dev
    python3-pip
    python3-setuptools
    python3-venv
    python3-wheel
    wayland-protocols
    xz-utils
)

DEPS=(
    apt-utils
    clinfo
    curl
    dropbear
    git
    git-lfs
    inetutils-syslogd
    iptables
    jq
    kmod
    libasan8
    libcairo2
    libcap2
    libdrm2
    libegl1
    libepoxy0
    libexpat1
    libfdt1
    libinput10
    "libclang-common-${LLVM_VERSION}-dev"
    "libclang-cpp${LLVM_VERSION}"
    "libllvm${LLVM_VERSION}"
    liblz4-1
    libpixman-1-0
    libpng16-16t64
    libproc2-0
    libpython3.13
    libtirpc3t64
    libubsan1
    libvulkan1
    libwayland-client0
    libwayland-server0
    libxcb-composite0
    libxcb-dri2-0
    libxcb-ewmh2
    libxcb-randr0
    libxcb-shm0
    libxcb-xfixes0
    libxcursor1
    libxcvt0
    libxfont2
    libxkbcommon0
    libxrandr2
    libxrender1
    ntpsec-ntpdig
    libxshmfence1
    ocl-icd-libopencl1
    pciutils
    python3-lxml
    python3-mako
    python3-numpy
    python3-packaging
    python3-pil
    python3-renderdoc
    python3-requests
    python3-simplejson
    python3-six
    python3-yaml
    socat
    spirv-tools
    sysvinit-core
    vulkan-tools
    waffle-utils
    xinit
    xserver-common
    xserver-xorg-video-amdgpu
    xserver-xorg-video-ati
    xauth
    zlib1g
)

HW_DEPS=(
    netcat-openbsd
    mount
    python3-serial
    tzdata
    zstd
)

apt-get update
apt-get dist-upgrade -y

apt-get install --purge -y \
      sysvinit-core libelogind0

apt-get install -y --no-remove "${DEPS[@]}" "${HW_DEPS[@]}"

apt-get install -y --no-install-recommends "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_pre_build.sh

# Needed for ci-fairy s3cp
pip3 install --break-system-packages "ci-fairy[s3] @ git+https://gitlab.freedesktop.org/freedesktop/ci-templates@$MESA_TEMPLATES_COMMIT"

# Needed for manipulation with traces yaml files.
pip3 install --break-system-packages yq

section_end debian_setup

############### Build ci-kdl

. .gitlab-ci/container/build-kdl.sh

############### Build mold

. .gitlab-ci/container/build-mold.sh

############### Build LLVM-SPIRV translator

. .gitlab-ci/container/build-llvm-spirv.sh

############### Build libclc

. .gitlab-ci/container/build-libclc.sh

############### Build Wayland

. .gitlab-ci/container/build-wayland.sh

############### Build Weston

. .gitlab-ci/container/build-weston.sh

############### Build XWayland

. .gitlab-ci/container/build-xwayland.sh

############### Install Rust toolchain

. .gitlab-ci/container/build-rust.sh test

############### Build Crosvm

# crosvm build fails on ARMv7 due to Xlib type-size issues
if [ "$DEBIAN_ARCH" != "armhf" ]; then
  . .gitlab-ci/container/build-crosvm.sh
fi

############### Build dEQP runner

. .gitlab-ci/container/build-deqp-runner.sh

############### Build apitrace

. .gitlab-ci/container/build-apitrace.sh

############### Uninstall the build software

uncollapsed_section_switch debian_cleanup "Cleaning up base Debian system"

apt-get purge -y "${EPHEMERAL[@]}"

# Properly uninstall rustup including cargo and init scripts on shells
rustup self uninstall -y

. .gitlab-ci/container/container_post_build.sh

section_end debian_cleanup
