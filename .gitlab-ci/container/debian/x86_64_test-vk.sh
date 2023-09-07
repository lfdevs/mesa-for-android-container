#!/usr/bin/env bash
# The relative paths in this file only become valid at runtime.
# shellcheck disable=SC1091
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y libelogind0  # this interfere with systemd deps, install separately

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      ccache \
      cmake \
      g++ \
      g++-mingw-w64-i686-posix \
      g++-mingw-w64-x86-64-posix \
      glslang-tools \
      libexpat1-dev \
      gnupg2 \
      libdrm-dev \
      libgbm-dev \
      libgles2-mesa-dev \
      liblz4-dev \
      libpciaccess-dev \
      libudev-dev \
      libvulkan-dev \
      libwaffle-dev \
      libx11-xcb-dev \
      libxcb-ewmh-dev \
      libxcb-keysyms1-dev \
      libxkbcommon-dev \
      libxrandr-dev \
      libxrender-dev \
      libzstd-dev \
      meson \
      mingw-w64-i686-dev \
      mingw-w64-tools \
      mingw-w64-x86-64-dev \
      p7zip \
      patch \
      pkgconf \
      python3-dev \
      python3-distutils \
      python3-pip \
      python3-setuptools \
      python3-wheel \
      software-properties-common \
      wine64-tools \
      xz-utils \
      "

apt-get install -y --no-remove --no-install-recommends \
      $STABLE_EPHEMERAL \
      curl \
      libepoxy0 \
      libxcb-shm0 \
      pciutils \
      python3-lxml \
      python3-simplejson \
      sysvinit-core \
      weston \
      xwayland \
      wine \
      wine64 \
      xinit \
      xserver-xorg-video-amdgpu \
      xserver-xorg-video-ati

apt-get update -q

############### Install DXVK

. .gitlab-ci/container/setup-wine.sh "/dxvk-wine64"
. .gitlab-ci/container/install-wine-dxvk.sh

############### Install apitrace binaries for wine

. .gitlab-ci/container/install-wine-apitrace.sh
# Add the apitrace path to the registry
wine \
    reg add "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment" \
    /v Path \
    /t REG_EXPAND_SZ \
    /d "C:\windows\system32;C:\windows;C:\windows\system32\wbem;Z:\apitrace-msvc-win64\bin" \
    /f

############### Building ...

. .gitlab-ci/container/container_pre_build.sh

############### Build parallel-deqp-runner's hang-detection tool

. .gitlab-ci/container/build-hang-detection.sh

############### Build piglit replayer

PIGLIT_BUILD_TARGETS="piglit_replayer" . .gitlab-ci/container/build-piglit.sh

############### Build Fossilize

. .gitlab-ci/container/build-fossilize.sh

############### Build dEQP VK

. .gitlab-ci/container/build-deqp.sh

############### Build apitrace

. .gitlab-ci/container/build-apitrace.sh

############### Build gfxreconstruct

. .gitlab-ci/container/build-gfxreconstruct.sh

############### Build VKD3D-Proton

. .gitlab-ci/container/setup-wine.sh "/vkd3d-proton-wine64"

. .gitlab-ci/container/build-vkd3d-proton.sh

############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      $STABLE_EPHEMERAL

apt-get autoremove -y --purge

#dpkg -r --force-depends "mesa-vulkan-drivers" "mesa-vdpau-drivers" "mesa-va-drivers" "libgl1-mesa-dri" "libglx-mesa0" "vdpau-driver-all" "va-driver-all" "libglx0" "libgl1" "libvdpau-va-gl1" "libglu1-mesa" "libegl-mesa0" "libgl1-mesa-dri" "libglapi-mesa" "libosmesa6"
