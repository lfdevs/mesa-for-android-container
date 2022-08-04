#!/bin/bash
# The relative paths in this file only become valid at runtime.
# shellcheck disable=SC1091

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

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
      pkg-config \
      python3-dev \
      python3-distutils \
      python3-pip \
      python3-setuptools \
      python3-wheel \
      software-properties-common \
      wget \
      wine64-tools \
      xz-utils \
      "

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL \
      libxcb-shm0 \
      pciutils \
      python3-lxml \
      python3-simplejson \
      xinit \
      xserver-xorg-video-amdgpu \
      xserver-xorg-video-ati

# We need multiarch for Wine
dpkg --add-architecture i386

# Install a more recent version of Wine than exists in Debian.
apt-key add .gitlab-ci/container/debian/winehq.gpg.key
apt-add-repository https://dl.winehq.org/wine-builds/debian/
apt update -qyy

# Needed for Valve's tracing jobs to collect information about the graphics
# hardware on the test devices.
pip3 install gfxinfo-mupuf==0.0.9

apt install -y --no-remove --install-recommends winehq-stable

function setup_wine() {
    export WINEDEBUG="-all"
    export WINEPREFIX="$1"

    # We don't want crash dialogs
    cat >crashdialog.reg <<EOF
Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Wine\WineDbg]
"ShowCrashDialog"=dword:00000000

EOF

    # Set the wine prefix and disable the crash dialog
    wine regedit crashdialog.reg
    rm crashdialog.reg

    # An immediate wine command may fail with: "${WINEPREFIX}: Not a
    # valid wine prefix."  and that is just spit because of checking
    # the existance of the system.reg file, which fails.  Just giving
    # it a bit more of time for it to be created solves the problem
    # ...
    while ! test -f  "${WINEPREFIX}/system.reg"; do sleep 1; done
}

############### Install DXVK

dxvk_install_release() {
    local DXVK_VERSION=${1:-"1.10.1"}

    wget "https://github.com/doitsujin/dxvk/releases/download/v${DXVK_VERSION}/dxvk-${DXVK_VERSION}.tar.gz"
    tar xzpf dxvk-"${DXVK_VERSION}".tar.gz
    "dxvk-${DXVK_VERSION}"/setup_dxvk.sh install
    rm -rf "dxvk-${DXVK_VERSION}"
    rm dxvk-"${DXVK_VERSION}".tar.gz
}

# Install from a Github PR number
dxvk_install_pr() {
    local __prnum=$1

    # NOTE: Clone all the ensite history of the repo so as not to think
    # harder about cloning just enough for 'git describe' to work.  'git
    # describe' is used by the dxvk build system to generate a
    # dxvk_version Meson variable, which is nice-to-have.
    git clone https://github.com/doitsujin/dxvk
    pushd dxvk
    git fetch origin pull/"$__prnum"/head:pr
    git checkout pr
    ./package-release.sh pr ../dxvk-build --no-package
    popd
    pushd ./dxvk-build/dxvk-pr
    ./setup_dxvk.sh install
    popd
    rm -rf ./dxvk-build ./dxvk
}

# Sets up the WINEPREFIX for the DXVK installation commands below.
setup_wine "/dxvk-wine64"
dxvk_install_release "1.10.1"
#dxvk_install_pr 2359

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

############### Build libdrm

. .gitlab-ci/container/build-libdrm.sh

############### Build Wayland

. .gitlab-ci/container/build-wayland.sh

############### Build parallel-deqp-runner's hang-detection tool

. .gitlab-ci/container/build-hang-detection.sh

############### Build piglit

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

setup_wine "/vkd3d-proton-wine64"

. .gitlab-ci/container/build-vkd3d-proton.sh

############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      $STABLE_EPHEMERAL

apt-get autoremove -y --purge
