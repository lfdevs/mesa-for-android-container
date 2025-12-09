#!/usr/bin/env bash
# The relative paths in this file only become valid at runtime.
# shellcheck disable=SC1091
# shellcheck disable=SC2086 # we want word splitting

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

uncollapsed_section_start debian_setup "Base Debian system setup"

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
    ccache
    cmake
    dpkg-dev
    g++
    glslang-tools
    libexpat1-dev
    gnupg2
    libdrm-dev
    libgbm-dev
    libgles2-mesa-dev
    liblz4-dev
    libpciaccess-dev
    libudev-dev
    libvulkan-dev
    libwaffle-dev
    libx11-xcb-dev
    libxcb-dri2-0-dev
    libxcb-ewmh-dev
    libxcb-glx0-dev
    libxcb-keysyms1-dev
    libxkbcommon-dev
    libxrandr-dev
    libxrender-dev
    libzstd-dev
    meson
    p7zip
    patch
    pkgconf
    python3-dev
    python3-pip
    python3-setuptools
    python3-wheel
    xz-utils
)

DEPS=(
    libfontconfig1
    libglu1-mesa
)

if [ "$DEBIAN_ARCH" != "armhf" ]; then
  # Wine isn't available on 32-bit ARM
  EPHEMERAL+=(
    wine64-tools
  )
  DEPS+=(
    wine
    wine64
  )
fi

apt-get update

apt-get install -y --no-remove --no-install-recommends \
      "${DEPS[@]}" "${EPHEMERAL[@]}" "${EXTRA_LOCAL_PACKAGES:-}"

############### Building ...

. .gitlab-ci/container/container_pre_build.sh

section_end debian_setup

############### Build dEQP VK

DEQP_API=tools \
DEQP_TARGET=default \
. .gitlab-ci/container/build-deqp.sh

if [ "$DEBIAN_ARCH" == "amd64" ]; then
  DEQP_API=VK-main \
  DEQP_TARGET=default \
  . .gitlab-ci/container/build-deqp.sh
fi

DEQP_API=VK \
DEQP_TARGET=default \
. .gitlab-ci/container/build-deqp.sh

rm -rf /VK-GL-CTS

############### Build Fossilize

if [ "$DEBIAN_ARCH" != "armhf" ]; then
  . .gitlab-ci/container/build-fossilize.sh
fi

############### Build gfxreconstruct

# gfxreconstruct thinks that ARMv7-on-AArch64 is cross-compilation
if [ "$DEBIAN_ARCH" != "armhf" ]; then
  . .gitlab-ci/container/build-gfxreconstruct.sh
fi

############### Build VKD3D-Proton

# Wine isn't available on 32-bit ARM
if [ "$DEBIAN_ARCH" != "armhf" ]; then
  uncollapsed_section_switch proton "Installing Proton (Wine/D3DVK emulation)"
  . .gitlab-ci/container/setup-wine.sh "/vkd3d-proton-wine64"
  . .gitlab-ci/container/build-vkd3d-proton.sh
fi

############### Uninstall the build software

uncollapsed_section_switch debian_cleanup "Cleaning up base Debian system"

apt-get purge -y "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh

section_end debian_cleanup

############### Remove unused packages

. .gitlab-ci/container/strip-rootfs.sh
