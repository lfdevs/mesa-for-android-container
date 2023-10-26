#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive
export LLVM_VERSION="${LLVM_VERSION:=15}"

apt-get install -y libelogind0  # this interfere with systemd deps, install separately

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      bzip2 \
      ccache \
      clang-${LLVM_VERSION} \
      cmake \
      g++ \
      glslang-tools \
      libasound2-dev \
      libcap-dev \
      libclang-cpp${LLVM_VERSION}-dev \
      libdrm-dev \
      libgles2-mesa-dev \
      libpciaccess-dev \
      libpng-dev \
      libudev-dev \
      libvulkan-dev \
      libwaffle-dev \
      libwayland-dev \
      libx11-xcb-dev \
      libxcb-dri2-0-dev \
      libxkbcommon-dev \
      libxrandr-dev \
      libxrender-dev \
      llvm-${LLVM_VERSION}-dev \
      make \
      meson \
      ocl-icd-opencl-dev \
      patch \
      pkgconf \
      python3-distutils \
      xz-utils \
      "

apt-get update

apt-get install -y --no-remove \
      $EXTRA_LOCAL_PACKAGES \
      $STABLE_EPHEMERAL \
      clinfo \
      iptables \
      libclang-common-${LLVM_VERSION}-dev \
      libclang-cpp${LLVM_VERSION} \
      libcap2 \
      libegl1 \
      libepoxy0 \
      libfdt1 \
      libxcb-shm0 \
      ocl-icd-libopencl1 \
      python3-lxml \
      python3-renderdoc \
      python3-simplejson \
      spirv-tools \
      sysvinit-core \
      weston


. .gitlab-ci/container/container_pre_build.sh

############### Build piglit

PIGLIT_OPTS="-DPIGLIT_BUILD_GLX_TESTS=ON -DPIGLIT_BUILD_CL_TESTS=ON -DPIGLIT_BUILD_DMA_BUF_TESTS=ON" . .gitlab-ci/container/build-piglit.sh

############### Build dEQP GL

DEQP_TARGET=surfaceless . .gitlab-ci/container/build-deqp.sh

############### Build apitrace

. .gitlab-ci/container/build-apitrace.sh

############### Build validation layer for zink

. .gitlab-ci/container/build-vulkan-validation.sh

############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      $STABLE_EPHEMERAL

apt-get autoremove -y --purge
